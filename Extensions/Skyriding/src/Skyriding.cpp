#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
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

// ---- Visuals (render only) ------------------------------------------------------------------------------------------
// Retail's speed aura (visual kit 159809, docs/SKYRIDING_RETAIL_ASSETS.md) and retail AdvFly* mount animations.
// Local player only: the client does not know who else skyrides.

struct WindPart {
    uint32_t fileId;
    uint32_t loopAnim;
    bool camera;                             // positioner 1797: between camera and rider, facing the view
    float scale, yaw, pitch, roll, raise;    // SpellVisualKitModelAttach: radians, model units
};

constexpr uint32_t kWindEndAnim = 159;
constexpr uint32_t kWindLingerMs = 800;      // the end animation plays this long before the models go
constexpr float kPi = 3.1415927f;
// Kit 159809's SpellVisualKitModelAttach rows (470110, 469934, 473527, 473531, 473845, 473846).
constexpr WindPart kWindParts[] = {
    { 4579907, 158, true, 1.25f, 0.0f, 0.0f, 0.0f, 0.0f },               // FX_Speed4_Aura: the screen tunnel
    { 4579908, 214, true, 1.25f, 0.0f, 0.0f, 0.0f, 0.0f },               // FX_Speed4_Aura01: its particles
    // FX_Speed4_Aura02 streamers. shortcut: retail places them between body points 60-63 (positioners 1841-1847,
    // not decoded); they ride the mount root with the kit's own angles instead.
    { 4640283, 0, false, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { 4640283, 0, false, 1.0f, 0.0f, 0.0f, kPi, 0.16f },
    { 4640283, 0, false, 1.0f, -0.3490658f, -0.0872665f, 0.0f, 0.0f },
    { 4640283, 0, false, 1.0f, 0.3490658f, -0.0872665f, kPi, 0.16f },
};
constexpr size_t kWindCount = sizeof(kWindParts) / sizeof(kWindParts[0]);

// Retail animation ids (above the 3.3.5 AnimationData range) and their AnimationData.Fallback, so a mount plays what
// retail would play for it: a missing AdvFlyFlap becomes Fly, AdvFlyStart becomes MountFlightStart, and so on.
namespace Anim {
constexpr uint32_t Fly = 135, Swim = 42, FlightStart = 564, FlightIdle = 548, JumpEnd = 39, JumpLandRun = 187,
                   Left = 1520, Right = 1522, Forward = 1524, Up = 1528, DiveLoop = 1530, Glide = 1532, Roll = 1534,
                   RollStart = 1556, RollEnd = 1558, DiveEnd = 1588, Start = 1618, Land = 1620, LandRun = 1622,
                   Idle = 1628, RollLeft = 1634, Flap = 1638, DiveStart = 1678, FlapBig = 1680, FlapUp = 1702,
                   SlowFall = 1704, FlapForward = 1706, GlideSlow = 1722, GlideFast = 1724, SecondFlap = 1726;
}
constexpr uint32_t kFallback[][2] = {
    { Anim::Left, 43 }, { Anim::Right, 44 }, { Anim::Forward, Anim::Swim }, { Anim::Up, Anim::SlowFall },
    { Anim::DiveLoop, Anim::Swim }, { Anim::Glide, Anim::Forward }, { Anim::Roll, Anim::RollLeft },
    { Anim::RollStart, Anim::Forward }, { Anim::RollEnd, Anim::Roll }, { Anim::DiveEnd, Anim::DiveLoop },
    { Anim::Start, Anim::FlightStart }, { Anim::Land, 572 }, { 572, 568 }, { 568, Anim::JumpEnd },
    { Anim::LandRun, 574 }, { 574, 570 }, { 570, Anim::JumpLandRun }, { Anim::Idle, Anim::FlightIdle },
    { Anim::FlightIdle, 532 }, { 532, 41 }, { Anim::RollLeft, Anim::Forward }, { Anim::Flap, Anim::Fly },
    { Anim::Fly, Anim::Swim }, { Anim::DiveStart, Anim::DiveLoop }, { Anim::FlapBig, Anim::Flap },
    { Anim::FlapUp, Anim::Flap }, { Anim::SlowFall, Anim::Idle }, { Anim::FlapForward, Anim::Flap },
    { Anim::GlideSlow, Anim::Glide }, { Anim::GlideFast, Anim::Glide }, { Anim::SecondFlap, Anim::FlapUp },
    { Anim::FlightStart, 37 },
};
constexpr uint32_t kSecondFlapMs = 1000;     // a flap this soon after the last one is retail's second flap
// FX_SurgeForward_Impact (kits 158171 Surge Forward, 175071 Aerial Halt) on the mount's attachment 17.
constexpr uint32_t kImpactFileId = 4579917, kImpactAttachment = 17;
constexpr uint32_t kImpactMs = 1500;         // shortcut: fixed lifetime, the model is not loaded when it starts

struct Visuals {
    CM2Model* wind[kWindCount] = {};
    CM2Model* windParent = nullptr;
    uint32_t windEndAt = 0;                  // end animation playing, release at this time
    uint32_t mountAnim = 0;                  // sequence we hold on the mount, 0 = native choice
    uint32_t queue[3] = {};                  // one-shot animations, played in order, each for its own length
    uint32_t queued = 0;
    uint32_t oneShotEnd = 0;                 // when queue[0] ends, 0 = not started
    uint32_t lastFlap = 0;
    bool impactPending = false;              // an ability asked for the impact effect; spawned on the next frame
    CM2Model* impact = nullptr;
    uint32_t impactEnd = 0;
    bool wasDiving = false;
    bool wasAirborne = false;
    bool forceWind = false;                  // 'skyriding wind': show the effect while mounted, for checking it
};

Visuals s_visuals;

C44Matrix WindOffset(const WindPart& part) {
    const float cy = std::cos(part.yaw), sy = std::sin(part.yaw);
    const float cp = std::cos(part.pitch), sp = std::sin(part.pitch);
    const float cr = std::cos(part.roll), sr = std::sin(part.roll);
    // Row vectors (v * M): roll about forward (x), then pitch about left (y), then yaw about up (z).
    const float k = part.scale;
    C44Matrix m;
    m.Identity();
    m.a0 = cp * cy * k;                   m.a1 = cp * sy * k;                   m.a2 = -sp * k;
    m.b0 = (sr * sp * cy - cr * sy) * k;  m.b1 = (sr * sp * sy + cr * cy) * k;  m.b2 = sr * cp * k;
    m.c0 = (cr * sp * cy + sr * sy) * k;  m.c1 = (cr * sp * sy - sr * cy) * k;  m.c2 = cr * cp * k;
    m.d2 = part.raise;
    return m;
}

void DestroyImpact() {
    CM2Model*& model = s_visuals.impact;
    if (!model) return;
    if (model->m_attachParent) model->DetachFromParent();
    model->Release();
    model = nullptr;
}

void UpdateImpact(CM2Model* mount, uint32_t now) {
    Visuals& v = s_visuals;
    if (v.impact && (int32_t(now - v.impactEnd) >= 0 || v.impact->m_attachParent != mount)) DestroyImpact();
    if (!v.impactPending || !mount) return;
    v.impactPending = false;
    DestroyImpact();
    char path[32];
    SStrPrintf(path, sizeof(path), "retail\\%u.m2", kImpactFileId);
    if (!(v.impact = mount->m_scene->CreateModel(path, 0))) return;
    v.impact->AttachToParent(mount, kImpactAttachment, nullptr, 1);
    v.impact->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 0, 1);
    v.impactEnd = now + kImpactMs;
}

