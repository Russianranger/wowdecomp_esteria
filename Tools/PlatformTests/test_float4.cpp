#include "Engine/Source/Platform/Float4.h"
#include "Engine/Source/Platform/CpuCapabilities.h"
#include "Engine/Source/Platform/ScalarMath.h"
#include <array>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>

int main() {
    using WowPlatform::CpuArchitecture;
    if (!WowPlatform::HasClientMath(CpuArchitecture::Arm64, 0) ||
        WowPlatform::HasClientMath(CpuArchitecture::X86, 0) ||
        !WowPlatform::HasClientMath(CpuArchitecture::X86, 4) ||
        WowPlatform::HasClientMath(CpuArchitecture::Unsupported, 0xffffffff)) return 7;
    // Misaligned source/destination buffers match the packed model stream case.
    std::mt19937 random(12340);
    std::uniform_real_distribution<float> values(-100.0f, 100.0f);
    for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        alignas(16) std::array<float, 18> matrix{};
        alignas(16) std::array<float, 6> actual{{901, 0, 0, 0, 0, 902}};
        for (float& value : matrix) value = values(random);
        const float x = values(random), y = values(random), z = values(random);
        const float* m = matrix.data() + 1;
        const auto transformed = WowSimd::Add(WowSimd::Add(WowSimd::Add(
            WowSimd::Multiply(WowSimd::Splat(x), WowSimd::Load(m)),
            WowSimd::Multiply(WowSimd::Splat(y), WowSimd::Load(m + 4))),
            WowSimd::Multiply(WowSimd::Splat(z), WowSimd::Load(m + 8))), WowSimd::Load(m + 12));
        WowSimd::Store(actual.data() + 1, transformed);
        if (actual[0] != 901 || actual[5] != 902) return 1;
        for (unsigned lane = 0; lane < 4; ++lane) {
            const double expected = double(x) * m[lane] + double(y) * m[4 + lane] + double(z) * m[8 + lane] + m[12 + lane];
            const double magnitude = std::abs(double(x) * m[lane]) + std::abs(double(y) * m[4 + lane]) + std::abs(double(z) * m[8 + lane]) + std::abs(m[12 + lane]);
            if (std::abs(actual[lane + 1] - expected) > 6 * std::numeric_limits<float>::epsilon() * magnitude + 0.00001) return 2;
        }
    }
    std::fesetround(FE_TONEAREST);
    const float input[] = {0.5f, 1.5f, 2.5f, -0.5f, -1.5f, -2.5f, 32767, -32768};
    const int32_t expected[] = {0, 2, 2, 0, -2, -2, 32767, -32768};
    for (unsigned i = 0; i < sizeof(input) / sizeof(input[0]); ++i)
        if (WowPlatform::RoundToInt32(input[i]) != expected[i]) return 3;
    if (WowPlatform::RoundToInt32(std::numeric_limits<float>::quiet_NaN()) != std::numeric_limits<int32_t>::min()) return 4;
    if (WowPlatform::RoundToInt32(2147483648.0f) != std::numeric_limits<int32_t>::min()) return 5;
    for (float value : {0.001f, 0.5f, 1.0f, 4.0f, 10000.0f})
        if (std::abs(WowPlatform::ReciprocalSquareRoot(value) * std::sqrt(value) - 1.0f) > 0.0004f) return 6;
    std::puts("SIMD matrix and voice scalar portability tests passed");
}
