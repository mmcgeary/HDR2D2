"""Tests for AstroPixelsPlus/R2Mood.h."""

import unittest

from r2_cpp import run_cpp


def run(body):
    run_cpp(["R2Mood.h"], body)


class MoodTableTest(unittest.TestCase):
    def test_rows_are_complete_and_ordered(self):
        run(r"""
            for (uint8_t m = 1; m <= 5; m++) {
                const MoodProfile& p = moodProfile(m);
                CHECK(p.soundBank == m);
                CHECK(p.lookIntervalS.lo > 0 && p.lookIntervalS.lo <= p.lookIntervalS.hi);
                CHECK(p.lookAngleDeg.lo > 0 && p.lookAngleDeg.lo <= p.lookAngleDeg.hi && p.lookAngleDeg.hi <= 135);
                CHECK(p.holdS.lo > 0 && p.holdS.lo <= p.holdS.hi);
                CHECK(p.remarkIntervalS.lo > 0 && p.remarkIntervalS.lo <= p.remarkIntervalS.hi);
                CHECK(p.holoHoldS.lo > 0 && p.holoHoldS.lo <= p.holoHoldS.hi);
                CHECK(p.domeCruiseDps > 0 && p.domeEaseS > 0);
                CHECK(p.holoRange > 0 && p.holoRange <= 1 && p.holoSpeed > 0);
                CHECK(p.readjustPct <= 100 && p.doubleTakePct <= 100 && p.chainPct <= 100);
                CHECK(p.glanceFrontPct <= 100 && p.noticePct <= 100 && p.commentPct <= 100);
            }
        """)

    def test_lookup_defaults_to_general(self):
        run(r"""
            CHECK(std::strcmp(moodProfile(kHappy).name, "happy") == 0);
            CHECK(std::strcmp(moodProfile(kMixed).name, "general") == 0);
            CHECK(std::strcmp(moodProfile(9).name, "general") == 0);
            CHECK(moodProfile(kSad).gesture == kSlowTurnAway);
            CHECK(moodProfile(kHappy).gesture == kWiggle);
        """)

    def test_pick_and_chance(self):
        run(r"""
            Range r = {2.0f, 6.0f};
            CHECK_NEAR(pick(r, lowest), 2.0, 0.001);
            CHECK_NEAR(pick(r, highest), 6.0, 0.001);
            Range flat = {3.0f, 3.0f};
            CHECK_NEAR(pick(flat, highest), 3.0, 0.001);
            CHECK(chance(1, lowest));
            CHECK(!chance(0, lowest));
            CHECK(!chance(99, highest));
            CHECK(chance(100, highest));
        """)


class MoodSelectorTest(unittest.TestCase):
    def test_fixed_moods_pass_through(self):
        run(r"""
            MoodSelector s;
            CHECK(s.update(kHappy, 0, lowest) == kHappy);
            CHECK(s.update(kSad, 10, lowest) == kSad);
            CHECK(s.update(7, 20, lowest) == kGeneral);
        """)

    def test_mixed_picks_and_repicks(self):
        run(r"""
            MoodSelector s;
            CHECK(s.update(kMixed, 1000, highest) == kWhistle);      // rnd(1,6) highest = 5
            CHECK(s.update(kMixed, 1000 + 179999, lowest) == kWhistle);  // next change at +180000
            CHECK(s.update(kMixed, 1000 + 180000, lowest) == kGeneral);  // rnd(1,6) lowest = 1
            CHECK(s.update(kHappy, 200000, lowest) == kHappy);
            CHECK(s.update(kMixed, 200020, highest) == kWhistle);    // back to mixed: picks at once
        """)


if __name__ == "__main__":
    unittest.main()
