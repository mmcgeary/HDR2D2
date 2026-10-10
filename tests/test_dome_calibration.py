"""Task 12: Unit tests for portable DomeCalibration state machine."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
SHARED = ROOT / "shared/R2BodyLink"

SOURCES = [
    BODY / "body/DomeCalibration.cpp",
    BODY / "body/ConfigStore.cpp",
    BODY / "body/DomePosition.cpp",
    BODY / "body/DomeController.cpp",
    BODY / "body/IbusInput.cpp",
    BODY / "body/WheelTest.cpp",
    BODY / "body/VescLink.cpp",
    SHARED / "src/Codec.cpp",
    SHARED / "src/Endpoint.cpp",
]

INCLUDE_DIRS = [
    ROOT / "TEENSY_BODY_CONTROLLER/include",
    BODY,
    SHARED,
    ROOT / "shared",
]

PRELUDE = r'''
#include <cassert>
#include <cstring>
#include <vector>
#include <iostream>
#include "Messages.h"
#include "body/ConfigStore.h"
#include "body/DomeCalibration.h"
#include "body/VescLink.h"

using namespace body;
using namespace r2link;

// Test utility: copies test_body_vesc.py's saved() values, VESC config accepted.
static CommissioningProfile saved_profile_for_tests() {
    CommissioningProfile p;
    for (int w = 0; w < 2; ++w) {
        const int v[] = {1, 42, 19, 1, 1000, 1000, 0, 1500, 1050, 1500, 150, 1500, 100, 50};
        for (int id = 5; id <= 18; ++id) assert(setField(p, id, w, v[id - 5]) == FieldResult::Ok);
        p.acceptance |= 1u << (kAcceptVescConfig + w);
    }
    assert(validateProfile(p));
    return p;
}

struct DummyStorage : public RawStorage {
    uint8_t mem[512];
    DummyStorage() { std::memset(mem, 0xFF, sizeof(mem)); }
    size_t size() const override { return sizeof(mem); }
    StorageResult read(size_t address, uint8_t* destination, size_t length) override {
        if (address + length > sizeof(mem)) return StorageResult::OutOfRange;
        std::memcpy(destination, mem + address, length);
        return StorageResult::Ok;
    }
    StorageResult write(size_t address, const uint8_t* source, size_t length) override {
        if (address + length > sizeof(mem)) return StorageResult::OutOfRange;
        std::memcpy(mem + address, source, length);
        return StorageResult::Ok;
    }
};

struct CalibrationFixture {
    DummyStorage storage;
    ConfigStore store;
    CommissioningProfile profile;
    DomeCalibration cal;
    RcSnapshot rc{};
    HallState hall{};
    uint32_t now{1000};

    CalibrationFixture() : storage(), store(storage), profile(), cal(store, profile) {
        // Setup initial radio: CH6 OFF (1000), CH9 ON (2000), sticks neutral (1500)
        rc.valid = true;
        rc.sample_ms = now;
        for (int i = 0; i < 10; ++i) rc.channels[i] = 1500;
        rc.channels[5] = 1000; // CH6 OFF
        rc.channels[8] = 2000; // CH9 ON (Auto Dome ON)
        cal.updateRc(rc, now);

        // Setup initial hall: both sensors valid, neither active
        hall.valid_mask = 0x03;
        hall.active_mask = 0x00;
        hall.source_age_ms = 0;
        cal.updateHall(hall, now);
    }

    void setCommissionedNeutral(uint16_t neutral = 1500) {
        setField(profile, kFieldServoNeutral, 0, neutral);
        setField(profile, kFieldServoMin, 0, 1000);
        setField(profile, kFieldServoMax, 0, 2000);
        setField(profile, kFieldAutoSpeed, 0, 15);
        setField(profile, kFieldCwRate, 0, 360);
        setField(profile, kFieldCcwRate, 0, 360);
        profile.acceptance |= (1u << kAcceptServoNeutral);
    }

    CommissionRequest makeBegin(uint8_t test, uint32_t run_id = 1, int32_t val = 0) {
        CommissionRequest req{};
        req.operation = static_cast<uint8_t>(CommissionOp::Begin);
        req.test = test;
        req.run_id = run_id;
        req.value = val;
        req.control_epoch = 1;
        return req;
    }

    CommissionRequest makeKeepalive(uint32_t run_id = 1) {
        CommissionRequest req{};
        req.operation = static_cast<uint8_t>(CommissionOp::Keepalive);
        req.run_id = run_id;
        req.control_epoch = 1;
        return req;
    }

    CommissionRequest makeCancel(uint32_t run_id = 1) {
        CommissionRequest req{};
        req.operation = static_cast<uint8_t>(CommissionOp::Cancel);
        req.run_id = run_id;
        req.control_epoch = 1;
        return req;
    }

    void tick(uint32_t t) {
        now = t;
        rc.sample_ms = now;
        cal.updateRc(rc, now);
        cal.updateHall(hall, now);
        cal.tick(now);
    }

    void advance(uint32_t to_ms) {
        while (now < to_ms) {
            uint32_t step = (now + 100 < to_ms) ? (now + 100) : to_ms;
            tick(step);
            if (cal.active()) {
                cal.handleRequest(makeKeepalive(cal.status().run_id), step);
            }
        }
    }

    CommissionRequest makeAccept(uint8_t bit) {
        CommissionRequest req{};
        req.operation = static_cast<uint8_t>(CommissionOp::Accept);
        req.value = bit;
        req.control_epoch = 1;
        return req;
    }

    CommissionRequest makeSetField(uint8_t field, uint8_t wheel, int32_t value) {
        CommissionRequest req{};
        req.operation = static_cast<uint8_t>(CommissionOp::SetField);
        req.field = field;
        req.wheel = wheel;
        req.value = value;
        req.control_epoch = 1;
        return req;
    }

    CommissionRequest makeWheelBegin(CommissionTest test, uint8_t wheel, uint32_t run_id, int32_t raised = 1) {
        CommissionRequest req = makeBegin(static_cast<uint8_t>(test), run_id, raised);
        req.wheel = wheel;
        return req;
    }
    VescSample wheelSample(int32_t erpm, uint32_t t) {
        VescSample s{}; s.valid = true; s.erpm = erpm; s.sample_ms = t; return s;
    }

    // Flip CH9 (auto dome) and let the RC snapshot settle.
    void setCh9(uint16_t us) { rc.channels[8] = us; tick(now + 20); }

    // Front edge to start, then three revolutions of rev_ms each with the rear
    // magnet passed half way round.
    void runTiming(CommissionTest test, uint32_t run_id, uint32_t rev_ms) {
        assert(cal.handleRequest(makeBegin(static_cast<uint8_t>(test), run_id), now) == Result::Accepted);
        uint32_t edge = now + 10;
        hall.active_mask = 0x01; tick(edge);
        hall.active_mask = 0x00; advance(edge + 50);
        for (int rev = 0; rev < 3; ++rev) {
            const uint32_t mid = edge + rev_ms / 2;
            advance(mid - 1);
            hall.active_mask = 0x02; tick(mid);
            hall.active_mask = 0x00; advance(mid + 50);
            edge += rev_ms;
            advance(edge - 1);
            hall.active_mask = 0x01; tick(edge);
            hall.active_mask = 0x00; tick(edge + 10);
        }
    }
};
'''

class DomeCalibrationTests(unittest.TestCase):
    def check(self, body):
        code = PRELUDE + "\nint main() {\n" + body + "\n    puts(\"ok\");\n    return 0;\n}\n"
        result = run_cpp(code, extra_sources=SOURCES, include_dirs=INCLUDE_DIRS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_keepalive_disconnect_during_motion_returns_to_neutral_at_300ms(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    // Start timing test CW
    auto req = f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 42);
    auto res = f.cal.handleRequest(req, f.now);
    assert(res == Result::Accepted);
    assert(f.cal.active());

    // Motion should be outputting speed pulses
    auto cmd = f.cal.output();
    assert(cmd.pulses);
    assert(cmd.pulse_us != 1500);

    // Keepalive at t=1100 (100ms later) maintains running state
    f.tick(1100);
    assert(f.cal.handleRequest(f.makeKeepalive(42), 1100) == Result::Accepted);
    assert(f.cal.active());

    // Advance 299ms without keepalive -> still running
    f.tick(1399);
    assert(f.cal.active());

    // Advance to 300ms without keepalive (t=1400) -> keepalive expired, test TimedOut!
    f.tick(1400);
    assert(!f.cal.active());
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::TimedOut));

    // Output must return to neutral
    cmd = f.cal.output();
    assert(cmd.pulses);
    assert(cmd.pulse_us == 1500);
''')

    def test_ch6_on_or_manual_cancels_and_ch9_off_does_not(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    // 1. CH6 ON cancels test
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 1), f.now) == Result::Accepted);
    assert(f.cal.active());
    f.rc.channels[5] = 1800; // CH6 ON!
    f.tick(1050);
    assert(!f.cal.active());
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Cancelled));
    assert(f.cal.output().pulse_us == 1500);

    // Reset RC
    f.rc.channels[5] = 1000;
    f.tick(1100);

    // 2. CH9 OFF does not cancel a running test any more.
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 2), f.now) == Result::Accepted);
    f.rc.channels[8] = 1000;
    f.advance(f.now + 200);
    assert(f.cal.active());
    f.cal.handleRequest(f.makeCancel(2), f.now);

    // Reset RC
    f.rc.channels[8] = 2000;
    f.tick(f.now + 20);

    // 3. Manual stick deflection cancels test
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 3), f.now) == Result::Accepted);
    assert(f.cal.active());
    f.rc.channels[3] = 1800; // Manual dome stick deflected!
    f.tick(f.now + 20);
    assert(!f.cal.active());
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Cancelled));
''')

    def test_dome_tests_accept_and_save_never_need_ch9(self):
        self.check(r"""
    CalibrationFixture f;
    f.rc.channels[8] = 1000; f.tick(f.now + 20);          // CH9 OFF throughout
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoNeutral, 0, 1500), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoMin, 0, 1000), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoMax, 0, 2000), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::Neutral), 3), f.now) == Result::Accepted);
    f.advance(f.now + 3100);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    assert(f.cal.handleRequest(f.makeAccept(kAcceptServoNeutral), f.now) == Result::Accepted);
    f.rc.channels[8] = 2000; f.tick(f.now + 20);          // and CH9 ON is fine too
    CommissionRequest save{}; save.operation = static_cast<uint8_t>(CommissionOp::Save); save.control_epoch = 1;
    assert(f.cal.handleRequest(save, f.now) == Result::Accepted);
    // CH6 ON still closes the gate.
    f.rc.channels[5] = 2000; f.tick(f.now + 20);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoNeutral, 0, 1505), f.now) == Result::Inhibited);
