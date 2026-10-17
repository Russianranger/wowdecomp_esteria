#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "Engine/Source/Os/OsMemory.h"

namespace {
void Check(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAILED: %s (error %lu)\n", message, GetLastError()); std::exit(1); }
}
}

int main() {
    constexpr uintptr_t range = 0x60000000;
    constexpr size_t granularity = 0x10000;
    constexpr size_t holeSize = 2 * 1024 * 1024;
    constexpr size_t request = 128 * 1024;
    constexpr size_t arenaSize = 2 * range + 2 * granularity;
    // Reserve an address window with exactly one usable hole, BELOW the model anchor.
    // A backward VirtualQuery(address-1) reports a suffix of the free region, so stepping
    // backward by BaseAddress only sees one page at a time and cannot fit a 128K request.
    auto arena = static_cast<uint8_t*>(VirtualAlloc(nullptr, arenaSize, MEM_RESERVE, PAGE_NOACCESS));
    Check(arena != nullptr, "reserve fixture address space");
    auto anchor = arena + range + granularity;
    auto hole = anchor - holeSize;
    Check(VirtualFree(arena, 0, MEM_RELEASE) != FALSE, "release fixture window");
    Check(VirtualAlloc(arena, size_t(hole - arena), MEM_RESERVE, PAGE_NOACCESS) == arena, "reserve lower boundary");
    Check(VirtualAlloc(anchor, size_t(arena + arenaSize - anchor), MEM_RESERVE, PAGE_NOACCESS) == anchor, "reserve upper boundary");
    const ULONGLONG started = GetTickCount64();
    void* allocated = OsAllocNear(anchor + 0x50, request);
    const auto elapsed = GetTickCount64() - started;
    Check(allocated != nullptr, "allocate from the free region below the anchor");
    const auto address = reinterpret_cast<uintptr_t>(allocated);
    Check(address >= reinterpret_cast<uintptr_t>(hole) && address + request <= reinterpret_cast<uintptr_t>(anchor),
          "allocation stays within the controlled hole");
    Check(address % granularity == 0, "allocation is aligned");
    static_cast<uint8_t*>(allocated)[0] = 0x12;
    static_cast<uint8_t*>(allocated)[request - 1] = 0x34;
    OsFreeNear(allocated);
    // Exhaust the entire nearby range; failure must be prompt and must not allocate far away.
    Check(VirtualAlloc(hole, holeSize, MEM_RESERVE, PAGE_NOACCESS) == hole, "fill the only hole");
    Check(OsAllocNear(anchor + 0x50, request) == nullptr, "exhausted nearby range returns failure");
    Check(VirtualFree(hole, 0, MEM_RELEASE) != FALSE, "release hole reservation");
    Check(VirtualFree(arena, 0, MEM_RELEASE) != FALSE, "release lower reservation");
    Check(VirtualFree(anchor, 0, MEM_RELEASE) != FALSE, "release upper reservation");
    std::printf("PASS: below-anchor allocation in %llu ms; exhausted range returns failure\n", elapsed);
}
