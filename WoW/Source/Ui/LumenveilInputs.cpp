// Not in the original 3.3.5a binary. What Lumenveil (Engine/Source/Lumenveil) reads of the running client each
// frame. The stock-client mod read these at fixed addresses (its engine.cpp and client_world.cpp); here they are the
// client's own variables, named, with the stock address each one replaces noted where the mapping is not obvious.
#include "Engine/Source/Lumenveil/Core/client_world.h"
#include "Engine/Source/Lumenveil/Core/engine.h"
#include "Engine/Source/Lumenveil/Core/fog_model.h"
#include "Engine/Source/Lumenveil/Core/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FFX/FFX.h"
#include "WoW/Source/Game/GameTime.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Types.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Object/Unit.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/World.h"

void ClientGameTimeChanged();

namespace {
    // The sun's day, as day fractions: the stock client's DNClouds::SetupBumpMap constants at 0x00A41CA4 and
    // 0x00A41CA0 (0x3E4E38E4 and 0x3F6C71C7), 4:50 and 22:10.
    constexpr float kSunDayStart = 29.0f / 144.0f;
    constexpr float kSunDayEnd = 133.0f / 144.0f;

    constexpr uint32_t kGroundProbeFlags = 0x100 | 0x10;   // terrain and WMOs
    constexpr uint32_t kUnitLootable = 0x1;                // UNIT_DYNFLAG_LOOTABLE
    constexpr uint32_t kCreatureGuidHigh = 0xF130;
    constexpr uint32_t kVehicleGuidHigh = 0xF150;
    constexpr uint32_t kPlayerGuidHigh = 0x0000;
    constexpr uint32_t kPetGuidHigh = 0xF140;
    constexpr uint32_t kMaxVisitedObjects = 4096;
    constexpr double kMaxIndoorStepSeconds = 0.25;

    struct CorpseScan {
        float from[3];
        int count;
        float position[kMaxCorpseInputs][3];
        uint32_t ids[kMaxCorpseInputs];
        float distanceSquared[kMaxCorpseInputs];
        uint32_t visited;
        int unitCount;
        UnitInput units[kMaxUnitInputs];
        uint64_t unitGuids[kMaxUnitInputs];
        uint64_t unitTargets[kMaxUnitInputs];
        float unitDistanceSquared[kMaxUnitInputs];
    };

    CorpseScan s_corpses = {};
    double s_corpseScanSeconds = -1.0;
    bool s_corpseLogged = false;
    bool s_unitsLogged = false;
    bool s_playerIndoors = false;
    float s_indoorWeight = 0.0f;
    double s_indoorSeconds = -1.0;
    bool s_playerLogged = false;
    uint32_t s_loggedClass = 0;

    void Copy(float* out, const C3Vector& v) {
        out[0] = v.x;
        out[1] = v.y;
        out[2] = v.z;
    }

    uint32_t Colour(const CImVector& c) {
        uint32_t value;
        std::memcpy(&value, &c, sizeof(value));   // b, g, r, a in memory: D3DCOLOR's 0xAARRGGBB
        return value;
    }

    bool Finite(const float* v, int count) {
        for (int i = 0; i < count; ++i) {
            if (!std::isfinite(v[i])) {
                return false;
            }
        }
        return true;
    }

    void DirectionFromSkyCentre(const C3Vector& body, const C3Vector& centre, float* out) {
        out[0] = body.x - centre.x;
        out[1] = body.y - centre.y;
        out[2] = body.z - centre.z;
        const float length = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
        if (length < 1e-6f) {
            out[0] = 0.0f;
            out[1] = 0.0f;
            out[2] = 1.0f;
            return;
        }
        out[0] /= length;
        out[1] /= length;
        out[2] /= length;
    }

