#include <cmath>
#include <cstdint>
#include <cstring>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Skyriding/SkyridingPhysics.h"
#include "Engine/Source/Skyriding/SkyridingResources.h"
#include "Engine/Source/Skyriding/SkyridingWire.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Object/CMovementGlobals.h"
#include "WoW/Source/Object/ObjectClient/Movement_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/UIUtil/InputControl.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/WorldClient/FrameTime.h"

// Skyriding (docs/SKYRIDING_ARCHITECTURE.md): momentum flight for the local player. The server authorizes the mode
// (SMSG_ESTERIA_SKYRIDE Begin/End) and owns ability charges; while airborne this extension replaces the native
// flight plot with the shared simulator (UnitScript::OnUnitPlotLocalMove), so the step still goes through the engine's
// collision and the position still goes out in native movement packets.

namespace {

using namespace Skyriding;
namespace Wire = SkyridingWire;

constexpr uint32_t kForceMovement = 0x40;    // UNIT_FLAG2_FORCE_MOVEMENT: CGInputControl::MovePlayer holds forward
constexpr uint32_t kHeartbeatMs = 100;       // native heartbeat is 500 ms
constexpr uint32_t kLaunchGraceMs = 500;     // Lift Off waits this long for the native flying flag

CVar* s_enabled;
CVar* s_style;
CVar* s_fov;

struct Session {
    bool authorized = false;                 // server sent Begin, no End since
    bool heard = false;                      // any server message: the hello arrived
    uint32_t helloAt = 0;
    uint32_t hellos = 0;                     // a stock server never answers: stop after a few
    Profile profile;
    Wire::ResourceState resources;
    uint32_t nextChargeAt = 0;               // FrameTime of the next charge, 0 = none pending
    uint32_t cooldownEnd[CommandCount] = {};
    bool pending[CommandCount] = {};         // asked, no reply yet: the reply carries the cooldown
    uint16_t sequence = 0;
    uint32_t corrections = 0;
    Command lastCommand = Command::None;
    AbilityResult lastResult = AbilityResult::Ok;
};

struct Flight {
    State sim;
    bool forcing = false;                    // we set kForceMovement (and must clear it)
    uint32_t launchAt = 0;                   // Lift Off predicted, waiting for MOVEMENTFLAG_FLYING
    bool lastStepOurs = false;
    C3Vector lastStart;
    Vec3 lastMove;
    float lastDt = 0.0f;
    bool ascendHeld = false;
    // Previous world frame sample, for the velocity a rider has when flight starts natively (jump + jump).
    C3Vector samplePos;
    uint32_t sampleTime = 0;
    Vec3 sampledVelocity;
    float fovOffset = 0.0f;
    // Render-only roll into turns (radians, positive = left wing down); never touches the simulated path.
    float bank = 0.0f;
    float lastHeading = 0.0f;
};

Session s_session;
Flight s_flight;

CGUnit_C* LocalPlayer() {
    return static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPEMASK_PLAYER, __FILE__, __LINE__));
}

bool Dynamic() {
    return s_session.authorized && (!s_enabled || s_enabled->m_intValue) && (!s_style || !s_style->m_intValue);
}

void Signal() {
    uint32_t event = sExtensionMgr->GetFrameScriptEventIndex("SKYRIDING_UPDATE");
    if (event != UINT32_MAX) FrameScript_SignalEvent(event, nullptr);
}

void Send(const uint8_t* bytes, size_t size) {
    if (size) sExtensionMgr->SendClientMessage(Wire::ClientOpcode, bytes, uint32_t(size));
}

void SendHello() {
    uint8_t buffer[Wire::MaxClientSize];
    Wire::Hello hello{ Wire::Version, s_style && s_style->m_intValue ? Wire::Style::Steady : Wire::Style::Dynamic };
    Send(buffer, Wire::Write(hello, buffer, sizeof(buffer)));
    s_session.helloAt = FrameTime::s_curTimeMs;
    ++s_session.hellos;
    sExtensionMgr->Log("Skyriding: hello sent (style %s)", hello.style == Wire::Style::Steady ? "steady" : "dynamic");
}

