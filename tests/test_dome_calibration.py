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

using namespace body;
using namespace r2link;

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

    def test_ch6_on_manual_or_ch9_off_cancels(self):
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

    // 2. CH9 OFF cancels test
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 2), f.now) == Result::Accepted);
    assert(f.cal.active());
    f.rc.channels[8] = 1000; // CH9 OFF!
    f.tick(1150);
    assert(!f.cal.active());
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Cancelled));

    // Reset RC
    f.rc.channels[8] = 2000;
    f.tick(1200);

    // 3. Manual stick deflection cancels test
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 3), f.now) == Result::Accepted);
    assert(f.cal.active());
    f.rc.channels[3] = 1800; // Manual dome stick deflected!
    f.tick(1250);
    assert(!f.cal.active());
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Cancelled));
''')

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

    // While CH9 is ON (2000), Save must be rejected (GateClosed / InvalidArgument)
    f.rc.channels[8] = 2000;
    f.tick(1000);
    assert(f.cal.handleRequest(req, f.now) != Result::Accepted);

    // With CH6 OFF (1000) and CH9 OFF (1000) and neutral sticks, Save succeeds
    f.rc.channels[5] = 1000;
    f.rc.channels[8] = 1000;
    f.tick(1020);
    assert(f.cal.handleRequest(req, f.now) == Result::Accepted);
    assert(f.cal.status().saved == 1);
    assert(f.cal.status().config_generation > 0);
''')


if __name__ == "__main__":
    unittest.main()
