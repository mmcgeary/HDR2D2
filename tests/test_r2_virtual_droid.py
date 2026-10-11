"""Thirty simulated minutes of Alive behaviour per mood: tracker, motion and
attention together on a simulated dome. Checks the spec's invariants."""

import unittest

from r2_cpp import run_cpp


class VirtualDroidTest(unittest.TestCase):
    def test_thirty_minutes_per_mood(self):
        run_cpp(["R2Logic.h", "R2Mood.h", "R2Eyes.h", "R2DomeTracker.h", "R2DomeMotion.h", "R2Attention.h",
                 "sim_dome.h"], r"""
            for (uint8_t mood = kGeneral; mood <= kWhistle; mood++)
            {
                DomeTracker tracker; DomeMotion motion; Attention attention(seeded);
                r2sim::SimDome dome(70, 0.35f, 15);
                uint32_t nowUs = 1000, nowMs = 0; uint16_t prevPulse = 0;
                int stepJumps = 0, trackErrors = 0, rangeErrors = 0, restErrors = 0, looks = 0;
                bool everKnown = false; const char* lastPhase = "";
                uint32_t restSince = 0;
                AttentionInputs in;
                in.radio = true; in.level = kAlive; in.mood = mood; in.stick = false;
                for (uint32_t i = 0; i < 90000; i++)
                {
                    nowMs += 20;
                    in.nowMs = nowMs;
                    in.swb = (i % 15000) == 7500;
                    in.driving = (i % 30000) >= 20000 && (i % 30000) < 20500;
                    in.domeKnown = tracker.known(); in.domeAngle = tracker.angle(); in.domeArrived = motion.arrived();
                    AttentionOutputs out = attention.update(in);
                    uint16_t pulse = motion.update(nowMs, true, 1500, out.dome, tracker.known(), tracker.angle(), tracker);
                    if (prevPulse != 0 && std::abs(int(pulse) - int(prevPulse)) > 10) stepJumps++;
                    prevPulse = pulse;
                    dome.step(tracker, nowUs, 20000, motion.direction(), motion.offsetUs());
                    if (tracker.known())
                    {
                        everKnown = true;
                        if (std::fabs(wrapDeg(tracker.angle() - dome.angle)) >= 31.0f) trackErrors++;
                        if (std::fabs(dome.angle) >= 150.0f) rangeErrors++;
                    }
                    if (std::strcmp(attention.phase(), "looking") == 0 && std::strcmp(lastPhase, "looking") != 0) looks++;
                    lastPhase = attention.phase();
                    if (!attention.busy() && motion.direction() == 0 && tracker.known())
                    {
                        if (restSince == 0) restSince = nowMs;
                        if (nowMs - restSince > 1000 && std::fabs(dome.angle) >= 12.0f) restErrors++;
                    }
                    else restSince = 0;
                }
                if (stepJumps || trackErrors || rangeErrors || restErrors || !everKnown || looks <= 10)
                    std::printf("mood %d: jumps %d track %d range %d rest %d known %d looks %d\n",
                                mood, stepJumps, trackErrors, rangeErrors, restErrors, everKnown, looks);
                CHECK(stepJumps == 0);
                CHECK(trackErrors == 0);
                CHECK(rangeErrors == 0);
                CHECK(restErrors == 0);
                CHECK(everKnown);
                CHECK(looks > 10);
            }
        """, optimize=True)


if __name__ == "__main__":
    unittest.main()
