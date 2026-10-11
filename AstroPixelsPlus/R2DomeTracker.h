#pragma once

// Dome angle from the ring of body magnets (spec §4). 0 = front, positive =
// clockwise seen from above, -180..+180. Magnets sit every 30 degrees, plus
// one at +5 degrees that pairs with the front magnet. The sensor turns with
// the dome: each blip means the dome reached the next magnet in its direction
// of travel. A blip that comes after much less travel than the one before
// marks the front pair. Between blips the angle is estimated from speeds the
// tracker learns as the dome turns.

#include <stdint.h>
#include <math.h>

namespace r2 {

static const float kFrontPairDeg = 5.0f;
static const uint8_t kMagnetCount = 13;
// Magnet positions in clockwise order from front, 0-360 degrees.
static const float kMagnetDeg[kMagnetCount] = {0, 5, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330};
// Learned speeds are kept for servo offsets 20-309 us in 10 us bands.
static const uint8_t kSpeedBands = 29;

inline float wrapDeg(float a)
{
    while (a > 180.0f)
        a -= 360.0f;
    while (a <= -180.0f)
        a += 360.0f;
    return a;
}

inline uint8_t nearestMagnetIndex(float deg)
{
    uint8_t best = 0;
    float bestDistance = 1000.0f;
    for (uint8_t i = 0; i < kMagnetCount; i++)
    {
        float d = fabsf(wrapDeg(deg - kMagnetDeg[i]));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

// True when deg is one of the magnet positions.
inline bool isMagnetAngle(float deg)
{
    return fabsf(wrapDeg(deg - kMagnetDeg[nearestMagnetIndex(deg)])) < 0.01f;
}

// The magnet after the one at `deg`, travelling in dir (+1 clockwise).
inline float nextMagnet(float deg, int8_t dir)
{
    uint8_t i = nearestMagnetIndex(deg);
    uint8_t n = dir > 0 ? uint8_t((i + 1) % kMagnetCount) : uint8_t((i + kMagnetCount - 1) % kMagnetCount);
    return wrapDeg(kMagnetDeg[n]);
}

class DomeTracker
{
public:
    struct Config
    {
        uint32_t bounceUs;     // blips closer than this to the last one are dropped
        float frontGapRatio;   // travel below this fraction of the previous gap = front pair
        float overdueFactor;   // learned travel beyond this many gaps without a blip = lost
        float unlearnedOverdueFactor; // the same before the speed is learned
        float defaultDpsPerUs; // speed estimate before anything is learned
        float alpha;           // learning rate
    };

    static Config defaults()
    {
        Config c = {8000, 0.4f, 2.5f, 6.0f, 0.5f, 0.3f};
        return c;
    }

    DomeTracker() : fCfg(defaults())
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSpeed[d][b] = fSaved[d][b] = 0.0f;
    }

    // The servo command now in force. direction: +1 clockwise, -1 anticlockwise,
    // 0 stopped. offsetUs = |pulse - neutral|.
    void setCommand(uint32_t nowUs, int8_t direction, uint16_t offsetUs)
    {
        advance(nowUs);
        if (direction != 0 && fLastDir != 0 && direction != fLastDir)
            reverse();
        if (direction != fDir || band(offsetUs) != band(fOffset))
            fSteady = false;
        fDir = direction;
        fOffset = offsetUs;
        if (direction != 0)
            fLastDir = direction;
    }

    // A magnet blip at tUs. Blips arrive in time order.
    void onBlip(uint32_t tUs)
    {
        if (fHaveBlip && tUs - fLastBlipUs < fCfg.bounceUs)
            return;
        advance(tUs);
        float travel = fTravelDeg;
        float prevTravel = fPrevTravelDeg;
        bool steady = fSteady && fHaveBlip && fDir != 0;
        uint32_t gapUs = tUs - fLastBlipUs;
        fHaveBlip = true;
        fLastBlipUs = tUs;
        fTravelDeg = 0.0f;
        fPrevTravelDeg = travel;
        fSteady = true;
        if (fLastDir == 0)
            return;
        if (prevTravel > 0.0f && travel < fCfg.frontGapRatio * prevTravel)
        {
            if (fKnown)
            {
                // Known position: a short gap only confirms front where the count
                // expects the pair. Anywhere else the estimates can't be trusted:
                // drop the position rather than jump to a wrong one.
                float expected = nextMagnet(fLastMagnet, fLastDir);
                bool pairExpected = (fLastDir > 0 && expected == kFrontPairDeg) || (fLastDir < 0 && expected == 0.0f);
                if (!pairExpected)
                {
                    markUnknown();
                    return;
                }
            }
            fKnown = true;
            fLearnable = true; // the front magnet's position is exact
            fLastMagnet = fLastDir > 0 ? kFrontPairDeg : 0.0f;
            return;
        }
        if (!fKnown)
            return;
        float next = nextMagnet(fLastMagnet, fLastDir);
        bool reversed = fJustReversed;
        fJustReversed = false;
        if (reversed)
            fPrevTravelDeg = 0.0f; // the stretch since the reversal is not a full gap
        // The pair's second magnet comes after a short gap. With no full gap to
        // compare (just after a reversal), trust the count.
        bool comparable = !reversed && prevTravel > 0.0f;
        bool pairSecond = comparable && ((fLastDir > 0 && next == kFrontPairDeg) || (fLastDir < 0 && next == 0.0f));
        if (pairSecond)
        {
            // The pair's second magnet always follows a short gap: the count is wrong.
            markUnknown();
            return;
        }
        if (steady && fLearnable)
            learn(fDir, fOffset, fabsf(wrapDeg(next - fLastMagnet)) / (float(gapUs) * 1e-6f));
        fLastMagnet = next;
        fLearnable = true;
    }

    // Advances the estimate and checks for a magnet that is overdue.
    void update(uint32_t nowUs)
    {
        advance(nowUs);
        float factor = learned(fDir, fOffset) ? fCfg.overdueFactor : fCfg.unlearnedOverdueFactor;
        if (fKnown && fDir != 0 && fTravelDeg > factor * gapAhead())
            markUnknown();
    }

    bool known() const { return fKnown; }

    float angle() const
    {
        if (!fKnown)
            return 0.0f;
        float limit = gapAhead() - 1.0f;
        float t = fTravelDeg < limit ? fTravelDeg : limit;
        if (t < 0.0f)
            t = 0.0f;
        return wrapDeg(fLastMagnet + float(fLastDir) * t);
    }

    // True when the estimate has run ahead to just short of the next magnet and
    // is waiting for it: the real dome is somewhere short of that magnet.
    bool held() const { return fKnown && fLastDir != 0 && fTravelDeg >= gapAhead() - 1.0f; }

    // True when the magnet at deg was the last one passed and the dome has moved
    // no more than tolDeg beyond it since: the dome is confirmed at that magnet.
    bool atMagnet(float deg, float tolDeg) const
    {
        return fKnown && fabsf(wrapDeg(fLastMagnet - deg)) < 0.01f && fTravelDeg <= tolDeg;
    }

    // Direction of the last movement: +1 clockwise, -1 anticlockwise, 0 never moved.
    int8_t travelDir() const { return fLastDir; }

    void markUnknown()
    {
        fKnown = false;
        fLearnable = false;
    }

    float speedDps(int8_t dir, uint16_t offsetUs) const
    {
        int b = band(offsetUs);
        if (b < 0 || dir == 0)
            return 0.0f;
        const float* row = fSpeed[dir > 0 ? 0 : 1];
        if (row[b] > 0.0f)
            return row[b];
        for (int d = 1; d < kSpeedBands; d++)
        {
            if (b - d >= 0 && row[b - d] > 0.0f)
                return row[b - d] * float(offsetUs) / float(bandCentre(b - d));
            if (b + d < kSpeedBands && row[b + d] > 0.0f)
                return row[b + d] * float(offsetUs) / float(bandCentre(b + d));
        }
        return fCfg.defaultDpsPerUs * float(offsetUs);
    }

    // Smallest offset expected to give at least dps (0 for no movement).
    uint16_t offsetForSpeed(int8_t dir, float dps) const
    {
        if (dps <= 0.0f || dir == 0)
            return 0;
        for (uint16_t off = 20; off < 310; off += 2)
            if (speedDps(dir, off) >= dps)
                return off;
        return 308;
    }

    void exportSpeeds(float out[2][kSpeedBands]) const
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                out[d][b] = fSpeed[d][b];
    }

    // Values that aren't a plausible speed (NaN, <= 0, >= 1000 deg/s) are ignored.
    void importSpeeds(const float in[2][kSpeedBands])
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSpeed[d][b] = (in[d][b] > 0.0f && in[d][b] < 1000.0f) ? in[d][b] : 0.0f;
    }

