#include "ThirdParty/DivxDecoder/DivxDecoder.h"
#include <xvid.h>
#include <algorithm>
#include <array>
#include <cstdio>
#if defined(_WIN32)
#include <filesystem>
#include <windows.h>
#endif

namespace {
constexpr int Width = 16;
constexpr int Height = 16;
constexpr int DecoderSlot = 1; // CSimpleMovieFrame's first allocated slot.
constexpr std::size_t GuardSize = 32;
constexpr unsigned char Sentinel = 0xa5;

int Fail(const char* message) {
    std::fprintf(stderr, "Movie adapter: %s\n", message);
    return 1;
}

struct EncoderGuard {
    void* handle = nullptr;
    ~EncoderGuard() {
        if (handle) xvid_encore(handle, XVID_ENC_DESTROY, nullptr, nullptr);
    }
};

struct DecoderAPI {
    decltype(&InitializeDivxDecoder) initialize = &InitializeDivxDecoder;
    decltype(&SetOutputFormat) setOutput = &SetOutputFormat;
    decltype(&DivxDecode) decode = &DivxDecode;
    decltype(&UnInitializeDivxDecoder) shutdown = &UnInitializeDivxDecoder;
};

#if defined(_WIN32)
struct LibraryGuard {
    HMODULE handle = nullptr;
    ~LibraryGuard() {
        if (handle) FreeLibrary(handle);
    }
};

template <typename Function>
bool ResolveExport(HMODULE library, const char* name, Function& function) {
    const auto address = GetProcAddress(library, name);
    if (!address) {
        std::fprintf(stderr, "Movie adapter: missing DLL export %s (Windows error %lu)\n",
                     name, static_cast<unsigned long>(GetLastError()));
        return false;
    }
    // The pointer type comes directly from the shared DivxDecoder.h ABI.
    function = reinterpret_cast<Function>(address);
    return true;
}
#endif

struct DecoderGuard {
    const DecoderAPI& api;
    bool active = false;
    ~DecoderGuard() {
        if (active) api.shutdown(DecoderSlot);
    }
};
}