    CGUnit_C* UnitOf(WOWGUID guid, uint32_t mask) {
        return guid ? static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, static_cast<TypeMask>(mask), __FILE__, __LINE__)) : nullptr;
    }

    CGUnit_C* ActivePlayer() {
        return UnitOf(ClntObjMgrGetActivePlayer(), TYPEMASK_UNIT);
    }

    void ReadWeather(FrameInputs& out) {
        out.weather = WeatherKind::Clear;
        out.weatherIntensity = 0.0f;
        const Weather* weather = World::s_weather;
        if (!weather) {
            return;
        }
        if (weather->m_rain) {
            out.weather = WeatherKind::Rain;
        } else if (weather->m_snow) {
            out.weather = WeatherKind::Snow;
        } else if (weather->m_sand) {
            out.weather = WeatherKind::Sand;
        }
        if (out.weather != WeatherKind::Clear && std::isfinite(weather->m_intensity)) {
            out.weatherIntensity = std::clamp(weather->m_intensity, 0.0f, 1.0f);
        }
    }

    // The glow full-screen effect's share of the picture, which Lumenveil's composite takes over from.
    float GlowScreenEffectAmount() {
        const FFX::Effect* effect = FFX::s_effect;
        if (!FFX::s_cvarFFX || !FFX::s_cvarFFX->m_intValue || !effect || effect != CGWorldFrame::ffxEffectGlow) {
            return 0.0f;
        }
        const float glow = DayNight::g_dnInfo.m_bands.m_glow;
        if (!effect->m_cvar || !effect->m_cvar->m_intValue || !std::isfinite(glow)) {
            return 0.0f;
        }
        return std::clamp(glow, 0.0f, 1.0f);
    }

    float ReadIndoorWeight(double seconds) {
        const CGUnit_C* player = ActivePlayer();
        const bool indoors = player && !player->IsOutdoors();
        if (indoors != s_playerIndoors) {
            s_playerIndoors = indoors;
            VF_LOG_INFO("indoors: the player is %s", indoors ? "indoors" : "outdoors");
        }
        const double step = s_indoorSeconds < 0.0 ? engine::kIndoorFadeSeconds : seconds - s_indoorSeconds;
        s_indoorSeconds = seconds;
        s_indoorWeight = engine::IndoorWeight(s_indoorWeight, indoors, std::clamp(step, 0.0, kMaxIndoorStepSeconds));
        return s_indoorWeight;
    }

    void ReadPlayer(FrameInputs& out) {
        out.playerKnown = false;
        out.targetKnown = false;
        CGUnit_C* player = ActivePlayer();
        if (!player || !player->m_unit) {
            return;
        }
        Copy(out.playerPos, player->GetPosition());
        out.playerFacing = player->GetFacing();
        if (!Finite(out.playerPos, 3) || !std::isfinite(out.playerFacing)) {
            return;
        }
        out.playerKnown = true;
        out.playerMoveFlags = player->m_clientFlags;
        out.playerMountDisplay = player->m_unit->m_unitFieldMountdisplayid;
        out.playerClass = player->m_unit->m_unitFieldBytes0[1];
        if (out.playerClass != s_loggedClass) {
            s_loggedClass = out.playerClass;
            VF_LOG_INFO("player: class %u", out.playerClass);
        }
        if (!s_playerLogged) {
            s_playerLogged = true;
            VF_LOG_INFO("player: at %.1f %.1f %.1f facing %.2f, movement flags 0x%08X", out.playerPos[0], out.playerPos[1],
                        out.playerPos[2], out.playerFacing, out.playerMoveFlags);
        }
        CGUnit_C* target = UnitOf(player->m_unit->m_unitFieldTarget, TYPEMASK_UNIT);
        if (!target || !target->m_unit) {
            return;
        }
        Copy(out.targetPos, target->GetPosition());
        out.targetBoundingRadius = target->m_unit->m_unitFieldBoundingradius;
        out.targetKnown = Finite(out.targetPos, 3);
    }

    void KeepUnit(CorpseScan& scan, WOWGUID guid, const CGUnitData* data, const float* at, float squared) {
        if (squared > engine::kUnitScanYards * engine::kUnitScanYards) {
            return;
        }
        int slot = scan.unitCount;
        if (scan.unitCount == kMaxUnitInputs) {
            slot = 0;
            for (int k = 1; k < kMaxUnitInputs; ++k) {
                if (scan.unitDistanceSquared[k] > scan.unitDistanceSquared[slot]) {
                    slot = k;
                }
            }
            if (scan.unitDistanceSquared[slot] <= squared) {
                return;
            }
        } else {
            ++scan.unitCount;
        }
        UnitInput& u = scan.units[slot];
        std::memcpy(u.feet, at, sizeof(u.feet));
        u.boundingRadius = data->m_unitFieldBoundingradius;
        u.target = -1;
        u.guid = guid.m_guid;
        scan.unitGuids[slot] = guid.m_guid;
        scan.unitTargets[slot] = data->m_unitFieldTarget.m_guid;
        scan.unitDistanceSquared[slot] = squared;
    }

    int32_t VisitObject(WOWGUID guid, void* param) {
        CorpseScan& scan = *static_cast<CorpseScan*>(param);
        if (++scan.visited > kMaxVisitedObjects) {
            return 0;
        }
        const uint32_t high = static_cast<uint32_t>(guid.m_guid >> 48);
        const bool creature = high == kCreatureGuidHigh || high == kVehicleGuidHigh;
        if (!creature && high != kPlayerGuidHigh && high != kPetGuidHigh) {
            return 1;
        }
        CGUnit_C* unit = UnitOf(guid, TYPEMASK_UNIT);
        if (!unit || (unit->m_typeId != ID_UNIT && unit->m_typeId != ID_PLAYER) || !unit->m_unit) {
            return 1;
        }
        const CGUnitData* data = unit->m_unit;
        const bool alive = data->m_unitFieldHealth != 0;
        const bool lootable = creature && unit->m_typeId == ID_UNIT && !alive && (data->m_unitDynamicFlags & kUnitLootable);
        if (!alive && !lootable) {
            return 1;
        }
        float at[3];
        Copy(at, unit->GetPosition());
        if (!Finite(at, 3)) {
            return 1;
        }
        const float d[3] = { at[0] - scan.from[0], at[1] - scan.from[1], at[2] - scan.from[2] };
        const float squared = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (alive) {
            KeepUnit(scan, guid, data, at, squared);
            return 1;
        }
        if (squared > engine::kCorpseScanYards * engine::kCorpseScanYards) {
            return 1;
        }
        int slot = scan.count;
        if (scan.count == kMaxCorpseInputs) {
            slot = 0;
            for (int k = 1; k < kMaxCorpseInputs; ++k) {
                if (scan.distanceSquared[k] > scan.distanceSquared[slot]) {
                    slot = k;
                }
            }
            if (scan.distanceSquared[slot] <= squared) {
                return 1;
            }
        } else {
            ++scan.count;
        }
        std::memcpy(scan.position[slot], at, sizeof(at));
        scan.ids[slot] = static_cast<uint32_t>(guid.m_guid);
        scan.distanceSquared[slot] = squared;
        return 1;
    }

    // The object list is walked ten times a second; between walks only the kept units' feet are refreshed.
    void ReadCorpses(FrameInputs& out) {
        if (s_corpseScanSeconds < 0.0 || out.seconds - s_corpseScanSeconds >= engine::kCorpseScanSeconds || out.seconds < s_corpseScanSeconds) {
            s_corpseScanSeconds = out.seconds;
            CorpseScan scan = {};
            std::memcpy(scan.from, out.camPos, sizeof(scan.from));
            ClntObjMgrEnumVisibleObjects(VisitObject, &scan);
            for (int i = 0; i < scan.unitCount; ++i) {
                for (int j = 0; j < scan.unitCount && scan.unitTargets[i]; ++j) {
                    if (j != i && scan.unitGuids[j] == scan.unitTargets[i]) {
                        scan.units[i].target = j;
                        break;
                    }
                }
            }
            s_corpses = scan;
            if (!s_unitsLogged && scan.unitCount > 0) {
                s_unitsLogged = true;
                VF_LOG_INFO("units: %d alive within %.0f yd of %u objects", scan.unitCount, engine::kUnitScanYards, scan.visited);
            }
            if (!s_corpseLogged && scan.count > 0) {
                s_corpseLogged = true;
                VF_LOG_INFO("corpses: %d lootable within %.0f yd of %u objects", scan.count, engine::kCorpseScanYards, scan.visited);
            }
        } else {
            for (int i = 0; i < s_corpses.unitCount; ++i) {
                WOWGUID guid;
                guid.m_guid = s_corpses.unitGuids[i];
                CGUnit_C* unit = UnitOf(guid, TYPEMASK_UNIT);
                float at[3];
                if (unit) {
                    Copy(at, unit->GetPosition());
                    if (Finite(at, 3)) {
                        std::memcpy(s_corpses.units[i].feet, at, sizeof(at));
                    }
                }
            }
        }
        out.corpseCount = s_corpses.count;
        std::memcpy(out.corpsePos, s_corpses.position, sizeof(out.corpsePos));
        std::memcpy(out.corpseIds, s_corpses.ids, sizeof(out.corpseIds));
        out.unitCount = s_corpses.unitCount;
        std::memcpy(out.units, s_corpses.units, sizeof(out.units));
    }
}