    bool dirty() const { return fDirty; }

    void markSaved()
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSaved[d][b] = fSpeed[d][b];
        fDirty = false;
    }

private:
    Config fCfg;
    float fSpeed[2][kSpeedBands];
    float fSaved[2][kSpeedBands];
    bool fDirty = false;

    bool fKnown = false;
    bool fLearnable = false;
    bool fJustReversed = false;
    float fLastMagnet = 0.0f;
    float fTravelDeg = 0.0f;
    float fPrevTravelDeg = 0.0f;

    int8_t fDir = 0;
    int8_t fLastDir = 0;
    uint16_t fOffset = 0;
    bool fSteady = false;

    bool fHaveBlip = false;
    uint32_t fLastBlipUs = 0;
    bool fHaveTime = false;
    uint32_t fLastUs = 0;

    static int band(uint16_t offsetUs)
    {
        if (offsetUs < 20)
            return -1;
        int b = (offsetUs - 20) / 10;
        return b >= kSpeedBands ? kSpeedBands - 1 : b;
    }

    static uint16_t bandCentre(int b) { return uint16_t(25 + 10 * b); }

    bool learned(int8_t dir, uint16_t offsetUs) const
    {
        int b = band(offsetUs);
        return b >= 0 && dir != 0 && fSpeed[dir > 0 ? 0 : 1][b] > 0.0f;
    }

    void learn(int8_t dir, uint16_t offsetUs, float dps)
    {
        int b = band(offsetUs);
        if (b < 0 || !(dps > 0.0f && dps < 1000.0f))
            return;
        uint8_t d = dir > 0 ? 0 : 1;
        float old = fSpeed[d][b];
        fSpeed[d][b] = old > 0.0f ? old + fCfg.alpha * (dps - old) : dps;
        float saved = fSaved[d][b];
        if (saved <= 0.0f || fabsf(fSpeed[d][b] - saved) > 0.1f * saved)
            fDirty = true;
    }

    float gapAhead() const
    {
        return fabsf(wrapDeg(nextMagnet(fLastMagnet, fLastDir) - fLastMagnet));
    }

    void advance(uint32_t nowUs)
    {
        if (fHaveTime && fDir != 0)
            fTravelDeg += speedDps(fDir, fOffset) * float(nowUs - fLastUs) * 1e-6f;
        fLastUs = nowUs;
        fHaveTime = true;
    }

    // Direction flipped between magnets: the magnet ahead becomes the one behind.
    void reverse()
    {
        fPrevTravelDeg = 0.0f;
        fSteady = false;
        fLearnable = false;
        fJustReversed = true;
        if (!fKnown)
        {
            fTravelDeg = 0.0f;
            return;
        }
        float gap = gapAhead();
        float t = fTravelDeg < gap ? fTravelDeg : gap;
        fLastMagnet = nextMagnet(fLastMagnet, fLastDir);
        fTravelDeg = gap - t;
    }
};

} // namespace r2
