// Behaviour checks for the shared Skyriding headers (docs/SKYRIDING_PHYSICS.md, docs/SKYRIDING_PROTOCOL.md).
// cmake -S Tools/SkyridingTests -B build/skyriding-tests && cmake --build build/skyriding-tests --config Release
// && ctest --test-dir build/skyriding-tests -C Release --output-on-failure

#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include "Engine/Source/Skyriding/SkyridingEnvelope.h"
#include "Engine/Source/Skyriding/SkyridingPhysics.h"
#include "Engine/Source/Skyriding/SkyridingResources.h"
#include "Engine/Source/Skyriding/SkyridingWire.h"

using namespace Skyriding;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++g_failures; std::printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const float kPi = 3.14159265f;
static const Profile kProfile;

static State Flying(float speed, float pitch = 0.0f, float z = 500.0f) {
    State s;
    s.locomotion = Locomotion::Airborne;
    s.position = { 0.0f, 0.0f, z };
    s.velocity = Direction(0.0f, pitch) * speed;
    return s;
}

static void Run(State& s, float facing, float pitch, float seconds, float fps = 60.0f) {
    for (float t = 0.0f; t < seconds - 1e-4f; t += 1.0f / fps) Advance(s, facing, pitch, kProfile, 1.0f / fps);
}

static float Speed(const State& s) { return s.velocity.Length(); }

static void TestMomentum() {
    State s = Flying(40.0f);
    Run(s, 0.0f, 0.0f, 1.0f);
    CHECK(Speed(s) > 30.0f && s.position.x > 30.0f, "speed %.2f x %.2f", Speed(s), s.position.x);
}

static void TestDive() {
    State s = Flying(30.0f, -1.05f);
    Run(s, 0.0f, -1.05f, 3.0f);
    CHECK(Speed(s) > 55.0f && Speed(s) <= kProfile.maxBoostSpeed, "dive speed %.2f", Speed(s));
    CHECK(Classify(s, kProfile) == AirState::Dive, "dive state %d", int(Classify(s, kProfile)));
    Run(s, 0.0f, -1.05f, 6.0f);
    CHECK(Speed(s) <= kProfile.maxAirSpeed + 3.0f, "dive settles at the cap: %.2f", Speed(s));
}

static void TestClimb() {
    State s = Flying(50.0f, 0.78f);
    const float z0 = s.position.z;
    Run(s, 0.0f, 0.78f, 1.5f);
    CHECK(s.position.z - z0 > 30.0f, "climb gained %.2f", s.position.z - z0);
    CHECK(Speed(s) < 35.0f, "climb speed %.2f", Speed(s));
}

static void TestGlideSink() {
    State s = Flying(40.0f);
    const float z0 = s.position.z;
    Run(s, 0.0f, 0.0f, 5.0f);
    const float sink = (z0 - s.position.z) / 5.0f;
    CHECK(sink > 0.05f && sink < 10.0f, "level glide sink %.3f yd/s", sink);
}

static void TestStallAndRecovery() {
    State s = Flying(5.0f, 0.5f);
    CHECK(Classify(s, kProfile) == AirState::Stall, "slow is stall");
    // Holding the nose up while stalled: the rider drops until fast enough, then the nose comes back (a phugoid).
    float lowestVz = 0.0f;
    for (int i = 0; i < 120; ++i) {
        Advance(s, 0.0f, 0.5f, kProfile, 1.0f / 60.0f);
        lowestVz = std::fmin(lowestVz, s.velocity.z);
    }
    CHECK(lowestVz < -8.0f && s.position.z < 500.0f, "stalled rider drops: lowest vz %.2f z %.1f", lowestVz, s.position.z);
    Run(s, 0.0f, -0.6f, 2.0f);
    CHECK(Speed(s) > kProfile.stallSpeed && Classify(s, kProfile) != AirState::Stall, "recovers: %.2f", Speed(s));
}

static float TimeToTurn(float speed) {
    State s = Flying(speed);
    for (int i = 1; i < 2000; ++i) {
        Advance(s, kPi / 2.0f, 0.0f, kProfile, 0.01f);
        if (std::atan2(s.velocity.y, s.velocity.x) > 80.0f * kPi / 180.0f) return i * 0.01f;
    }
    return 99.0f;
}

static void TestTurns() {
    const float slow = TimeToTurn(20.0f), fast = TimeToTurn(60.0f);
    CHECK(slow < fast && fast < 5.0f, "turn time slow %.2f fast %.2f", slow, fast);
}

