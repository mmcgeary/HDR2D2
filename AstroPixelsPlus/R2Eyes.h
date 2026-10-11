#pragma once

// Holoprojector "eyes" (spec §6). Every move is a smooth two-axis move: both
// servos of an eye start together and take the same time, eased by ReelTwo.
// The front eye leads the dome; the top and rear eyes wander on their own,
// like a chameleon's.

#include <stdint.h>
#include <math.h>
#include "R2Mood.h"

namespace r2 {

struct EyePos
{
    float h;
    float v;
};

// One move for one eye. pending = false means no new move this update.
struct EyeMove
{
    bool pending;
    uint32_t durationMs;
    EyePos to;
};

enum EyeCue : uint8_t
{
    kCueNone,
    kCueLook,
    kCueReturn,
    kCueFlick
};

// Eye order and their PCA9685 channels (WIRING.md): front 0/1, rear 2/3, top 4/5.
enum Eye : uint8_t
{
    kFrontEye = 0,
    kRearEye = 1,
    kTopEye = 2
};
static const uint8_t kEyeServoH[3] = {0, 2, 4};
static const uint8_t kEyeServoV[3] = {1, 3, 5};

// Set an axis to true if that holo's mechanism is mounted the other way round
// (it looks left when it should look right, or up when it should look down).
static const bool kEyeFlipH[3] = {false, false, false};
static const bool kEyeFlipV[3] = {false, false, false};

inline float clampUnit(float x)
{
    return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

// Eye position (-1..+1; +h = R2's right, +v = up) to a ReelTwo servo position
// (0..1 across the servo table's start..end pulses). ReelTwo's HoloLights uses
// 0 = right / up and 1 = left / down, and treats that whole range as safe.
inline float eyeServoPos(float pos, bool flip)
{
    float p = (1.0f - clampUnit(pos)) / 2.0f;
    return flip ? 1.0f - p : p;
}

// Bigger moves take longer; the mood's holo speed scales the time.
inline uint32_t eyeMoveDuration(EyePos from, EyePos to, float speed)
{
    float dh = fabsf(to.h - from.h);
    float dv = fabsf(to.v - from.v);
    float d = (dh > dv ? dh : dv) / 2.0f;
    if (speed < 0.1f)
        speed = 0.1f;
    return uint32_t((600.0f + 900.0f * d) / speed);
}

// -1..+1
inline float signedFraction(RandomFn rnd)
{
    return float(rnd(0, 20001)) / 10000.0f - 1.0f;
}

inline uint32_t pickMs(const Range& r, float scale, RandomFn rnd)
{
    return uint32_t(pick(r, rnd) * scale * 1000.0f);
}

// A wandering eye: move to a point, hold, then adjust, rest or move on.
class ChameleonEye
{
public:
    // active = false: ease to centre once and stay (Still). rangeScale and
    // holdScale shrink the range and lengthen the holds (front eye between
    // looks, driving).
    EyeMove update(uint32_t nowMs, bool active, const MoodProfile& mood, float rangeScale, float holdScale,
                   RandomFn rnd)
    {
        EyeMove none = {false, 0, fPos};
        if (!active)
        {
            fStarted = false;
            if (fPos.h != 0.0f || fPos.v != 0.0f)
            {
                EyePos centre = {0.0f, 0.0f};
                return moveTo(nowMs, centre, mood.holoSpeed, 0);
            }
            return none;
        }
        if (!fStarted)
        {
            fStarted = true;
            fNextMs = nowMs + pickMs(mood.holoHoldS, holdScale, rnd);
            return none;
        }
        if (!due(nowMs))
            return none;
        uint32_t hold = pickMs(mood.holoHoldS, holdScale, rnd);
        uint32_t roll = rnd(0, 100);
        EyePos to;
        if (roll < 25)
        {
            to.h = clampUnit(fPos.h + 0.1f * signedFraction(rnd));
            to.v = clampUnit(fPos.v + 0.1f * signedFraction(rnd));
        }
        else if (roll < 45)
        {
            to.h = 0.0f;
            to.v = 0.0f;
        }
        else
        {
            float range = mood.holoRange * rangeScale;
            to.h = clampUnit(range * signedFraction(rnd));
            to.v = clampUnit(range * signedFraction(rnd) + mood.holoVBias);
        }
        return moveTo(nowMs, to, mood.holoSpeed, hold);
    }

    // Starts a move now; the next decision comes after the move plus holdMs.
    EyeMove moveTo(uint32_t nowMs, EyePos to, float speed, uint32_t holdMs)
    {
        EyeMove move = {true, eyeMoveDuration(fPos, to, speed), to};
        fPos = to;
        fNextMs = nowMs + move.durationMs + holdMs;
        return move;
    }

    EyePos position() const { return fPos; }

    bool due(uint32_t nowMs) const { return int32_t(nowMs - fNextMs) >= 0; }

private:
    EyePos fPos = {0.0f, 0.0f};
    uint32_t fNextMs = 0;
    bool fStarted = false;
};

// The front eye: wanders gently between looks, leads the dome into a look,
// adjusts while the dome holds, and flicks toward front on SwB.
class LeadingEye
{
public:
    EyeMove update(uint32_t nowMs, bool active, const MoodProfile& mood, EyeCue cue, int8_t side, bool calm,
                   RandomFn rnd)
    {
        if (!active)
        {
            fMode = kRoaming;
            return fEye.update(nowMs, false, mood, 1.0f, 1.0f, rnd);
        }
        if (cue == kCueLook)
        {
            fMode = kLooking;
            fNextAdjustMs = nowMs + rnd(1500, 3001);
            EyePos to = {clampUnit(0.6f * mood.holoRange * float(side)), clampUnit(0.2f * signedFraction(rnd))};
            return fEye.moveTo(nowMs, to, mood.holoSpeed, 0);
        }
        if (cue == kCueReturn)
        {
            fMode = kRoaming;
            EyePos centre = {0.0f, 0.0f};
            return fEye.moveTo(nowMs, centre, mood.holoSpeed, pickMs(mood.holoHoldS, 1.5f, rnd));
        }
        if (cue == kCueFlick)
        {
            if (fMode != kFlicking)
            {
                fResumeMode = fMode;
                fFlickBack = fEye.position();
            }
            fMode = kFlicking;
            EyePos up = {0.0f, 0.2f};
            return fEye.moveTo(nowMs, up, 4.0f, 600);
        }
        EyeMove none = {false, 0, fEye.position()};
        switch (fMode)
        {
            case kFlicking:
                if (fEye.due(nowMs))
                {
                    fMode = fResumeMode;
                    fNextAdjustMs = nowMs + rnd(1500, 3001);
                    return fEye.moveTo(nowMs, fFlickBack, mood.holoSpeed,
                                       fMode == kRoaming ? pickMs(mood.holoHoldS, 1.5f, rnd) : 0);
                }
                return none;
            case kLooking:
                if (int32_t(nowMs - fNextAdjustMs) >= 0)
                {
                    fNextAdjustMs = nowMs + rnd(1500, 3001);
                    EyePos p = fEye.position();
                    EyePos to = {clampUnit(p.h + 0.1f * signedFraction(rnd)), clampUnit(p.v + 0.1f * signedFraction(rnd))};
                    return fEye.moveTo(nowMs, to, mood.holoSpeed, 0);
                }
                return none;
            case kRoaming:
                return fEye.update(nowMs, true, mood, calm ? 0.35f : 0.5f, calm ? 2.25f : 1.5f, rnd);
        }
        return none;
    }

    bool looking() const { return fMode == kLooking; }

    EyePos position() const { return fEye.position(); }

private:
    enum Mode : uint8_t
    {
        kRoaming,
        kLooking,
        kFlicking
    };

    ChameleonEye fEye;
    Mode fMode = kRoaming;
    Mode fResumeMode = kRoaming;
    EyePos fFlickBack = {0.0f, 0.0f};
    uint32_t fNextAdjustMs = 0;
};

} // namespace r2