""")

    def test_apply_baseline_op_and_acceptance_masks_in_status(self):
        self.check(r"""
    CalibrationFixture f;
    CommissioningProfile saved;
    f.cal.setSavedProfile(saved);
    f.cal.setObservedFirmware(0, true, 6, 2);
    f.cal.setObservedFirmware(1, false, 0, 0);
    CommissionRequest b{}; b.operation = 7; b.control_epoch = 1;
    assert(f.cal.handleRequest(b, f.now) == Result::Accepted);
    int32_t v = 0;
    assert(getField(f.profile, kFieldFwMajor, 0, v) && v == 6);
    assert(!getField(f.profile, kFieldFwMajor, 1, v));
    assert(getField(f.profile, kFieldBrakeMa, 1, v) && v == 3000);
    f.profile.acceptance = 0x011;
    f.tick(f.now + 20);
    assert(f.cal.status().staged_acceptance == 0x011 && f.cal.status().saved_acceptance == 0);
    assert(f.cal.status().unsaved == 1);
    saved = f.profile; f.tick(f.now + 20);
    assert(f.cal.status().unsaved == 0 && f.cal.status().saved_acceptance == 0x011);
    f.rc.channels[5] = 2000; f.tick(f.now + 20);
    assert(f.cal.handleRequest(b, f.now) == Result::Inhibited);   // stationary gate
    // Field reads say whether the field is set.
    assert(f.cal.diagnostics(1, kFieldBrakeMa, 1, 0).known == 1 && f.cal.diagnostics(1, kFieldBrakeMa, 1, 0).value == 3000);
    assert(f.cal.diagnostics(1, kFieldDirection, 0, 0).known == 0);