void SetForceForward(CGUnit_C* unit, bool on) {
    if (!unit || s_flight.forcing == on) return;
    uint32_t& flags2 = unit->m_unit->m_unitFieldFlags2;
    // Never clear a bit the server set itself (vehicles and scripted movement use it).
    if (on && (flags2 & kForceMovement)) return;
    flags2 = on ? flags2 | kForceMovement : flags2 & ~kForceMovement;
    s_flight.forcing = on;
    CGInputControl::GetActive()->UpdatePlayer(FrameTime::s_curTimeMs, 1);
}

void Land(CGUnit_C* unit) {
    s_flight.sim.locomotion = Locomotion::Grounded;
    s_flight.sim.boostSeconds = s_flight.sim.hoverSeconds = 0.0f;
    s_flight.lastStepOurs = false;
    s_flight.launchAt = 0;
    SetForceForward(unit, false);
    Signal();
}

void ResetFlight() {
    if (s_flight.forcing) SetForceForward(LocalPlayer(), false);
    const float fov = s_flight.fovOffset;
    s_flight = Flight();
    s_flight.fovOffset = fov;
}

// Local prediction of an ability the server will be asked for. Returns false when the mirror says no.
bool Predict(CGUnit_C* unit, Command command) {
    CMovement_C& mover = unit->m_movementData;
    const bool airborne = s_flight.sim.locomotion == Locomotion::Airborne;
    const uint32_t now = FrameTime::s_curTimeMs;
    if (NeedsAirborne(command) != airborne || s_session.pending[uint8_t(command)]) return false;
    if (s_session.cooldownEnd[uint8_t(command)] && int32_t(s_session.cooldownEnd[uint8_t(command)] - now) > 0) return false;
    if (s_session.resources.charges < Cost(command)) return false;
    if (command == Command::LiftOff) {
        if (!(mover.m_flags & MOVEMENTFLAG_CAN_FLY) || (mover.m_flags & (MOVEMENTFLAG_ONTRANSPORT | MOVEMENTFLAG_ROOT | MOVEMENTFLAG_SWIMMING))) return false;
        s_flight.sim.velocity = s_flight.sampledVelocity;
        s_flight.launchAt = now;
        mover.OnFlightLocal(now, 1);
    }
    s_session.resources.charges = uint8_t(s_session.resources.charges - Cost(command));
    Apply(s_flight.sim, command, s_session.profile, mover.m_facing);
    if (command == Command::LiftOff) SetForceForward(unit, true);
    return true;
}

bool RequestAbility(Command command) {
    CGUnit_C* unit = LocalPlayer();
    if (!Dynamic() || !unit || !Predict(unit, command)) return false;
    uint8_t buffer[Wire::MaxClientSize];
    Wire::Ability ability{ command, ++s_session.sequence };
    Send(buffer, Wire::Write(ability, buffer, sizeof(buffer)));
    s_session.lastCommand = command;
    s_session.pending[uint8_t(command)] = true;
    Signal();
    return true;
}

void SetResources(const Wire::ResourceState& resources) {
    s_session.resources = resources;
    s_session.nextChargeAt = resources.nextChargeMs ? FrameTime::s_curTimeMs + resources.nextChargeMs : 0;
}

