#ifndef ESTERIA_SKYRIDING_PHYSICS_H
#define ESTERIA_SKYRIDING_PHYSICS_H

#include <cmath>
#include <cstdint>

// Shared verbatim between the client (Engine/Source/Skyriding/SkyridingPhysics.h) and the server
// (src/server/shared/Skyriding/SkyridingPhysics.h). Do not let the copies drift. No game types: plain floats.
//
// Momentum flight for Skyriding (docs/SKYRIDING_PHYSICS.md). One formulation: world velocity v under gravity, a lift
// force perpendicular to the path that holds the path against gravity when fast enough, drag along the path, and
// steering that only rotates v. Gravity, lift and steering never add energy, drag and the speed caps only remove it;
// only Command effects (abilities) add it. The server relies on that to validate client positions (EnergyGain).
//
// Units: yards, seconds, radians. x north, y west, z up; facing 0 = +x, counter-clockwise; pitch up positive.
// Every Profile value is a tuning-only placeholder, not a Blizzard number.

namespace Skyriding {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    float Length() const { return std::sqrt(Dot(*this)); }
    bool Finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

inline Vec3 Direction(float facing, float pitch) {
    const float c = std::cos(pitch);
    return { c * std::cos(facing), c * std::sin(facing), std::sin(pitch) };
}

enum class Locomotion : uint8_t { Grounded = 0, Airborne = 1 };
enum class AirState : uint8_t { Glide = 0, Dive = 1, Climb = 2, Stall = 3 };

// Ability effects the server approved (or the client predicts). Values are on the wire (SkyridingWire.h).
enum class Command : uint8_t { None = 0, LiftOff = 1, SurgeForward = 2, SkywardAscent = 3, WhirlingSurge = 4, AerialHalt = 5, Flap = 6 };
constexpr uint8_t CommandCount = 7;

struct Profile {
    float gravity = 19.29f;         // the client's falling acceleration
    float dragLinear = 0.01f;       // 1/s
    float dragQuadratic = 0.0006f;  // 1/yd
    float overspeedDrag = 6.0f;     // 1/s, bleeds speed above maxAirSpeed once a boost ends
    float minGlideSpeed = 20.0f;    // full lift at and above this speed
    float stallSpeed = 10.0f;       // below this the nose cannot be pulled up
    float liftEfficiency = 0.985f;  // < 1: a level glide slowly sinks
    float maxAirSpeed = 65.0f;
    float maxBoostSpeed = 80.0f;
    float turnResponse = 3.5f;      // 1/s at zero speed
    float turnSpeedRef = 45.0f;     // turn response halves at this speed: wider turns when fast
    float surgeAccel = 30.0f, surgeSeconds = 1.0f;
    float whirlAccel = 45.0f, whirlSeconds = 1.5f;
    float ascentImpulse = 30.0f;
    float liftOffUp = 32.0f, liftOffForward = 12.0f;
    float flapImpulse = 7.0f;
    float haltFactor = 0.15f, haltSeconds = 1.0f, haltGravityScale = 0.15f;
    float thrillSpeed = 50.0f;      // Thrill of the Skies threshold (resource policies)