""")

    def test_stop_remains_absolute(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    // With motion locked, Begin is Inhibited
    f.cal.setMotionLocked(true);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 1), f.now) == Result::Inhibited);
    assert(!f.cal.active());

    // When unlocked, Begin succeeds
    f.cal.setMotionLocked(false);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 1), f.now) == Result::Accepted);
    assert(f.cal.active());

    // Locking during test immediately cancels test
    f.cal.setMotionLocked(true);
    f.tick(1050);
    assert(!f.cal.active());
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Cancelled));
''')

    def test_repeated_start_has_one_run(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    // First Begin with run_id 7 starts test
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 7), f.now) == Result::Accepted);
    assert(f.cal.active());
    assert(f.cal.status().run_id == 7);

    // Re-transmitting same Begin request is idempotent (returns Accepted, does not launch 2nd run)
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 7), f.now) == Result::Accepted);
    assert(f.cal.active());
    assert(f.cal.status().run_id == 7);

    // Begin with different run_id while running is Busy
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 8), f.now) == Result::Busy);
''')

    def test_stale_or_invalid_hall_never_completes(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::FrontRef), 1), f.now) == Result::Accepted);
    assert(f.cal.active());

    // Hall becomes stale (>200ms)
    f.hall.source_age_ms = 250;
    f.tick(1100);

    // Front sensor active edge while stale must NOT complete the test
    f.hall.active_mask = 0x01; // Front active
    f.tick(1150);
    assert(!f.cal.active());
    // Stale sensor causes test cancellation/failure, never successful completion
    assert(f.cal.status().state != static_cast<uint8_t>(CommissionState::Completed));
''')

    def test_isolated_neutral_bootstrap_disables_pulses_on_cancel_until_accepted(self):
        self.check(r'''
    CalibrationFixture f;
    // Profile has NO accepted neutral initially
    assert((f.profile.acceptance & (1u << kAcceptServoNeutral)) == 0);

    // Unaccepted neutral: before any test, output has no pulses
    assert(!f.cal.output().pulses);

    // Begin neutral test with trial pulse 1520us
    auto req = f.makeBegin(static_cast<uint8_t>(CommissionTest::Neutral), 10, 1520);
    assert(f.cal.handleRequest(req, f.now) == Result::Accepted);
    assert(f.cal.active());

    // While test is running, trial pulse is emitted
    auto cmd = f.cal.output();
    assert(cmd.pulses);
    assert(cmd.pulse_us == 1520);

    // When cancelled before neutral is accepted, pulses must be disabled (no creep)
    f.cal.cancel(f.now);
    assert(!f.cal.active());
    assert(!f.cal.output().pulses);

    // Now accept neutral in profile
    f.setCommissionedNeutral(1500);
    // Once neutral is accepted, cancellation outputs accepted neutral
    f.cal.cancel(f.now);
    assert(f.cal.output().pulses);
    assert(f.cal.output().pulse_us == 1500);
''')

    def test_neutral_test_completes_after_observation_window_and_is_acceptable(self):
        self.check(r'''
    CalibrationFixture f;
    setField(f.profile, kFieldServoNeutral, 0, 1510);
    setField(f.profile, kFieldServoMin, 0, 1000);
    setField(f.profile, kFieldServoMax, 0, 2000);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::Neutral), 5), f.now) == Result::Accepted);
    assert(f.cal.output().pulse_us == 1510);  // the staged neutral is the trial pulse
    f.advance(f.now + 2900);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Running));
    f.advance(f.now + 200);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    f.setCh9(1000);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptServoNeutral), f.now) == Result::Accepted);
    assert(f.profile.acceptance & (1u << kAcceptServoNeutral));
''')

    def test_reference_acceptance_requires_its_own_completed_test(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::Neutral), 1), f.now) == Result::Accepted);
    f.advance(f.now + 3100);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    f.setCh9(1000);
    // A completed Neutral run is not evidence for the front reference.
    assert(f.cal.handleRequest(f.makeAccept(kAcceptFrontRef), f.now) == Result::Inhibited);
    f.setCh9(2000);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::FrontRef), 2), f.now) == Result::Accepted);
    f.hall.active_mask = 0x01; f.tick(f.now + 20);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    f.hall.active_mask = 0x00; f.setCh9(1000);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptRearRef), f.now) == Result::Inhibited);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptFrontRef), f.now) == Result::Accepted);
    // Changing the servo configuration after the run voids its evidence.
    f.setCh9(2000);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::RearRef), 3), f.now) == Result::Accepted);
    f.hall.active_mask = 0x02; f.tick(f.now + 20);
    f.hall.active_mask = 0x00; f.setCh9(1000);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoMax, 0, 1990), f.now) == Result::Accepted);
    f.profile.acceptance |= (1u << kAcceptServoNeutral);  // re-accepted neutral, old rear run still stale
    assert(f.cal.handleRequest(f.makeAccept(kAcceptRearRef), f.now) == Result::Inhibited);
''')

    def test_timing_runs_keep_both_directions_and_stage_measured_rates(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);
    f.runTiming(CommissionTest::TimingCw, 10, 4000);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    assert(f.cal.status().proposed_cw_ddeg_s == 900);
    assert(f.profile.cw_ddeg_per_s == 900);
    f.setCh9(1000);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptAutoTiming), f.now) == Result::Inhibited);  // CCW missing
    f.setCh9(2000);
    f.runTiming(CommissionTest::TimingCcw, 11, 4500);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    assert(f.cal.status().proposed_cw_ddeg_s == 900);   // not wiped by the CCW run
    assert(f.cal.status().proposed_ccw_ddeg_s == 800);
    assert(f.profile.ccw_ddeg_per_s == 800);
    f.setCh9(1000);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptAutoTiming), f.now) == Result::Accepted);
''')

    def test_set_field_requires_stationary_gate(self):
        self.check(r'''
    CalibrationFixture f;
    f.rc.channels[5] = 2000; f.setCh9(1000);  // CH6 ON
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoNeutral, 0, 1510), f.now) == Result::Inhibited);
    f.rc.channels[5] = 1000; f.setCh9(2000);  // CH9 ON no longer matters
    f.rc.channels[3] = 1800; f.setCh9(1000);  // dome stick deflected
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoNeutral, 0, 1510), f.now) == Result::Inhibited);
    assert(f.profile.servo_neutral == 0);
    f.rc.channels[3] = 1500; f.setCh9(1000);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoNeutral, 0, 1510), f.now) == Result::Accepted);
    assert(f.profile.servo_neutral == 1510);
''')

    def test_three_revolutions_timing_and_median_speed(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    // Begin Timing CW run (test 4)
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 100), f.now) == Result::Accepted);
    assert(f.cal.active());

    // Initial phase: find front reference edge (anchors 0 deg, discarded from timing)
    f.hall.active_mask = 0x01; // front detected
    f.advance(1050);
    f.hall.active_mask = 0x00; // leaves front
    f.advance(1100);

    // Rev 1:
    // Must detect rear between front edges!
    f.advance(2500);
    f.hall.active_mask = 0x02; // rear detected
    f.tick(2500);
    f.advance(2600);
    f.hall.active_mask = 0x00;
    f.tick(2600);
    // Front detected again at t=5100 (Rev 1 duration = 5100 - 1050 = 4050ms)
    f.advance(5100);
    f.hall.active_mask = 0x01;
    f.tick(5100);
    f.advance(5150);
    f.hall.active_mask = 0x00;
    f.tick(5150);

    // Rev 2:
    f.advance(7000);
    f.hall.active_mask = 0x02; // rear detected
    f.tick(7000);
    f.advance(7100);
    f.hall.active_mask = 0x00;
    f.tick(7100);
    // Front detected again at t=9150 (Rev 2 duration = 9150 - 5100 = 4050ms)
    f.advance(9150);
    f.hall.active_mask = 0x01;
    f.tick(9150);
    f.advance(9200);
    f.hall.active_mask = 0x00;
    f.tick(9200);

    // Rev 3:
    f.advance(11000);
    f.hall.active_mask = 0x02; // rear detected
    f.tick(11000);
    f.advance(11100);
    f.hall.active_mask = 0x00;
    f.tick(11100);
    // Front detected again at t=13150 (Rev 3 duration = 13150 - 9150 = 4000ms)
    f.advance(13150);
    f.hall.active_mask = 0x01;
    f.tick(13150);

    // All 3 revolutions completed!
    assert(!f.cal.active());
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    assert(f.cal.status().revolution_ms[0] == 4050);
    assert(f.cal.status().revolution_ms[1] == 4050);
    assert(f.cal.status().revolution_ms[2] == 4000);

    // Median duration is 4050ms -> Rate = 3600000 / 4050 = 888 ddeg/s
    assert(f.cal.status().proposed_cw_ddeg_s > 0);
''')

    def test_artificial_magnet_width_does_not_shorten_duration(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 101), f.now) == Result::Accepted);

    // Initial front rising edge at t=1000
    f.hall.active_mask = 0x01;
    f.tick(1000);
    // Wide magnet stays active for 400ms! (falling edge at 1400)
    f.advance(1200);
    f.hall.active_mask = 0x00;
    f.advance(1400);

    // Rear active
    f.advance(3000);
    f.hall.active_mask = 0x02;
    f.tick(3000);
    f.advance(3200);
    f.hall.active_mask = 0x00;
    f.tick(3200);

    // Next front RISING edge at t=5000 (Duration measured from rising edge to rising edge: exactly 4000ms!)
    f.advance(5000);
    f.hall.active_mask = 0x01;
    f.tick(5000);
    assert(f.cal.status().revolution_ms[0] == 4000);
''')

    def test_explicit_save_gates_and_read_back(self):
        self.check(r'''
    CalibrationFixture f;
    f.setCommissionedNeutral(1500);

    // Save request
    CommissionRequest req{};
    req.operation = static_cast<uint8_t>(CommissionOp::Save);
    req.control_epoch = 1;

    // While CH6 is ON (2000), Save must be rejected
    f.rc.channels[5] = 2000;
    f.tick(1000);
    assert(f.cal.handleRequest(req, f.now) != Result::Accepted);

    // With CH6 OFF (1000) and neutral sticks, Save succeeds (CH9 irrelevant)
    f.rc.channels[5] = 1000;
    f.rc.channels[8] = 1000;
    f.tick(1020);
    assert(f.cal.handleRequest(req, f.now) == Result::Accepted);
    assert(f.cal.status().saved == 1);
    assert(f.cal.status().config_generation > 0);
''')

    def test_wheel_test_lifecycle_and_gates(self):
        self.check(r'''
    CalibrationFixture f;
    f.rc.channels[8] = 1000; f.tick(f.now + 20);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 5), f.now) == Result::NotReady);
    assert(f.cal.status().error == 12);
    f.cal.setWheelReady(0, true);
    f.cal.updateWheelSample(f.wheelSample(400, f.now), f.now);          // still coasting
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 5), f.now) == Result::Inhibited);
    assert(f.cal.status().error == 14);
    f.cal.updateWheelSample(f.wheelSample(0, f.now), f.now);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 6, 0), f.now) == Result::InvalidArgument);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 6), f.now) == Result::Accepted);
    assert(f.cal.wheelTestBusy() && f.cal.wheelUnderTest() == 0);
    assert(f.cal.wheelCommand().mode == WheelTestCommand::Duty && f.cal.wheelCommand().duty_permille == 100);
    // Keepalive lost mid-spin: brake 300ms, then nothing; state Cancelled.
    const uint32_t start = f.now;
    for (uint32_t t = start + 1; t <= start + 400; ++t) { f.cal.updateWheelSample(f.wheelSample(600, t), t); f.tick(t); }
    assert(f.cal.status().state != static_cast<uint8_t>(CommissionState::Running));
    assert(f.cal.wheelCommand().mode == WheelTestCommand::Brake && f.cal.wheelTestBusy());
    CommissionRequest save{}; save.operation = static_cast<uint8_t>(CommissionOp::Save); save.control_epoch = 1;
    assert(f.cal.handleRequest(save, f.now) == Result::Busy);            // still braking out
    assert(f.cal.handleRequest(f.makeSetField(kFieldBrakeMa, 0, 3000), f.now) == Result::Busy);
    const uint32_t brake_start = f.now;
    for (uint32_t t = brake_start + 1; t <= brake_start + 400; ++t) { f.cal.updateWheelSample(f.wheelSample(0, t), t); f.tick(t); }
    assert(!f.cal.wheelTestBusy() && f.cal.wheelCommand().mode == WheelTestCommand::Disable);
''')

    def test_wheel_sign_offs_need_a_passed_test_on_the_saved_settings(self):
        self.check(r'''
    CalibrationFixture f;
    CommissioningProfile saved = saved_profile_for_tests();
    f.profile = saved; f.cal.setSavedProfile(saved);
    f.rc.channels[8] = 1000; f.tick(f.now + 20);
    f.cal.setWheelReady(1, true);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake + 1), f.now) == Result::Inhibited);   // no test yet
    f.cal.updateWheelSample(f.wheelSample(0, f.now), f.now);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelTimeout, 1, 9), f.now) == Result::Accepted);
    // Spin 1s at 800 erpm, then the VESC stops the wheel 300ms after commands stop.
    uint32_t cut = 0;
    const uint32_t start = f.now;
    for (uint32_t t = start + 1; t <= start + 3000 && f.cal.wheelTestBusy(); ++t) {
        const bool spinning = f.cal.wheelCommand().mode == WheelTestCommand::Duty;
        if (!spinning && !cut) cut = t;
        const int32_t erpm = spinning ? 800 : (t - cut < 300 ? 800 : 0);
        f.cal.updateWheelSample(f.wheelSample(erpm, t), t);
        f.tick(t);
        if (t % 100 == 0 && f.cal.active()) f.cal.handleRequest(f.makeKeepalive(9), t);
    }
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    assert(f.cal.status().wheel == 1 && f.cal.status().stop_ms >= 290 && f.cal.status().stop_ms <= 400);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake + 1), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake), f.now) == Result::Inhibited);       // left not tested
    // A staged change to a tested field voids the evidence.
    f.profile.acceptance &= ~(1u << (kAcceptTimeoutBrake + 1));
    assert(f.cal.handleRequest(f.makeSetField(kFieldTimeoutBrakeMa, 1, 2500), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake + 1), f.now) == Result::Inhibited);
''')


if __name__ == "__main__":
    unittest.main()
