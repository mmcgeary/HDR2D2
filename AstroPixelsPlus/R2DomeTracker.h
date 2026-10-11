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
// Learned speeds are kept for servo offsets 20-309 us in 10 us bands (the
// last band takes everything above 300 us).
static const uint8_t kSpeedBands = 29;

// Learned dome speeds, as saved in preferences. For each direction (0
// clockwise, 1 anticlockwise) and band: the speed measured, in degrees per
// second, and the servo offset it was measured at. 0 = not learned.
struct DomeSpeeds
{
    float dps[2][kSpeedBands];
    float atUs[2][kSpeedBands];
};

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
        float maxGapGrowth;    // a previous gap longer than this many times the one before it was a stall or lost blips
        float overdueFactor;   // learned travel beyond this many gaps without a blip = lost
        float unlearnedOverdueFactor; // the same before the speed is learned
        float defaultDpsPerUs; // speed estimate before anything is learned
        float zeroSpeedUs;     // offset where the dome stops, until learned speeds show it
        float alpha;           // learning rate
    };

    static Config defaults()
    {
        Config c = {8000, 0.4f, 2.5f, 2.5f, 6.0f, 0.5f, 20.0f, 0.3f};
        return c;
    }

    DomeTracker() : fCfg(defaults())
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSpeeds.dps[d][b] = fSpeeds.atUs[d][b] = fSaved[d][b] = 0.0f;
    }

    // The Hall sensor's level, read just before the loop's time: true while it
    // is over a magnet. Call before setCommand().
    void setSensor(bool overMagnet) { fOverMagnet = overMagnet; }

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
        float prevPrevTravel = fPrevPrevTravelDeg;
        bool steady = fSteady && fHaveBlip && fDir != 0;
        uint32_t gapUs = tUs - fLastBlipUs;
        fHaveBlip = true;
        fLastBlipUs = tUs;
        fTravelDeg = 0.0f;
        fPrevPrevTravelDeg = prevTravel;
        fPrevTravelDeg = travel;
        fSteady = true;
        if (fLastDir == 0)
            return;
        // The previous gap is a baseline only if it was an ordinary gap: one far
        // longer than the gap before it was a stall or lost blips.
        bool baseline = prevTravel > 0.0f && (prevPrevTravel <= 0.0f || prevTravel <= fCfg.maxGapGrowth * prevPrevTravel);
        if (baseline && travel < fCfg.frontGapRatio * prevTravel)
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

    // Speed at an offset: straight lines between the learned speeds, and
    // beyond them a line to the offset where the dome stops. A band one
    // direction hasn't learned uses the other direction's speed: a dome turns
    // much the same both ways.
    float speedDps(int8_t dir, uint16_t offsetUs) const
    {
        if (band(offsetUs) < 0 || dir == 0)
            return 0.0f;
        uint8_t d = dir > 0 ? 0 : 1;
        float off = float(offsetUs);
        int lo = -1, hi = -1;
        float vLo = 0.0f, atLo = 0.0f, vHi = 0.0f, atHi = 0.0f;
        for (int b = 0; b < kSpeedBands; b++)
        {
            float v, at;
            if (!point(d, b, v, at))
                continue;
            if (at <= off)
            {
                lo = b;
                vLo = v;
                atLo = at;
            }
            else if (hi < 0)
            {
                hi = b;
                vHi = v;
                atHi = at;
            }
        }
        if (lo < 0 && hi < 0)
            return fCfg.defaultDpsPerUs * off;
        if (lo >= 0 && hi >= 0)
            return vLo + (vHi - vLo) * (off - atLo) / (atHi - atLo);
        float v = lo >= 0 ? vLo : vHi;
        float at = lo >= 0 ? atLo : atHi;
        float zero = zeroSpeedUs(d);
        if (zero > at - 1.0f)
            zero = at - 1.0f;
        float speed = v * (off - zero) / (at - zero);
        return speed > 0.0f ? speed : 0.0f;
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

    void exportSpeeds(DomeSpeeds& out) const { out = fSpeeds; }

    // A band whose speed isn't plausible (NaN, <= 0, >= 1000 deg/s) or whose
    // offset lies outside the band is ignored.
    void importSpeeds(const DomeSpeeds& in)
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
            {
                float v = in.dps[d][b];
                float at = in.atUs[d][b];
                float lo = float(20 + 10 * b);
                float hi = b == kSpeedBands - 1 ? 1000.0f : lo + 10.0f;
                bool ok = v > 0.0f && v < 1000.0f && at >= lo && at < hi;
                fSpeeds.dps[d][b] = ok ? v : 0.0f;
                fSpeeds.atUs[d][b] = ok ? at : 0.0f;
            }
    }

    bool dirty() const { return fDirty; }

    void markSaved()
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSaved[d][b] = fSpeeds.dps[d][b];
        fDirty = false;
    }