bool engine::ReadClientWorld(FrameInputs& out) {
    const DayNight::DNInfo& dn = DayNight::g_dnInfo;

    Copy(out.camPos, CWorldScene::s_activeWorldView);   // 0x00CD8F5C
    Copy(out.camTarget, CWorldScene::camTarget);

    out.dayFraction = dn.m_dayProgression;
    const bool clientMoonHours = !(out.dayFraction >= kSunDayStart && out.dayFraction <= kSunDayEnd);
    float toSun[3];
    float toMoon[3];
    DirectionFromSkyCentre(DayNight::g_planets.m_sun.m_position, dn.m_cameraPos, toSun);
    DirectionFromSkyCentre(DayNight::g_planets.m_moon.m_position, dn.m_cameraPos, toMoon);
    out.sunElevation = toSun[2];
    out.moonElevation = toMoon[2];
    out.lightIsMoon = MoonLightsTheFog(clientMoonHours, out.sunElevation, out.moonElevation);
    std::memcpy(out.toLight, out.lightIsMoon ? toMoon : toSun, sizeof(out.toLight));
    ReadWeather(out);

    // The stock mod's second day/night fog group (0x00D38BA0..A8) is DNInfo's m_fogInterior.
    out.fogColor = Colour(dn.m_fogInterior.m_color);
    out.fogStart = dn.m_fogInterior.m_start;
    out.fogEnd = dn.m_fogInterior.m_end;
    out.sunColor = Colour(dn.m_bands.m_sunCloudColors[0]);
    out.directColor = Colour(dn.m_bands.m_diffuse);
    out.ambientColor = Colour(dn.m_bands.m_ambient);
    out.inLiquid = engine::CameraInLiquid();
    out.mapId = s_mapId;
    out.zoneId = CGGameUI::m_areaID;
    out.skyAboveHorizonColor = Colour(dn.m_bands.m_skyColors[3]);
    out.skyHorizonColor = Colour(dn.m_bands.m_skyColors[4]);
    out.lightParams.stormBlend = std::isfinite(dn.m_stormBlend) ? std::clamp(dn.m_stormBlend, 0.0f, 1.0f) : 0.0f;
    const int32_t slot = static_cast<int32_t>(dn.m_overrideLightParamsId);
    if (slot >= 0 && slot < FogData::kLightParamsSlots) {
        out.lightParams.screenEffectSlot = slot;
    }
    out.indoors = ReadIndoorWeight(out.seconds);
    ReadPlayer(out);
    ReadCorpses(out);

    out.zoneFogDistance = dn.m_bands.m_fogEnd;
    out.clientGlowAmount = out.inLiquid ? 0.0f : GlowScreenEffectAmount();
    out.clientFarClip = World::s_farClip;

    return Finite(out.camPos, 3) && Finite(out.toLight, 3) && std::isfinite(out.fogEnd) && std::isfinite(out.fogStart);
}

