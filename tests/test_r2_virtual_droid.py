"""Thirty simulated minutes per level and mood (spec §11), with the operator
and radio events a droid meets: SwB every 5 minutes, 10 s of driving every 10,
a stick turn every 7, 3 s of radio loss every 11, and 5 s of Still at 20
minutes in Alive.

The simulated dome is shaped like the real one: the Hall sensor reads low
across a 1 degree arc around each magnet and blips once entering it, the dome's
speed lags the command by 40 ms, and the servo has a 15 us deadband. Blips
reach the tracker through an 8-entry ring and r2::drainBlips(), in the order
R2Remote's loop uses."""

import unittest

from r2_cpp import run_cpp

HELPERS = r"""
// A virtual droid: the R2Remote loop, pass for pass, on a simulated dome.
struct Droid
{
    DomeTracker tracker; DomeMotion motion; Attention attention; MoodSelector moods;
    r2sim::SimDome dome;
    uint32_t nowUs = 1000, nowMs = 0; uint8_t tail = 0;
    uint8_t level, dialMood;
    // results
    int passes = 0, stepJumps = 0, lost = 0, looks = 0, frontRests = 0, frontErrors = 0, rangeErrors = 0;
    int gapErrors = 0, quietErrors = 0, sounds = 0, autoDomeErrors = 0, stillMoves = 0, neutralErrors = 0;
    int trackLate = 0;
    float worstTrack = 0, worstTrackLate = 0, worstFront = 0, worstRange = 0;
    bool everKnown = false;

    Droid(uint8_t lvl, uint8_t mood) : attention(seeded), dome(70, 0.35f, 15), level(lvl), dialMood(mood)
    {
        dome.arcDeg = 1.0f;
        dome.lagS = 0.04f;
    }

    void run(uint32_t total)
    {
        uint16_t prevPulse = 0; bool prevCounted = false; bool wasKnown = false;
        const char* lastPhase = ""; uint32_t lastSoundMs = 0; bool anySound = false;
        bool stickAway = false; uint32_t checkFrontAt = 0; bool returning = false;
        float stillAngle = dome.angle; bool stillRef = false;
        for (uint32_t i = 0; i < total; i++)
        {
            // Operator and radio events
            bool radio = !((i % 33000) >= 25000 && (i % 33000) < 25150);       // 3 s loss every 11 min
            uint16_t stick = 1500;
            if ((i % 21000) >= 14000 && (i % 21000) < 14075)                    // 1.5 s stick every 7 min
                stick = ((i / 21000) % 2) ? 1350 : 1650;
            AttentionInputs in;
            in.radio = radio;
            in.level = (level == kAlive && i >= 60000 && i < 60250) ? uint8_t(kStill) : level;  // 5 s of Still at 20 min
            in.stick = stick != 1500;
            in.driving = (i % 30000) >= 20000 && (i % 30000) < 20500;          // 10 s of driving every 10 min
            in.swb = (i % 15000) == 7500;                                       // SwB every 5 min

            // The R2Remote loop
            nowMs += 20;
            bool over = dome.sensorLow();
            tail = drainBlips(dome.ring, dome.head, tail, nowUs, tracker);
            tracker.update(nowUs);
            in.mood = moods.update(dialMood, nowMs, seeded);
            in.nowMs = nowMs;
            in.domeKnown = tracker.known(); in.domeAngle = tracker.angle(); in.domeArrived = motion.arrived();
            AttentionOutputs out = attention.update(in);
            uint16_t pulse = motion.update(nowMs, radio, stick, out.dome, tracker.known(), tracker.angle(), tracker);
            tracker.setSensor(over);
            tracker.setCommand(nowUs, motion.direction(), motion.offsetUs());
            dome.move(nowUs, 20000, motion.direction(), motion.offsetUs());
            nowUs += 20000;
            passes++;

            // Invariants
            bool counted = radio && !motion.manual();
            if (prevPulse != 0 && counted && prevCounted && std::abs(int(pulse) - int(prevPulse)) > 10) stepJumps++;
            if (!radio && pulse != motion.neutral()) neutralErrors++;
            prevPulse = pulse; prevCounted = counted;
            if (motion.manual()) stickAway = true;
            if (wasKnown && !tracker.known()) lost++;
            wasKnown = tracker.known();
            if (tracker.known())
            {
                everKnown = true;
                float e = std::fabs(wrapDeg(tracker.angle() - dome.angle));
                if (e > worstTrack) worstTrack = e;
                if (nowMs > 300000 && !stickAway) { if (e > worstTrackLate) worstTrackLate = e; if (e >= 5.0f) trackLate++; }
                if (!stickAway && std::fabs(dome.angle) > worstRange) worstRange = std::fabs(dome.angle);
                if (!stickAway && std::fabs(dome.angle) > 141.0f) rangeErrors++;
            }
            const char* phase = attention.phase();
            if (std::strcmp(phase, "looking") == 0 && std::strcmp(lastPhase, "looking") != 0) looks++;
            // Every look and gesture in Alive ends with a return to front.
            if (returning && !attention.busy()) checkFrontAt = nowMs + 1000;
            returning = attention.busy();
            if (attention.busy()) checkFrontAt = 0;          // a new look or gesture started first
            if (checkFrontAt && nowMs >= checkFrontAt)
            {
                checkFrontAt = 0;
                if (tracker.known() && level == kAlive && !stickAway)
                {
                    frontRests++;
                    float e = std::fabs(dome.angle);
                    if (e > worstFront) worstFront = e;
                    if (e > 3.0f) { frontErrors++; std::printf("  front miss t=%.1f real %.2f tracker %.2f\n", nowMs / 1000.0, dome.angle, tracker.angle()); }
                }
                if (tracker.known() && std::fabs(dome.angle) < 3.0f) stickAway = false;
            }
            lastPhase = phase;
            if (out.soundBank >= 0)
            {
                sounds++;
                if (!out.soundForced && anySound && nowMs - lastSoundMs < 4000) gapErrors++;
                if (!out.soundForced && (in.level == kStill || !radio)) quietErrors++;
                lastSoundMs = nowMs; anySound = true;
            }
            if (level != kAlive && out.dome.kind != kDomeHold) autoDomeErrors++;
            if (level == kStill)
            {
                if (motion.manual() || dome.vel != 0.0f) stillRef = false;
                else if (!stillRef) { stillRef = true; stillAngle = dome.angle; }
                else if (std::fabs(dome.angle - stillAngle) > 0.5f) stillMoves++;
            }
        }
    }

    void print(const char* name) const
    {
        std::printf("%-14s jumps %d lost %d track %.1f late %.1f (%d) front %d/%d worst %.1f range %.1f gap %d quiet %d sounds %d auto %d still %d neutral %d looks %d known %d\n",
                    name, stepJumps, lost, worstTrack, worstTrackLate, trackLate, frontErrors, frontRests, worstFront, worstRange,
                    gapErrors, quietErrors, sounds, autoDomeErrors, stillMoves, neutralErrors, looks, everKnown);
    }
};
"""