int main(int argc, char** argv) {
    if (argc != 1 && argc != 2)
        return Fail("usage: test_xvid_adapter [absolute-path-to-XvidDecoder.dll]");

    DecoderAPI api;
#if defined(_WIN32)
    // Declared before every decoder guard so all exported cleanup calls finish
    // before the DLL is unloaded, including failures inside the shared suite.
    LibraryGuard library;
    if (argc == 2) {
        const std::filesystem::path dllPath(argv[1]);
        if (!dllPath.is_absolute())
            return Fail("DLL mode requires an absolute DLL path");
        library.handle = LoadLibraryW(dllPath.c_str());
        if (!library.handle) {
            std::fprintf(stderr, "Movie adapter: cannot load DLL %s (Windows error %lu)\n",
                         argv[1], static_cast<unsigned long>(GetLastError()));
            return 1;
        }
        if (!ResolveExport(library.handle, "InitializeDivxDecoder", api.initialize) ||
            !ResolveExport(library.handle, "SetOutputFormat", api.setOutput) ||
            !ResolveExport(library.handle, "DivxDecode", api.decode) ||
            !ResolveExport(library.handle, "UnInitializeDivxDecoder", api.shutdown))
            return 1;
    }
#else
    (void)argv;
    if (argc == 2)
        return Fail("DLL mode is available only on Windows");
#endif

    xvid_gbl_init_t init{};
    init.version = XVID_VERSION;
    if (xvid_global(nullptr, XVID_GBL_INIT, &init, nullptr) < 0)
        return Fail("codec initialization failed");

    xvid_enc_create_t encoder{};
    encoder.version = XVID_VERSION;
    encoder.width = Width;
    encoder.height = Height;
    encoder.fincr = 1;
    encoder.fbase = 25;
    encoder.max_key_interval = 1;
    if (xvid_encore(nullptr, XVID_ENC_CREATE, &encoder, nullptr) < 0)
        return Fail("encoder creation failed");
    EncoderGuard encoderGuard{encoder.handle};

    // Top half red, bottom half blue: distinguishes BGRA from RGBA and
    // bottom-up movie output from top-down output without external assets.
    std::array<unsigned char, Width * Height * 3> source{};
    for (int y = 0; y < Height; ++y) {
        for (int x = 0; x < Width; ++x) {
            source[(y * Width + x) * 3 + (y < Height / 2 ? 2 : 0)] = 240;
        }
    }
    std::array<unsigned char, 8192> packet{};
    xvid_enc_frame_t encode{};
    encode.version = XVID_VERSION;
    encode.input.csp = XVID_CSP_BGR;
    encode.input.plane[0] = source.data();
    encode.input.stride[0] = Width * 3;
    encode.bitstream = packet.data();
    encode.length = static_cast<int>(packet.size());
    encode.type = XVID_TYPE_IVOP;
    encode.quant = 2;
    const int bytes = xvid_encore(encoder.handle, XVID_ENC_ENCODE, &encode, nullptr);
    if (bytes <= 0 || bytes > static_cast<int>(packet.size()))
        return Fail("encoder did not produce a bounded frame");

    if (!api.initialize(-1, Width, Height) ||
        !api.initialize(16, Width, Height) ||
        !api.setOutput(-1, 1, Width, Height) ||
        !api.shutdown(16))
        return Fail("out-of-range slot was accepted");

    DivxDecodeFrame frame{};
    if (!api.decode(DecoderSlot, &frame, 0))
        return Fail("decode before initialization was accepted");
    if (api.initialize(DecoderSlot, Width, Height))
        return Fail("adapter initialization failed");
    DecoderGuard decoderGuard{api, true};
    if (api.setOutput(DecoderSlot, 1, Width, Height))
        return Fail("BGRA output setup failed");
    if (!api.decode(DecoderSlot, nullptr, 0))
        return Fail("null frame was accepted");

    std::array<unsigned char, Width * Height * 4 + GuardSize * 2> pixels{};
    pixels.fill(Sentinel);
    frame.m_input = packet.data();
    frame.m_inputSize = static_cast<uint32_t>(bytes);
    frame.m_output = pixels.data() + GuardSize;

    // Skipped frames still update decoder state, but must not write pixels.
    frame.m_render = 0;
    if (api.decode(DecoderSlot, &frame, 0))
        return Fail("skipped frame decode failed");
    if (!std::all_of(pixels.begin(), pixels.end(), [](unsigned char value) { return value == Sentinel; }))
        return Fail("skipped frame modified output");

    // Reopening a slot must release its previous state and accept a fresh GOP.
    if (api.initialize(DecoderSlot, Width, Height) ||
        api.setOutput(DecoderSlot, 1, Width, Height))
        return Fail("slot reinitialization failed");
    frame.m_render = 1;
    if (api.decode(DecoderSlot, &frame, 0))
        return Fail("rendered frame decode failed");
    if (!std::all_of(pixels.begin(), pixels.begin() + GuardSize, [](unsigned char value) { return value == Sentinel; }) ||
        !std::all_of(pixels.end() - GuardSize, pixels.end(), [](unsigned char value) { return value == Sentinel; }))
        return Fail("decoder wrote outside the output buffer");

    const auto* output = pixels.data() + GuardSize;
    for (int y = 0; y < Height; ++y) {
        for (int x = 0; x < Width; ++x) {
            const auto* pixel = output + (y * Width + x) * 4;
            const int dominant = y < Height / 2 ? 0 : 2;
            const int other = y < Height / 2 ? 2 : 0;
            // Tolerate lossy MPEG-4 rounding while requiring both channel
            // order and vertical orientation for every output pixel.
            if (pixel[dominant] < 200 || pixel[other] > 40 || pixel[1] > 40)
                return Fail("BGRA channel order or vertical flip is incorrect");
        }
    }

    if (api.shutdown(DecoderSlot))
        return Fail("adapter shutdown failed");
    decoderGuard.active = false;
    if (!api.decode(DecoderSlot, &frame, 0) ||
        !api.setOutput(DecoderSlot, 1, Width, Height) ||
        !api.shutdown(DecoderSlot))
        return Fail("closed slot remained usable");

    std::printf("Movie adapter (%s): BGRA/VFLIP, output bounds, skipped frames and slot lifecycle passed\n",
                argc == 2 ? "DLL" : "direct");
    return 0;
}
