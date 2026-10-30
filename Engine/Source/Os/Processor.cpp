#include <cstdint>
#include "Engine/Source/Os/Processor.h"
#include "Engine/Source/Platform/CpuCapabilities.h"

// VERIFY: e905bbd2 auto
// OFFSET: 0x0086BB80
uint32_t OsGetProcessorFeatures() {
    int32_t vendor;
    return OsGetProcessorFeaturesEx(vendor);
}

// Capability of the client math/voice implementations, separate from the legacy
// x86 CPUID mask exposed by OsGetProcessorFeatures().
bool OsSupportsClientMath() {
#if defined(_M_ARM64) || defined(__aarch64__)
    return WowPlatform::HasClientMath(WowPlatform::CpuArchitecture::Arm64, 0);
#elif defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
    return WowPlatform::HasClientMath(WowPlatform::CpuArchitecture::X86, OsGetProcessorFeatures());
#else
    return false;
#endif
}
