#pragma once

// Turns dome requests into smooth servo pulses (spec §5). Moves ease up to a
// cruise speed and ease down onto the target. The stick always wins.

#include <stdint.h>
#include <math.h>
#include "R2Logic.h"
#include "R2DomeTracker.h"

namespace r2 {

enum DomeRequestKind : uint8_t
{
    kDomeHold,
    kDomeGoTo,
    kDomeSeek
};

struct DomeRequest
{
    DomeRequestKind kind;
    float targetDeg;
    float cruiseDps;
    float easeS;
    bool slowFinish; // last 15 degrees at the final speed (returns to front)
};

inline DomeRequest domeHold()
{
    DomeRequest r = {kDomeHold, 0.0f, 0.0f, 0.5f, false};
    return r;
}

inline DomeRequest domeGoTo(float target, float cruise, float ease, bool slowFinish = false)
{
    DomeRequest r = {kDomeGoTo, target, cruise, ease, slowFinish};
    return r;
}

inline DomeRequest domeSeek()
{
    DomeRequest r = {kDomeSeek, 0.0f, 20.0f, 0.5f, false};
    return r;
}

static const float kPi = 3.14159265f;
static const float kDomeLimitDeg = 135.0f;

inline float clampDeg(float a)
{
    return a > kDomeLimitDeg ? kDomeLimitDeg : (a < -kDomeLimitDeg ? -kDomeLimitDeg : a);
}

// Speed fraction along a sine-shaped ramp, s from 0 to 1.
inline float sineRamp(float s)
{
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    return (1.0f - cosf(kPi * s)) / 2.0f;
}

// Distance covered ramping from 0 to s, as a fraction of cruise x ease time.
inline float rampDistance(float s)
{
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    return s / 2.0f - sinf(kPi * s) / (2.0f * kPi);
}

class DomeMotion
{
public:
    struct Config
    {
        uint16_t stickDeadbandUs;
        uint16_t minMoveOffsetUs; // servo deadband: smallest offset that moves the dome
        float finalDps;           // speed for the final approach
        float arriveDeg;
        float slowZoneDeg;
        float maxStepUs;          // per 20 ms
    };

    static Config defaults()
    {
        Config c = {80, 40, 15.0f, 3.0f, 15.0f, 10.0f};
        return c;
    }

    DomeMotion() : fCfg(defaults()) {}

