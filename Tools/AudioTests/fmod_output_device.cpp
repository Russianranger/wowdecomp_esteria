// Real reconstructed FMOD mixer -> selected Windows output backend. No game data.
// A separate receiver must verify actual delivered PCM; this executable's marker
// only establishes the API/clock lifecycle and cannot qualify audible output.
#include <windows.h>
#include <fmod.hpp>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
std::atomic<unsigned> endCallbacks{0};
FMOD_RESULT F_CALLBACK ChannelEnded(FMOD_CHANNEL*, FMOD_CHANNEL_CALLBACKTYPE type,
                                   int command, unsigned data1, unsigned data2) {
    if (type == FMOD_CHANNEL_CALLBACKTYPE_END && command == 0x455354 && !data1 && !data2)
        endCallbacks.fetch_add(1, std::memory_order_release);
    return FMOD_OK;
}
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Check(FMOD_RESULT result, const char* operation) {
    if (result != FMOD_OK) {
        std::fprintf(stderr, "FMOD error %d during %s\n", static_cast<int>(result), operation);
        throw std::runtime_error(operation);
    }
}
struct Owned {
    HWND window = nullptr;
    FMOD::System* system = nullptr;
    FMOD::Sound* sound = nullptr;
    ~Owned() {
        if (sound) sound->release();
        if (system) { system->close(); system->release(); }
        if (window) DestroyWindow(window);
    }
};
}

int main(int argc, char** argv) {
    if (argc != 2 || (std::strcmp(argv[1], "--winmm") && std::strcmp(argv[1], "--dsound"))) {
        std::fputs("Usage: fmod_output_device --winmm|--dsound\n", stderr);
        return 2;
    }
    const bool directSound = !std::strcmp(argv[1], "--dsound");
    const FMOD_OUTPUTTYPE selected = directSound ? FMOD_OUTPUTTYPE_DSOUND : FMOD_OUTPUTTYPE_WINMM;
    const char* backend = directSound ? "DirectSound" : "WinMM";
    try {
        Owned owned;
        owned.window = CreateWindowExW(0, L"STATIC", L"Esteria FMOD output fixture",
            WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Require(owned.window != nullptr, "create real cooperative-level window");
        Check(FMOD::System_Create(&owned.system), "create reconstructed FMOD system");
        unsigned version = 0;
        Check(owned.system->getVersion(&version), "get source FMOD version");
        Require(version == FMOD_VERSION, "matched source FMOD version");
        Check(owned.system->setOutput(selected), "select explicit real Windows output");
        int drivers = 0;
        Check(owned.system->getNumDrivers(&drivers), "enumerate actual output devices");
        Require(drivers > 0, "at least one real output device must exist");
        Check(owned.system->setDriver(0), "select output device");
        char device[256] = {};
        Check(owned.system->getDriverName(0, device, sizeof(device)), "observe output device");
        std::printf("FMOD output: %s; device: %s; version: %08x\n", backend, device, version);
        Check(owned.system->setSpeakerMode(FMOD_SPEAKERMODE_STEREO), "select stereo speakers");
        Check(owned.system->setSoftwareFormat(48000, FMOD_SOUND_FORMAT_PCM16, 2, 2, FMOD_DSP_RESAMPLER_LINEAR),
              "select stereo PCM16 mixer");
        Check(owned.system->setDSPBufferSize(480, 4), "select bounded 10ms mixer periods");
        Check(owned.system->init(8, FMOD_INIT_NORMAL, owned.window), "initialize real Windows output");
        FMOD_OUTPUTTYPE actual = FMOD_OUTPUTTYPE_NOSOUND;
        Check(owned.system->getOutput(&actual), "inspect actual output selection");
        Require(actual == selected, "output must not fall back to a no-sound backend");

        constexpr unsigned rate = 48000, frames = rate * 2;
        std::vector<int16_t> samples(frames * 2);
        constexpr double tau = 6.28318530717958647692;
        for (unsigned i = 0; i < frames; ++i) {
            samples[i * 2] = static_cast<int16_t>(8192 * std::sin(tau * (i % 48) / 48));
            samples[i * 2 + 1] = static_cast<int16_t>(8192 * std::sin(tau * (i % 24) / 24));
        }
        FMOD_CREATESOUNDEXINFO info = {};
        info.cbsize = sizeof(info); info.length = static_cast<unsigned>(samples.size() * sizeof(samples[0]));
        info.numchannels = 2; info.defaultfrequency = rate; info.format = FMOD_SOUND_FORMAT_PCM16;
        Check(owned.system->createSound(reinterpret_cast<const char*>(samples.data()),
              FMOD_OPENMEMORY | FMOD_OPENRAW | FMOD_SOFTWARE | FMOD_2D | FMOD_LOOP_OFF, &info, &owned.sound),
              "create known stereo signal through actual FMOD sound loader");
        FMOD::Channel* channel = nullptr;
        Check(owned.system->playSound(FMOD_CHANNEL_FREE, owned.sound, true, &channel), "create paused software channel");
        Require(channel != nullptr, "software channel exists");
        Check(channel->setCallback(FMOD_CHANNEL_CALLBACKTYPE_END, ChannelEnded, 0x455354),
              "register actual channel end callback while paused");
        Check(channel->setPaused(false), "start real mixer playback");
        const ULONGLONG began = GetTickCount64();
        bool sawPlaying = false;
        unsigned furthest = 0;
        while (GetTickCount64() - began < 8000) {
            Check(owned.system->update(), "update actual mixer/output");
            if (endCallbacks.load(std::memory_order_acquire)) break;
            bool playing = false;
            const FMOD_RESULT playingResult = channel->isPlaying(&playing);
            // ChannelI::stopEx invokes END before releasing the real channel.
            // Natural completion may invalidate its handle between API calls;
            // only the actual callback can establish completion in that race.
            if (playingResult == FMOD_ERR_INVALID_HANDLE && endCallbacks.load(std::memory_order_acquire)) break;
            Check(playingResult, "observe actual channel lifecycle");
            if (!playing) break;
            sawPlaying = true;
            unsigned position = 0;
            const FMOD_RESULT positionResult = channel->getPosition(&position, FMOD_TIMEUNIT_PCM);
            if (positionResult == FMOD_ERR_INVALID_HANDLE && endCallbacks.load(std::memory_order_acquire)) break;
            Check(positionResult, "observe advancing sample position");
            if (position > furthest) furthest = position;
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
            Sleep(10);
        }
        Require(sawPlaying && endCallbacks.load(std::memory_order_acquire) == 1 && furthest > frames * 9 / 10,
                "playback must advance through the two-second signal and finish within its deadline");
        // Mixer completion precedes physical queue consumption. Give the four
        // short output buffers time to drain before releasing the backend.
        Sleep(500);
        Check(owned.sound->release(), "release synthetic sound"); owned.sound = nullptr;
        Check(owned.system->close(), "close real output");
        Check(owned.system->release(), "release FMOD system"); owned.system = nullptr;
        std::printf("PASS: Reconstructed FMOD %s stereo playback lifecycle (PCM capture required)\n", backend);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: FMOD output fixture: %s\n", error.what());
        return 1;
    }
}