int32_t Receive(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    if (!message || message->m_read > message->m_size) return 0;
    const uint32_t size = message->m_size - message->m_read;
    if (!size || size > Wire::MaxServerSize || !message->FetchRead(message->m_read, size)) return 0;
    uint8_t bytes[Wire::MaxServerSize];
    message->GetData(bytes, size);

    Wire::Reader reader(bytes, size);
    Wire::ServerMsg type;
    if (!Wire::ReadServerType(reader, type)) return 0;
    if (!s_session.heard) sExtensionMgr->Log("Skyriding: server answered");
    s_session.heard = true;
    switch (type) {
        case Wire::ServerMsg::Begin: {
            Wire::Begin begin;
            if (!Wire::Read(reader, begin)) {
                sExtensionMgr->Log("Skyriding: bad Begin (%u bytes)", size);
                return 0;
            }
            s_session.authorized = true;
            s_session.profile = begin.physics;
            SetResources(begin.resources);
            sExtensionMgr->Log("Skyriding: begin, %u/%u charges", begin.resources.charges, begin.resources.maxCharges);
            break;
        }
        case Wire::ServerMsg::End: {
            Wire::EndReason reason;
            if (!Wire::ReadEnd(reader, reason)) return 0;
            s_session.authorized = false;
            ResetFlight();
            sExtensionMgr->Log("Skyriding: end, reason %u", unsigned(reason));
            break;
        }
        case Wire::ServerMsg::AbilityResult: {
            Wire::AbilityReply reply;
            if (!Wire::Read(reader, reply)) return 0;
            SetResources(reply.resources);
            s_session.cooldownEnd[uint8_t(reply.command)] = reply.cooldownMs ? FrameTime::s_curTimeMs + reply.cooldownMs : 0;
            s_session.pending[uint8_t(reply.command)] = false;
            s_session.lastResult = reply.result;
            break;
        }
        case Wire::ServerMsg::Resources: {
            Wire::ResourceState resources;
            if (!Wire::Read(reader, resources)) return 0;
            SetResources(resources);
            break;
        }
        case Wire::ServerMsg::Correction:
            if (!reader.Done()) return 0;
            s_flight.sim.velocity = {};
            s_flight.sim.boostSeconds = 0.0f;
            s_flight.lastStepOurs = false;
            ++s_session.corrections;
            break;
    }
    Signal();
    return 1;
}

class SkyridingUnit : public UnitScript {
    public:
    SkyridingUnit() : UnitScript("SkyridingUnit") {}

    bool OnUnitPlotLocalMove(CGUnit_C& unit, uint32_t time, uint32_t elapsed, C3Vector& move) override {
        if (!Dynamic() || !(unit.m_obj->m_type & TYPEMASK_PLAYER) || !(unit.m_obj->m_guid == ClntObjMgrGetActivePlayer())) return false;
        CMovement_C& mover = unit.m_movementData;
        const uint32_t flags = mover.m_flags;
        Flight& f = s_flight;

        if (!(flags & MOVEMENTFLAG_FLYING) || (flags & (MOVEMENTFLAG_ONTRANSPORT | MOVEMENTFLAG_SWIMMING)) || unit.m_vehicle) {
            // Lift Off was predicted and the native flying flag is still on its way: wait a little, then give up.
            if (f.sim.locomotion == Locomotion::Airborne && !(f.launchAt && time - f.launchAt < kLaunchGraceMs)) Land(&unit);
            return false;
        }

        if (f.sim.locomotion == Locomotion::Grounded) {
            // Flight started natively (jump, then jump in the air): carry the momentum the rider had.
            f.sim.locomotion = Locomotion::Airborne;
            f.sim.velocity = f.sampledVelocity;
            f.lastHeading = std::atan2(f.sim.velocity.y, f.sim.velocity.x);
            f.lastStepOurs = false;
            SetForceForward(&unit, true);
            Signal();
        }
        f.launchAt = 0;

        // Collision feedback: the engine may have shortened the last step (walls, ground).
        if (f.lastStepOurs && f.lastDt > 0.0f) {
            const Vec3 actual(mover.m_position.x - f.lastStart.x, mover.m_position.y - f.lastStart.y, mover.m_position.z - f.lastStart.z);
            const float expected = f.lastMove.Dot(f.lastMove);
            if (expected > 1e-6f && actual.Length() < 2.0f * std::sqrt(expected) + 1.0f && actual.Dot(f.lastMove) < 0.9f * expected) {
                // Touching down while descending: hand back to ground movement.
                if (f.lastMove.z < -0.02f && actual.z > f.lastMove.z * 0.5f && !(flags & MOVEMENTFLAG_ASCENDING)) {
                    mover.OnFlightLocal(time, 0);
                    Land(&unit);
                    return false;
                }
                f.sim.velocity = actual * (1.0f / f.lastDt);
            }
        }

        // The jump key while flying is the native ascend flag; its press is a Flap.
        const bool ascend = (flags & MOVEMENTFLAG_ASCENDING) != 0;
        if (ascend && !f.ascendHeld) RequestAbility(Command::Flap);
        f.ascendHeld = ascend;

        f.sim.position = Vec3(mover.m_position.x, mover.m_position.y, mover.m_position.z);
        const float dt = elapsed * 0.001f;
        const Vec3 step = Advance(f.sim, mover.m_facing, mover.m_pitch, s_session.profile, dt);
        move = C3Vector(step.x, step.y, step.z);
        f.lastStart = mover.m_position;
        f.lastMove = step;
        f.lastDt = dt;
        f.lastStepOurs = true;

        // Bank from the turn rate of the travel direction, more at speed, smoothed.
        const float heading = std::atan2(f.sim.velocity.y, f.sim.velocity.x);
        float turn = heading - f.lastHeading;
        turn -= 6.2831853f * std::floor((turn + 3.1415927f) / 6.2831853f);
        f.lastHeading = heading;
        const float horizontal = std::sqrt(f.sim.velocity.x * f.sim.velocity.x + f.sim.velocity.y * f.sim.velocity.y);
        const float target = dt > 0.0f ? std::fmax(-0.7f, std::fmin(0.7f, turn / dt * 0.4f * std::fmin(1.0f, horizontal / 40.0f))) : 0.0f;
        f.bank += (target - f.bank) * (1.0f - std::exp(-5.0f * dt));

        CMovementGlobals* globals = MovementGetGlobals();
        if (globals && int32_t(globals->m_timeCounterMaybe - (time + kHeartbeatMs)) > 0) globals->m_timeCounterMaybe = time + kHeartbeatMs;
        return true;
    }

