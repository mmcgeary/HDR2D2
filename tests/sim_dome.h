#pragma once

// Test-only physical dome: a real angle driven by the servo command, and a
// blip to the tracker whenever the dome sensor reaches one of the body magnets.

#include "R2DomeTracker.h"

namespace r2sim {

struct SimDome
{
    float angle;          // real angle, -180..180
    float dpsPerUs;       // real degrees per second per microsecond of servo offset above the deadband
    uint16_t deadbandUs;  // offsets at or below this don't move the dome
    float arcDeg;         // the sensor reads low within this many degrees of a magnet (0 = a point)
    float lagS;           // the real speed follows the command with this time constant (0 = at once)
    float vel;            // real speed, degrees per second, positive clockwise
    bool magnetPresent[r2::kMagnetCount];
    uint32_t blips;       // blips reported so far
    bool lateBlips;       // hand each blip over on the next step, after the tracker was updated past it
    uint32_t pending[8];
    uint8_t pendingCount;

    SimDome(float startDeg, float dpsPerUsReal, uint16_t deadband)
        : angle(startDeg), dpsPerUs(dpsPerUsReal), deadbandUs(deadband), arcDeg(0.0f), lagS(0.0f), vel(0.0f),
          blips(0), lateBlips(false), pendingCount(0)
    {
        for (uint8_t i = 0; i < r2::kMagnetCount; i++)
            magnetPresent[i] = true;
    }

    float speed(uint16_t offsetUs) const
    {
        return offsetUs > deadbandUs ? dpsPerUs * float(offsetUs - deadbandUs) : 0.0f;
    }

    // True while the sensor is over a magnet (the Hall output is low).
    bool sensorLow() const
    {
        if (arcDeg <= 0.0f)
            return false;
        for (uint8_t i = 0; i < r2::kMagnetCount; i++)
            if (magnetPresent[i] && std::fabs(r2::wrapDeg(angle - r2::kMagnetDeg[i])) <= arcDeg)
                return true;
        return false;
    }

    // Moves the dome for stepUs under one command in 1 ms slices, reporting each
    // magnet reached to the tracker at the slice it happens in. Ends with update().
    void step(r2::DomeTracker& tracker, uint32_t& nowUs, uint32_t stepUs, int8_t dir, uint16_t offsetUs)
    {
        for (uint8_t i = 0; i < pendingCount; i++)
            tracker.onBlip(pending[i]);
        pendingCount = 0;
        tracker.setSensor(sensorLow());
        tracker.setCommand(nowUs, dir, offsetUs);
        float target = float(dir) * speed(offsetUs);
        for (uint32_t s = 0; s < stepUs; s += 1000)
        {
            float before = angle;
            bool wasLow = sensorLow();
            if (lagS > 0.0f)
            {
                vel += (target - vel) * (0.001f / lagS);
                if (target == 0.0f && std::fabs(vel) < 0.05f)
                    vel = 0.0f;
            }
            else
                vel = target;
            angle = r2::wrapDeg(angle + vel * 0.001f);
            if (vel == 0.0f)
                continue;
            bool blip = false;
            if (arcDeg > 0.0f)
                blip = !wasLow && sensorLow(); // the Hall output falls entering an arc
            else
            {
                float d = vel > 0.0f ? 1.0f : -1.0f;
                for (uint8_t i = 0; i < r2::kMagnetCount; i++)
                {
                    if (!magnetPresent[i])
                        continue;
                    // The slice covers (before, after]: the magnet was ahead before it
                    // and is level with or behind the dome after it.
                    float ahead = r2::wrapDeg(r2::kMagnetDeg[i] - before) * d;
                    float aheadAfter = r2::wrapDeg(r2::kMagnetDeg[i] - angle) * d;
                    if (ahead > 0.0f && ahead < 10.0f && aheadAfter <= 0.0f)
                        blip = true;
                }
            }
            if (blip)
            {
                blips++;
                if (lateBlips && pendingCount < 8)
                    pending[pendingCount++] = nowUs + s + 1000;
                else
                    tracker.onBlip(nowUs + s + 1000);
            }
        }
        nowUs += stepUs;
        tracker.update(nowUs);
    }
};

} // namespace r2sim
