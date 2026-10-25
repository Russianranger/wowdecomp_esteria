#ifndef ESTERIA_SKYRIDING_ENVELOPE_H
#define ESTERIA_SKYRIDING_ENVELOPE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "SkyridingPhysics.h"

// Shared verbatim between the client (Engine/Source/Skyriding/) and the server (src/server/shared/Skyriding/).
// The server's check of a rider's reported positions. The simulator never raises E = v^2/2 + g z on its own
// (SkyridingPhysics.h), so the budget (an upper bound of the rider's true E) grows only by approved ability credit
// plus a small drift, and follows the measured E down. A sample may exceed the budget by a fixed noise allowance,
// which never accumulates. Needs no world geometry, unlike a re-simulation. Also: a speed cap, and no hovering
// outside Aerial Halt.

namespace Skyriding {

struct EnvelopeConfig {
    float driftPerSecond = 25.0f;        // yd^2/s^2 the budget may grow per second without credit (integration error)
    float noise = 300.0f;                // yd^2/s^2 a single sample may read above the budget
    uint32_t hoverMs = 2500;             // longest stay below hoverSpeed
    uint32_t creditMs = 4000;            // unused ability credit expires after this
    float hoverSpeed = 3.0f;
    uint32_t minSampleMs = 80;           // closer samples only get the height check
    uint32_t clockSlackMs = 1000;        // how far the client's clock may run ahead of the server's since the start
};

enum class EnvelopeResult : uint8_t { Ok = 0, Speed, Energy, Height, Hover };

struct Envelope {
    bool have = false;
    Vec3 last;
    uint32_t clientTime = 0;               // last sample, client clock (clamped, see Check)
    uint32_t clientBase = 0, serverBase = 0;  // first sample
    float budget = 0.0f;   // upper bound of the rider's energy at the last sample
    float credit = 0.0f;   // approved ability energy not yet seen
    uint32_t creditUntil = 0;
    float speed = 0.0f;
    uint32_t slowMs = 0;
    uint32_t hoverUntil = 0;

    void Reset() { have = false; slowMs = 0; }

    void Grant(Command command, const Profile& p, const EnvelopeConfig& c, uint32_t serverNow) {
        // Generous: the bound is the worst case at top speed. A boost spends it over several samples.
        credit += EnergyGain(command, p) * 1.25f;
        creditUntil = serverNow + c.creditMs;
        if (command == Command::AerialHalt) hoverUntil = serverNow + uint32_t((p.haltSeconds + 0.5f) * 1000.0f);
    }

    // One airborne sample. startSpeed bounds the speed of the first sample (ground speed plus a jump). Times in ms;
    // the client's are the packet's, the server's are when it arrived.
    EnvelopeResult Check(const Vec3& pos, uint32_t clientNow, uint32_t serverNow, float startSpeed, const Profile& p, const EnvelopeConfig& c) {
        const float potential = p.gravity * pos.z;
        if (credit > 0.0f && int32_t(serverNow - creditUntil) >= 0) credit = 0.0f;
        if (!have) {
            budget = 0.5f * startSpeed * startSpeed + potential + credit;
            credit = 0.0f;
            have = true;
            last = pos;
            clientTime = clientBase = clientNow;
            serverBase = serverNow;
            slowMs = 0;
            return EnvelopeResult::Ok;
        }

        // The client's clock may not gain on the server's (a stretched interval would hide speed); per sample it may
        // jitter freely, since packets can arrive in bursts.
        const uint32_t latest = clientBase + (serverNow - serverBase) + c.clockSlackMs;
        if (int32_t(clientNow - latest) > 0) clientNow = latest;
        const int32_t clientMs = int32_t(clientNow - clientTime);
        if (clientMs < int32_t(c.minSampleMs)) {
            // Too close to the last sample for a speed; height alone still has to fit the budget.
            return potential > budget + credit + c.noise ? EnvelopeResult::Height : EnvelopeResult::Ok;
        }

        const float dt = clientMs * 0.001f;
        const float measured = (pos - last).Length() / dt;
        if (measured > p.maxBoostSpeed * 1.15f + 5.0f) return EnvelopeResult::Speed;

        const float energy = 0.5f * measured * measured + potential;
        const float available = budget + credit + c.driftPerSecond * dt;
        if (energy > available + c.noise) return EnvelopeResult::Energy;

        if (measured < c.hoverSpeed && int32_t(serverNow - hoverUntil) >= 0) slowMs += uint32_t(dt * 1000.0f);
        else slowMs = 0;
        if (slowMs > c.hoverMs) return EnvelopeResult::Hover;

        const float next = std::min(available, energy + c.noise);
        credit = std::max(0.0f, budget + credit - next);
        budget = next;
        speed = measured;
        last = pos;
        clientTime = clientNow;
        return EnvelopeResult::Ok;
    }
};

}  // namespace Skyriding

#endif