MOODS = r"""
    static const char* names[] = {"mixed", "general", "chat", "happy", "sad", "whistle"};
"""


def run_level(level, checks):
    run_cpp(["R2Logic.h", "R2Mood.h", "R2Eyes.h", "R2DomeTracker.h", "R2DomeMotion.h", "R2Attention.h",
             "sim_dome.h"], MOODS + r"""
        for (int mood = kMixed; mood <= kWhistle; mood++)
        {
            Droid d{static_cast<uint8_t>(""" + level + r"""), static_cast<uint8_t>(mood)};
            d.run(90000);
            int before = failures;
            CHECK(d.stepJumps == 0);
            CHECK(d.neutralErrors == 0);
            CHECK(d.gapErrors == 0);
            CHECK(d.quietErrors == 0);
            """ + checks + r"""
            if (failures != before) d.print(names[mood]);
        }
    """, HELPERS, optimize=True)


class VirtualDroidTest(unittest.TestCase):
    def test_alive(self):
        run_level("kAlive", r"""
            CHECK(d.everKnown);
            CHECK(d.lost == 0);
            CHECK(d.worstTrack < 20.0f);       // learning: never a magnet out
            CHECK(d.trackLate == 0);           // after 5 minutes: within 5 degrees
            CHECK(d.frontRests > 10);
            CHECK(d.frontErrors == 0);         // every return rests within 3 degrees of front
            CHECK(d.rangeErrors == 0);
            CHECK(d.looks > 10);
        """)

    def test_aware(self):
        run_level("kAware", r"""
            CHECK(d.autoDomeErrors == 0);      // the dome moves only by stick
            CHECK(d.looks > 10);
            CHECK(d.sounds > 12);              // remarks and notices as well as SwB
        """)

    def test_still(self):
        run_level("kStill", r"""
            CHECK(d.autoDomeErrors == 0);
            CHECK(d.stillMoves == 0);
            CHECK(d.looks == 0);
            CHECK(d.sounds == 6);              // SwB only
        """)


if __name__ == "__main__":
    unittest.main()