void DestroyWind() {
    for (CM2Model*& model : s_visuals.wind) {
        if (!model) continue;
        if (model->m_attachParent) model->DetachFromParent();
        model->Release();
        model = nullptr;
    }
    s_visuals.windParent = nullptr;
    s_visuals.windEndAt = 0;
}

void UpdateWind(CM2Model* parent, bool on, uint32_t now) {
    Visuals& v = s_visuals;
    // A new mount model (remount, display change) takes fresh effects.
    if (v.windParent && v.windParent != parent) DestroyWind();
    if (on && parent && !v.windParent) {
        char path[32];
        for (size_t i = 0; i < kWindCount; ++i) {
            SStrPrintf(path, sizeof(path), "retail\\%u.m2", kWindParts[i].fileId);
            CM2Model* model = parent->m_scene->CreateModel(path, 0);
            if (!model) continue;
            model->AttachToParent(parent, 0, nullptr, 1);
            // An attached model's world transform is its offset from the parent (CM2Model::AnimateMT); camera parts
            // are placed every frame (PlaceCameraWind).
            model->m_worldTransform = WindOffset(kWindParts[i]);
            model->SetBoneSequence(0xFFFFFFFF, kWindParts[i].loopAnim, 0xFFFFFFFF, 0, 1.0f, 0, 1);
            v.wind[i] = model;
        }
        v.windParent = parent;
    } else if (on && v.windEndAt) {
        for (size_t i = 0; i < kWindCount; ++i) {
            if (v.wind[i]) v.wind[i]->SetBoneSequence(0xFFFFFFFF, kWindParts[i].loopAnim, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        }
        v.windEndAt = 0;
    } else if (!on && v.windParent && !v.windEndAt) {
        for (CM2Model* model : v.wind) {
            if (model) model->SetBoneSequence(0xFFFFFFFF, kWindEndAnim, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        }
        v.windEndAt = now + kWindLingerMs;
    } else if (!on && v.windEndAt && int32_t(now - v.windEndAt) >= 0) {
        DestroyWind();
    }
}

// Positioner 1797: halfway between the camera and the rider, turned with the camera, so the tunnel fills the view.
// Runs after the mount's world transform is final for the frame; the offset is expressed in the mount's frame.
void PlaceCameraWind(CGUnit_C& unit) {
    Visuals& v = s_visuals;
    CGCamera* camera = CGWorldFrame::GetActiveCamera();
    if (!v.windParent || !camera) return;
    const C3Vector eye = camera->m_position, rider = unit.GetPosition();
    const C3Vector forward = camera->Forward(), right = camera->Right(), up = camera->Up();
    for (size_t i = 0; i < kWindCount; ++i) {
        if (!v.wind[i] || !kWindParts[i].camera) continue;
        const float k = kWindParts[i].scale;
        C44Matrix world;
        world.Identity();
        world.a0 = forward.x * k; world.a1 = forward.y * k; world.a2 = forward.z * k;
        world.b0 = -right.x * k;  world.b1 = -right.y * k;  world.b2 = -right.z * k;
        world.c0 = up.x * k;      world.c1 = up.y * k;      world.c2 = up.z * k;
        world.d0 = (eye.x + rider.x) * 0.5f; world.d1 = (eye.y + rider.y) * 0.5f; world.d2 = (eye.z + rider.z) * 0.5f;
        v.wind[i]->m_worldTransform = world * v.windParent->m_worldTransform.Inverse();
    }
}

// First animation along the retail fallback chain that the model has; 0 when none (the native animation stays).
uint32_t Resolve(CM2Model* model, uint32_t id) {
    for (int guard = 0; id && guard < 12; ++guard) {
        if (model->HasSequence(id)) return id;
        uint32_t next = 0;
        for (const auto& pair : kFallback) {
            if (pair[0] == id) next = pair[1];
        }
        id = next;
    }
    return 0;
}

uint32_t SequenceMs(CM2Model* model, uint32_t id) {
    const M2Data* data = model->m_shared->m_data;
    const uint16_t index = model->FindSequenceIndex(data, id, 0);
    return index < data->m_sequences.m_count ? data->m_sequences[index].m_duration : 0;
}

// Replaces the one-shot queue: these play in order, each for its full length, before the flight state resumes.
void PlayOneShots(std::initializer_list<uint32_t> anims) {
    Visuals& v = s_visuals;
    v.queued = 0;
    for (uint32_t anim : anims) {
        if (v.queued < 3) v.queue[v.queued++] = anim;
    }
    v.oneShotEnd = 0;
}

// The ability's animation over the flight state, played when the client predicts the ability. The ability ones are
// retail's AnimKits (SpellVisualAnim 388575 Surge Forward, 385287 Skyward Ascent, 384115 Whirling Surge, 397544
// Aerial Halt).
void OnAbilityVisual(Command command, bool climbing) {
    const uint32_t now = FrameTime::s_curTimeMs;
    switch (command) {
        case Command::LiftOff: PlayOneShots({ Anim::FlapBig }); break;
        case Command::SurgeForward: PlayOneShots({ Anim::FlapBig }); s_visuals.impactPending = true; break;
        case Command::SkywardAscent: PlayOneShots({ Anim::FlapUp, Anim::SecondFlap }); break;
        case Command::WhirlingSurge: PlayOneShots({ Anim::Roll, Anim::RollEnd }); break;
        case Command::AerialHalt: PlayOneShots({ Anim::SlowFall }); s_visuals.impactPending = true; break;
        case Command::Flap: {
            const bool second = s_visuals.lastFlap && now - s_visuals.lastFlap < kSecondFlapMs;
            PlayOneShots({ climbing ? (second ? Anim::SecondFlap : Anim::FlapUp) : Anim::FlapForward });
            s_visuals.lastFlap = now;
            break;
        }
        default: break;
    }
}

uint32_t ChooseMountAnim(CM2Model* model, uint32_t now) {
    Visuals& v = s_visuals;
    const State& s = s_flight.sim;
    // One-shots: skip the ones the model resolves to nothing; each runs its sequence's length.
    while (v.queued) {
        const uint32_t id = Resolve(model, v.queue[0]);
        if (id && !v.oneShotEnd) v.oneShotEnd = now + std::max(200u, SequenceMs(model, id));
        if (id && int32_t(now - v.oneShotEnd) < 0) return id;
        for (uint32_t i = 1; i < v.queued; ++i) v.queue[i - 1] = v.queue[i];
        --v.queued;
        v.oneShotEnd = 0;
    }
    if (s.locomotion != Locomotion::Airborne) return 0;
    const float speed = s.velocity.Length();
    if (s.hoverSeconds > 0.0f) return Resolve(model, Anim::SlowFall);
    switch (Classify(s, s_session.profile)) {
        case AirState::Dive: return Resolve(model, Anim::DiveLoop);
        case AirState::Stall: return Resolve(model, Anim::SlowFall);
        case AirState::Climb: return Resolve(model, Anim::Up);
        default: break;
    }
    if (s_flight.bank > 0.35f) return Resolve(model, Anim::Left);
    if (s_flight.bank < -0.35f) return Resolve(model, Anim::Right);
    if (speed >= s_session.profile.thrillSpeed) return Resolve(model, Anim::GlideFast);
    if (speed < 0.5f * s_session.profile.thrillSpeed) return Resolve(model, Anim::GlideSlow);
    return Resolve(model, Anim::Glide);
}

void UpdateMountAnim(CGUnit_C& unit, uint32_t now) {
    Visuals& v = s_visuals;
    CM2Model* mount = unit.m_mountModel;
    const bool airborne = s_flight.sim.locomotion == Locomotion::Airborne;
    // Transitions: takeoff (an ability already queued its own), into and out of a dive, landing.
    if (airborne && !v.wasAirborne && !v.queued) PlayOneShots({ Anim::Start });
    if (!airborne && v.wasAirborne) {
        const bool moving = (unit.m_movementData.m_flags & (MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD)) != 0;
        PlayOneShots({ moving ? Anim::LandRun : Anim::Land });
    }
    const bool diving = airborne && Classify(s_flight.sim, s_session.profile) == AirState::Dive;
    if (airborne && diving != v.wasDiving && !v.queued) PlayOneShots({ diving ? Anim::DiveStart : Anim::DiveEnd });
    v.wasAirborne = airborne;
    v.wasDiving = diving;

    UpdateImpact(mount, now);
    const bool usable = mount && mount->IsLoaded(0, 0) && !unit.m_vehicle;
    const uint32_t want = usable ? ChooseMountAnim(mount, now) : 0;
    if (want && want == v.mountAnim) {
        // Something else replaced our sequence (not every native path goes through OnUnitMountSequence): put it back.
        M2BoneSequenceInfo info;
        if (mount->GetBoneSequenceInfo(0xFFFFFFFF, info) && info.m_sequenceId != want) {
            unit.SetKeyBoneSequence(mount, 0xFFFFFFFF, want, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
        }
        return;
    }
    if (want == v.mountAnim) return;
    const uint32_t previous = v.mountAnim;
    v.mountAnim = want;
    if (want) unit.SetKeyBoneSequence(mount, 0xFFFFFFFF, want, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
    // Hand the mount back standing; the native animation update replaces it as soon as the unit moves.
    else if (previous && usable) unit.SetKeyBoneSequence(mount, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1, 0);
}

void ResetVisuals() {
    DestroyWind();
    DestroyImpact();
    const bool forceWind = s_visuals.forceWind;
    s_visuals = Visuals();
    s_visuals.forceWind = forceWind;
}

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

void Land(CGUnit_C* unit, const char* reason) {
    sExtensionMgr->Log("Skyriding: landed (%s), flags 0x%08X", reason, unit ? unit->m_movementData.m_flags : 0u);
    s_flight.sim.locomotion = Locomotion::Grounded;
    s_flight.sim.boostSeconds = s_flight.sim.hoverSeconds = 0.0f;
    s_flight.lastStepOurs = false;
    s_flight.launchAt = 0;
    SetForceForward(unit, false);
    Signal();
}

void ResetFlight() {
    if (s_flight.forcing) SetForceForward(LocalPlayer(), false);
    ResetVisuals();
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
        // The native auto-land (CGUnit_C::UpdateFlightStatus) cancels flight under 1.5 yd once it is 2 s old: restart its clock.
        CGUnit_C::s_flightStatusTime = now;
    }
    s_session.resources.charges = uint8_t(s_session.resources.charges - Cost(command));
    Apply(s_flight.sim, command, s_session.profile, mover.m_facing);
    OnAbilityVisual(command, mover.m_pitch > 0.3f);
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
            if (f.sim.locomotion == Locomotion::Airborne && !(f.launchAt && time - f.launchAt < kLaunchGraceMs)) Land(&unit, (flags & MOVEMENTFLAG_FLYING) ? "transport, swimming or vehicle" : "native flying flag gone");
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
        // Retail skims the ground: only a real touchdown (below) lands, never the native height check.
        CGUnit_C::s_flightStatusTime = time;

        // Collision feedback: the engine may have shortened the last step (walls, ground).
        if (f.lastStepOurs && f.lastDt > 0.0f) {
            const Vec3 actual(mover.m_position.x - f.lastStart.x, mover.m_position.y - f.lastStart.y, mover.m_position.z - f.lastStart.z);
            const float expected = f.lastMove.Dot(f.lastMove);
            if (expected > 1e-6f && actual.Length() < 2.0f * std::sqrt(expected) + 1.0f && actual.Dot(f.lastMove) < 0.9f * expected) {
                // Touching down while descending: hand back to ground movement.
                if (f.lastMove.z < -0.02f && actual.z > f.lastMove.z * 0.5f && !(flags & MOVEMENTFLAG_ASCENDING)) {
                    mover.OnFlightLocal(time, 0);
                    Land(&unit, "touched down");
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

    bool OnUnitMountSequence(CGUnit_C& unit, uint32_t& sequenceId) override {
        if (!s_visuals.mountAnim || !(unit.m_obj->m_guid == ClntObjMgrGetActivePlayer())) return false;
        sequenceId = s_visuals.mountAnim;
        return true;
    }

    void OnUnitWorldTransformUpdated(CGUnit_C& unit, float) override {
        Flight& f = s_flight;
        if (!(unit.m_obj->m_guid == ClntObjMgrGetActivePlayer())) return;
        const uint32_t now = FrameTime::s_curTimeMs;
        const bool airborne = f.sim.locomotion == Locomotion::Airborne;
        const bool windOn = unit.m_mountModel && (s_visuals.forceWind || airborne) && (s_visuals.forceWind || f.sim.velocity.Length() >= s_session.profile.thrillSpeed || Classify(f.sim, s_session.profile) == AirState::Dive);
        UpdateWind(unit.m_mountModel, windOn, now);
        UpdateMountAnim(unit, now);
        CM2Model* model = unit.GetObjectModel();
        if (airborne && std::fabs(f.bank) >= 0.001f && model) {
            // Rows b (model left) and c (model up) turn about row a (model forward).
            C44Matrix& m = model->m_worldTransform;
            const float c = std::cos(f.bank), s = std::sin(f.bank);
            const C3Vector left(m.b0, m.b1, m.b2), up(m.c0, m.c1, m.c2);
            m.b0 = left.x * c - up.x * s; m.b1 = left.y * c - up.y * s; m.b2 = left.z * c - up.z * s;
            m.c0 = up.x * c + left.x * s; m.c1 = up.y * c + left.y * s; m.c2 = up.z * c + left.z * s;
        }
        PlaceCameraWind(unit);
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

        if (!Dynamic() && f.sim.locomotion == Locomotion::Airborne) Land(unit, "not authorized");
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
    if (arguments && !SStrCmpI(arguments, "wind", STORM_MAX_STR)) {
        s_visuals.forceWind = !s_visuals.forceWind;
        sExtensionMgr->Log("Skyriding: wind forced %s", s_visuals.forceWind ? "on" : "off");
        return 1;
    }
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
    sExtensionMgr->RegisterConsoleCommand("skyriding", Command_Skyriding, CATEGORY_GAME, "Skyriding status; 'skyriding steady|dynamic' sets the flight style, 'skyriding wind' toggles the speed effect");
    new SkyridingUnit();
    new SkyridingWorld();
    new SkyridingClient();
    new SkyridingClientDB();
}
