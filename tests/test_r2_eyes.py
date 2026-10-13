"""Tests for AstroPixelsPlus/R2Eyes.h."""

import unittest

from r2_cpp import run_cpp


def run(body):
    run_cpp(["R2Eyes.h"], body)


class PulseAndDurationTest(unittest.TestCase):
    def test_eye_servo_position_follows_reeltwo(self):
        run(r"""
            // ReelTwo HoloLights: 0 = right / up, 1 = left / down. Eyes: +h = right, +v = up.
            CHECK_NEAR(eyeServoPos(1, false), 0.0, 0.0001);
            CHECK_NEAR(eyeServoPos(-1, false), 1.0, 0.0001);
            CHECK_NEAR(eyeServoPos(0, false), 0.5, 0.0001);
            CHECK_NEAR(eyeServoPos(0.5f, false), 0.25, 0.0001);
            CHECK_NEAR(eyeServoPos(0.5f, true), 0.75, 0.0001);
            CHECK_NEAR(eyeServoPos(2, false), 0.0, 0.0001);
            CHECK_NEAR(eyeServoPos(-3, true), 0.0, 0.0001);
        """)

    def test_move_duration_grows_with_distance_and_mood_speed(self):
        run(r"""
            EyePos a = {0, 0}, b = {1, 0}, c = {-1, 0}, d = {1, 0.2f};
            CHECK(eyeMoveDuration(a, b, 1.0f) == 1050);
            CHECK(eyeMoveDuration(c, b, 1.0f) == 1500);
            CHECK(eyeMoveDuration(c, b, 2.0f) == 750);
            CHECK(eyeMoveDuration(a, d, 1.0f) == 1050);   // larger axis decides
            CHECK(eyeMoveDuration(a, a, 1.0f) == 600);
        """)


class ChameleonEyeTest(unittest.TestCase):
    def test_holds_first_then_moves_both_axes_together(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kGeneral);       // holo hold 3-8 s, range 0.5
            EyeMove first = e.update(0, true, m, 1.0f, 1.0f, highest);
            CHECK(!first.pending);                              // starts with a hold
            CHECK(!e.update(7999, true, m, 1.0f, 1.0f, highest).pending);
            EyeMove mv = e.update(8000, true, m, 1.0f, 1.0f, highest);
            CHECK(mv.pending);                                  // highest roll 99: new point
            CHECK_NEAR(mv.to.h, 0.5, 0.001);
            CHECK_NEAR(mv.to.v, 0.5, 0.001);
            CHECK(mv.durationMs == 825);
            CHECK(!e.update(8000 + 825 + 7999, true, m, 1.0f, 1.0f, highest).pending);
            CHECK(e.update(8000 + 825 + 8000, true, m, 1.0f, 1.0f, highest).pending);
        """)

    def test_micro_adjust_and_rest(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, 1.0f, 1.0f, lowest);
            EyeMove adj = e.update(3000, true, m, 1.0f, 1.0f, lowest);   // lowest roll 0: micro-adjust
            CHECK(adj.pending);
            CHECK_NEAR(adj.to.h, -0.1, 0.001);
            CHECK_NEAR(adj.to.v, -0.1, 0.001);
        """)

    def test_inactive_eases_to_centre_once(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kHappy);
            e.update(0, true, m, 1.0f, 1.0f, highest);
            CHECK(e.update(4000, true, m, 1.0f, 1.0f, highest).pending);  // moved away
            EyeMove home = e.update(4100, false, m, 1.0f, 1.0f, highest);
            CHECK(home.pending);
            CHECK_NEAR(home.to.h, 0, 0.001);
            CHECK_NEAR(home.to.v, 0, 0.001);
            CHECK(!e.update(4200, false, m, 1.0f, 1.0f, highest).pending);
            CHECK(!e.update(90000, false, m, 1.0f, 1.0f, highest).pending);
        """)

    def test_hold_scale_lengthens_holds(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, 1.0f, 1.5f, highest);                     // 8 s x 1.5
            CHECK(!e.update(11999, true, m, 1.0f, 1.5f, highest).pending);
            CHECK(e.update(12000, true, m, 1.0f, 1.5f, highest).pending);
        """)

    def test_eyes_wander_independently_and_stay_in_range(self):
        run(r"""
            ChameleonEye top, rear;
            const MoodProfile& m = moodProfile(kSad);
            int same = 0, moves = 0; double vSum = 0;
            for (uint32_t t = 0; t < 600000; t += 20) {
                EyeMove a = top.update(t, true, m, 1.0f, 1.0f, seeded);
                EyeMove b = rear.update(t, true, m, 1.0f, 1.0f, seeded);
                if (a.pending) { moves++; vSum += a.to.v; CHECK(a.to.h >= -1 && a.to.h <= 1 && a.to.v >= -1 && a.to.v <= 1); }
                if (a.pending && b.pending) same++;
            }
            CHECK(moves > 20);
            CHECK(same < moves / 2);          // not moving in lockstep
            CHECK(vSum / moves < -0.1);       // sad: biased downwards
        """)


