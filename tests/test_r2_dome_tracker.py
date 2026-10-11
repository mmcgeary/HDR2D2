"""Tests for AstroPixelsPlus/R2DomeTracker.h."""

import unittest

from r2_cpp import run_cpp


def run(body):
    run_cpp(["R2DomeTracker.h", "sim_dome.h"], body, optimize=True)


class GeometryTest(unittest.TestCase):
    def test_next_magnet(self):
        run(r"""
            CHECK(nextMagnet(0, 1) == 5);
            CHECK(nextMagnet(5, 1) == 30);
            CHECK(nextMagnet(5, -1) == 0);
            CHECK(nextMagnet(0, -1) == -30);
            CHECK(nextMagnet(150, 1) == 180);
            CHECK(nextMagnet(180, 1) == -150);
            CHECK(nextMagnet(-150, -1) == 180);
            CHECK_NEAR(wrapDeg(190), -170, 0.001);
            CHECK_NEAR(wrapDeg(-180), 180, 0.001);
        """)


class SimDomeTest(unittest.TestCase):
    def test_slice_ending_exactly_on_a_magnet_still_blips(self):
        run(r"""
            // At 8.75 deg/s from -29.90374, a 1 ms slice ends exactly on -30 after float rounding.
            DomeTracker t; r2sim::SimDome dome(-29.90374f, 0.35f, 15); uint32_t now = 1000;
            while (dome.angle > -30.5f) dome.step(t, now, 1000, -1, 40);
            CHECK(dome.blips == 1);
        """)

    def test_each_magnet_blips_once_per_pass(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 2000; i++) dome.step(t, now, 20000, 1, 100);   // 1190 degrees
            CHECK(dome.blips == 43 || dome.blips == 44);                       // 13 per turn, 3.3 turns
        """)


class SpeedModelTest(unittest.TestCase):
    def test_default_speed_and_inverse(self):
        run(r"""
            DomeTracker t;
            CHECK_NEAR(t.speedDps(1, 100), 50, 0.01);
            CHECK_NEAR(t.speedDps(-1, 100), 50, 0.01);
            CHECK(t.speedDps(1, 10) == 0);
            CHECK(t.speedDps(0, 100) == 0);
            CHECK(t.offsetForSpeed(1, 20) == 40);
            CHECK(t.offsetForSpeed(1, 15) == 30);
            CHECK(t.offsetForSpeed(1, 0) == 0);
            uint16_t prev = 0;
            for (int dps = 5; dps < 140; dps += 5) { uint16_t o = t.offsetForSpeed(1, float(dps)); CHECK(o >= prev); prev = o; }
        """)

    def test_import_sanitises_corrupt_speeds(self):
        run(r"""
            DomeTracker t;
            float table[2][kSpeedBands];
            for (int d = 0; d < 2; d++) for (int b = 0; b < kSpeedBands; b++) table[d][b] = NAN;
            table[0][3] = -5.0f;
            table[0][5] = 1e9f;
            table[0][8] = 30.0f;            // offsets 100-109
            t.importSpeeds(table);
            CHECK_NEAR(t.speedDps(1, 105), 30, 0.01);
            float far = t.speedDps(1, 200);
            CHECK(far == far);              // not NaN
            CHECK_NEAR(far, 30.0 * 200 / 105, 0.5);
            CHECK_NEAR(t.speedDps(1, 50), 30.0 * 50 / 105, 0.5);
            CHECK_NEAR(t.speedDps(-1, 100), 50, 0.01);   // anticlockwise row all invalid: default
        """)


class TrackingTest(unittest.TestCase):
    def test_unknown_until_front_pair_clockwise(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            CHECK(!t.known());
            bool sawKnown = false;
            while (dome.angle < 100.0f) {
                dome.step(t, now, 20000, 1, 100);
                if (t.known() && !sawKnown) { sawKnown = true; CHECK_NEAR(dome.angle, 5.0, 1.0); CHECK_NEAR(t.angle(), dome.angle, 2.0); }
                if (t.known()) CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f);
            }
            CHECK(sawKnown);
        """)

    def test_front_pair_anticlockwise(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(100, 0.35f, 15); uint32_t now = 1000;
            bool sawKnown = false;
            while (dome.angle > -100.0f) {
                dome.step(t, now, 20000, -1, 100);
                if (t.known() && !sawKnown) { sawKnown = true; CHECK_NEAR(dome.angle, 0.0, 1.0); CHECK_NEAR(t.angle(), dome.angle, 2.0); }
            }
            CHECK(sawKnown);
        """)

    def test_learns_speed_and_tracks_closely(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);   // about two turns
            CHECK(t.known());
            CHECK_NEAR(t.speedDps(1, 100), dome.speed(100), 1.5);
            CHECK(t.dirty());
            double worst = 0;
            for (int i = 0; i < 700; i++) { dome.step(t, now, 20000, 1, 100); double e = std::fabs(wrapDeg(t.angle() - dome.angle)); if (e > worst) worst = e; }
            CHECK(worst < 3.0);
            float table[2][kSpeedBands]; t.exportSpeeds(table);
            CHECK(table[0][8] > 0);
            t.markSaved();
            CHECK(!t.dirty());
        """)

    def test_missing_magnet_drops_position_then_refinds_front(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            dome.magnetPresent[4] = false;                                   // 90 degrees gone
            double worst = 0; bool lost = false;
            for (int i = 0; i < 2600; i++) {                                 // about 3 turns
                dome.step(t, now, 20000, 1, 100);
                if (!t.known()) lost = true;
                if (t.known()) { double e = std::fabs(wrapDeg(t.angle() - dome.angle)); if (e > worst) worst = e; }
            }
            CHECK(lost);                                                     // the miscount is caught at the pair
            CHECK(worst < 62.0);                                             // never worse than one miscount
            CHECK(t.known());                                                // found again at a later pair
        """)

    def test_stalled_dome_becomes_unknown(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            CHECK(t.known());
            dome.dpsPerUs = 0.0f;                                            // jammed
            for (int i = 0; i < 250; i++) dome.step(t, now, 20000, 1, 100);  // 5 s commanded
            CHECK(!t.known());
        """)

    def test_stalled_dome_unknown_even_before_learning(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 40);                                      // 20 deg/s by default, never learned
            t.onBlip(1000000); t.onBlip(2500000); t.onBlip(2750000);     // known at 5
            CHECK(t.known());
            t.update(2750000 + 7000000);                                 // 140 deg estimated, no magnet
            CHECK(t.known());
            t.update(2750000 + 8000000);                                 // 160 deg: beyond 6 x the 25 deg gap ahead
            CHECK(!t.known());
        """)

    def test_reversal_counts_back(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            while (dome.angle < 45.0f) dome.step(t, now, 20000, 1, 100);     // past the pair to 45
            CHECK(t.known());
            while (dome.angle > -40.0f) { dome.step(t, now, 20000, -1, 100); if (t.known()) CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f); }
            CHECK(t.known());
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f);
        """)

    def test_long_stop_does_not_fake_front(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            while (dome.angle < 45.0f) dome.step(t, now, 20000, 1, 100);
            for (int i = 0; i < 500; i++) dome.step(t, now, 20000, 0, 0);    // 10 s still
            float before = t.angle();
            while (dome.angle < 75.0f) dome.step(t, now, 20000, 1, 100);     // passes 60
            CHECK(t.known());
            CHECK(t.angle() > before);
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f);
        """)


    def test_reversal_inside_front_pair_keeps_position(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);     // learn, ends near -47
            while (!(dome.angle > 1.0f && dome.angle < 4.0f)) dome.step(t, now, 20000, 1, 40);  // creep to just past 0
            for (int i = 0; i < 25; i++) dome.step(t, now, 20000, 0, 0);       // rest inside the pair
            CHECK(t.known());
            while (dome.angle > -40.0f) { dome.step(t, now, 20000, -1, 100); CHECK(t.known()); }
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 3.0f);
        """)

    def test_reversal_just_past_the_pair_keeps_position(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            while (!(dome.angle > 5.2f && dome.angle < 6.0f)) dome.step(t, now, 20000, 1, 40);   // just past the pair
            CHECK(t.known());
            while (dome.angle > -40.0f) { dome.step(t, now, 20000, -1, 100); CHECK(t.known()); }
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 3.0f);
        """)

    def test_reversal_while_over_a_magnet(self):
        # A real sensor reads low across an arc around each magnet and blips once,
        # entering it. The dome stops inside the arc of the magnet it just
        # reported, then turns back: that magnet gives no new blip.
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            dome.arcDeg = 1.0f;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);      // learn, ends near -47
            while (!(dome.sensorLow() && dome.angle > 55 && dome.angle < 65)) dome.step(t, now, 20000, 1, 40);
            for (int i = 0; i < 25; i++) dome.step(t, now, 20000, 0, 0);         // rest on magnet 60
            CHECK(t.known());
            while (dome.angle > -40.0f) { dome.step(t, now, 20000, -1, 100); CHECK(t.known()); }
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 3.0f);
        """)

    def test_reversal_on_the_front_magnet(self):
        # The common case: a return to front stops just after the front blip.
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            dome.arcDeg = 1.0f;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);      // ends near -47
            while (!(dome.sensorLow() && dome.angle > -2 && dome.angle < 2)) dome.step(t, now, 20000, 1, 40);
            for (int i = 0; i < 25; i++) dome.step(t, now, 20000, 0, 0);
            CHECK_NEAR(t.angle(), 0.0, 0.5);
            while (dome.angle > -70.0f) { dome.step(t, now, 20000, -1, 100); CHECK(t.known()); }
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 3.0f);
        """)

    def test_stop_over_a_magnet_then_carry_on(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            dome.arcDeg = 1.0f;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            while (!(dome.sensorLow() && dome.angle > 55 && dome.angle < 65)) dome.step(t, now, 20000, 1, 40);
            for (int i = 0; i < 25; i++) dome.step(t, now, 20000, 0, 0);
            while (dome.angle < 140.0f) { dome.step(t, now, 20000, 1, 100); CHECK(t.known()); }
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 3.0f);
        """)

    def test_estimate_held_short_of_next_magnet(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);   // known at 5, next magnet 30
            CHECK(!t.held());
            t.update(2500000);                                           // 20 deg of 25
            CHECK(!t.held());
            t.update(3000000);                                           // 45 deg estimated: held at 29
            CHECK(t.held());
            CHECK_NEAR(t.angle(), 29.0, 0.01);
            CHECK(t.travelDir() == 1);
            t.onBlip(3100000);                                           // magnet 30 arrives
            CHECK(!t.held());
            CHECK_NEAR(t.angle(), 30.0, 0.01);
        """)


class BlipEdgeCaseTest(unittest.TestCase):
    def test_short_gap_where_no_pair_is_expected_marks_unknown(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 40);                                      // 20 deg/s by default
            t.onBlip(1000000); t.onBlip(2500000); t.onBlip(2750000);     // known at 5
            t.onBlip(4000000);                                           // 30
            t.onBlip(5500000);                                           // 60
            CHECK_NEAR(t.angle(), 60.0, 0.01);
            t.onBlip(5600000);                                           // short gap, but 90 is next
            CHECK(!t.known());
        """)

    def test_at_magnet(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);
            CHECK(!t.atMagnet(0, 3));                                    // unknown
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);     // known at 5
            CHECK(t.atMagnet(5, 3));
            CHECK(!t.atMagnet(0, 3));
            t.update(2140000);                                           // 2 deg on
            CHECK(t.atMagnet(5, 3));
            t.update(2200000);                                           // 5 deg on
            CHECK(!t.atMagnet(5, 3));
            CHECK(isMagnetAngle(0) && isMagnetAngle(5) && isMagnetAngle(-30) && isMagnetAngle(180));
            CHECK(!isMagnetAngle(25) && !isMagnetAngle(-31));
        """)

    def test_first_gap_after_front_is_learned(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);                                     // 50 deg/s by default
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);     // known at 5
            t.onBlip(3100000);                                           // 30: 25 deg in 1.0 s
            CHECK_NEAR(t.speedDps(1, 100), 25.0, 0.01);
        """)

    def test_bounce_is_dropped(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);                 // 50 deg/s by default
            t.onBlip(1000000);
            t.onBlip(1002000);                       // 2 ms later: bounce
            CHECK(!t.known());
            t.onBlip(2000000);
            t.onBlip(2100000);                       // 5 deg after a 50 deg gap: front pair
            CHECK(t.known());
            CHECK_NEAR(t.angle(), 5.0, 0.01);
        """)

    def test_blip_while_stopped_counts_last_direction(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);
            CHECK(t.known());
            t.setCommand(2500000, 0, 0);
            t.onBlip(3000000);                       // coasting onto the next magnet
            CHECK(t.known());
            CHECK_NEAR(t.angle(), 30.0, 0.01);
        """)

    def test_micros_wraparound(self):
        run(r"""
            DomeTracker t;
            uint32_t base = 0xFFFFFFFFu - 1500000u;
            t.setCommand(base, 1, 100);
            t.onBlip(base + 1000000u);
            t.onBlip(base + 2000000u);               // wraps past zero
            t.onBlip(base + 2100000u);
            CHECK(t.known());
            CHECK_NEAR(t.angle(), 5.0, 0.01);
            t.update(base + 2300000u);               // 0.2 s at 50 deg/s
            CHECK_NEAR(t.angle(), 15.0, 0.5);
        """)

    def test_blip_read_after_a_later_update(self):
        # The loop can read a blip after it has already updated the tracker past
        # the blip's time: the interrupt fired between the read and the update.
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            dome.lateBlips = true;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            CHECK(t.known());
            double worst = 0; bool lost = false;
            for (int i = 0; i < 1300; i++) {
                dome.step(t, now, 20000, 1, 100);
                if (!t.known()) lost = true;
                else { double e = std::fabs(wrapDeg(t.angle() - dome.angle)); if (e > worst) worst = e; }
            }
            CHECK(!lost);
            CHECK(worst < 3.0);
        """)

    def test_stalled_dome_that_frees_does_not_fake_front(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            dome.dpsPerUs = 0.0f;                                            // jammed
            for (int i = 0; i < 250; i++) dome.step(t, now, 20000, 1, 100);
            CHECK(!t.known());
            dome.dpsPerUs = 0.35f;                                           // free again
            while (!t.known()) dome.step(t, now, 20000, 1, 100);
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 3.0f);        // found at the real front pair
        """)

    def test_drain_feeds_blips_in_order_up_to_now(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);                                     // 50 deg/s by default
            volatile uint32_t ring[8] = {1000000, 2000000, 2100000, 3100000, 0, 0, 0, 0};
            uint8_t tail = drainBlips(ring, 4, 0, 2500000, t);           // the 3.1 s blip is after now
            CHECK(tail == 3);
            CHECK(t.known());
            CHECK_NEAR(t.angle(), 5.0, 0.01);
            t.setCommand(2500000, 1, 100);
            tail = drainBlips(ring, 4, tail, 3200000, t);
            CHECK(tail == 4);
            CHECK_NEAR(t.angle(), 30.0, 0.01);
            t.update(3200000);
            CHECK_NEAR(t.angle(), 30.0 + 0.1 * t.speedDps(1, 100), 0.5);
        """)

    def test_drain_overflow_drops_position(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);     // known at 5
            volatile uint32_t ring[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            uint8_t tail = drainBlips(ring, 9, 0, 9000000, t);           // 9 stored, 8 kept: one lost
            CHECK(tail == 9);
            CHECK(!t.known());
            t.onBlip(9600000); t.onBlip(10200000);                       // ordinary 30 deg gaps
            CHECK(!t.known());                                           // no false front from the lost stretch
        """)

    def test_missing_short_gap_at_pair_marks_unknown(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);   // known at 5
            t.onBlip(2600000);                                          // 30
            t.onBlip(3200000);                                          // 60
            CHECK_NEAR(t.angle(), 60.0, 0.01);
            for (int i = 0; i < 10; i++) t.onBlip(3800000 + 600000 * i); // 90, 120 ... -30, 0 (0 is a normal gap)
            CHECK_NEAR(t.angle(), 0.0, 0.01);
            t.onBlip(3800000 + 600000 * 10);                             // should have been short: count is wrong
            CHECK(!t.known());
        """)


if __name__ == "__main__":
    unittest.main()
