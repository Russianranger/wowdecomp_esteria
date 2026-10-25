#ifndef ESTERIA_SKYRIDING_RESOURCES_H
#define ESTERIA_SKYRIDING_RESOURCES_H

#include <algorithm>
#include <cstdint>
#include "SkyridingPhysics.h"

// Shared verbatim between the client (Engine/Source/Skyriding/) and the server (src/server/shared/Skyriding/).
// Ability costs, cooldowns and charge regeneration. The server's copy is the authority; the client only mirrors what
// the server last sent and predicts the cost of a press. All numbers are tuning-only.

namespace Skyriding {

// Charges: retail 11.2.7 shared charges. Vigor: Dragonflight, regenerates only on the ground or at Thrill speed.
enum class ResourceProfile : uint8_t { Charges = 0, Vigor = 1 };

enum class AbilityResult : uint8_t { Ok = 0, NoCharge = 1, Cooldown = 2, BadState = 3, Disabled = 4 };

struct ResourceRules {
    ResourceProfile profile = ResourceProfile::Charges;
    uint8_t maxCharges = 6;
    uint32_t regenMs = 12000;           // Charges: one charge per this, anywhere
    uint32_t thrillRegenMs = 4000;      // Charges: above Thrill speed. Vigor: thrillVigorMs below
    uint32_t groundRegenMs = 5000;      // Vigor: on the ground
    uint32_t thrillVigorMs = 15000;     // Vigor: above Thrill speed in the air
    uint32_t whirlCooldownMs = 30000;
    uint32_t haltCooldownMs = 6000;
    uint32_t flapCooldownMs = 600;
};

inline uint8_t Cost(Command c) {
    return c == Command::LiftOff || c == Command::SurgeForward || c == Command::SkywardAscent ? 1 : 0;
}

inline uint32_t CooldownMs(Command c, const ResourceRules& r) {
    switch (c) {
        case Command::WhirlingSurge: return r.whirlCooldownMs;
        case Command::AerialHalt: return r.haltCooldownMs;
        case Command::Flap: return r.flapCooldownMs;
        default: return 0;
    }
}

// Lift Off leaves the ground; every other ability is used in the air.
inline bool NeedsAirborne(Command c) { return c != Command::LiftOff; }

struct Resources {
    uint8_t charges = 6;
    uint32_t regenProgressMs = 0;
    uint32_t cooldownLeftMs[CommandCount] = {};

    void Reset(const ResourceRules& r) { *this = Resources(); charges = r.maxCharges; }

    AbilityResult Check(Command c, bool airborne) const {
        if (c == Command::None || static_cast<uint8_t>(c) >= CommandCount) return AbilityResult::BadState;
        if (NeedsAirborne(c) != airborne) return AbilityResult::BadState;
        if (cooldownLeftMs[static_cast<uint8_t>(c)]) return AbilityResult::Cooldown;
        if (charges < Cost(c)) return AbilityResult::NoCharge;
        return AbilityResult::Ok;
    }

    AbilityResult Use(Command c, bool airborne, const ResourceRules& r) {
        const AbilityResult result = Check(c, airborne);
        if (result != AbilityResult::Ok) return result;
        charges = static_cast<uint8_t>(charges - Cost(c));
        cooldownLeftMs[static_cast<uint8_t>(c)] = CooldownMs(c, r);
        return result;
    }

    // Current regeneration period, 0 when nothing regenerates in this situation.
    static uint32_t Period(const ResourceRules& r, bool airborne, bool thrill) {
        if (r.profile == ResourceProfile::Charges) return thrill ? r.thrillRegenMs : r.regenMs;
        if (!airborne) return r.groundRegenMs;
        return thrill ? r.thrillVigorMs : 0;
    }

    // Advances cooldowns and regeneration. Returns true when the charge count changed.
    bool Tick(uint32_t dtMs, const ResourceRules& r, bool airborne, bool thrill) {
        for (uint32_t& left : cooldownLeftMs) left = left > dtMs ? left - dtMs : 0;
        const uint32_t period = Period(r, airborne, thrill);
        if (charges >= r.maxCharges || !period) {
            if (charges >= r.maxCharges) regenProgressMs = 0;
            return false;
        }
        regenProgressMs += dtMs;
        bool changed = false;
        while (regenProgressMs >= period && charges < r.maxCharges) {
            regenProgressMs -= period;
            ++charges;
            changed = true;
        }
        if (charges >= r.maxCharges) regenProgressMs = 0;
        return changed;
    }

    uint32_t NextChargeMs(const ResourceRules& r, bool airborne, bool thrill) const {
        const uint32_t period = Period(r, airborne, thrill);
        if (charges >= r.maxCharges || !period) return 0;
        return period - std::min(regenProgressMs, period);
    }
};

}  // namespace Skyriding

#endif