class LeadingEyeTest(unittest.TestCase):
    def test_look_return_and_adjust(self):
        run(r"""
            LeadingEye e;
            const MoodProfile& m = moodProfile(kHappy);          // range 0.8
            e.update(0, true, m, kCueNone, 0, false, lowest);
            EyeMove look = e.update(100, true, m, kCueLook, 1, false, lowest);
            CHECK(look.pending);
            CHECK(e.looking());
            CHECK_NEAR(look.to.h, 0.48, 0.001);                  // 0.6 x 0.8 toward the right
            CHECK(!e.update(1599, true, m, kCueNone, 0, false, lowest).pending);
            CHECK(e.update(1600, true, m, kCueNone, 0, false, lowest).pending);   // adjust after 1.5 s
            EyeMove back = e.update(2000, true, m, kCueReturn, 0, false, lowest);
            CHECK(back.pending);
            CHECK(!e.looking());
            CHECK_NEAR(back.to.h, 0, 0.001);
            CHECK_NEAR(back.to.v, 0, 0.001);
            EyeMove left = e.update(2100, true, m, kCueLook, -1, false, lowest);
            CHECK_NEAR(left.to.h, -0.48, 0.001);
        """)

    def test_flick_and_resume(self):
        run(r"""
            LeadingEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, kCueNone, 0, false, lowest);
            e.update(10, true, m, kCueLook, 1, false, lowest);    // to (0.3, -0.2)
            EyeMove flick = e.update(20, true, m, kCueFlick, 0, false, lowest);
            CHECK(flick.pending);
            CHECK_NEAR(flick.to.h, 0, 0.001);
            CHECK_NEAR(flick.to.v, 0.2, 0.001);
            uint32_t back = 20 + flick.durationMs + 600;
            CHECK(!e.update(back - 1, true, m, kCueNone, 0, false, lowest).pending);
            EyeMove resume = e.update(back, true, m, kCueNone, 0, false, lowest);
            CHECK(resume.pending);
            CHECK_NEAR(resume.to.h, 0.3, 0.001);
            CHECK_NEAR(resume.to.v, -0.2, 0.001);
            CHECK(e.looking());
        """)

    def test_inactive_centres(self):
        run(r"""
            LeadingEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, kCueLook, 1, false, lowest);
            EyeMove rest = e.update(50, false, m, kCueNone, 0, false, lowest);
            CHECK(rest.pending);
            CHECK_NEAR(rest.to.h, 0, 0.001);
            CHECK(!e.looking());
        """)


