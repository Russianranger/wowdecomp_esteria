#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#if !defined(WOWDECOMP_SIMD_SCALAR) && (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__))
#include <xmmintrin.h>
#endif

namespace WowPlatform {
// Preserve the original x86 approximation for reference comparisons. On ARM,
// use the hardware sqrt/divide instructions through the standard library.
inline float ReciprocalSquareRoot(float value) {
#if !defined(WOWDECOMP_SIMD_SCALAR) && (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__))
    return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(value)));
#else
    return 1.0f / std::sqrt(value);
#endif
}

// The voice codec uses current-rounding-mode conversion, not truncation. x86
// returns the integer-indefinite sentinel for NaN/overflow; explicitly retain
// that behavior without an out-of-range C++ cast on the other architectures.
inline int32_t RoundToInt32(float value) {
#if !defined(WOWDECOMP_SIMD_SCALAR) && (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__))
    return _mm_cvtss_si32(_mm_set_ss(value));
#else
    const float rounded = std::nearbyint(value);
    if (!std::isfinite(rounded) || rounded < -2147483648.0f || rounded >= 2147483648.0f)
        return std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(rounded);
#endif
}
} // namespace WowPlatform
