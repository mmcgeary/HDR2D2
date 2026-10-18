#pragma once

// The attention planner (spec §8): when R2 looks around, where, what it says,
// and how SwB gestures play out. Each update returns a dome request, a cue for
// the front eye and a sound request. Work is queued as small steps (wait,
// sound, eye cue, dome move, seek); interruptions clear the queue.

#include <stdint.h>
#include <math.h>
#include "R2Logic.h"
#include "R2Mood.h"
#include "R2Eyes.h"
#include "R2DomeMotion.h"

namespace r2 {

enum Level : uint8_t
{
    kStill = 0,
    kAware = 1,
    kAlive = 2
};

struct AttentionInputs
{
    uint32_t nowMs;
    bool radio;        // receiver heard (false = behave as Still)
    uint8_t level;     // Level
    uint8_t mood;      // active mood 1-5
    bool stick;        // left stick out of its deadband
    bool driving;
    bool swb;          // SwB flipped down since the last update
    bool domeKnown;
    float domeAngle;
    bool domeArrived;  // DomeMotion::arrived() from the last update
};

struct AttentionOutputs
{
    DomeRequest dome;
    EyeCue eyeCue;
    int8_t eyeSide;
    bool eyesActive;
    bool calm;
    int8_t soundBank;  // -1 = none
    bool soundForced;  // SwB: plays regardless of the gap between sounds
};

class Attention
{
public:
    explicit Attention(RandomFn rnd) : fRnd(rnd), fRemark(40000, 90000) {}

    AttentionOutputs update(const AttentionInputs& in)
    {
        const MoodProfile& mood = moodProfile(in.mood);
        AttentionOutputs out;
        out.dome = domeHold();
        out.eyeCue = kCueNone;
        out.eyeSide = 0;
        out.soundBank = -1;
        out.soundForced = false;
        uint8_t level = in.radio ? in.level : uint8_t(kStill);
        out.eyesActive = level != kStill;
        out.calm = false;

        if (!fStarted)
        {
            fStarted = true;
            fLevel = level;
            fLastSoundMs = in.nowMs - kSoundGapMs;
            scheduleLook(in.nowMs, mood);
        }
        if (in.swb)
        {
            out.soundBank = int8_t(mood.soundBank);
            out.soundForced = true;
            fLastSoundMs = in.nowMs;
        }
        if (level != fLevel)
        {
            if (fLevel == kAlive)
            {
                // A look in progress finishes facing front. A dome the operator
                // turned stays where it is.
                // Decided on the dome being known, not away: a dome passing front
                // on its way out would otherwise coast to a stop off front.
                bool midLook = hasDomeSteps();
                clear();
                if (midLook && in.domeKnown)
                    pushReturn(mood);
                else
                    returnEye(mood);
            }
            if (level == kStill)
            {
                fDomeDisabled = false;
                fSeekFailures = 0;
                fEyeLooking = false; // the eyes rest
            }
            if (fLevel == kStill)
                scheduleLook(in.nowMs, mood);
            fLevel = level;
        }
        if (level == kStill)
        {
            fRemark.update(in.nowMs, false, fRnd);
            runQueue(in, out, mood, level);
            finishUpdate(in, mood);
            return out;
        }
        if (in.stick)
        {
            if (hasDomeSteps())
                fReturnAfterStick = true;
            clear();
            if (fEyeLooking)
            {
                fEyeLooking = false;
                out.eyeCue = kCueReturn;
            }
            fResumeMs = in.nowMs + kResumeMs;
            fRemark.defer(fResumeMs);
            fPhase = "manual";
            return out;
        }
        if (int32_t(in.nowMs - fResumeMs) < 0)
        {
            fRemark.defer(fResumeMs);
            if (int32_t(fLookDueMs - fResumeMs) < 0)
                fLookDueMs = fResumeMs;
            return out;
        }
        if (fReturnAfterStick)
        {
            fReturnAfterStick = false;
            if (level == kAlive && domeAway(in))
                pushReturn(mood);
        }
        if (in.driving)
        {
            fCalmUntilMs = in.nowMs + kResumeMs;
            if (hasDomeSteps() && !onlyReturn())
            {
                clear();
                // Only Alive turns the dome; Aware brings just the eye back.
                if (level == kAlive && in.domeKnown)
                    pushReturn(mood);
                else
                    returnEye(mood);
            }
        }
        if (int32_t(fLookDueMs - fCalmUntilMs) < 0 && int32_t(in.nowMs - fCalmUntilMs) < 0)
            fLookDueMs = fCalmUntilMs;
        out.calm = int32_t(in.nowMs - fCalmUntilMs) < 0;
        if (in.swb)
        {
            clear();
            fEyeLooking = false; // the flick ends the look (LeadingEye)
            pushGesture(in, mood, level);
            fPhase = "gesture";
        }
        if (fCount == 0 && !out.calm && int32_t(in.nowMs - fLookDueMs) >= 0)
        {
            buildLook(in, mood, level);
            fPhase = "looking";
        }
        fRemark.setRange(uint32_t(mood.remarkIntervalS.lo * 1000.0f), uint32_t(mood.remarkIntervalS.hi * 1000.0f));
        if (fRemark.update(in.nowMs, true, fRnd))
            requestSound(in, out, mood);
        runQueue(in, out, mood, level);
        finishUpdate(in, mood);
        return out;
    }

