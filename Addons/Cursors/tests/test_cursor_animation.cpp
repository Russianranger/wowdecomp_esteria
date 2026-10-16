#include "WoW/Source/UIUtil/CursorAnimation.h"
#include <cstdio>

int main() {
    // Boundaries from the supplied ANI: frame 0 holds for 30/60 seconds,
    // then frames 1..4 each hold for 5/60 seconds. The cycle is 50/60 seconds.
    const uint32_t cases[][2] = {
        { 0, 0 }, { 416, 0 }, { 417, 0 }, { 499, 0 }, { 500, 1 },
        { 583, 1 }, { 584, 2 }, { 666, 2 }, { 667, 3 },
        { 749, 3 }, { 750, 4 }, { 833, 4 }, { 834, 0 },
        { 1334, 1 }, { 1584, 4 }, { 1667, 0 }, { 500000, 0 },
    };
    for (const auto& test : cases) {
        const uint32_t actual = CursorAnimationFrame(test[0]);
        if (actual != test[1]) {
            std::fprintf(stderr, "%u ms: expected frame %u, got %u\n", test[0], test[1], actual);
            return 1;
        }
    }
    // Cursor.cpp subtracts unsigned timestamps before asking for the frame.
    const uint32_t start = 0xFFFFFF00u;
    const uint32_t now = start + 750u;
    if (CursorAnimationFrame(now - start) != 4) {
        return 1;
    }
    std::puts("ANI timing: held/repeated frames, boundaries, looping, skipped frames, clock wrap passed");
    return 0;
}
