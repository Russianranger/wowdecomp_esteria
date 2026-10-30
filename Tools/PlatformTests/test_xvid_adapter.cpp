#include "ThirdParty/DivxDecoder/DivxDecoder.h"
#include <xvid.h>
#include <algorithm>
#include <array>
#include <cstdio>

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

struct DecoderGuard {
    bool active = false;
    ~DecoderGuard() {
        if (active) UnInitializeDivxDecoder(DecoderSlot);
    }
};
}

int main() {
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

    if (!InitializeDivxDecoder(-1, Width, Height) ||
        !InitializeDivxDecoder(16, Width, Height) ||
        !SetOutputFormat(-1, 1, Width, Height) ||
        !UnInitializeDivxDecoder(16))
        return Fail("out-of-range slot was accepted");

    DivxDecodeFrame frame{};
    if (!DivxDecode(DecoderSlot, &frame, 0))
        return Fail("decode before initialization was accepted");
    if (InitializeDivxDecoder(DecoderSlot, Width, Height))
        return Fail("adapter initialization failed");
    DecoderGuard decoderGuard{true};
    if (SetOutputFormat(DecoderSlot, 1, Width, Height))
        return Fail("BGRA output setup failed");
    if (!DivxDecode(DecoderSlot, nullptr, 0))
        return Fail("null frame was accepted");

    std::array<unsigned char, Width * Height * 4 + GuardSize * 2> pixels{};
    pixels.fill(Sentinel);
    frame.m_input = packet.data();
    frame.m_inputSize = static_cast<uint32_t>(bytes);
    frame.m_output = pixels.data() + GuardSize;

    // Skipped frames still update decoder state, but must not write pixels.
    frame.m_render = 0;
    if (DivxDecode(DecoderSlot, &frame, 0))
        return Fail("skipped frame decode failed");
    if (!std::all_of(pixels.begin(), pixels.end(), [](unsigned char value) { return value == Sentinel; }))
        return Fail("skipped frame modified output");

    // Reopening a slot must release its previous state and accept a fresh GOP.
    if (InitializeDivxDecoder(DecoderSlot, Width, Height) ||
        SetOutputFormat(DecoderSlot, 1, Width, Height))
        return Fail("slot reinitialization failed");
    frame.m_render = 1;
    if (DivxDecode(DecoderSlot, &frame, 0))
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

    if (UnInitializeDivxDecoder(DecoderSlot))
        return Fail("adapter shutdown failed");
    decoderGuard.active = false;
    if (!DivxDecode(DecoderSlot, &frame, 0) ||
        !SetOutputFormat(DecoderSlot, 1, Width, Height) ||
        !UnInitializeDivxDecoder(DecoderSlot))
        return Fail("closed slot remained usable");

    std::puts("Movie adapter: BGRA/VFLIP, output bounds, skipped frames and slot lifecycle passed");
    return 0;
}