    bool domeDisabled() const { return fDomeDisabled; }
    bool busy() const { return fCount > 0; }
    const char* phase() const { return fPhase; }

private:
    enum StepKind : uint8_t
    {
        kStepWait,
        kStepSound,
        kStepEye,
        kStepDome,
        kStepSeek
    };

    struct Step
    {
        StepKind kind;
        uint32_t ms;
        EyeCue cue;
        int8_t side;
        float target;
        float cruise;
        float ease;
        bool slowFinish;
        bool isReturn;
        bool started;
        uint32_t untilMs;
    };

    static const uint8_t kCapacity = 32;
    static const uint32_t kSoundGapMs = 4000;
    static const uint32_t kResumeMs = 3000;
    static const uint32_t kSeekTimeoutMs = 21600; // 1.2 turns at 20 deg/s

    RandomFn fRnd;
    RandomTimer fRemark;
    Step fQueue[kCapacity];
    uint8_t fHead = 0;
    uint8_t fCount = 0;
    bool fStarted = false;
    bool fWasBusy = false;
    uint8_t fLevel = kStill;
    uint32_t fLookDueMs = 0;
    uint32_t fLastSoundMs = 0;
    uint32_t fResumeMs = 0;
    uint32_t fCalmUntilMs = 0;
    bool fReturnAfterStick = false;
    bool fEyeLooking = false; // the front eye is out on a look
    uint8_t fSeekFailures = 0;
    bool fDomeDisabled = false;
    const char* fPhase = "rest";

    // ---- queue ----

    static Step makeStep(StepKind kind)
    {
        Step s;
        s.kind = kind;
        s.ms = 0;
        s.cue = kCueNone;
        s.side = 0;
        s.target = 0.0f;
        s.cruise = 0.0f;
        s.ease = 0.0f;
        s.slowFinish = false;
        s.isReturn = false;
        s.started = false;
        s.untilMs = 0;
        return s;
    }

    void push(const Step& s)
    {
        if (fCount < kCapacity)
            fQueue[(fHead + fCount++) % kCapacity] = s;
    }

    void pushWait(uint32_t ms)
    {
        Step s = makeStep(kStepWait);
        s.ms = ms;
        push(s);
    }

    void pushSound() { push(makeStep(kStepSound)); }

    void pushEye(EyeCue cue, int8_t side)
    {
        Step s = makeStep(kStepEye);
        s.cue = cue;
        s.side = side;
        push(s);
    }

    void pushDome(float target, float cruise, float ease, bool slowFinish = false, bool isReturn = false)
    {
        Step s = makeStep(kStepDome);
        s.target = clampDeg(target);
        s.cruise = cruise;
        s.ease = ease;
        s.slowFinish = slowFinish;
        s.isReturn = isReturn;
        push(s);
    }

    void pushSeek() { push(makeStep(kStepSeek)); }