    void OnUnitWorldTransformUpdated(CGUnit_C& unit, float) override {
        Flight& f = s_flight;
        if (f.sim.locomotion != Locomotion::Airborne || std::fabs(f.bank) < 0.001f || !(unit.m_obj->m_guid == ClntObjMgrGetActivePlayer())) return;
        CM2Model* model = unit.GetObjectModel();
        if (!model) return;
        // Rows b (model left) and c (model up) turn about row a (model forward).
        C44Matrix& m = model->m_worldTransform;
        const float c = std::cos(f.bank), s = std::sin(f.bank);
        const C3Vector left(m.b0, m.b1, m.b2), up(m.c0, m.c1, m.c2);
        m.b0 = left.x * c - up.x * s; m.b1 = left.y * c - up.y * s; m.b2 = left.z * c - up.z * s;
        m.c0 = up.x * c + left.x * s; m.c1 = up.y * c + left.y * s; m.c2 = up.z * c + left.z * s;
    }
};

class SkyridingWorld : public WorldScript {
    public:
    SkyridingWorld() : WorldScript("SkyridingWorld") {}

    void OnWorldUpdate(float) override {
        CGUnit_C* unit = LocalPlayer();
        if (!unit) return;
        Flight& f = s_flight;
        const uint32_t now = FrameTime::s_curTimeMs;
        // The first hello can beat the server's "in world" state and be dropped: repeat until the server answers.
        if (!s_session.heard && s_session.hellos < 10 && now - s_session.helloAt > 3000) SendHello();
        const C3Vector pos = unit->m_movementData.m_position;
        if (f.sampleTime && now != f.sampleTime && now - f.sampleTime < 500) {
            const float dt = (now - f.sampleTime) * 0.001f;
            f.sampledVelocity = Vec3(pos.x - f.samplePos.x, pos.y - f.samplePos.y, pos.z - f.samplePos.z) * (1.0f / dt);
            if (!f.sampledVelocity.Finite() || f.sampledVelocity.Length() > s_session.profile.maxBoostSpeed) f.sampledVelocity = {};
        }
        f.samplePos = pos;
        f.sampleTime = now;

        if (!Dynamic() && f.sim.locomotion == Locomotion::Airborne) Land(unit);
        // A server update of UNIT_FIELD_FLAGS_2 rewrites the field: keep holding forward while airborne.
        if (f.forcing && !(unit->m_unit->m_unitFieldFlags2 & kForceMovement)) {
            f.forcing = false;
            SetForceForward(unit, true);
        }

        // Speed widens the view a little (skyridingFov radians at full speed, 0 turns it off).
        const float speed = f.sim.locomotion == Locomotion::Airborne ? f.sim.velocity.Length() : 0.0f;
        const float maxFov = s_fov ? s_fov->m_floatValue : 0.0f;
        const float target = maxFov * std::fmax(0.0f, std::fmin(1.0f, (speed - 30.0f) / 40.0f));
        if (std::fabs(target - f.fovOffset) > 0.005f) {
            if (CGCamera* camera = CGWorldFrame::GetActiveCamera()) {
                camera->SetDesiredFoVOffset(target, 0.0f, 1.0f, now);
                f.fovOffset = target;
            }
        }
    }
};

