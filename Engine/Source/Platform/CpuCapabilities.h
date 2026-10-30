#pragma once
#include <cstdint>

namespace WowPlatform {
enum class CpuArchitecture { X86, Arm64, Unsupported };
// CPU feature bit 4 is SSE only on x86. The ported numeric code has native ARM64
// implementations; do not lie about the instruction flags to enable that code.
constexpr bool HasClientMath(CpuArchitecture architecture, uint32_t x86Features) {
    return architecture == CpuArchitecture::Arm64 ||
        (architecture == CpuArchitecture::X86 && (x86Features & 4) != 0);
}
}