bool engine::CameraInLiquid() {
    return s_liquidTypeId != 0;   // 0x00CD8794
}

engine::StockFog engine::ReadStockFog() {
    StockFog fog;
    fog.start[0] = DayNight::g_dnInfo.m_fog.m_start;
    fog.end[0] = DayNight::g_dnInfo.m_fog.m_end;
    fog.start[1] = DayNight::g_dnInfo.m_fogInterior.m_start;
    fog.end[1] = DayNight::g_dnInfo.m_fogInterior.m_end;
    return fog;
}

void engine::WriteStockFog(const StockFog& fog) {
    DayNight::g_dnInfo.m_fog.m_start = fog.start[0];
    DayNight::g_dnInfo.m_fog.m_end = fog.end[0];
    DayNight::g_dnInfo.m_fogInterior.m_start = fog.start[1];
    DayNight::g_dnInfo.m_fogInterior.m_end = fog.end[1];
}

bool engine::ProbeGroundHeight(void*, float x, float y, float top, float bottom, float& height) {
    const C3Vector start(x, y, top);
    const C3Vector end(x, y, bottom);
    C3Vector hit(x, y, bottom);
    float fraction = 1.0f;
    const bool found = World::Intersect(start, end, &hit, &fraction, kGroundProbeFlags, nullptr) != 0;
    height = hit.z;
    return found && fraction >= 0.0f && fraction <= 1.0f;
}

