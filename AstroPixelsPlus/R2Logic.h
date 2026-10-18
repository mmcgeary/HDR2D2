#pragma once

// Hardware-free logic for the FlySky remote features: iBUS decoding, sticks,
// switches, dials, volume, random timers and mood sounds. Everything here is
// plain C++ so it can be unit-tested on a PC (see tests/test_r2_logic.py).
// R2Remote.h connects it to the ESP32 pins, sound player and servos.

#include <stdint.h>

namespace r2 {

// Random source: returns a value in [lo, hi), like Arduino's random(lo, hi).
typedef uint32_t (*RandomFn)(uint32_t lo, uint32_t hi);

static const uint16_t kCentreUs = 1500;

////////////////////////////////
// FlySky iBUS: 32-byte frames of 0x20 0x40, 14 little-endian channels and a
// checksum equal to 0xFFFF minus the sum of the first 30 bytes.

class IbusParser
{
public:
    static const uint8_t kChannels = 14;

    IbusParser()
    {
        for (uint8_t i = 0; i < kChannels; i++)
            fChannels[i] = kCentreUs;
    }

    // Returns true when this byte completed a valid frame.
    bool feed(uint8_t b)
    {
        if (fPos == 0)
        {
            if (b == 0x20)
                fBuf[fPos++] = b;
            return false;
        }
        if (fPos == 1)
        {
            if (b == 0x40)
                fBuf[fPos++] = b;
            else
                fPos = (b == 0x20) ? 1 : 0;
            return false;
        }
        fBuf[fPos++] = b;
        if (fPos < sizeof(fBuf))
            return false;
        fPos = 0;

        uint16_t sum = 0xFFFF;
        for (uint8_t i = 0; i < 30; i++)
            sum -= fBuf[i];
        if (sum != uint16_t(fBuf[30] | (fBuf[31] << 8)))
            return false;
        for (uint8_t i = 0; i < kChannels; i++)
            fChannels[i] = uint16_t(fBuf[2 + i * 2] | (fBuf[3 + i * 2] << 8)) & 0x0FFF;
        return true;
    }

    // ch is 1-based, matching the transmitter's channel numbers.
    uint16_t channel(uint8_t ch) const
    {
        return (ch >= 1 && ch <= kChannels) ? fChannels[ch - 1] : kCentreUs;
    }

private:
    uint8_t fBuf[32];
    uint8_t fPos = 0;
    uint16_t fChannels[kChannels];
};

////////////////////////////////
// Sticks, switches and dials

inline bool deflected(uint16_t us, uint16_t band)
{
    return us > kCentreUs + band || us < kCentreUs - band;
}

// Two-position switch: on when flipped down (about 2000us).
inline bool switchOn(uint16_t us)
{
    return us > 1750;
}

// Three-position switch: false only in the up position (about 1000us).
inline bool switchNotUp(uint16_t us)
{
    return us > 1250;
}

// Three-position switch: 0 up (about 1000us), 1 middle, 2 down (about 2000us).
inline uint8_t switchLevel(uint16_t us)
{
    return us < 1250 ? 0 : (us > 1750 ? 2 : 1);
}

// Splits a 1000-2000us dial into equal positions numbered 0..positions-1.
inline uint8_t dialPosition(uint16_t us, uint8_t positions)
{
    uint32_t v = us < 1000 ? 0 : us > 2000 ? 1000 : uint32_t(us - 1000);
    uint8_t pos = uint8_t(v * positions / 1001);
    return pos < positions ? pos : positions - 1;
}

// dialPosition() with hysteresis: a dial resting on the edge between two
// positions stays on the one it was in until it is clearly past the edge.
class DialSelector
{
public:
    static const uint16_t kHysteresisUs = 15;

