#pragma once

// Small, explicit vector API used by the reconstructed skinning kernels. Keep
// separate multiply/add operations: fused operations change desktop rounding.
#if !defined(WOWDECOMP_SIMD_SCALAR) && (defined(_M_ARM64) || defined(__aarch64__))
# if defined(_MSC_VER) && !defined(__clang__)
#  include <arm64_neon.h>
# else
#  include <arm_neon.h>
# endif
# define WOWDECOMP_SIMD_NEON 1
#elif !defined(WOWDECOMP_SIMD_SCALAR) && (defined(_M_X64) || defined(_M_IX86) || defined(__SSE__))
# include <xmmintrin.h>
# define WOWDECOMP_SIMD_SSE 1
#endif

namespace WowSimd {
inline bool Supported(unsigned legacyProcessorFeatures) {
#if defined(WOWDECOMP_SIMD_NEON)
    (void)legacyProcessorFeatures;
    return true;
#else
    return (legacyProcessorFeatures & 4) != 0;
#endif
}
#if defined(WOWDECOMP_SIMD_NEON)
using Float4 = float32x4_t;
inline Float4 Splat(float x) { return vdupq_n_f32(x); }
inline Float4 Load(const float* p) { return vld1q_f32(p); }
inline void Store(float* p, Float4 v) { vst1q_f32(p, v); }
inline Float4 Add(Float4 a, Float4 b) { return vaddq_f32(a, b); }
inline Float4 Multiply(Float4 a, Float4 b) { return vmulq_f32(a, b); }
#elif defined(WOWDECOMP_SIMD_SSE)
using Float4 = __m128;
inline Float4 Splat(float x) { return _mm_set1_ps(x); }
// Both APIs accept unaligned input. Several model streams originate in packed
// files; the old aligned-load variant unnecessarily depended on their allocator.
inline Float4 Load(const float* p) { return _mm_loadu_ps(p); }
inline void Store(float* p, Float4 v) { _mm_storeu_ps(p, v); }
inline Float4 Add(Float4 a, Float4 b) { return _mm_add_ps(a, b); }
inline Float4 Multiply(Float4 a, Float4 b) { return _mm_mul_ps(a, b); }
#else
struct Float4 { float lane[4]; };
inline Float4 Splat(float x) { return {{x, x, x, x}}; }
inline Float4 Load(const float* p) { return {{p[0], p[1], p[2], p[3]}}; }
inline void Store(float* p, Float4 v) { for (unsigned i = 0; i < 4; ++i) p[i] = v.lane[i]; }
inline Float4 Add(Float4 a, Float4 b) {
    return {{a.lane[0] + b.lane[0], a.lane[1] + b.lane[1], a.lane[2] + b.lane[2], a.lane[3] + b.lane[3]}};
}
inline Float4 Multiply(Float4 a, Float4 b) {
    return {{a.lane[0] * b.lane[0], a.lane[1] * b.lane[1], a.lane[2] * b.lane[2], a.lane[3] * b.lane[3]}};
}
#endif
} // namespace WowSimd