class SkyridingClient : public ClientScript {
    public:
    SkyridingClient() : ClientScript("SkyridingClient") {}

    void OnStartup() override {
        s_enabled = sExtensionMgr->RegisterCVar("skyridingEnabled", "Skyriding: 0 keeps every mount on stock flight", 1, "1", nullptr, CATEGORY_GAME);
        s_style = sExtensionMgr->RegisterCVar("skyridingStyle", "Skyriding: 0 dynamic (skyriding), 1 steady flight", 1, "0", OnStyle, CATEGORY_GAME);
        s_fov = sExtensionMgr->RegisterCVar("skyridingFov", "Skyriding: extra field of view (radians) at high speed", 1, "0.15", nullptr, CATEGORY_GAME);
    }

    void OnEnterWorld(uint32_t) override {
        s_session = Session();
        ResetFlight();
        SendHello();
    }

    void OnLeaveWorld() override {
        ResetFlight();
        s_session = Session();
    }

    static bool OnStyle(CVar*, const char*, const char* value, void*) {
        uint8_t buffer[Wire::MaxClientSize];
        const Wire::Style style = value && value[0] == '1' ? Wire::Style::Steady : Wire::Style::Dynamic;
        if (LocalPlayer()) Send(buffer, Wire::WriteStyle(style, buffer, sizeof(buffer)));
        return true;
    }
};

// Every open-world area is flyable: the same patch the server makes (Skyriding.FlyEverywhere), so the client lets a
// flying mount be cast and flown in former no-fly zones. AreaTable flags 0x400 flyable, 0x20000000 no-fly zone.
class SkyridingClientDB : public ClientDBScript {
    public:
    SkyridingClientDB() : ClientDBScript("SkyridingClientDB") {}

    void OnClientDBInitialize() override {
        uint32_t changed = 0;
        for (int32_t i = 0; i < g_areaTableDB.m_numRecords; ++i) {
            AreaTableRec& area = g_areaTableDB.m_records[i];
            const MapRec* map = g_mapDB.GetRecord(area.m_continentID);
            if (!map || map->m_instanceType != 0) continue;
            const int32_t flags = (area.m_flags | 0x400) & ~0x20000000;
            if (flags != area.m_flags) {
                area.m_flags = flags;
                ++changed;
            }
        }
        sExtensionMgr->Log("Skyriding: flying opened in %u areas", changed);
    }
};

// ---- Lua ------------------------------------------------------------------------------------------------------------

const char* const kCommandNames[CommandCount] = { "", "LIFTOFF", "SURGE", "ASCENT", "WHIRL", "HALT", "FLAP" };
const char* const kAirStates[] = { "GLIDE", "DIVE", "CLIMB", "STALL" };

Command ParseCommand(lua_State* L, int index) {
    if (lua_isnumber(L, index)) {
        const double value = lua_tonumber(L, index);
        return value >= 1 && value < CommandCount && std::floor(value) == value ? Command(int(value)) : Command::None;
    }
    if (const char* name = lua_tostring(L, index)) {
        for (uint8_t i = 1; i < CommandCount; ++i) {
            if (!SStrCmpI(name, kCommandNames[i], STORM_MAX_STR)) return Command(i);
        }
    }
    return Command::None;
}

float SecondsUntil(uint32_t at) {
    return at && int32_t(at - FrameTime::s_curTimeMs) > 0 ? (at - FrameTime::s_curTimeMs) * 0.001f : 0.0f;
}

