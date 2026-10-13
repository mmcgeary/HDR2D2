"""Tests for AstroPixelsPlus/R2DomeMotion.h."""

import unittest

from r2_cpp import run_cpp

HELPERS = r"""
// An ideal dome that moves exactly at the tracker's model speed.
static uint16_t tick(DomeMotion& m, const DomeTracker& model, uint32_t& nowMs, float& angle,
                     const DomeRequest& req, uint16_t stick = 1500, bool radio = true, bool known = true)
{
    nowMs += 20;
    uint16_t pulse = m.update(nowMs, radio, stick, req, known, angle, model);
    angle += float(m.direction()) * model.speedDps(m.direction(), m.offsetUs()) * 0.02f;
    return pulse;
}

// Turns the simulated dome until the tracker knows where it is and has learned
// speeds both ways at 60 and 220 us.
static void learnDome(DomeTracker& t, r2sim::SimDome& d, uint32_t& nowUs)
{
    const uint16_t offs[] = {60, 220};
    for (int dir = 1; dir >= -1; dir -= 2)
        for (uint16_t o : offs)
            for (int i = 0; i < 2000; i++) d.step(t, nowUs, 20000, int8_t(dir), o);
}

// One loop pass on the simulated dome.
static void pass(DomeMotion& m, DomeTracker& t, r2sim::SimDome& d, uint32_t& nowMs, uint32_t& nowUs, const DomeRequest& req)
{
    nowMs += 20;
    m.update(nowMs, true, 1500, req, t.known(), t.angle(), t);
    d.step(t, nowUs, 20000, m.direction(), m.offsetUs());
}

// Runs a request until it arrives, then holds for a second.
static void moveAndSettle(DomeMotion& m, DomeTracker& t, r2sim::SimDome& d, uint32_t& nowMs, uint32_t& nowUs, const DomeRequest& req)
{
    for (int i = 0; i < 3000 && !m.arrived(); i++) pass(m, t, d, nowMs, nowUs, req);
    for (int i = 0; i < 50; i++) pass(m, t, d, nowMs, nowUs, domeHold());
}
"""


def run(body):
    run_cpp(["R2Logic.h", "R2DomeTracker.h", "R2DomeMotion.h", "sim_dome.h"], body, HELPERS)


class BasicsTest(unittest.TestCase):
    def test_no_pulses_before_radio_then_neutral(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            CHECK(tick(m, model, now, a, domeHold(), 1500, false) == 0);
            CHECK(tick(m, model, now, a, domeHold(), 1500, true) == 1500);
            for (int i = 0; i < 5; i++) tick(m, model, now, a, domeHold(), 1530, true);
            CHECK(tick(m, model, now, a, domeHold(), 1530, true) == 1530);   // subtrim sets neutral (eased in)
            CHECK(m.neutral() == 1530);
            CHECK(tick(m, model, now, a, domeHold(), 1530, false) == 1530);  // radio lost: last neutral
        """)

    def test_stick_passes_through_and_releases_smoothly(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            CHECK(tick(m, model, now, a, domeHold(), 1800) == 1800);
            CHECK(m.manual());
            uint16_t prev = 1800;
            for (int i = 0; i < 40; i++) { uint16_t p = tick(m, model, now, a, domeHold()); CHECK(prev - p <= 10); prev = p; }
            CHECK(prev == 1500);
        """)

    def test_clamp_and_ramp_helpers(self):
        run(r"""
            CHECK_NEAR(clampDeg(170), 135, 0.001);
            CHECK_NEAR(clampDeg(-170), -135, 0.001);
            CHECK_NEAR(clampDeg(40), 40, 0.001);
            CHECK_NEAR(sineRamp(0), 0, 0.001);
            CHECK_NEAR(sineRamp(1), 1, 0.001);
            CHECK_NEAR(sineRamp(0.5f), 0.5, 0.001);
            CHECK_NEAR(rampDistance(1), 0.5, 0.001);
            CHECK_NEAR(rampDistance(0), 0, 0.001);
        """)