class HoloLampFaderTest(unittest.TestCase):
    def test_initial_state_idle_and_dark(self):
        run(r"""
            HoloLampFader fader;
            for (uint8_t i = 0; i < 3; i++) {
                CHECK(fader.state(i) == HoloLampFader::kIdle);
                CHECK(fader.level(i) == 0);
                CHECK(!fader.owned(i));
            }
        """)

    def test_short_hold_rejected(self):
        run(r"""
            HoloLampFader fader;
            CHECK(!fader.triggerHold(kFrontEye, 1000, 600, 1499, 100, lowest));
            CHECK(fader.state(kFrontEye) == HoloLampFader::kIdle);
            CHECK(!fader.owned(kFrontEye));
        """)

    def test_chance_roll(self):
        run(r"""
            HoloLampFader fader;
            CHECK(!fader.triggerHold(kRearEye, 1000, 600, 2000, 0, lowest));
            CHECK(!fader.triggerHold(kRearEye, 1000, 600, 2000, 50, highest));
            CHECK(fader.triggerHold(kRearEye, 1000, 600, 2000, 100, highest));
            CHECK(fader.state(kRearEye) == HoloLampFader::kWaiting);
        """)

    def test_waits_for_the_move_then_fades_in_holds_and_fades_out_by_the_hold_end(self):
        run(r"""
            const uint8_t L = HoloLampFader::kLampLevel;
            HoloLampFader fader;
            // Eye move of 600 ms, then a 2000 ms hold: lit from 1600, dark by 3600.
            CHECK(fader.triggerHold(kFrontEye, 1000, 600, 2000, 100, lowest));
            fader.update(1000);
            CHECK(fader.owned(kFrontEye));                 // the lamp owns the holo from the trigger
            CHECK(fader.level(kFrontEye) == 0);
            fader.update(1599);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kWaiting);
            CHECK(fader.level(kFrontEye) == 0);

            fader.update(1600);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kFadeIn);
            CHECK(fader.level(kFrontEye) == 0);
            fader.update(1725);
            CHECK(fader.level(kFrontEye) == L / 2);
            fader.update(1850);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kHold);
            CHECK(fader.level(kFrontEye) == L);

            fader.update(3249);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kHold);
            fader.update(3250);                            // 3600 - fade-out time
            CHECK(fader.state(kFrontEye) == HoloLampFader::kFadeOut);
            CHECK(fader.level(kFrontEye) == L);
            fader.update(3425);
            CHECK(fader.level(kFrontEye) == L / 2);
            fader.update(3600);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kIdle);
            CHECK(fader.level(kFrontEye) == 0);
            CHECK(!fader.owned(kFrontEye));
        """)

    def test_release_fades_out_from_where_it_is(self):
        run(r"""
            const uint8_t L = HoloLampFader::kLampLevel;
            HoloLampFader fader;
            CHECK(fader.triggerHold(kFrontEye, 0, 0, 8000, 100, lowest));
            fader.update(0);
            fader.update(125);                             // half way in
            CHECK(fader.level(kFrontEye) == L / 2);
            fader.release(kFrontEye, 125);
            fader.update(125);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kFadeOut);
            CHECK(fader.level(kFrontEye) == L / 2);        // no jump up
            fader.update(300);
            CHECK(fader.level(kFrontEye) < L / 2);
            fader.update(475);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kIdle);
            CHECK(!fader.owned(kFrontEye));
        """)

    def test_release_before_lit_never_lights(self):
        run(r"""
            HoloLampFader fader;
            CHECK(fader.triggerHold(kTopEye, 0, 600, 3000, 100, lowest));
            fader.update(300);
            fader.release(kTopEye, 300);
            CHECK(!fader.owned(kTopEye));
            fader.update(1000);
            CHECK(fader.level(kTopEye) == 0);
            fader.release(kTopEye, 1000);                  // releasing an idle lamp does nothing
            CHECK(fader.state(kTopEye) == HoloLampFader::kIdle);
        """)

    def test_lamps_are_independent(self):
        run(r"""
            HoloLampFader fader;
            CHECK(fader.triggerHold(kFrontEye, 0, 0, 2000, 100, lowest));
            CHECK(fader.triggerHold(kRearEye, 100, 0, 1600, 100, lowest));
            CHECK(fader.triggerHold(kTopEye, 200, 400, 3000, 100, lowest));
            fader.update(0); fader.update(100); fader.update(300);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kHold);
            CHECK(fader.state(kRearEye) == HoloLampFader::kFadeIn);
            CHECK(fader.state(kTopEye) == HoloLampFader::kWaiting);
            fader.release(kRearEye, 300);
            fader.update(700);
            CHECK(fader.state(kRearEye) == HoloLampFader::kIdle);
            CHECK(fader.state(kFrontEye) == HoloLampFader::kHold);
            CHECK(fader.state(kTopEye) == HoloLampFader::kFadeIn);
        """)


if __name__ == "__main__":
    unittest.main()