    void pushReturn(const MoodProfile& mood, bool withDome = true)
    {
        pushEye(kCueReturn, 0);
        if (withDome)
            pushDome(0.0f, mood.domeCruiseDps, mood.domeEaseS, true, true);
        fPhase = "returning";
    }

    // After clearing a look that leaves the dome where it is: the eye still comes home.
    void returnEye(const MoodProfile& mood)
    {
        if (fEyeLooking)
            pushReturn(mood, false);
    }

    void clear()
    {
        fHead = 0;
        fCount = 0;
    }

    Step& front() { return fQueue[fHead]; }

    void pop()
    {
        fHead = uint8_t((fHead + 1) % kCapacity);
        fCount--;
    }

    bool hasDomeSteps() const
    {
        for (uint8_t i = 0; i < fCount; i++)
        {
            StepKind k = fQueue[(fHead + i) % kCapacity].kind;
            if (k == kStepDome || k == kStepSeek)
                return true;
        }
        return false;
    }

    bool onlyReturn() const
    {
        for (uint8_t i = 0; i < fCount; i++)
        {
            const Step& s = fQueue[(fHead + i) % kCapacity];
            if ((s.kind == kStepDome && !s.isReturn) || s.kind == kStepSeek)
                return false;
        }
        return true;
    }

    // ---- building looks and gestures ----

    static bool domeAway(const AttentionInputs& in) { return in.domeKnown && fabsf(in.domeAngle) > 3.0f; }

    int8_t randomSide() { return chance(50, fRnd) ? 1 : -1; }

    float pickTarget(const MoodProfile& mood) { return clampDeg(float(randomSide()) * pick(mood.lookAngleDeg, fRnd)); }

    void scheduleLook(uint32_t nowMs, const MoodProfile& mood)
    {
        fLookDueMs = nowMs + uint32_t(pick(mood.lookIntervalS, fRnd) * 1000.0f);
    }

    void buildLook(const AttentionInputs& in, const MoodProfile& mood, uint8_t level)
    {
        if (level == kAlive && !fDomeDisabled && !in.domeKnown)
            pushSeek();
        if (chance(mood.noticePct, fRnd))
        {
            pushSound();
            pushWait(500);
        }
        float target = pickTarget(mood);
        pushLookBody(target, 0.0f, mood);
        if (chance(mood.chainPct, fRnd))
            pushLookBody(pickTarget(mood), target, mood);
        pushReturn(mood, level == kAlive);
        fPhase = "looking";
    }

    void pushLookBody(float target, float previous, const MoodProfile& m)
    {
        static const Range kReadjust = {10.0f, 15.0f};
        pushEye(kCueLook, target >= 0.0f ? 1 : -1);
        pushWait(fRnd(400, 601));
        pushDome(target, m.domeCruiseDps, m.domeEaseS);
        uint32_t hold = uint32_t(pick(m.holdS, fRnd) * 1000.0f);
        pushWait(hold / 2);
        uint32_t roll = fRnd(0, 100);
        uint32_t edge = m.readjustPct;
        if (roll < edge)
        {
            pushDome(target + float(randomSide()) * pick(kReadjust, fRnd), m.domeCruiseDps, m.domeEaseS);
        }
        else if (roll < (edge += m.doubleTakePct))
        {
            pushDome(target - 0.4f * (target - previous), m.domeCruiseDps * 1.5f, 0.25f);
            pushWait(300);
            pushDome(target, m.domeCruiseDps * 1.5f, 0.25f);
        }
        else if (roll < (edge += m.glanceFrontPct))
        {
            pushDome(target - (target >= 0.0f ? 30.0f : -30.0f), m.domeCruiseDps, m.domeEaseS);
            pushWait(400);
            pushDome(target, m.domeCruiseDps, m.domeEaseS);
        }
        else if (roll < (edge += m.commentPct))
        {
            pushSound();
        }
        pushWait(hold - hold / 2);
    }

    void pushGlance(float a, float cruise)
    {
        pushDome(a + 20.0f * float(randomSide()), cruise, 0.3f);
        pushWait(300);
        pushDome(a, cruise, 0.3f);
    }

