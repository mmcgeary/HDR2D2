#pragma once

// Mood profiles (spec §7): the numbers that give each mood its personality.
// Starting values; tune them on the droid.

#include <stdint.h>
#include "R2Logic.h"

namespace r2 {

struct Range
{
    float lo;
    float hi;
};

inline float pick(const Range& r, RandomFn rnd)
{
    if (r.hi <= r.lo)
        return r.lo;
    return r.lo + (r.hi - r.lo) * float(rnd(0, 10001)) / 10000.0f;
}

inline bool chance(uint8_t percent, RandomFn rnd)
{
    return rnd(0, 100) < percent;
}

enum Gesture : uint8_t
{
    kGlance,
    kHalfTurnFront,
    kWiggle,
    kSlowTurnAway,
    kFaceFront
};

// Mood dial positions. 1-5 are also the Marcduino sound banks.
enum Mood : uint8_t
{
    kMixed = 0,
    kGeneral = 1,
    kChat = 2,
    kHappy = 3,
    kSad = 4,
    kWhistle = 5
};

struct MoodProfile
{
    const char* name;
    Range lookIntervalS;
    Range lookAngleDeg;
    float domeCruiseDps;
    float domeEaseS;
    Range holdS;
    uint8_t readjustPct;
    uint8_t doubleTakePct;
    uint8_t chainPct;
    uint8_t glanceFrontPct;
    uint8_t noticePct;
    uint8_t commentPct;
    Range remarkIntervalS;
    float holoRange;
    Range holoHoldS;
    float holoSpeed;
    float holoVBias;
    uint8_t soundBank;
    Gesture gesture;
    // Alive lighting
    uint16_t psiSpeedMs;       // PSI colour wipe, ms per column: 50 (stock, fastest) and up in 50 ms steps
    uint8_t fldDelay;          // front logic's longest pause on a key colour, in 10 ms frames (stock 10)
    uint8_t rldDelay;          // rear logic's, likewise (stock 40)
    uint8_t holoHoldChancePct; // chance the front holo lamp lights for a look (top and rear: a quarter of it)
};

static const MoodProfile kMoodProfiles[5] = {
    // name      look every   look angle   cruise ease   hold         adj dbl chn gfr ntc cmt  remark every  range  holo hold    speed vbias  bank gesture         psi fld rld holo%
    {"general", {12, 35}, {25, 100}, 45, 0.6f, {2, 6}, 30, 15, 15, 0, 25, 20, {40, 90}, 0.5f, {3, 8}, 1.0f, 0.0f, 1, kGlance, 100, 10, 40, 15},
    {"chat", {10, 28}, {20, 80}, 50, 0.5f, {2, 5}, 25, 10, 20, 0, 40, 60, {20, 50}, 0.55f, {3, 7}, 1.0f, 0.0f, 2, kHalfTurnFront, 50, 7, 28, 35},
    {"happy", {6, 18}, {20, 120}, 75, 0.35f, {1, 3}, 35, 20, 25, 0, 40, 40, {25, 60}, 0.8f, {1.5f, 4}, 1.4f, 0.1f, 3, kWiggle, 50, 5, 20, 25},
    {"sad", {30, 75}, {15, 60}, 25, 1.0f, {3, 8}, 10, 5, 5, 0, 15, 15, {60, 150}, 0.4f, {5, 12}, 0.6f, -0.4f, 4, kSlowTurnAway, 200, 16, 64, 0},
    {"whistle", {10, 30}, {25, 90}, 50, 0.5f, {1.5f, 4}, 20, 10, 10, 50, 30, 30, {30, 70}, 0.6f, {2, 6}, 1.0f, 0.0f, 5, kFaceFront, 50, 6, 24, 40},
};


// The PSI colour wipe's speed setting for psiSpeedMs: 0 is 50 ms per column
// (stock), each step 50 ms more, up to 9.
inline uint8_t psiSpeedScale(uint16_t msPerColumn)
{
    uint16_t steps = uint16_t((msPerColumn + 25) / 50);
    return steps <= 1 ? 0 : (steps >= 10 ? 9 : uint8_t(steps - 1));
}

// A logic display's pause while R2 is speaking: 60% of the mood's, but never
// below 3 frames (or the mood's own, if that is lower).
inline uint8_t speakingDelay(uint8_t delay)
{
    uint8_t d = uint8_t(uint16_t(delay) * 3 / 5);
    if (d >= 3)
        return d;
    return delay < 3 ? delay : 3;
}

// mood 1-5; anything else gives General.
inline const MoodProfile& moodProfile(uint8_t mood)
{
    return kMoodProfiles[(mood >= kGeneral && mood <= kWhistle) ? mood - 1 : 0];
}

// Turns the dial position into the active mood (1-5). Mixed picks one of the
// five at random and picks again every 1-3 minutes.
class MoodSelector
{
public:
    uint8_t update(uint8_t dialMood, uint32_t nowMs, RandomFn rnd)
    {
        if (dialMood != kMixed)
        {
            fWasMixed = false;
            fActive = (dialMood >= kGeneral && dialMood <= kWhistle) ? dialMood : uint8_t(kGeneral);
            return fActive;
        }
        if (!fWasMixed || int32_t(nowMs - fNextChangeMs) >= 0)
        {
            fWasMixed = true;
            fActive = uint8_t(rnd(1, 6));
            fNextChangeMs = nowMs + rnd(60000, 180001);
        }
        return fActive;
    }

    uint8_t active() const { return fActive; }

private:
    uint8_t fActive = kGeneral;
    bool fWasMixed = false;
    uint32_t fNextChangeMs = 0;
};

} // namespace r2