private:
    Config fCfg;
    DomeSpeeds fSpeeds;
    float fSaved[2][kSpeedBands];
    bool fDirty = false;

    bool fKnown = false;
    bool fLearnable = false;
    bool fJustReversed = false;
    bool fOverMagnet = false;
    float fLastMagnet = 0.0f;
    float fTravelDeg = 0.0f;
    float fPrevTravelDeg = 0.0f;
    float fPrevPrevTravelDeg = 0.0f;

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

    bool learned(int8_t dir, uint16_t offsetUs) const
    {
        int b = band(offsetUs);
        return b >= 0 && dir != 0 && fSpeeds.dps[dir > 0 ? 0 : 1][b] > 0.0f;
    }

    // The learned speed for band b in direction d, or failing that the other
    // direction's. False when neither has learned it.
    bool point(uint8_t d, int b, float& v, float& at) const
    {
        if (fSpeeds.dps[d][b] <= 0.0f)
            d = uint8_t(1 - d);
        v = fSpeeds.dps[d][b];
        at = fSpeeds.atUs[d][b];
        return v > 0.0f;
    }

    // Where the line through the lowest learned speed and one learned at least
    // 40 us above it reaches zero: the servo deadband. The default until then.
    float zeroSpeedUs(uint8_t d) const
    {
        bool haveLow = false;
        float vLow = 0.0f, atLow = 0.0f;
        for (int b = 0; b < kSpeedBands; b++)
        {
            float v, at;
            if (!point(d, b, v, at))
                continue;
            if (!haveLow)
            {
                haveLow = true;
                vLow = v;
                atLow = at;
            }
            else if (at - atLow >= 40.0f && v > vLow)
            {
                float zero = atLow - vLow * (at - atLow) / (v - vLow);
                return zero < 0.0f ? 0.0f : zero;
            }
        }
        return fCfg.zeroSpeedUs;
    }

    void learn(int8_t dir, uint16_t offsetUs, float dps)
    {
        int b = band(offsetUs);
        if (b < 0 || !(dps > 0.0f && dps < 1000.0f))
            return;
        uint8_t d = dir > 0 ? 0 : 1;
        float& v = fSpeeds.dps[d][b];
        float& at = fSpeeds.atUs[d][b];
        if (v > 0.0f)
        {
            v += fCfg.alpha * (dps - v);
            at += fCfg.alpha * (float(offsetUs) - at);
        }
        else
        {
            v = dps;
            at = float(offsetUs);
        }
        float saved = fSaved[d][b];
        if (saved <= 0.0f || fabsf(v - saved) > 0.1f * saved)
            fDirty = true;
    }

    float gapAhead() const
    {
        return fabsf(wrapDeg(nextMagnet(fLastMagnet, fLastDir) - fLastMagnet));
    }

    void advance(uint32_t nowUs)
    {
        // A blip can be read after the tracker was updated past its time. Time
        // never runs backwards here.
        if (fHaveTime && int32_t(nowUs - fLastUs) <= 0)
            return;
        if (fHaveTime && fDir != 0)
            fTravelDeg += speedDps(fDir, fOffset) * float(nowUs - fLastUs) * 1e-6f;
        fLastUs = nowUs;
        fHaveTime = true;
    }

    // Direction flipped. Between magnets, the magnet ahead becomes the one
    // behind. Over the magnet just reported, the sensor stays low as the dome
    // backs off it: no new blip, so that magnet stays the last one passed.
    void reverse()
    {
        fPrevTravelDeg = 0.0f;
        fPrevPrevTravelDeg = 0.0f;
        fSteady = false;
        fLearnable = false;
        fJustReversed = true;
        if (!fKnown || fOverMagnet)
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

// Feeds the tracker the blip times the interrupt stored, oldest first, up to
// nowUs. ring keeps the last 8 times; head counts every blip stored and tail
// every blip read. A blip after nowUs waits for the next call, so blips and
// commands reach the tracker in time order. More than 8 waiting means some
// were overwritten: the position becomes unknown. Returns the new tail.
inline uint8_t drainBlips(const volatile uint32_t ring[8], uint8_t head, uint8_t tail, uint32_t nowUs,
                          DomeTracker& tracker)
{
    if (uint8_t(head - tail) > 8)
    {
        tracker.markUnknown();
        return head;
    }
    while (tail != head)
    {
        uint32_t t = ring[tail & 7];
        if (int32_t(t - nowUs) > 0)
            break;
        tracker.onBlip(t);
        tail = uint8_t(tail + 1);
    }
    return tail;
}

} // namespace r2
