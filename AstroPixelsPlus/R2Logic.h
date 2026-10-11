#pragma once

// Hardware-free logic for the FlySky remote features: iBUS decoding, the
// dome state machine, dials, switches and random timers. Everything here is
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
// Repeating random timer used for chatter and holo twitches.

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

inline void pickMoodSound(uint8_t mood, const uint8_t bankSizes[5], RandomFn rnd,
                          uint8_t &bank, uint8_t &sound)
{
    if (mood >= 1 && mood <= 5)
    {
        bank = mood;
        sound = uint8_t(rnd(1, uint32_t(bankSizes[mood - 1]) + 1));
        return;
    }
    uint32_t total = 0;
    for (uint8_t i = 0; i < 5; i++)
        total += bankSizes[i];
    uint32_t n = rnd(0, total);
    for (bank = 1; bank < 5 && n >= bankSizes[bank - 1]; bank++)
        n -= bankSizes[bank - 1];
    sound = uint8_t(n + 1);
}

////////////////////////////////
// Dome: the stick always wins. With the stick centred and random dome on, the
// dome turns away from front, pauses, and turns back until the front Hall
// sensor sees the magnet. Without a known position it first seeks front.

struct DomeInputs
{
    uint32_t now;
    bool radio;        // iBUS frames arriving
    uint16_t stick;    // dome channel, us
    bool randomOn;     // random dome switch
    bool driving;      // either drive channel deflected
    bool atFront;      // front Hall sensor sees the magnet
};

class DomeBrain
{
public:
    enum State
    {
        kIdle,
        kManual,
        kSeeking,
        kAtFront,
        kTurningAway,
        kLooking,
        kReturning,
        kFault
    };

    struct Config
    {
        uint16_t deadbandUs = 80;
        uint16_t turnSpeedUs = 120;
        uint16_t seekSpeedUs = 80;
        uint32_t idleBeforeRandomMs = 3000;
        uint32_t pauseMinMs = 4000;
        uint32_t pauseMaxMs = 12000;
        uint32_t turnMinMs = 400;
        uint32_t turnMaxMs = 1500;
        uint32_t lookMinMs = 1000;
        uint32_t lookMaxMs = 4000;
        uint32_t seekTimeoutMs = 20000;
        uint32_t returnExtraMs = 5000;
    };

    explicit DomeBrain(RandomFn rnd) : fRnd(rnd) {}
    DomeBrain(RandomFn rnd, const Config &config) : fCfg(config), fRnd(rnd) {}

    // Returns the servo pulse in us, or 0 for "send no pulses".
    uint16_t update(const DomeInputs &in)
    {
        if (!in.radio)
        {
            // Keep sending the last neutral: some servos hold their last
            // command when pulses stop. Before any radio, send nothing.
            fState = kIdle;
            return fNeutralUs;
        }
        if (deflected(in.stick, fCfg.deadbandUs))
        {
            fState = kManual;
            fLastActivity = in.now;
            return in.stick;
        }
        // Stick centred: its value is the servo's neutral, including any
        // subtrim set on the transmitter.
        fNeutralUs = in.stick;
        if (in.driving)
            fLastActivity = in.now;
        if (fState == kManual)
            fState = kIdle;

        if (in.atFront && (fState == kSeeking || fState == kReturning))
            enterAtFront(in.now);

        bool quiet = int32_t(in.now - fLastActivity) >= int32_t(fCfg.idleBeforeRandomMs);
        switch (fState)
        {
            case kIdle:
                if (in.randomOn && quiet && !in.driving)
                {
                    if (in.atFront)
                    {
                        enterAtFront(in.now);
                    }
                    else
                    {
                        fState = kSeeking;
                        fDeadline = in.now + fCfg.seekTimeoutMs;
                        return uint16_t(fNeutralUs + fCfg.seekSpeedUs);
                    }
                }
                return fNeutralUs;

            case kSeeking:
                if (!in.randomOn)
                {
                    fState = kIdle;
                    return fNeutralUs;
                }
                if (expired(in.now, fDeadline))
                {
                    fState = kFault;
                    return fNeutralUs;
                }
                return uint16_t(fNeutralUs + fCfg.seekSpeedUs);

            case kAtFront:
                if (!in.randomOn)
                {
                    fState = kIdle;
                    return fNeutralUs;
                }
                if (quiet && !in.driving && expired(in.now, fUntil))
                {
                    fDirection = fRnd(0, 2) ? 1 : -1;
                    uint32_t turnMs = fRnd(fCfg.turnMinMs, fCfg.turnMaxMs);
                    fUntil = in.now + turnMs;
                    fReturnBudget = turnMs * 3 + fCfg.returnExtraMs;
                    fState = kTurningAway;
                    return speed(fCfg.turnSpeedUs);
                }
                return fNeutralUs;

            case kTurningAway:
                if (!in.randomOn || in.driving)
                    return startReturning(in.now);
                if (expired(in.now, fUntil))
                {
                    fState = kLooking;
                    fUntil = in.now + fRnd(fCfg.lookMinMs, fCfg.lookMaxMs);
                    return fNeutralUs;
                }
                return speed(fCfg.turnSpeedUs);

            case kLooking:
                if (!in.randomOn || in.driving || expired(in.now, fUntil))
                    return startReturning(in.now);
                return fNeutralUs;

            case kReturning:
                if (expired(in.now, fDeadline))
                {
                    fState = kFault;
                    return fNeutralUs;
                }
                return speed(-int16_t(fCfg.seekSpeedUs));

            case kFault:
                // Stays stopped until random dome is switched off.
                if (!in.randomOn)
                    fState = kIdle;
                return fNeutralUs;

            case kManual:
                break;
        }
        return fNeutralUs;
    }

    State state() const { return fState; }
    uint16_t neutral() const { return fNeutralUs; }

private:
    Config fCfg;
    RandomFn fRnd;
    State fState = kIdle;
    uint16_t fNeutralUs = 0;
    uint32_t fLastActivity = 0;
    uint32_t fUntil = 0;
    uint32_t fDeadline = 0;
    uint32_t fReturnBudget = 0;
    int8_t fDirection = 1;

    static bool expired(uint32_t now, uint32_t when)
    {
        return int32_t(now - when) >= 0;
    }

    // Offset from neutral in the current turn direction.
    uint16_t speed(int16_t offsetUs) const
    {
        return uint16_t(int32_t(fNeutralUs) + int32_t(fDirection) * offsetUs);
    }

    void enterAtFront(uint32_t now)
    {
        fState = kAtFront;
        fUntil = now + fRnd(fCfg.pauseMinMs, fCfg.pauseMaxMs);
    }

    uint16_t startReturning(uint32_t now)
    {
        fState = kReturning;
        fDeadline = now + fReturnBudget;
        return speed(-int16_t(fCfg.seekSpeedUs));
    }
};

} // namespace r2