static void TestNoEnergyWithoutCommands() {
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> yaw(-kPi, kPi), pitch(-1.4f, 1.4f), speed(0.0f, 75.0f);
    for (int run = 0; run < 50; ++run) {
        State s = Flying(speed(rng), pitch(rng));
        float energy = Energy(s.position, s.velocity, kProfile);
        const float f = yaw(rng), p = pitch(rng);
        for (int i = 0; i < 500; ++i) {
            Advance(s, f, p, kProfile, 0.016f);
            const float next = Energy(s.position, s.velocity, kProfile);
            CHECK(next <= energy + 0.25f, "run %d step %d gained %.4f", run, i, next - energy);
            energy = next;
        }
    }
}

static void TestCommandsAndBounds() {
    for (uint8_t c = 1; c < CommandCount; ++c) {
        State s = Flying(40.0f, 0.3f);
        if (Command(c) == Command::LiftOff) { s.locomotion = Locomotion::Grounded; s.velocity = { 7.0f, 0.0f, 0.0f }; }
        const float before = Energy(s.position, s.velocity, kProfile);
        Apply(s, Command(c), kProfile, 0.0f);
        Run(s, 0.0f, 0.3f, 2.0f);
        State base = Flying(40.0f, 0.3f);
        Run(base, 0.0f, 0.3f, 2.0f);
        const float gain = Energy(s.position, s.velocity, kProfile) - before;
        CHECK(gain <= EnergyGain(Command(c), kProfile) + 1.0f, "command %u gain %.1f bound %.1f", c, gain, EnergyGain(Command(c), kProfile));
        CHECK(s.velocity.Finite() && s.position.Finite(), "command %u finite", c);
    }
    State surge = Flying(40.0f), plain = Flying(40.0f);
    Apply(surge, Command::SurgeForward, kProfile, 0.0f);
    Run(surge, 0.0f, 0.0f, 1.0f);
    Run(plain, 0.0f, 0.0f, 1.0f);
    CHECK(Speed(surge) > Speed(plain) + 15.0f, "surge %.1f vs %.1f", Speed(surge), Speed(plain));

    State ascent = Flying(30.0f);
    Apply(ascent, Command::SkywardAscent, kProfile, 0.0f);
    Run(ascent, 0.0f, 0.0f, 1.0f);
    CHECK(ascent.position.z > 510.0f, "ascent height %.1f", ascent.position.z);

    State halt = Flying(60.0f);
    Apply(halt, Command::AerialHalt, kProfile, 0.0f);
    CHECK(Speed(halt) < 10.0f, "halt speed %.1f", Speed(halt));

    State ground;
    ground.velocity = { 7.0f, 0.0f, 0.0f };
    CHECK(Step(ground, 0.0f, 0.0f, kProfile, 0.02f).Length() == 0.0f, "grounded riders are native");
    Apply(ground, Command::LiftOff, kProfile, 0.0f);
    Run(ground, 0.0f, 0.2f, 1.0f);
    CHECK(ground.locomotion == Locomotion::Airborne && ground.position.z > 10.0f, "lift off z %.1f", ground.position.z);
}

static void TestFrameRateIndependence() {
    State a = Flying(45.0f), b = Flying(45.0f);
    Apply(a, Command::SurgeForward, kProfile, 0.0f);
    Apply(b, Command::SurgeForward, kProfile, 0.0f);
    Run(a, 0.6f, -0.3f, 5.0f, 30.0f);
    Run(b, 0.6f, -0.3f, 5.0f, 144.0f);
    const float error = (a.position - b.position).Length(), path = a.position.Length();
    CHECK(error < 0.02f * path, "30 vs 144 fps differ by %.2f yd over %.1f", error, path);
}

static void TestReproducible() {
    State a = Flying(33.0f, 0.2f), b = Flying(33.0f, 0.2f);
    for (int i = 0; i < 400; ++i) {
        const float f = std::sin(i * 0.05f), p = 0.5f * std::cos(i * 0.03f);
        Advance(a, f, p, kProfile, 0.02f);
        Advance(b, f, p, kProfile, 0.02f);
    }
    CHECK(a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z, "same inputs, same result");
}

static void TestBadInput() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    State s = Flying(40.0f);
    s.velocity.x = nan;
    Advance(s, nan, inf, kProfile, 0.016f);
    Advance(s, 1.0f, 1.0f, kProfile, 1000.0f);
    Advance(s, 1.0f, 1.0f, kProfile, -1.0f);
    Advance(s, 1.0f, 1.0f, kProfile, nan);
    CHECK(s.position.Finite() && s.velocity.Finite(), "finite after bad input");
    State fast = Flying(500.0f);
    Advance(fast, 0.0f, 0.0f, kProfile, 0.02f);
    CHECK(Speed(fast) <= kProfile.maxBoostSpeed + 1e-3f, "speed capped %.1f", Speed(fast));
}

