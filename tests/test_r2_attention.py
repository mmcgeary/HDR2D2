"""Tests for AstroPixelsPlus/R2Attention.h.

Each test drives the planner every 20 ms with a fake dome that reaches any
target one update after it is requested, and records a token per event:
S<bank> sound (S<bank>! forced), EL+/EL- look cue, ER return cue, EF flick,
D<deg> new dome target, K seek.
"""

import unittest

from r2_cpp import run_cpp

HELPERS = r"""
struct FakeDome
{
    float angle;
    bool known;
    bool arrived;
    DomeRequestKind kind;
    float target;
    FakeDome() : angle(0), known(true), arrived(false), kind(kDomeHold), target(0) {}
    void apply(const DomeRequest& r)
    {
        bool fresh = r.kind != kind || r.targetDeg != target;
        kind = r.kind; target = r.targetDeg;
        if (r.kind == kDomeGoTo && known) { if (fresh) arrived = false; else { angle = clampDeg(r.targetDeg); arrived = true; } }
        else arrived = false;
    }
};

struct Rig
{
    Attention att;
    AttentionInputs in;
    FakeDome dome;
    std::string log;
    AttentionOutputs last;
    float lastTokenTarget;
    bool lastWasGoTo;
    explicit Rig(RandomFn rnd, uint8_t level, uint8_t mood) : att(rnd), lastTokenTarget(1e9f), lastWasGoTo(false)
    {
        in.nowMs = 0; in.radio = true; in.level = level; in.mood = mood; in.stick = false; in.driving = false;
        in.swb = false; in.domeKnown = true; in.domeAngle = 0; in.domeArrived = false;
    }
    void tick()
    {
        in.nowMs += 20;
        in.domeKnown = dome.known; in.domeAngle = dome.angle; in.domeArrived = dome.arrived;
        last = att.update(in);
        in.swb = false;
        char buf[32];
        if (last.soundBank >= 0) { std::snprintf(buf, sizeof(buf), "S%d%s ", last.soundBank, last.soundForced ? "!" : ""); log += buf; }
        if (last.eyeCue == kCueLook) log += last.eyeSide > 0 ? "EL+ " : "EL- ";
        if (last.eyeCue == kCueReturn) log += "ER ";
        if (last.eyeCue == kCueFlick) log += "EF ";
        if (last.dome.kind == kDomeGoTo && (!lastWasGoTo || last.dome.targetDeg != lastTokenTarget)) {
            std::snprintf(buf, sizeof(buf), "D%d ", int(std::lround(last.dome.targetDeg))); log += buf;
            lastTokenTarget = last.dome.targetDeg;
        }
        lastWasGoTo = last.dome.kind == kDomeGoTo;
        if (last.dome.kind == kDomeSeek && log.find("K ") == std::string::npos) log += "K ";
        dome.apply(last.dome);
    }
    void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 20) tick(); }
    bool has(const char* s) const { return log.find(s) != std::string::npos; }
};
"""


def run(body):
    run_cpp(["R2Logic.h", "R2Mood.h", "R2Eyes.h", "R2DomeTracker.h", "R2DomeMotion.h", "R2Attention.h"], body, HELPERS)