class GoToTest(unittest.TestCase):
    def test_eased_move_arrives(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            DomeRequest req = domeGoTo(90, 45, 0.6f);
            uint16_t prev = 1500, peak = 0; bool falling = false; int ticks = 0;
            while (!m.arrived() && ticks < 2000) {
                uint16_t p = tick(m, model, now, a, req); ticks++;
                CHECK(std::abs(int(p) - int(prev)) <= 10);
                if (p > peak) { CHECK(!falling); peak = p; } else if (p < peak) falling = true;
                prev = p;
            }
            CHECK(m.arrived());
            CHECK_NEAR(a, 90, 4);
            CHECK(peak >= 1500 + model.offsetForSpeed(1, 40));
            for (int i = 0; i < 30; i++) prev = tick(m, model, now, a, req);
            CHECK(prev == 1500);
        """)

    def test_step_limit_holds_at_the_real_loop_rate(self):
        # The ESP32 loop runs every 1-3 ms, not every 20: the pulse still moves
        # at most 10 us per 20 ms, and the move still gets there.
        run(r"""
            for (uint32_t stepMs = 1; stepMs <= 3; stepMs++)
            {
                DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
                tick(m, model, now, a, domeHold());
                uint16_t prev = 1500; uint16_t windowStart = 1500; uint32_t windowMs = now; int worst = 0;
                DomeRequest req = domeGoTo(90, 75, 0.35f);
                for (int i = 0; i < 60000 && !m.arrived(); i++)
                {
                    now += stepMs;
                    uint16_t p = m.update(now, true, 1500, req, true, a, model);
                    a += float(m.direction()) * model.speedDps(m.direction(), m.offsetUs()) * 0.001f * float(stepMs);
                    CHECK(std::abs(int(p) - int(prev)) <= 1 + int(stepMs));
                    if (now - windowMs >= 20)
                    {
                        int j = std::abs(int(p) - int(windowStart));
                        if (j > worst) worst = j;
                        windowStart = p; windowMs = now;
                    }
                    prev = p;
                }
                CHECK(m.arrived());
                CHECK_NEAR(a, 90, 4);
                CHECK(worst <= 11);   // 10 us per 20 ms; a window can span 21 ms at 3 ms passes
                CHECK(worst >= 9);    // and the limit is actually used
            }
        """)

    def test_target_clamped_to_135(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            for (int i = 0; i < 3000 && !m.arrived(); i++) tick(m, model, now, a, domeGoTo(170, 60, 0.5f));
            CHECK(m.arrived());
            CHECK_NEAR(a, 135, 4);
        """)

    def test_crosses_front_not_the_back(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 100;
            tick(m, model, now, a, domeHold());
            float last = a;
            for (int i = 0; i < 3000 && !m.arrived(); i++) { tick(m, model, now, a, domeGoTo(-100, 60, 0.5f)); CHECK(a <= last + 0.001f); last = a; }
            CHECK(m.arrived());
            CHECK_NEAR(a, -100, 4);
        """)

    def test_unknown_position_holds(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            for (int i = 0; i < 50; i++) CHECK(tick(m, model, now, a, domeGoTo(60, 45, 0.6f), 1500, true, false) == 1500);
            CHECK(!m.arrived());
        """)

    def test_slow_finish(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 60;
            tick(m, model, now, a, domeHold());
            DomeRequest req = domeGoTo(0, 45, 0.6f, true);
            for (int i = 0; i < 3000 && !m.arrived(); i++) {
                tick(m, model, now, a, req);
                if (a < 12.0f && m.direction() != 0) CHECK(m.offsetUs() <= 40);
            }
            CHECK(m.arrived());
            CHECK_NEAR(a, 0, 4);
        """)

    def test_radio_loss_stops_at_once(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            for (int i = 0; i < 40; i++) tick(m, model, now, a, domeGoTo(120, 60, 0.5f));
            CHECK(m.offsetUs() > 40);
            CHECK(tick(m, model, now, a, domeGoTo(120, 60, 0.5f), 1500, false) == 1500);
        """)

    def test_seek_turns_clockwise_until_known(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            uint16_t p = 0;
            for (int i = 0; i < 60; i++) p = tick(m, model, now, a, domeSeek(), 1500, true, false);
            CHECK(p == 1500 + model.offsetForSpeed(1, 20));
            CHECK(m.direction() == 1);
            for (int i = 0; i < 60; i++) p = tick(m, model, now, a, domeSeek(), 1500, true, true);
            CHECK(p == 1500);
        """)


class HeldEstimateTest(unittest.TestCase):
    def test_speeds_up_again_after_a_held_estimate_clears(self):
        run(r"""
            DomeTracker tracker; DomeMotion m; r2sim::SimDome dome(-50, 0.35f, 15);
            uint32_t nowUs = 1000, nowMs = 0;
            while (!tracker.known()) { nowMs += 20; m.update(nowMs, true, 1500, domeSeek(), tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            DomeRequest req = domeGoTo(120, 45, 0.5f);
            bool sawHeld = false, cleared = false; uint16_t peakAfter = 0;
            for (int i = 0; i < 4000 && !m.arrived(); i++) {
                nowMs += 20; m.update(nowMs, true, 1500, req, tracker.known(), tracker.angle(), tracker);
                dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs());
                if (tracker.held()) sawHeld = true;
                else if (sawHeld) cleared = true;
                if (cleared && tracker.angle() < 90.0f && m.offsetUs() > peakAfter) peakAfter = m.offsetUs();
            }
            CHECK(sawHeld);
            CHECK(cleared);
            CHECK(peakAfter > tracker.offsetForSpeed(1, 30));               // back up toward cruise, not creeping
        """)

    def test_profile_stays_monotonic_when_the_estimate_runs_ahead(self):
        # Spec §11: the velocity profile is monotonic up and down. Only the seek
        # speed is learned, so the estimate runs about 20% fast at cruise and is
        # held short of each magnet. The cruise must carry on (and get learned).
        run(r"""
            DomeTracker tracker; DomeMotion m; r2sim::SimDome dome(-100, 0.35f, 15);
            uint32_t nowUs = 1000, nowMs = 0;
            while (!tracker.known() || dome.angle < 10.0f) dome.step(tracker, nowUs, 20000, 1, 40);
            for (int i = 0; i < 10; i++) { nowMs += 20; m.update(nowMs, true, 1500, domeHold(), tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            DomeRequest req = domeGoTo(130, 45, 0.6f);
            uint16_t peak = 0, prev = 0; bool down = false; int rises = 0; bool sawHeld = false;
            for (int i = 0; i < 4000 && !m.arrived(); i++) {
                nowMs += 20; m.update(nowMs, true, 1500, req, tracker.known(), tracker.angle(), tracker);
                dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs());
                uint16_t o = m.offsetUs();
                if (tracker.held()) sawHeld = true;
                if (o > peak) peak = o;
                if (o + 10 < peak) down = true;
                if (down && o > prev + 4) rises++;   // a learned speed shifts the offset by a µs or two
                prev = o;
            }
            CHECK(m.arrived());
            CHECK(sawHeld);
            CHECK(rises == 0);
            CHECK_NEAR(tracker.speedDps(1, peak) / dome.speed(peak), 1.0, 0.05);
        """)

    def test_fast_moves_stop_on_target(self):
        # Happy's cruise and ease, and a wiggle step, need bigger steps than the
        # 10 us per 20 ms limit to follow their ramp: the ease stretches to fit.
        run(r"""
            DomeTracker t; DomeMotion m; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t nowUs = 1000, nowMs = 0;
            learnDome(t, dome, nowUs);
            CHECK(t.known());
            moveAndSettle(m, t, dome, nowMs, nowUs, domeGoTo(-30, 45, 0.6f));
            moveAndSettle(m, t, dome, nowMs, nowUs, domeGoTo(100, 75, 0.35f));
            CHECK_NEAR(dome.angle, 100, 3.0);
            moveAndSettle(m, t, dome, nowMs, nowUs, domeGoTo(110, 80, 0.2f));
            CHECK_NEAR(dome.angle, 110, 3.0);
            moveAndSettle(m, t, dome, nowMs, nowUs, domeGoTo(70, 67.5f, 0.25f));   // a double-take at 1.5 x General
            CHECK_NEAR(dome.angle, 70, 3.0);
        """)

    def test_no_arrival_at_speed(self):
        # The target changes to just ahead of a dome turning at 75 deg/s: it must
        # brake and come back, not call it arrived and coast on.
        run(r"""
            DomeTracker t; DomeMotion m; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t nowUs = 1000, nowMs = 0;
            learnDome(t, dome, nowUs);
            moveAndSettle(m, t, dome, nowMs, nowUs, domeGoTo(-60, 45, 0.6f));
            DomeRequest far = domeGoTo(120, 75, 0.35f);
            while (t.angle() < 30.0f) pass(m, t, dome, nowMs, nowUs, far);
            float near = t.angle() + 2.0f;
            DomeRequest req = domeGoTo(near, 75, 0.35f);
            pass(m, t, dome, nowMs, nowUs, req);
            pass(m, t, dome, nowMs, nowUs, req);
            CHECK(!m.arrived());
            moveAndSettle(m, t, dome, nowMs, nowUs, req);
            CHECK_NEAR(dome.angle, near, 3.0);
        """)

    def test_return_to_front_ends_on_the_front_magnet(self):
        run(r"""
            DomeTracker tracker; DomeMotion m; r2sim::SimDome dome(-50, 0.35f, 15);
            uint32_t nowUs = 1000, nowMs = 0;
            while (!tracker.known()) { nowMs += 20; m.update(nowMs, true, 1500, domeSeek(), tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            DomeRequest req = domeGoTo(-45, 45, 0.6f); int ticks = 0;
            do { nowMs += 20; m.update(nowMs, true, 1500, req, tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            while (!m.arrived() && ticks++ < 4000);
            req = domeGoTo(0, 45, 0.6f, true); ticks = 0;
            do { nowMs += 20; m.update(nowMs, true, 1500, req, tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            while (!m.arrived() && ticks++ < 4000);
            CHECK(m.arrived());
            for (int i = 0; i < 10; i++) { nowMs += 20; m.update(nowMs, true, 1500, req, tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            CHECK(std::fabs(dome.angle) < 3.0f);
            CHECK(tracker.known());
        """)

    def test_no_arrival_while_estimate_is_held(self):
        run(r"""
            // Real dome far slower than the default model: the estimate runs ahead
            // and parks short of the next magnet. The move must end on the real target.
            DomeTracker tracker; DomeMotion m; r2sim::SimDome dome(-50, 0.35f, 15);
            uint32_t nowUs = 1000, nowMs = 0;
            while (!tracker.known()) { nowMs += 20; uint16_t p = m.update(nowMs, true, 1500, domeSeek(), tracker.known(), tracker.angle(), tracker); (void)p; dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            int ticks = 0;
            DomeRequest req = domeGoTo(-43, 20, 1.0f);
            while (!m.arrived() && ticks++ < 4000) { nowMs += 20; m.update(nowMs, true, 1500, req, tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            CHECK(m.arrived());
            CHECK(std::fabs(dome.angle - (-43.0f)) < 6.0f);
            req = domeGoTo(2, 20, 1.0f); ticks = 0;
            do { nowMs += 20; m.update(nowMs, true, 1500, req, tracker.known(), tracker.angle(), tracker); dome.step(tracker, nowUs, 20000, m.direction(), m.offsetUs()); }
            while (!m.arrived() && ticks++ < 4000);
            CHECK(m.arrived());
            CHECK(std::fabs(dome.angle - 2.0f) < 6.0f);
        """)


if __name__ == "__main__":
    unittest.main()