static void TestResources() {
    ResourceRules rules;
    Resources r;
    r.Reset(rules);
    CHECK(r.charges == 6, "starts full");
    CHECK(r.Use(Command::SurgeForward, false, rules) == AbilityResult::BadState, "surge needs the air");
    for (int i = 0; i < 6; ++i) CHECK(r.Use(Command::SurgeForward, true, rules) == AbilityResult::Ok, "use %d", i);
    CHECK(r.Use(Command::SkywardAscent, true, rules) == AbilityResult::NoCharge, "empty");
    CHECK(r.Use(Command::WhirlingSurge, true, rules) == AbilityResult::Ok, "whirl is free");
    CHECK(r.Use(Command::WhirlingSurge, true, rules) == AbilityResult::Cooldown, "whirl cooldown");
    CHECK(r.NextChargeMs(rules, true, false) == rules.regenMs, "next charge %u", r.NextChargeMs(rules, true, false));
    CHECK(r.Tick(rules.regenMs, rules, true, false) && r.charges == 1, "regen one");
    r.Tick(rules.thrillRegenMs * 2, rules, true, true);
    CHECK(r.charges == 3, "thrill regen %u", r.charges);
    r.Tick(rules.whirlCooldownMs, rules, true, false);
    CHECK(r.Check(Command::WhirlingSurge, true) == AbilityResult::Ok, "cooldown over");

    ResourceRules vigor;
    vigor.profile = ResourceProfile::Vigor;
    Resources v;
    v.Reset(vigor);
    v.Use(Command::LiftOff, false, vigor);
    CHECK(!v.Tick(60000, vigor, true, false) && v.charges == 5, "vigor does not regen in slow flight");
    CHECK(v.Tick(vigor.groundRegenMs, vigor, false, false) && v.charges == 6, "vigor regens on the ground");
}

static void TestWire() {
    using namespace SkyridingWire;
    uint8_t buf[MaxServerSize];

    Begin begin;
    begin.resources = { ResourceProfile::Vigor, 4, 6, 1234 };
    begin.physics.maxAirSpeed = 61.0f;
    const size_t n = Write(begin, buf, sizeof(buf));
    CHECK(n == 3 + 7 + Profile::FieldCount * 4, "begin size %zu", n);
    Reader r(buf, n);
    ServerMsg type;
    Begin back;
    CHECK(ReadServerType(r, type) && type == ServerMsg::Begin && Read(r, back), "begin round trip");
    CHECK(back.physics.maxAirSpeed == 61.0f && back.resources.charges == 4 && back.resources.nextChargeMs == 1234, "begin values");
    Reader shortReader(buf, n - 1);
    CHECK(ReadServerType(shortReader, type) && !Read(shortReader, back), "truncated begin rejected");

    begin.physics.gravity = -1.0f;
    Reader bad(buf, Write(begin, buf, sizeof(buf)));
    CHECK(ReadServerType(bad, type) && !Read(bad, back), "invalid profile rejected");

    AbilityReply reply{ Command::SkywardAscent, 65535, AbilityResult::Cooldown, 500, { ResourceProfile::Charges, 2, 6, 99 } };
    Reader rr(buf, Write(reply, buf, sizeof(buf)));
    AbilityReply replyBack;
    CHECK(ReadServerType(rr, type) && type == ServerMsg::AbilityResult && Read(rr, replyBack), "reply round trip");
    CHECK(replyBack.sequence == 65535 && replyBack.result == AbilityResult::Cooldown && replyBack.resources.charges == 2, "reply values");

    uint8_t small[MaxClientSize];
    Ability ability{ Command::Flap, 7 };
    const size_t an = Write(ability, small, sizeof(small));
    Reader ar(small, an);
    ClientMsg ctype;
    Ability abilityBack;
    CHECK(ReadClientType(ar, ctype) && ctype == ClientMsg::Ability && Read(ar, abilityBack) && abilityBack.command == Command::Flap
        && abilityBack.sequence == 7, "ability round trip");
    small[1] = CommandCount;
    Reader badAbility(small, an);
    CHECK(ReadClientType(badAbility, ctype) && !Read(badAbility, abilityBack), "unknown command rejected");
    CHECK(Write(begin, small, sizeof(small)) == 0, "writer refuses overflow");

    Hello hello{ Version, Style::Steady };
    Reader hr(small, Write(hello, small, sizeof(small)));
    Hello helloBack;
    CHECK(ReadClientType(hr, ctype) && Read(hr, helloBack) && helloBack.style == Style::Steady, "hello round trip");
    uint8_t trailing[4] = { uint8_t(ClientMsg::Style), 0, 0, 0 };
    Reader tr(trailing, 3);
    Style style;
    CHECK(ReadClientType(tr, ctype) && !ReadStyle(tr, style), "trailing bytes rejected");
}

