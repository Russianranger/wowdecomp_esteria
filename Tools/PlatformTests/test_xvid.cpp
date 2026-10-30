#include <xvid.h>
#include <array>
#include <cstdio>

int main() {
    xvid_gbl_init_t init{}; init.version = XVID_VERSION;
    if (xvid_global(nullptr, XVID_GBL_INIT, &init, nullptr) < 0) return 1;
    xvid_enc_create_t encoder{};
    encoder.version = XVID_VERSION; encoder.width = 16; encoder.height = 16;
    encoder.fincr = 1; encoder.fbase = 25; encoder.max_key_interval = 1;
    if (xvid_encore(nullptr, XVID_ENC_CREATE, &encoder, nullptr) < 0) return 2;
    std::array<unsigned char, 16 * 16 * 3> rgb{};
    std::array<unsigned char, 8192> packet{};
    xvid_enc_frame_t frame{};
    frame.version = XVID_VERSION; frame.input.csp = XVID_CSP_BGR;
    frame.input.plane[0] = rgb.data(); frame.input.stride[0] = 16 * 3;
    frame.bitstream = packet.data(); frame.length = static_cast<int>(packet.size());
    frame.type = XVID_TYPE_IVOP; frame.quant = 2;
    xvid_enc_stats_t encoded{}; encoded.version = XVID_VERSION;
    const int bytes = xvid_encore(encoder.handle, XVID_ENC_ENCODE, &frame, &encoded);
    xvid_encore(encoder.handle, XVID_ENC_DESTROY, nullptr, nullptr);
    if (bytes <= 0 || bytes > static_cast<int>(packet.size())) return 3;
    xvid_dec_create_t decoder{};
    decoder.version = XVID_VERSION; decoder.width = 16; decoder.height = 16;
    if (xvid_decore(nullptr, XVID_DEC_CREATE, &decoder, nullptr) < 0) return 4;
    std::array<unsigned char, 16 * 16 * 4> pixels{}; pixels.fill(255);
    xvid_dec_frame_t decode{}; decode.version = XVID_VERSION;
    decode.general = XVID_LOWDELAY; decode.bitstream = packet.data(); decode.length = bytes;
    decode.output.csp = XVID_CSP_BGRA; decode.output.plane[0] = pixels.data(); decode.output.stride[0] = 16 * 4;
    const int consumed = xvid_decore(decoder.handle, XVID_DEC_DECODE, &decode, nullptr);
    xvid_decore(decoder.handle, XVID_DEC_DESTROY, nullptr, nullptr);
    if (consumed <= 0) return 5;
    for (unsigned p = 0; p < pixels.size(); p += 4)
        if (pixels[p] > 3 || pixels[p+1] > 3 || pixels[p+2] > 3) return 6;
    std::puts("Portable MPEG-4 movie codec encode/decode passed");
}