    uint8_t update(uint16_t us, uint8_t positions)
    {
        uint8_t pos = dialPosition(us, positions);
        if (fHave && pos != fPos && fPos < positions)
        {
            // The current position's band, widened by the hysteresis.
            uint32_t lo = 1000 + uint32_t(fPos) * 1001 / positions;
            uint32_t hi = 1000 + uint32_t(fPos + 1) * 1001 / positions;
            if (uint32_t(us) + kHysteresisUs >= lo && uint32_t(us) < hi + kHysteresisUs)
                return fPos;
        }
        fHave = true;
        fPos = pos;
        return pos;
    }

private:
    bool fHave = false;
    uint8_t fPos = 0;
};

// Fires once each time a switch moves to on.
class EdgeTrigger
{
public:
    bool update(bool on)
    {
        bool fired = on && !fWasOn;
        fWasOn = on;
        return fired;
    }

private:
    // Starts "on" so a switch already down at power-up does not fire.
    bool fWasOn = true;
};

////////////////////////////////
// Volume dial: 1000-2000us to DFPlayer levels 0-30. Small jitter is ignored.

class VolumeDial
{
public:
    static const uint8_t kMaxLevel = 30;

    // Returns the new level, or -1 when the level should not change.
    int update(uint16_t us)
    {
        if (fLastUs != 0 && (us > fLastUs ? us - fLastUs : fLastUs - us) < kHysteresisUs)
            return -1;
        uint32_t v = us < 1000 ? 0 : us > 2000 ? 1000 : uint32_t(us - 1000);
        int level = int((v * kMaxLevel + 500) / 1000);
        fLastUs = us;
        if (level == fLevel)
            return -1;
        fLevel = level;
        return level;
    }

private:
    static const uint16_t kHysteresisUs = 20;
    uint16_t fLastUs = 0;
    int fLevel = -1;
};

////////////////////////////////
// Repeating random timer, used for lone remarks.

class RandomTimer
{
public:
    RandomTimer(uint32_t minMs, uint32_t maxMs) : fMin(minMs), fMax(maxMs) {}

    void setRange(uint32_t minMs, uint32_t maxMs)
    {
        fMin = minMs;
        fMax = maxMs > minMs ? maxMs : minMs + 1;
    }

    // True when the timer is due. Restarts the wait each time it fires and
    // whenever it is re-enabled.
    bool update(uint32_t now, bool enabled, RandomFn rnd)
    {
        if (!enabled)
        {
            fArmed = false;
            return false;
        }
        if (!fArmed)
        {
            restart(now, rnd);
            return false;
        }
        if (int32_t(now - fNext) < 0)
            return false;
        restart(now, rnd);
        return true;
    }

    // Keeps the wait but moves a deadline earlier than t to t, so a timer that
    // falls due while paused fires once the pause ends.
    void defer(uint32_t t)
    {
        if (fArmed && int32_t(fNext - t) < 0)
            fNext = t;
    }

    void restart(uint32_t now, RandomFn rnd)
    {
        fNext = now + rnd(fMin, fMax);
        fArmed = true;
    }

private:
    uint32_t fMin;
    uint32_t fMax;
    uint32_t fNext = 0;
    bool fArmed = false;
};

////////////////////////////////
// Mood sounds use the Marcduino sound banks: 1 general, 2 chat, 3 happy,
// 4 sad, 5 whistle. Mood 0 mixes all five banks, weighted by their sizes.

static const uint8_t kMoodCount = 6;

inline uint16_t pickMoodSound(uint8_t mood, const uint8_t clips[5], const uint8_t phrases[5], RandomFn rnd)
{
    if (mood >= 1 && mood <= 5)
    {
        uint8_t m = mood - 1;
        uint32_t pool = clips[m] + phrases[m];
        if (pool == 0)
            return 1;
        uint32_t n = rnd(0, pool);
        if (n < clips[m])
            return uint16_t(m * 25 + n + 1);
        return uint16_t(300 + m * 150 + (n - clips[m] + 1));
    }
    uint32_t total = 0;
    for (uint8_t i = 0; i < 5; i++)
        total += clips[i] + phrases[i];
    if (total == 0)
        return 1;
    uint32_t n = rnd(0, total);
    for (uint8_t i = 0; i < 5; i++)
    {
        uint32_t pool = clips[i] + phrases[i];
        if (n < pool)
        {
            if (n < clips[i])
                return uint16_t(i * 25 + n + 1);
            return uint16_t(300 + i * 150 + (n - clips[i] + 1));
        }
        n -= pool;
    }
    return 1;
}

} // namespace r2
