#pragma once

// Hardware-free logic for the FlySky remote features: iBUS decoding, sticks,
// switches, dials, volume, random timers and mood sounds. Everything here is
// plain C++ so it can be unit-tested on a PC (see tests/test_r2_logic.py).
// R2Remote.h connects it to the ESP32 pins, sound player and servos.

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

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

////////////////////////////////
// Teensy 4.1 HCR Vocalizer commands and ASCII telemetry parsing

class HcrCommand
{
public:
    // Outgoing mood configuration command string
    static const char* moodSetup(uint8_t mood)
    {
        switch (mood)
        {
            case 1: // General: Override reset, Musing ON, min 12s, max 45s
                return "<OR,M1,MN12,MX45>";
            case 2: // Chat: Musing OFF, 30% Happy, 20% Conflict
                return "<M0,OR,OH30,OC20>";
            case 3: // Happy: Musing OFF, 80% Happy
                return "<M0,OR,OH80>";
            case 4: // Sad: Musing OFF, 85% Sad
                return "<M0,OR,OS85>";
            case 5: // Whistle: Musing OFF, 95% Happy
                return "<M0,OR,OH95>";
            default:
                return "<OR,M1,MN12,MX45>";
        }
    }

    // Outgoing procedural stimuli command string
    static const char* stimulus(uint8_t mood, bool variation = false)
    {
        switch (mood)
        {
            case 1: // General: Short happy or manual muse trigger
                return variation ? "<SH0>" : "<MM>";
            case 2: // Chat: Short happy or conversational blip
                return variation ? "<SH0>" : "<SC0>";
            case 3: // Happy: Happy phrase 1 or 0
                return variation ? "<SH1>" : "<SH0>";
            case 4: // Sad: Sad whimper 1 or 0
                return variation ? "<SS1>" : "<SS0>";
            case 5: // Whistle: Melodic whistle / call
                return variation ? "<CA0901C1020>" : "<SH1>";
            default:
                return variation ? "<SH0>" : "<MM>";
        }
    }

    // Telemetry queries
    static const char* queryVocalizing()
    {
        return "<QPV>";
    }

    static const char* queryDuration()
    {
        return "<QT>";
    }
};

// Stream parser for HCR ASCII telemetry responses (<QPV,0>, <QPV,1>, <QT,1.42>)
class HcrTelemetryParser
{
public:
    HcrTelemetryParser() : fVocalizing(false), fDuration(0.0f), fPos(0), fInPacket(false) {}

    // Feed a single character from the serial stream. Returns true if a valid packet was parsed.
    bool feed(char c)
    {
        if (c == '<')
        {
            fInPacket = true;
            fPos = 0;
            return false;
        }
        if (!fInPacket)
            return false;
        if (c == '>')
        {
            fInPacket = false;
            fBuf[fPos] = '\0';
            return parsePacket(fBuf);
        }
        if (fPos < sizeof(fBuf) - 1)
        {
            fBuf[fPos++] = c;
        }
        else
        {
            // Packet overflow, drop corrupted packet
            fInPacket = false;
            fPos = 0;
        }
        return false;
    }

    bool isVocalizing() const { return fVocalizing; }
    float currentDuration() const { return fDuration; }

    void reset()
    {
        fVocalizing = false;
        fDuration = 0.0f;
        fPos = 0;
        fInPacket = false;
    }

private:
    bool parsePacket(const char* buf)
    {
        if (strncmp(buf, "QPV,", 4) == 0)
        {
            const char* val = buf + 4;
            while (*val == ' ')
                val++;
            fVocalizing = (*val == '1');
            return true;
        }
        if (strncmp(buf, "QT,", 3) == 0)
        {
            const char* val = buf + 3;
            while (*val == ' ')
                val++;
            fDuration = strtof(val, nullptr);
            return true;
        }
        if (strcmp(buf, "QE") == 0 || strncmp(buf, "QE,", 3) == 0)
        {
            return true;
        }
        return false;
    }

    bool fVocalizing;
    float fDuration;
    char fBuf[32];
    uint8_t fPos;
    bool fInPacket;
};

} // namespace r2