    void pushGesture(const AttentionInputs& in, const MoodProfile& mood, uint8_t level)
    {
        pushEye(kCueFlick, 0);
        if (level != kAlive || fDomeDisabled || !in.domeKnown)
            return;
        float a = in.domeAngle;
        float fast = mood.domeCruiseDps * 1.5f;
        switch (mood.gesture)
        {
            case kGlance:
                pushGlance(a, fast);
                break;
            case kHalfTurnFront:
                if (fabsf(a) < 20.0f)
                    pushGlance(a, fast);
                else
                {
                    pushDome(a / 2.0f, mood.domeCruiseDps, mood.domeEaseS);
                    pushWait(800);
                }
                break;
            case kWiggle:
                for (uint8_t i = 0; i < 2; i++)
                {
                    pushDome(a + 10.0f, 80.0f, 0.2f);
                    pushDome(a - 10.0f, 80.0f, 0.2f);
                }
                pushDome(a, 80.0f, 0.2f);
                break;
            case kSlowTurnAway:
            {
                int8_t side = a > 3.0f ? 1 : (a < -3.0f ? -1 : randomSide());
                pushDome(a + 45.0f * float(side), 20.0f, 1.0f);
                pushWait(1000);
                pushDome(a, 20.0f, 1.0f);
                break;
            }
            case kFaceFront:
                break;
        }
        pushDome(0.0f, mood.domeCruiseDps, mood.domeEaseS, true, true);
    }

    // ---- running ----

    void requestSound(const AttentionInputs& in, AttentionOutputs& out, const MoodProfile& mood)
    {
        if (out.soundBank >= 0 || int32_t(in.nowMs - fLastSoundMs) < int32_t(kSoundGapMs))
            return;
        out.soundBank = int8_t(mood.soundBank);
        fLastSoundMs = in.nowMs;
    }

    void runQueue(const AttentionInputs& in, AttentionOutputs& out, const MoodProfile& mood, uint8_t level)
    {
        while (fCount > 0)
        {
            Step& s = front();
            if (s.kind == kStepWait)
            {
                if (!s.started)
                {
                    s.started = true;
                    s.untilMs = in.nowMs + s.ms;
                }
                if (int32_t(in.nowMs - s.untilMs) < 0)
                    return;
                pop();
                continue;
            }
            if (s.kind == kStepSound)
            {
                if (level != kStill)
                    requestSound(in, out, mood);
                pop();
                continue;
            }
            if (s.kind == kStepEye)
            {
                if (s.cue == kCueLook)
                    fEyeLooking = true;
                else if (s.cue == kCueReturn)
                    fEyeLooking = false;
                out.eyeCue = s.cue;
                out.eyeSide = s.side;
                pop();
                continue;
            }
            bool allowed = !fDomeDisabled && (level == kAlive || s.isReturn);
            if (s.kind == kStepSeek)
            {
                if (!allowed || in.domeKnown)
                {
                    if (in.domeKnown && s.started)
                        fSeekFailures = 0;
                    pop();
                    continue;
                }
                if (!s.started)
                {
                    s.started = true;
                    s.untilMs = in.nowMs + kSeekTimeoutMs;
                }
                if (int32_t(in.nowMs - s.untilMs) >= 0)
                {
                    if (++fSeekFailures >= 2)
                        fDomeDisabled = true;
                    clear();
                    return;
                }
                out.dome = domeSeek();
                return;
            }
            if (!allowed || !in.domeKnown)
            {
                pop();
                continue;
            }
            out.dome = domeGoTo(s.target, s.cruise, s.ease, s.slowFinish);
            if (!s.started)
            {
                s.started = true;
                return;
            }
            if (in.domeArrived)
            {
                pop();
                continue;
            }
            return;
        }
    }

    void finishUpdate(const AttentionInputs& in, const MoodProfile& mood)
    {
        if (fCount > 0)
        {
            fWasBusy = true;
            return;
        }
        if (fWasBusy)
        {
            fWasBusy = false;
            scheduleLook(in.nowMs, mood);
        }
        fPhase = "rest";
    }
};

} // namespace r2