class LookTest(unittest.TestCase):
    def test_plain_look_with_no_extras(self):
        run(r"""
            Rig r(highest, kAlive, kGeneral);       // highest: no notice, no event, no chain; side -1
            r.run(34900);
            CHECK(r.log.empty());                    // first look after 35 s
            r.run(10000);
            CHECK(r.log == "EL- D-100 ER D0 ");
            CHECK(std::fabs(r.dome.angle) < 0.01f);
            CHECK(!r.att.busy());
            CHECK(std::strcmp(r.att.phase(), "rest") == 0);
        """)

    def test_full_look_sequence(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);         // lowest: every chance true, side +1, smallest values
            r.run(28000);                            // the next look starts at 29.5 s
            CHECK(r.log == "S1 EL+ D25 D35 EL+ D25 D35 ER D0 ");
        """)

    def test_aware_moves_no_dome(self):
        run(r"""
            Rig r(lowest, kAware, kGeneral);
            r.run(28000);
            CHECK(r.log == "S1 EL+ EL+ ER ");
            CHECK(r.last.eyesActive);
        """)

    def test_lone_remark(self):
        run(r"""
            Rig r(highest, kAware, kChat);          // remark every 50 s (highest), looks every 28 s
            r.run(51000);
            CHECK(r.has("S2 "));
        """)


class LevelTest(unittest.TestCase):
    def test_still_does_nothing_but_swb(self):
        run(r"""
            Rig r(lowest, kStill, kHappy);
            r.run(60000);
            CHECK(r.log.empty());
            CHECK(!r.last.eyesActive);
            r.in.swb = true; r.tick(); r.run(5000);
            CHECK(r.log == "S3! ");
        """)

    def test_radio_loss_behaves_as_still(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.in.radio = false;
            r.run(60000);
            CHECK(r.log.empty());
            CHECK(!r.last.eyesActive);
        """)

    def test_still_mid_look_returns_dome(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(13000);                            // look under way, dome at 25
            CHECK(r.has("D25 "));
            r.in.level = kStill; r.log.clear();
            r.run(5000);
            CHECK(r.log == "ER D0 ");
            CHECK(std::fabs(r.dome.angle) < 0.01f);
            r.log.clear(); r.run(60000);
            CHECK(r.log.empty());
        """)

    def test_still_mid_gesture_returns_and_silences(self):
        run(r"""
            Rig r(lowest, kAlive, kHappy);
            r.run(1000);
            r.in.swb = true; r.tick(); r.tick();     // wiggle starts
            CHECK(r.has("S3! EF D10 "));
            r.in.level = kStill; r.log.clear();
            r.run(60000);
            CHECK(r.log == "ER D0 ");
            CHECK(!r.last.eyesActive);
        """)

    def test_still_leaves_a_stick_turned_dome_alone(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(1000);                             // no look yet
            r.in.stick = true; r.dome.angle = 60;    // the operator turns the dome to someone
            r.run(500);
            r.in.stick = false;
            r.run(4000);
            r.in.level = kStill; r.log.clear();
            r.run(5000);
            CHECK(r.log.empty());
            CHECK(std::fabs(r.dome.angle - 60) < 0.01f);
        """)

    def test_aware_leaves_a_stick_turned_dome_alone(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(1000);
            r.in.stick = true; r.dome.angle = -40;
            r.run(500);
            r.in.stick = false;
            r.run(4000);
            r.in.level = kAware; r.log.clear();
            r.run(3000);
            CHECK(!r.has("D0 "));
            CHECK(std::fabs(r.dome.angle + 40) < 0.01f);
        """)


class InterruptionTest(unittest.TestCase):
    def test_stick_pauses_and_returns(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(13000);
            CHECK(r.has("D25 "));
            r.dome.angle = 25;
            r.in.stick = true; r.log.clear();
            r.run(500);
            CHECK(std::strcmp(r.att.phase(), "manual") == 0);
            r.in.stick = false;
            r.run(2900);
            CHECK(r.log.empty());                    // waits 3 s after release
            r.run(400);
            CHECK(r.log == "ER D0 ");
        """)

    def test_driving_returns_and_blocks_looks(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(13000);
            r.in.driving = true; r.log.clear();
            r.tick(); r.tick();
            CHECK(r.log == "ER D0 ");
            CHECK(r.last.calm);
            r.run(20000);                            // keep driving
            CHECK(r.log == "ER D0 ");
            r.in.driving = false;
            r.run(2900);
            CHECK(r.last.calm);
            r.run(200);
            CHECK(!r.last.calm);
        """)


class GestureTest(unittest.TestCase):
    def test_happy_wiggle(self):
        run(r"""
            Rig r(lowest, kAlive, kHappy);
            r.run(1000);
            r.in.swb = true; r.run(3000);
            CHECK(r.log.find("S3! EF D10 D-10 D10 D-10 D0 ") == 0);
        """)

    def test_aware_gesture_is_flick_only(self):
        run(r"""
            Rig r(lowest, kAware, kHappy);
            r.run(1000);
            r.in.swb = true; r.run(3000);
            CHECK(r.log == "S3! EF ");
        """)

    def test_sad_slow_turn_away(self):
        run(r"""
            Rig r(lowest, kAlive, kSad);
            r.run(1000);
            r.dome.angle = 30;
            r.in.swb = true; r.run(5000);
            CHECK(r.log.find("S4! EF D75 D30 D0 ") == 0);
        """)

    def test_sound_gap_skips_remark_after_swb(self):
        run(r"""
            Rig r(lowest, kAware, kChat);            // look at 10 s, remark due at 20 s
            r.run(19000);
            CHECK(r.log == "S2 EL+ EL+ ER ");
            r.in.swb = true; r.run(4000);            // SwB at 19 s: the 20 s remark falls inside the 4 s gap
            CHECK(r.log == "S2 EL+ EL+ ER S2! EF ");
        """)

    def test_mood_change_mid_look(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(12100);                            // notice played (S1), look under way
            CHECK(r.has("S1 "));
            r.in.mood = kHappy;
            r.run(20000);
            CHECK(r.has("D25 D35 "));                // the look keeps General's numbers
            r.log.clear();
            r.in.swb = true; r.tick();
            CHECK(r.log.find("S3!") == 0);           // new sounds use the new mood
        """)


class SeekTest(unittest.TestCase):
    def test_seek_before_look_when_lost(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.dome.known = false;
            r.run(13000);
            CHECK(r.has("K "));
            r.dome.known = true; r.dome.angle = 5;
            r.run(20000);
            CHECK(r.has("D25 "));
        """)

    def test_two_failed_seeks_disable_dome_until_still(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.dome.known = false;
            r.run(12000 + 21600 + 12000 + 21600 + 200);
            CHECK(r.att.domeDisabled());
            r.in.level = kStill; r.tick();
            CHECK(!r.att.domeDisabled());
        """)


if __name__ == "__main__":
    unittest.main()