// Honest riders: the client simulates at 60 fps, a heartbeat every 100 ms reaches the server 30-150 ms later, abilities
// are granted when their request arrives. No sample may fail.
static void TestEnvelopeHonest() {
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const EnvelopeConfig config;
    int failures = 0, samples = 0;
    for (int run = 0; run < 200; ++run) {
        State s;
        s.position = { 0.0f, 0.0f, 200.0f + 300.0f * unit(rng) };
        s.velocity = { 7.0f + 10.0f * unit(rng), 0.0f, 0.0f };
        Envelope envelope;
        uint32_t clientMs = 1000, nextBeat = 1000, latency = 60;
        float facing = 0.0f, pitch = 0.0f;
        const auto use = [&](Command c) {
            Apply(s, c, kProfile, facing);
            envelope.Grant(c, kProfile, config, clientMs + latency);
        };
        use(Command::LiftOff);
        for (int frame = 0; frame < 60 * 30; ++frame) {
            if (frame % 30 == 0) { facing += (unit(rng) - 0.5f) * 2.0f; pitch = (unit(rng) - 0.55f) * 2.4f; }
            if (unit(rng) < 0.004f) use(Command(2 + int(unit(rng) * 4.99f)));
            if (unit(rng) < 0.01f) use(Command::Flap);
            clientMs += 16 + (frame % 3 == 0);
            Advance(s, facing, pitch, kProfile, ((frame % 3 == 0) ? 17 : 16) * 0.001f);
            if (s.position.z < 5.0f) { s.position.z = 5.0f; s.velocity.z = std::fmax(s.velocity.z, 0.0f); }
            if (int32_t(clientMs - nextBeat) >= 0) {
                nextBeat = clientMs + 100;
                latency = 30 + uint32_t(unit(rng) * 120.0f);
                const EnvelopeResult r = envelope.Check(s.position, clientMs, clientMs + latency, 15.0f, kProfile, config);
                ++samples;
                if (r != EnvelopeResult::Ok && failures++ < 5) {
                    std::printf("  run %d t=%.1fs result %d speed %.1f z %.1f\n", run, clientMs * 0.001f, int(r), Speed(s), s.position.z);
                }
                if (r != EnvelopeResult::Ok) envelope.Reset();
            }
        }
    }
    CHECK(failures == 0, "%d of %d honest samples failed", failures, samples);
}

static int Cheat(float vx, float vz, uint32_t ms, float startSpeed) {
    const EnvelopeConfig config;
    Envelope envelope;
    Vec3 pos(0.0f, 0.0f, 100.0f);
    for (uint32_t t = 0; t <= ms; t += 100) {
        if (envelope.Check(pos, t, t + 50, startSpeed, kProfile, config) != EnvelopeResult::Ok) return int(t);
        pos += Vec3(vx, 0.0f, vz) * 0.1f;
    }
    return -1;
}

static void TestEnvelopeCheats() {
    CHECK(Cheat(20.0f, 10.0f, 20000, 15.0f) >= 0, "steady climb is caught");
    CHECK(Cheat(0.0f, 0.0f, 10000, 15.0f) >= 0, "hover is caught");
    CHECK(Cheat(120.0f, 0.0f, 2000, 15.0f) >= 0, "speed hack is caught");
    CHECK(Cheat(30.0f, -2.0f, 20000, 30.0f) < 0, "a slow descending glide passes");

    // Claiming a long client interval to hide speed: bounded by what the server saw.
    const EnvelopeConfig config;
    Envelope envelope;
    Vec3 pos(0.0f, 0.0f, 100.0f);
    bool caught = false;
    for (uint32_t t = 0, claimed = 0; t <= 3000 && !caught; t += 100, claimed += 1000) {
        caught = envelope.Check(pos, claimed, t, 15.0f, kProfile, config) != EnvelopeResult::Ok;
        pos += Vec3(10.0f, 0.0f, 0.0f);
    }
    CHECK(caught, "time-dilated speed hack is caught");
}

int main() {
    TestMomentum();
    TestDive();
    TestClimb();
    TestGlideSink();
    TestStallAndRecovery();
    TestTurns();
    TestNoEnergyWithoutCommands();
    TestCommandsAndBounds();
    TestFrameRateIndependence();
    TestReproducible();
    TestBadInput();
    TestResources();
    TestWire();
    TestEnvelopeHonest();
    TestEnvelopeCheats();
    std::printf(g_failures ? "%d failure(s)\n" : "all skyriding tests passed\n", g_failures);
    return g_failures ? 1 : 0;
}