// client_world.h: the region panel's clock and weather, which change them on the player's own screen only. The clock
// is set as the mod sets the stock client's (its client_world.cpp SetClock); the weather is read only.
// shortcut: GameWeather::Set returns false, upgrade when the region panel's weather override is ported.
namespace {
    bool s_clientWorldEnabled = false;
}

bool ClientWorldMatchesClient() {
    return true;
}

void EnableClientWorld() {
    s_clientWorldEnabled = true;
}

bool ClientWorldEnabled() {
    return s_clientWorldEnabled;
}

bool ClientWorldFailed() {
    return false;
}

ClientClockState GameClock::Read() {
    ClientClockState state = {};
    const int minute = g_clientGameTime.m_minute;
    const int hour = g_clientGameTime.m_hour;
    const float speed = g_clientGameTime.m_minutesPerSecond;
    state.valid = minute >= 0 && minute < 60 && hour >= 0 && hour < 24 && speed > 0.0f;
    state.minute = hour * 60 + minute;
    state.speed = speed;
    return state;
}

bool GameClock::Set(int minute, float speed) {
    if (!s_clientWorldEnabled) {
        return false;
    }
    // The clock keeps the server's time less its own offset; the time set is the one the server would send.
    WowTime time = g_clientGameTime;
    const int stored = WrapMinute(WrapMinute(minute) - g_clientGameTime.m_minuteOffset);
    time.m_minute = stored % 60;
    time.m_hour = stored / 60;
    g_clientGameTime.GameTimeSetTime(time, true);
    ClientGameTimeChanged();
    g_clientGameTime.GameTimeSetMinutesPerSecond(std::clamp(speed, kClientSlowestClock, kClientFastestClock));
    return true;
}

ClientWeatherState GameWeather::Read() {
    ClientWeatherState state = {};
    const Weather* weather = World::s_weather;
    state.valid = weather != nullptr;
    if (weather) {
        state.type = weather->m_pendingType;
        state.target = weather->m_targetIntensity;
        state.current = weather->m_intensity;
    }
    return state;
}

bool GameWeather::Set(int, float, bool) {
    return false;
}

int CurrentAreaId() {
    return CGGameUI::m_subzoneID;   // 0x00BD0810
}
