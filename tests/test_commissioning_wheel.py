"""Guided commissioning: WheelTest state machine with scripted telemetry."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
SHARED = ROOT / "shared/R2BodyLink"
SOURCES = [BODY / "body/WheelTest.cpp"]
PRELUDE = r'''
#include <cassert>
#include "body/WheelTest.h"
using namespace body;
// A raised wheel: follows duty with a first-order lag, decays at `decay` erpm/ms
// when braked (or when the VESC timeout brake engages 150ms after commands stop).
struct Wheel {
    double erpm = 0; int32_t fault = 0; bool timeout_brake = true; double decay = 2.0;
    uint32_t last_cmd_ms = 0; bool stale = false;
    void step(const WheelTestCommand& c, uint32_t now) {
        if (c.mode == WheelTestCommand::Duty) { erpm += (c.duty_permille * 8.0 - erpm) * 0.01; last_cmd_ms = now; }
        else if (c.mode == WheelTestCommand::Brake) { erpm -= (erpm > 0 ? 1 : -1) * decay * (std::abs(erpm) > decay); last_cmd_ms = now; }
        else if (timeout_brake && now - last_cmd_ms > 150) erpm -= (erpm > 0 ? 1 : -1) * decay * (std::abs(erpm) > decay);
        else erpm *= 0.9995;   // coast
    }
    VescSample sample(uint32_t now) const {
        VescSample s{}; s.valid = !stale; s.stale = stale; s.erpm = int32_t(erpm); s.fault = uint8_t(fault);
        s.motor_mA = int32_t(-std::abs(erpm)); s.sample_ms = now; return s;
    }
};
static WheelTestPhase run(WheelTest& t, Wheel& w, uint32_t from, uint32_t to) {
    for (uint32_t now = from; now <= to && t.busy(); ++now) { t.update(w.sample(now), now); w.step(t.command(), now); }
    return t.phase();
}
'''


class WheelTestTests(unittest.TestCase):
    def check(self, body):
        result = run_cpp(PRELUDE.replace("#include <cassert>", "#include <cassert>\n#include <cmath>\n#include <cstdlib>")
                         + "int main() {\n" + body + "\nreturn 0;\n}\n",
                         extra_sources=SOURCES, include_dirs=[BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_timeout_brake_passes_when_the_vesc_stops_the_wheel(self):
        self.check(r'''
    WheelTest t; Wheel w;
    t.begin(WheelTestKind::TimeoutBrake, 300, 200, 0);
    assert(t.command().mode == WheelTestCommand::Duty && t.command().duty_permille == 100);
    assert(run(t, w, 0, 6000) == WheelTestPhase::Passed);
    assert(t.result().stop_ms > 150 && t.result().stop_ms < 1500);
    assert(t.result().peak_erpm >= 100 && t.command().mode == WheelTestCommand::Disable);
''')

    def test_timeout_brake_fails_when_the_wheel_keeps_turning(self):
        self.check(r'''
    WheelTest t; Wheel w; w.timeout_brake = false;
    t.begin(WheelTestKind::TimeoutBrake, 300, 200, 0);
    assert(run(t, w, 0, 6000) == WheelTestPhase::Failed);
    assert(t.result().error == WheelTest::kErrTimeoutBrakeInactive);
''')

    def test_a_wheel_that_never_turns_fails(self):
        self.check(r'''
    WheelTest t; Wheel w; w.decay = 1e9;   // seized: erpm stays 0
    t.begin(WheelTestKind::Direction, 300, 200, 0);
    for (uint32_t now = 0; now <= 3000 && t.busy(); ++now) { VescSample s{}; s.valid = true; s.sample_ms = now; t.update(s, now); }
    assert(t.phase() == WheelTestPhase::Failed && t.result().error == WheelTest::kErrNotTurning);
''')

    def test_direction_spins_positive_then_brakes(self):
        self.check(r'''
    WheelTest t; Wheel w;
    t.begin(WheelTestKind::Direction, 300, 200, 0);
    assert(run(t, w, 0, 6000) == WheelTestPhase::Passed);
    assert(t.result().peak_erpm >= 100);   // signed: positive for a positive raw command
''')

    def test_reversal_brakes_dwells_and_reverses(self):
        self.check(r'''
    WheelTest t; Wheel w;
    t.begin(WheelTestKind::Reversal, 300, 200, 0);
    bool saw_brake = false, saw_reverse = false;
    for (uint32_t now = 0; now <= 6000 && t.busy(); ++now) {
        t.update(w.sample(now), now);
        saw_brake |= t.command().mode == WheelTestCommand::Brake && w.erpm > 300;
        saw_reverse |= t.command().mode == WheelTestCommand::Duty && t.command().duty_permille == -100;
        if (saw_reverse) assert(std::abs(w.erpm) <= 300 || w.erpm < 0);   // never reversed at speed
        w.step(t.command(), now);
    }
    assert(t.phase() == WheelTestPhase::Passed && saw_brake && saw_reverse);
''')

    def test_fault_stale_abort_and_time_limit(self):
        self.check(r'''
    { WheelTest t; Wheel w; t.begin(WheelTestKind::TimeoutBrake, 300, 200, 0);
      run(t, w, 0, 500); w.fault = 4;
      assert(run(t, w, 501, 6000) == WheelTestPhase::Failed);
      assert(t.result().error == WheelTest::kErrVescFault && t.result().fault == 4); }
    { WheelTest t; Wheel w; t.begin(WheelTestKind::Direction, 300, 200, 0);
      run(t, w, 0, 500); w.stale = true;
      t.update(w.sample(501), 501);
      assert(t.command().mode == WheelTestCommand::Brake);          // brake out first
      assert(run(t, w, 502, 6000) == WheelTestPhase::Failed && t.result().error == WheelTest::kErrTelemetry); }
    { WheelTest t; Wheel w; t.begin(WheelTestKind::Reversal, 300, 200, 0);
      run(t, w, 0, 700); t.abort(701);
      assert(t.command().mode == WheelTestCommand::Brake && t.busy());
      assert(run(t, w, 702, 1100) == WheelTestPhase::Failed && t.result().error == 0);
      assert(t.command().mode == WheelTestCommand::Disable); }
''')


if __name__ == "__main__":
    unittest.main()