    // Field order is the wire order (SkyridingWire.h). Ranges reject nonsense from a bad server config.
    static constexpr uint32_t FieldCount = 23;
    float* Fields() { return &gravity; }
    const float* Fields() const { return &gravity; }
    bool Valid() const {
        for (uint32_t i = 0; i < FieldCount; ++i) {
            if (!std::isfinite(Fields()[i]) || Fields()[i] < 0.0f || Fields()[i] > 1000.0f) return false;
        }
        return gravity > 0.0f && minGlideSpeed > stallSpeed && maxBoostSpeed >= maxAirSpeed && maxAirSpeed > 0.0f
            && liftEfficiency <= 1.0f && haltFactor <= 1.0f && haltGravityScale <= 1.0f;
    }
};
static_assert(sizeof(Profile) == Profile::FieldCount * sizeof(float), "Profile must stay a flat float array");

struct State {
    Vec3 position;
    Vec3 velocity;
    Locomotion locomotion = Locomotion::Grounded;
    float boostAccel = 0.0f;
    float boostSeconds = 0.0f;
    float hoverSeconds = 0.0f;
    uint32_t tick = 0;
};

// Specific mechanical energy (per unit mass): what the server's envelope check tracks.
inline float Energy(const Vec3& position, const Vec3& velocity, const Profile& p) {
    return 0.5f * velocity.Dot(velocity) + p.gravity * position.z;
}

inline AirState Classify(const State& s, const Profile& p) {
    const float speed = s.velocity.Length();
    if (speed < p.stallSpeed) return AirState::Stall;
    const float climb = s.velocity.z / speed;
    if (climb < -0.2f) return AirState::Dive;
    if (climb > 0.2f) return AirState::Climb;
    return AirState::Glide;
}

// Applies an approved ability. facing is the rider's view (Lift Off launches along it).
inline void Apply(State& s, Command command, const Profile& p, float facing) {
    switch (command) {
        case Command::LiftOff:
            s.locomotion = Locomotion::Airborne;
            s.velocity.z = std::fmax(s.velocity.z, 0.0f) + p.liftOffUp;
            s.velocity += Direction(facing, 0.0f) * p.liftOffForward;
            break;
        case Command::SurgeForward:
            s.boostAccel = std::fmax(s.boostSeconds > 0.0f ? s.boostAccel : 0.0f, p.surgeAccel);
            s.boostSeconds = std::fmax(s.boostSeconds, p.surgeSeconds);
            break;
        case Command::WhirlingSurge:
            s.boostAccel = std::fmax(s.boostSeconds > 0.0f ? s.boostAccel : 0.0f, p.whirlAccel);
            s.boostSeconds = std::fmax(s.boostSeconds, p.whirlSeconds);
            break;
        case Command::SkywardAscent:
            s.velocity.z += p.ascentImpulse;
            break;
        case Command::AerialHalt:
            s.velocity = s.velocity * p.haltFactor;
            s.boostSeconds = 0.0f;
            s.hoverSeconds = p.haltSeconds;
            break;
        case Command::Flap:
            s.velocity.z += p.flapImpulse;
            break;
        case Command::None:
            break;
    }
}

// Upper bound of the energy one approved command can add, for the server's envelope (worst case at maxBoostSpeed).
inline float EnergyGain(Command command, const Profile& p) {
    const float v = p.maxBoostSpeed;
    const auto impulse = [v](float dv) { return v * dv + 0.5f * dv * dv; };
    switch (command) {
        case Command::LiftOff: return impulse(p.liftOffUp + p.liftOffForward);
        case Command::SurgeForward: return p.surgeAccel * p.surgeSeconds * v;
        case Command::WhirlingSurge: return p.whirlAccel * p.whirlSeconds * v;
        case Command::SkywardAscent: return impulse(p.ascentImpulse);
        case Command::Flap: return impulse(p.flapImpulse);
        // Reduced gravity while hovering lets a rising rider keep some potential energy.
        case Command::AerialHalt: return p.gravity * (1.0f - p.haltGravityScale) * p.haltFactor * v * p.haltSeconds;
        case Command::None: break;
    }
    return 0.0f;
}

// One integration step of an airborne rider. facing/pitch: where the rider wants to go. Returns the displacement
// (also added to s.position). A grounded rider does not move here: ground movement stays native.
inline Vec3 Step(State& s, float facing, float pitch, const Profile& p, float dt) {
    if (s.locomotion != Locomotion::Airborne || !(dt > 0.0f) || dt > 0.25f) return {};
    if (!s.velocity.Finite()) s.velocity = {};
    if (!std::isfinite(facing)) facing = 0.0f;
    if (!std::isfinite(pitch)) pitch = 0.0f;
    pitch = std::fmax(-1.5f, std::fmin(1.5f, pitch));

    const Vec3 want = Direction(facing, pitch);
    float speed = s.velocity.Length();
    Vec3 path = speed > 0.01f ? s.velocity * (1.0f / speed) : want;

    // Steering rotates the path toward the view; speed is kept, so no energy is added. Fast riders turn wider and a
    // stalled rider cannot pull the nose up.
    float alpha = 1.0f - std::exp(-p.turnResponse / (1.0f + speed / p.turnSpeedRef) * dt);
    if (want.z > path.z) {
        alpha *= std::fmax(0.0f, std::fmin(1.0f, (speed - p.stallSpeed) / (p.minGlideSpeed - p.stallSpeed)));
    }
    Vec3 turned = path + (want - path) * alpha;
    const float turnedLength = turned.Length();
    if (turnedLength > 1e-4f) {
        path = turned * (1.0f / turnedLength);
        s.velocity = path * speed;
    }

    const float gravity = s.hoverSeconds > 0.0f ? p.gravity * p.haltGravityScale : p.gravity;
    Vec3 accel(0.0f, 0.0f, -gravity);

    // Lift: perpendicular to the path in its vertical plane, cancelling (most of) gravity's pull across the path,
    // limited by speed squared so a slow rider drops. Perpendicular, so it does no work.
    Vec3 normal = Vec3(0.0f, 0.0f, 1.0f) - path * path.z;
    const float normalLength = normal.Length();
    if (normalLength > 1e-3f) {
        normal = normal * (1.0f / normalLength);
        const float needed = gravity * normal.z * p.liftEfficiency;
        const float capacity = p.gravity / (p.minGlideSpeed * p.minGlideSpeed) * speed * speed;
        accel += normal * std::fmin(needed, capacity);
    }

    float along = -(p.dragLinear * speed + p.dragQuadratic * speed * speed);
    if (s.boostSeconds > 0.0f) {
        along += s.boostAccel;
        s.boostSeconds = std::fmax(0.0f, s.boostSeconds - dt);
    } else if (speed > p.maxAirSpeed) {
        along -= (speed - p.maxAirSpeed) * p.overspeedDrag;
    }
    accel += path * along;
    s.hoverSeconds = std::fmax(0.0f, s.hoverSeconds - dt);

    s.velocity += accel * dt;
    speed = s.velocity.Length();
    if (speed > p.maxBoostSpeed) s.velocity = s.velocity * (p.maxBoostSpeed / speed);

    const Vec3 move = s.velocity * dt;
    s.position += move;
    ++s.tick;
    return move;
}

// Advances by a frame of any length in equal substeps of at most maxStep (frame-rate independent within a few
// percent; the server does not replay, so exact fixed-step determinism is not needed). Long frames are cut at 0.25 s.
inline Vec3 Advance(State& s, float facing, float pitch, const Profile& p, float frameSeconds, float maxStep = 0.02f) {
    if (!(frameSeconds > 0.0f)) return {};
    frameSeconds = std::fmin(frameSeconds, 0.25f);
    const int steps = static_cast<int>(std::ceil(frameSeconds / maxStep));
    const float dt = frameSeconds / static_cast<float>(steps);
    Vec3 total;
    for (int i = 0; i < steps; ++i) total += Step(s, facing, pitch, p, dt);
    return total;
}

}  // namespace Skyriding

#endif