    // Returns the servo pulse in us, or 0 for no pulses (before any radio).
    uint16_t update(uint32_t nowMs, bool radio, uint16_t stickUs, const DomeRequest& req, bool known, float angleDeg,
                    const DomeTracker& speeds)
    {
        float dt = fHaveTime ? float(nowMs - fLastMs) / 1000.0f : 0.0f;
        if (dt > 0.1f)
            dt = 0.1f;
        fLastMs = nowMs;
        fHaveTime = true;

        if (!radio)
        {
            fFrac = 0.0f;
            fMoveDir = 0;
            fManual = false;
            fPulse = fNeutral;
            return fNeutral;
        }
        if (deflected(stickUs, fCfg.stickDeadbandUs))
        {
            fManual = true;
            fFrac = 0.0f;
            fMoveDir = 0;
            fArrived = false;
            fPulse = stickUs;
            return stickUs;
        }
        fManual = false;
        fNeutral = stickUs;
        if (fPulse == 0)
            fPulse = stickUs;
        if (req.kind != fReqKind || req.targetDeg != fReqTarget)
        {
            fReqKind = req.kind;
            fReqTarget = req.targetDeg;
            fArrived = false;
            fBraking = false;
            fGoDir = 0;
            fWasHeld = false;
        }

        int8_t dir = 0;
        bool accelerate = false;
        bool approach = false;
        float remaining = 0.0f;
        if (req.kind == kDomeSeek && !known)
        {
            dir = 1;
            accelerate = true;
            fCruise = req.cruiseDps;
            fEase = req.easeS;
        }
        else if (req.kind == kDomeGoTo && known && !fArrived)
        {
            float target = clampDeg(req.targetDeg);
            remaining = target - angleDeg;
            // Passed: moving under this request, and the target is now behind.
            bool passed = fGoDir != 0 && remaining * float(fGoDir) < 0.0f;
            bool nearOrPast = fabsf(remaining) <= fCfg.arriveDeg || passed;
            // A target on a magnet (front, for every return) is only reached when
            // that magnet has actually been passed.
            bool awaitingMagnet = speeds.known() && isMagnetAngle(target) && !speeds.atMagnet(target, fCfg.arriveDeg);
            fCruise = req.cruiseDps;
            fEase = req.easeS;
            // A slow finish brakes to reach the final speed where the slow zone starts.
            float brakeAt = fabsf(remaining) - (req.slowFinish ? fCfg.slowZoneDeg : 0.0f);
            bool brakeDue = fBraking || brakeAt <= fCruise * fEase * rampDistance(fFrac) + 0.5f;
            if ((speeds.held() && (nearOrPast || brakeDue)) || (nearOrPast && awaitingMagnet))
            {
                // The estimate ran ahead of the real dome: it is short of the next
                // magnet. Far from the target that changes nothing (the real dome
                // is further back still). Near it, creep on until a magnet confirms
                // where the dome is; the move then carries on from the true position.
                dir = speeds.travelDir();
                fGoDir = 0;
                fBraking = true;
                fWasHeld = true;
                approach = true;
            }
            else if (nearOrPast)
            {
                fArrived = true;
                fBraking = false;
                fFrac = 0.0f;
            }
            else
            {
                if (fWasHeld)
                {
                    // The magnet confirmed the position: speed up again if there's room.
                    fWasHeld = false;
                    fBraking = false;
                }
                dir = remaining > 0.0f ? 1 : -1;
                if (fMoveDir != 0 && dir != fMoveDir)
                {
                    fFrac = 0.0f;
                    fBraking = false;
                }
                fGoDir = dir;
                // Once braking starts it continues to the target (no re-acceleration).
                if (!fBraking && brakeAt <= fCruise * fEase * rampDistance(fFrac) + 0.5f)
                    fBraking = true;
                accelerate = !fBraking;
                approach = true;
            }
        }

        float ease = fEase > 0.05f ? fEase : 0.05f;
        fFrac += (accelerate ? dt : -dt) / ease;
        fFrac = fFrac < 0.0f ? 0.0f : (fFrac > 1.0f ? 1.0f : fFrac);
        float speed = fCruise * sineRamp(fFrac);
        if (approach)
        {
            if (!accelerate && speed < fCfg.finalDps)
                speed = fCfg.finalDps;
            if (req.slowFinish && fabsf(remaining) < fCfg.slowZoneDeg && speed > fCfg.finalDps)
                speed = fCfg.finalDps;
        }
        if (dir == 0)
            dir = fFrac > 0.0f ? fMoveDir : 0;
        if (dir == 0)
            speed = 0.0f;
        fMoveDir = speed > 0.0f ? dir : 0;

        uint16_t offset = 0;
        if (speed > 0.0f)
        {
            offset = speeds.offsetForSpeed(dir, speed);
            if (offset < fCfg.minMoveOffsetUs)
                offset = fCfg.minMoveOffsetUs;
        }
        int32_t target = int32_t(fNeutral) + int32_t(dir) * int32_t(offset);
        int32_t limit = int32_t(fCfg.maxStepUs * dt / 0.02f + 0.001f);
        if (limit < 1 && dt > 0.0f)
            limit = 1;
        int32_t step = target - int32_t(fPulse);
        if (step > limit)
            step = limit;
        if (step < -limit)
            step = -limit;
        fPulse = uint16_t(int32_t(fPulse) + step);
        return fPulse;
    }

    bool arrived() const { return fArrived; }
    bool manual() const { return fManual; }
    uint16_t neutral() const { return fNeutral; }

    // The command actually sent, as the tracker needs it.
    int8_t direction() const
    {
        int32_t d = int32_t(fPulse) - int32_t(fNeutral);
        return (fPulse == 0 || (d < 20 && d > -20)) ? 0 : (d > 0 ? 1 : -1);
    }

    uint16_t offsetUs() const
    {
        int32_t d = int32_t(fPulse) - int32_t(fNeutral);
        return direction() == 0 ? 0 : uint16_t(d > 0 ? d : -d);
    }

private:
    Config fCfg;
    bool fHaveTime = false;
    uint32_t fLastMs = 0;
    uint16_t fNeutral = 0;
    uint16_t fPulse = 0;
    bool fManual = false;
    bool fArrived = false;
    DomeRequestKind fReqKind = kDomeHold;
    float fReqTarget = 0.0f;
    float fFrac = 0.0f;
    float fCruise = 0.0f;
    float fEase = 0.5f;
    int8_t fMoveDir = 0;
    int8_t fGoDir = 0;
    bool fBraking = false;
    bool fWasHeld = false;
};

} // namespace r2