// active, airborne, speed, verticalSpeed, airState, charges, maxCharges, nextChargeSeconds, resourceProfile, thrill, style
int32_t Script_GetSkyridingInfo(lua_State* L) {
    const State& s = s_flight.sim;
    const bool airborne = s.locomotion == Locomotion::Airborne;
    const float speed = airborne ? s.velocity.Length() : 0.0f;
    lua_pushboolean(L, Dynamic());
    lua_pushboolean(L, airborne);
    lua_pushnumber(L, speed);
    lua_pushnumber(L, airborne ? s.velocity.z : 0.0f);
    lua_pushstring(L, airborne ? kAirStates[uint8_t(Classify(s, s_session.profile))] : "GROUND");
    lua_pushnumber(L, s_session.resources.charges);
    lua_pushnumber(L, s_session.resources.maxCharges);
    lua_pushnumber(L, SecondsUntil(s_session.nextChargeAt));
    lua_pushstring(L, s_session.resources.profile == ResourceProfile::Vigor ? "VIGOR" : "CHARGES");
    lua_pushboolean(L, airborne && speed >= s_session.profile.thrillSpeed);
    lua_pushstring(L, s_style && s_style->m_intValue ? "STEADY" : "DYNAMIC");
    return 11;
}

// x, y, z, vx, vy, vz, tick, corrections, lastAbility, lastResult
int32_t Script_GetSkyridingDebug(lua_State* L) {
    const State& s = s_flight.sim;
    lua_pushnumber(L, s.position.x);
    lua_pushnumber(L, s.position.y);
    lua_pushnumber(L, s.position.z);
    lua_pushnumber(L, s.velocity.x);
    lua_pushnumber(L, s.velocity.y);
    lua_pushnumber(L, s.velocity.z);
    lua_pushnumber(L, s.tick);
    lua_pushnumber(L, s_session.corrections);
    lua_pushstring(L, kCommandNames[uint8_t(s_session.lastCommand)]);
    lua_pushnumber(L, uint8_t(s_session.lastResult));
    return 10;
}

int32_t Script_GetSkyridingCooldown(lua_State* L) {
    const Command command = ParseCommand(L, 1);
    if (command == Command::None) return luaL_error(L, "Usage: GetSkyridingCooldown(\"SURGE\"|1..6)");
    lua_pushnumber(L, SecondsUntil(s_session.cooldownEnd[uint8_t(command)]));
    return 1;
}

int32_t Script_UseSkyridingAbility(lua_State* L) {
    const Command command = ParseCommand(L, 1);
    if (command == Command::None) return luaL_error(L, "Usage: UseSkyridingAbility(\"LIFTOFF\"|\"SURGE\"|\"ASCENT\"|\"WHIRL\"|\"HALT\"|\"FLAP\")");
    lua_pushboolean(L, RequestAbility(command));
    return 1;
}

int32_t Command_Skyriding(const char*, const char* arguments) {
    if (s_style && arguments && !SStrCmpI(arguments, "steady", STORM_MAX_STR)) s_style->Set("1", true, false, false, true);
    else if (s_style && arguments && !SStrCmpI(arguments, "dynamic", STORM_MAX_STR)) s_style->Set("0", true, false, false, true);
    const State& s = s_flight.sim;
    sExtensionMgr->Log("Skyriding: authorized %d dynamic %d airborne %d speed %.1f charges %u/%u corrections %u",
        int(s_session.authorized), int(Dynamic()), int(s.locomotion == Locomotion::Airborne), s.velocity.Length(),
        s_session.resources.charges, s_session.resources.maxCharges, s_session.corrections);
    return 1;
}

}  // namespace

void AddSkyridingExtension() {
    sExtensionMgr->RegisterMessageHandler(Wire::ServerOpcode, Receive, nullptr);
    sExtensionMgr->RegisterLuaFunction("GetSkyridingInfo", Script_GetSkyridingInfo, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("GetSkyridingDebug", Script_GetSkyridingDebug, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("GetSkyridingCooldown", Script_GetSkyridingCooldown, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("UseSkyridingAbility", Script_UseSkyridingAbility, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("SKYRIDING_UPDATE", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterConsoleCommand("skyriding", Command_Skyriding, CATEGORY_GAME, "Skyriding status; 'skyriding steady|dynamic' sets the flight style");
    new SkyridingUnit();
    new SkyridingWorld();
    new SkyridingClient();
    new SkyridingClientDB();
}
