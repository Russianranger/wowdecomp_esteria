#pragma once

#include "Addons/Cursors/Textures/StarcraftAnimation.h"

// ANI rates are in 1/60-second jiffies. Derive the frame from elapsed time so
// low FPS skips missed frames without changing the animation's overall speed.
inline uint32_t CursorAnimationFrame(uint32_t elapsedMs) {
    const uint32_t jiffy = static_cast<uint32_t>((static_cast<uint64_t>(elapsedMs) * 60 / 1000)
        % kStarcraftCycleJiffies);
    for (const auto& step : kStarcraftSteps) {
        if (jiffy < step.endJiffy) {
            return step.frame;
        }
    }
    return 0;
}
