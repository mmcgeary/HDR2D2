#pragma once

// Test-only physical dome: a real angle driven by the servo command, and a
// blip to the tracker whenever the dome sensor passes one of the body magnets.

#include "R2DomeTracker.h"

namespace r2sim {

struct SimDome
{
    float angle;          // real angle, -180..180
    float dpsPerUs;       // real degrees per second per microsecond of servo offset above the deadband
    uint16_t deadbandUs;  // offsets at or below this don't move the dome
    bool magnetPresent[r2::kMagnetCount];
    uint32_t blips;       // blips reported so far

    SimDome(float startDeg, float dpsPerUsReal, uint16_t deadband)
        : angle(startDeg), dpsPerUs(dpsPerUsReal), deadbandUs(deadband), blips(0)
    {
        for (uint8_t i = 0; i < r2::kMagnetCount; i++)
            magnetPresent[i] = true;
    }

    float speed(uint16_t offsetUs) const
    {
        return offsetUs > deadbandUs ? dpsPerUs * float(offsetUs - deadbandUs) : 0.0f;
    }

    // Moves the dome for stepUs under one command in 1 ms slices, reporting each
    // magnet pass to the tracker at the slice it happens in. Ends with update().
    void step(r2::DomeTracker& tracker, uint32_t& nowUs, uint32_t stepUs, int8_t dir, uint16_t offsetUs)
    {
        tracker.setCommand(nowUs, dir, offsetUs);
        float v = speed(offsetUs);
        for (uint32_t s = 0; s < stepUs; s += 1000)
        {
            float before = angle;
            angle = r2::wrapDeg(angle + float(dir) * v * 0.001f);
            if (dir == 0 || v == 0.0f)
                continue;
            for (uint8_t i = 0; i < r2::kMagnetCount; i++)
            {
                if (!magnetPresent[i])
                    continue;
                // The slice covers (before, after]: the magnet was ahead before it
                // and is level with or behind the dome after it.
                float ahead = r2::wrapDeg(r2::kMagnetDeg[i] - before) * float(dir);
                float aheadAfter = r2::wrapDeg(r2::kMagnetDeg[i] - angle) * float(dir);
                bool crossed = ahead > 0.0f && ahead < 10.0f && aheadAfter <= 0.0f;
                if (crossed)
                {
                    blips++;
                    tracker.onBlip(nowUs + s + 1000);
                }
            }
        }
        nowUs += stepUs;
        tracker.update(nowUs);
    }
};

} // namespace r2sim
