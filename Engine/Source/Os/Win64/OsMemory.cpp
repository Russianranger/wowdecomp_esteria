#if defined(WOWDECOMP_ARCH_64)

#include <cstdint>
#include <windows.h>
#include "Engine/Source/Os/OsMemory.h"

static const uintptr_t s_nearRange = 0x60000000;

void* OsAllocNear(const void* anchor, size_t size) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t granularity = info.dwAllocationGranularity;
    if (!size || size > UINTPTR_MAX - (granularity - 1)) {
        return nullptr;
    }
    uintptr_t bytes = (size + granularity - 1) & ~(granularity - 1);
    uintptr_t center = reinterpret_cast<uintptr_t>(anchor);
    uintptr_t minimum = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
    uintptr_t maximum = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);

    if (center < minimum || center >= maximum) {
        return nullptr;
    }

    if (center > minimum + s_nearRange) {
        minimum = center - s_nearRange;
    }

    if (maximum - center > s_nearRange) {
        maximum = center + s_nearRange;
    }

    // VirtualQuery reports the region from the queried page FORWARD, even when that page
    // is inside a larger free/reserved region. Querying address - 1 and stepping to BaseAddress
    // therefore walks backward one page at a time and misses free holes wider than one page.
    // Walk both halves forward so each query skips the complete remaining region.
    const auto allocateInRange = [bytes, granularity](uintptr_t address, uintptr_t limit) -> void* {
        MEMORY_BASIC_INFORMATION region;
        while (address < limit && VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) {
            const uintptr_t base = reinterpret_cast<uintptr_t>(region.BaseAddress);
            if (region.RegionSize > UINTPTR_MAX - base) {
                break;
            }
            const uintptr_t end = base + region.RegionSize;
            if (end <= address) {
                break;
            }

            if (region.State == MEM_FREE) {
                const uintptr_t first = base > address ? base : address;
                if (first > UINTPTR_MAX - (granularity - 1)) {
                    break;
                }
                const uintptr_t candidate = (first + granularity - 1) & ~(granularity - 1);
                const uintptr_t availableEnd = end < limit ? end : limit;
                if (candidate < availableEnd && bytes <= availableEnd - candidate) {
                    void* ptr = VirtualAlloc(reinterpret_cast<void*>(candidate), bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
                    if (ptr) {
                        return ptr;
                    }
                }
            }
            address = end;
        }
        return nullptr;
    };

    if (void* ptr = allocateInRange(center, maximum)) {
        return ptr;
    }
    return allocateInRange(minimum, center);
}

void OsFreeNear(void* ptr) {
    if (ptr) {
        VirtualFree(ptr, 0, MEM_RELEASE);
    }
}

#endif
