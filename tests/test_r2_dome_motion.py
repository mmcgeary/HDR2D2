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
"""


def run(body):
    run_cpp(["R2Logic.h", "R2DomeTracker.h", "R2DomeMotion.h"], body, HELPERS)


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


if __name__ == "__main__":
    unittest.main()
