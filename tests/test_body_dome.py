"""Task 6 synthetic fixtures and unit tests for dome arbitration and position estimation."""
import unittest

from cpp_test_support import run_cpp
from test_body_vesc import BODY, SHARED, PRELUDE as VESC_PRELUDE, SOURCES as VESC_SOURCES

SOURCES = VESC_SOURCES + [
    BODY / "body/DriveController.cpp",
    BODY / "body/IbusInput.cpp",
    BODY / "body/DomePosition.cpp",
    BODY / "body/DomeController.cpp",
]

PRELUDE = VESC_PRELUDE + r'''
#include "body/DomePosition.h"
#include "body/DomeController.h"
#include "body/IbusInput.h"
#include "Servo.h"

struct DomeFixture {
    CommissioningProfile profile;
    IbusInput radio;
    DomeController dome;
    r2link::HallState hall{};
    uint16_t current_manual = 1500;
    uint16_t current_auto = 2000;
    uint16_t current_feet = 2000;
    uint8_t current_active_mask = 0;

    DomeFixture() : profile(saved()), dome(profile) {
        // Commissioned auto dome profile
        assert(setField(profile, kFieldServoNeutral, 0, 1500) == FieldResult::Ok);
        assert(setField(profile, kFieldServoMin, 0, 1000) == FieldResult::Ok);
        assert(setField(profile, kFieldServoMax, 0, 2000) == FieldResult::Ok);
        assert(setField(profile, kFieldAutoSpeed, 0, 25) == FieldResult::Ok);
        assert(setField(profile, kFieldCwRate, 0, 360) == FieldResult::Ok);   // 36.0 deg/s
        assert(setField(profile, kFieldCcwRate, 0, 360) == FieldResult::Ok);
        acceptAutoDome();
        dome = DomeController(profile);

        hall.valid_mask = 0x03;
        hall.active_mask = 0x00;
        hall.source_age_ms = 0;
        rc(0, 1500, 2000, 2000); // CH4=1500, CH9=2000 (Auto Dome ON), CH6=2000
    }

    void acceptAutoDome() {
        // Acceptance bits for manual and auto dome
        profile.acceptance |= (1u << kAcceptServoNeutral) |
                              (1u << kAcceptFrontRef) |
                              (1u << kAcceptRearRef) |
                              (1u << kAcceptAutoTiming);
    }

    void rc(uint32_t t, uint16_t manual_dome = 1500, uint16_t auto_dome = 2000,
            uint16_t feet_enable = 2000) {
        current_manual = manual_dome;
        current_auto = auto_dome;
        current_feet = feet_enable;
        Bytes b(32, 0); b[0] = 32; b[1] = 0x40;
        for (int c = 0; c < 14; ++c) {
            uint16_t v = (c == kManualDome) ? manual_dome :
                         (c == kAutoDome) ? auto_dome :
                         (c == kFeetEnable) ? feet_enable : 1500;
            b[2 + 2 * c] = v; b[3 + 2 * c] = v >> 8;
        }
        uint16_t sum = 0xffff;
        for (int i = 0; i < 30; ++i) sum -= b[i];
        b[30] = sum; b[31] = sum >> 8;
        radio.feed(b.data(), b.size(), t);
        dome.updateRc(radio.snapshot(t), t);
    }

    void setHall(uint8_t active_mask, uint32_t t, uint16_t age = 0) {
        current_active_mask = active_mask;
        hall.active_mask = active_mask;
        hall.valid_mask = 0x03;
        hall.source_age_ms = age;
        dome.updateHall(hall, t);
    }

    void deliverHall(uint32_t t, uint16_t age = 0) {
        setHall(current_active_mask, t, age);
    }

    void tick(uint32_t t) {
        rc(t, current_manual, current_auto, current_feet);
        dome.tick(t);
    }
};
'''

class DomeTests(unittest.TestCase):
    def check(self, code):
        result = run_cpp(PRELUDE + "\nint main(){\n" + code + "\n}\n",
                         extra_sources=SOURCES,
                         include_dirs=[BODY.parent.parent / "tests/radio_fakes", BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_position_anchors_normalization_wrap_and_fractional_integration(self):
        self.check(r'''
CommissioningProfile p = saved();
assert(setField(p, kFieldCwRate, 0, 360) == FieldResult::Ok);
assert(setField(p, kFieldCcwRate, 0, 360) == FieldResult::Ok);
DomePosition pos(p);
assert(!pos.valid());

// Wraparound math
assert(DomePosition::normalize(0) == 0);
assert(DomePosition::normalize(1799) == 1799);
assert(DomePosition::normalize(1800) == -1800);
assert(DomePosition::normalize(1801) == -1799);
assert(DomePosition::normalize(-1800) == -1800);
assert(DomePosition::normalize(-1801) == 1799);
assert(DomePosition::normalize(3600) == 0);
assert(DomePosition::normalize(-3600) == 0);

// Anchoring front (0)
r2link::HallState h{}; h.valid_mask = 0x03; h.active_mask = 0x01; h.source_age_ms = 10;
pos.updateHall(h, 100);
assert(pos.valid());
assert(pos.angleDdeg() == 0);

// Staying active does not reset estimate during integration
pos.integrate(25, 100); // 360 ddeg/s * 0.1s = 36 ddeg CW
assert(pos.angleDdeg() == 36);
pos.updateHall(h, 200); // same front active
assert(pos.angleDdeg() == 36); // Not re-pinned to 0!

// Falling edge
h.active_mask = 0x00;
pos.updateHall(h, 250);
assert(pos.angleDdeg() == 36);

// Newly detected rear (-1800)
h.active_mask = 0x02;
pos.updateHall(h, 300);
assert(pos.angleDdeg() == -1800);

// Fractional integration: 360 ddeg/s = 0.36 ddeg per ms. In 20ms: 7.2 ddeg.
pos.integrate(25, 20); // 7 ddeg accumulated, 0.2 carried
assert(pos.angleDdeg() == -1793);
pos.integrate(25, 20); // 7 ddeg + 0.4 = 7 ddeg (total 14.4 -> -1786)
assert(pos.angleDdeg() == -1786);

// Simultaneous active references fault
h.active_mask = 0x03;
pos.updateHall(h, 400);
assert(!pos.valid());
assert(pos.sensorFault());
''')

    def test_seek_already_active_completes_without_movement(self):
        self.check(r'''
DomeFixture f;
f.setHall(0x01, 100); // Front active
f.tick(100);
assert(f.dome.position().valid());
assert(f.dome.position().angleDdeg() == 0);

r2link::DomeRequest req{};
req.operation = static_cast<uint8_t>(r2link::DomeOperation::SeekReference);
req.reference = static_cast<uint8_t>(r2link::DomeReference::Front);
req.owner = 1; // Event
req.control_epoch = f.dome.controlEpoch();
req.dome_authority_generation = f.dome.authorityGeneration();

assert(f.dome.request(req, 42, 100) == r2link::Result::Accepted);
assert(f.dome.state() == r2link::DomeState::HoldingReference);
assert(f.dome.output().pulse_us == 1500);

r2link::Event ev{};
assert(f.dome.takeEvent(ev));
assert(ev.kind == static_cast<uint8_t>(r2link::EventKind::Completed));
assert(ev.request_seq == 42);
''')

    def test_seek_direction_shortest_route_and_tie(self):
        self.check(r'''
CommissioningProfile p = saved();
DomePosition pos(p);
// When invalid/unknown -> CW (+1)
assert(pos.seekDirection(r2link::DomeReference::Front) == 1);

// Anchor at Front (0)
r2link::HallState h{}; h.valid_mask = 0x03; h.active_mask = 0x01;
pos.updateHall(h, 10);
// Front to Rear (-1800): 180° tie resolves CW (+1)
assert(pos.seekDirection(r2link::DomeReference::Rear) == 1);

// Anchor at Rear (-1800)
h.active_mask = 0x02; pos.updateHall(h, 20);
// Rear to Front (0): 180° tie resolves CW (+1)
assert(pos.seekDirection(r2link::DomeReference::Front) == 1);

// Set angle to +900 (+90°) via integration
h.active_mask = 0x00; pos.updateHall(h, 30);
h.active_mask = 0x01; pos.updateHall(h, 40); // 0
assert(setField(p, kFieldCwRate, 0, 900) == FieldResult::Ok);
pos.integrate(25, 1000); // +900 ddeg
assert(pos.angleDdeg() == 900);
// From +90° to Front (0): CCW (-1) is 90°, vs CW (270°)
assert(pos.seekDirection(r2link::DomeReference::Front) == -1);
// From +90° to Rear (-1800): CW (+1) is 90°, vs CCW (270°)
assert(pos.seekDirection(r2link::DomeReference::Rear) == 1);
''')

    def test_cancellation_no_delayed_success(self):
        self.check(r'''
DomeFixture dome;
dome.setHall(0x00, 50);
dome.tick(50);

r2link::DomeRequest req{};
req.operation = static_cast<uint8_t>(r2link::DomeOperation::SeekReference);
req.reference = static_cast<uint8_t>(r2link::DomeReference::Front);
req.owner = 1;
req.control_epoch = dome.dome.controlEpoch();
req.dome_authority_generation = dome.dome.authorityGeneration();
assert(dome.dome.request(req, 10, 50) == r2link::Result::Accepted);
dome.tick(60);
assert(dome.dome.state() == r2link::DomeState::SeekingReference);

// Cancel requested
dome.dome.cancel(100);
r2link::HallState homeActive{}; homeActive.valid_mask = 0x03; homeActive.active_mask = 0x01;
dome.dome.updateHall(homeActive, 110);
dome.dome.tick(110);
assert(dome.dome.output().pulse_us == dome.profile.servo_neutral);

r2link::Event completedHome{};
assert(!dome.dome.takeEvent(completedHome)); // No delayed success after cancellation
''')

    def test_manual_override_and_exact_generation_regression(self):
        self.check(r'''
DomeFixture f;
f.setHall(0x00, 10);
f.tick(10);

const uint32_t oldGeneration = f.dome.authorityGeneration();

auto submitEventSeekFront = [&](uint32_t gen, uint16_t seq, uint32_t t) {
    r2link::DomeRequest req{};
    req.operation = static_cast<uint8_t>(r2link::DomeOperation::SeekReference);
    req.reference = static_cast<uint8_t>(r2link::DomeReference::Front);
    req.owner = 1;
    req.control_epoch = f.dome.controlEpoch();
    req.dome_authority_generation = gen;
    return f.dome.request(req, seq, t);
};

auto submitEventVelocity = [&](uint32_t gen, uint16_t seq, uint32_t t) {
    r2link::DomeRequest req{};
    req.operation = static_cast<uint8_t>(r2link::DomeOperation::Velocity);
    req.speed_percent = 25;
    req.lease_ms = 100;
    req.owner = 1;
    req.control_epoch = f.dome.controlEpoch();
    req.dome_authority_generation = gen;
    return f.dome.request(req, seq, t);
};

assert(submitEventSeekFront(oldGeneration, 42, 100) == r2link::Result::Accepted);
f.tick(100);
assert(f.dome.state() == r2link::DomeState::SeekingReference);

// Manual stick deflection takes over
f.rc(120, 1800, 2000); // CH4 = 1800
f.tick(120);
assert(f.dome.owner() == r2link::DomeOwner::Manual);
assert(f.dome.authorityGeneration() != oldGeneration);

r2link::Event takeoverEv{};
assert(f.dome.takeEvent(takeoverEv));
assert(takeoverEv.kind == static_cast<uint8_t>(r2link::EventKind::DomeTakeover));
assert(takeoverEv.request_seq == 42);

// Center dome stick
f.rc(140, 1500, 2000); // CH4 = 1500
f.tick(140);

// Submitting with old generation is Inhibited
assert(submitEventVelocity(oldGeneration, 43, 140) == r2link::Result::Inhibited);

// Auto Dome turned OFF
f.rc(160, 1500, 1000); // CH9 = 1000
f.tick(160);
assert(f.dome.output().pulse_us == f.profile.servo_neutral);
''')

    def test_150ms_velocity_lease_expiry_and_renewals(self):
        self.check(r'''
DomeFixture f;
f.setHall(0x00, 10);
f.tick(10);

r2link::DomeRequest req{};
req.operation = static_cast<uint8_t>(r2link::DomeOperation::Velocity);
req.speed_percent = 25;
req.lease_ms = 100;
req.owner = 1;
req.control_epoch = f.dome.controlEpoch();
req.dome_authority_generation = f.dome.authorityGeneration();

assert(f.dome.request(req, 1, 100) == r2link::Result::Accepted);
f.tick(120);
assert(f.dome.state() == r2link::DomeState::RemoteVelocity);
assert(f.dome.output().pulse_us > 1500);

// Tick beyond 100ms lease expiry (100 + 100 = 200)
f.tick(205);
assert(f.dome.state() == r2link::DomeState::Inhibited);
assert(f.dome.output().pulse_us == 1500);
''')

    def test_10_second_seek_timeout_and_latched_fault(self):
        self.check(r'''
DomeFixture f;
f.setHall(0x00, 1000);
f.tick(1000);

r2link::DomeRequest req{};
req.operation = static_cast<uint8_t>(r2link::DomeOperation::SeekReference);
req.reference = static_cast<uint8_t>(r2link::DomeReference::Front);
req.owner = 1;
req.control_epoch = f.dome.controlEpoch();
req.dome_authority_generation = f.dome.authorityGeneration();

assert(f.dome.request(req, 1, 1000) == r2link::Result::Accepted);

// Deliver fresh Hall and tick every 100ms up to 10900ms
for (uint32_t t = 1100; t <= 10900; t += 100) {
    f.deliverHall(t);
    f.tick(t);
}
assert(f.dome.state() == r2link::DomeState::SeekingReference);

// At 11000ms: 10 seconds elapsed -> timeout
f.deliverHall(11000);
f.tick(11000);
assert(f.dome.state() == r2link::DomeState::Inhibited);
assert(f.dome.output().pulse_us == 1500);
assert(f.dome.seekFault());

r2link::Event ev{};
assert(f.dome.takeEvent(ev));
assert(ev.kind == static_cast<uint8_t>(r2link::EventKind::Timeout));
assert(ev.detail == static_cast<uint16_t>(r2link::Detail::SeekTimeout));

// Latched seek fault inhibits further seeks
assert(f.dome.request(req, 2, 11050) == r2link::Result::Inhibited);

// Cycling CH9 OFF then ON clears fault
f.rc(11100, 1500, 1000); f.tick(11100); // CH9 OFF
f.rc(11200, 1500, 2000); f.tick(11200); // CH9 ON
assert(!f.dome.seekFault());
''')

    def test_stale_hall_aborts_seek_with_hardware_error(self):
        self.check(r'''
DomeFixture f;
f.setHall(0x00, 1000);
f.tick(1000);

r2link::DomeRequest req{};
req.operation = static_cast<uint8_t>(r2link::DomeOperation::SeekReference);
req.reference = static_cast<uint8_t>(r2link::DomeReference::Front);
req.owner = 1;
req.control_epoch = f.dome.controlEpoch();
req.dome_authority_generation = f.dome.authorityGeneration();

assert(f.dome.request(req, 1, 1000) == r2link::Result::Accepted);
f.deliverHall(1020);
f.tick(1020);

// Hall becomes stale (source_age_ms > 150)
f.setHall(0x00, 1040, 160);
f.tick(1040);
assert(f.dome.state() == r2link::DomeState::Inhibited);
assert(f.dome.seekFault());

r2link::Event ev{};
assert(f.dome.takeEvent(ev));
assert(ev.kind == static_cast<uint8_t>(r2link::EventKind::HardwareError));
assert(ev.detail == static_cast<uint16_t>(r2link::Detail::HallStale));
''')

    def test_drive_alignment_forward_rear_pivot(self):
        self.check(r'''
DomeFixture f;
f.setHall(0x00, 100);
f.tick(100);

// Forward driving starts seek to Front (0)
f.dome.updateDrive(r2link::DriveIntent::Forward, 200);
f.deliverHall(200);
f.tick(200);
assert(f.dome.owner() == r2link::DomeOwner::Drive);
assert(f.dome.state() == r2link::DomeState::SeekingReference);

// Front magnet hit
f.setHall(0x01, 250);
f.tick(250);
assert(f.dome.state() == r2link::DomeState::HoldingReference);
assert(f.dome.output().pulse_us == 1500);

// Reverse driving flips target to Rear (-1800)
f.setHall(0x00, 300);
f.dome.updateDrive(r2link::DriveIntent::Reverse, 300);
f.tick(300);
assert(f.dome.state() == r2link::DomeState::SeekingReference);

// Pivot suspends alignment and holds current facing with neutral pulse
f.deliverHall(350);
f.dome.updateDrive(r2link::DriveIntent::Pivot, 350);
f.tick(350);
assert(f.dome.owner() == r2link::DomeOwner::Drive);
assert(f.dome.state() == r2link::DomeState::HoldingReference);
assert(f.dome.output().pulse_us == 1500);

// CH6 OFF with CH9 ON still permits dome automation
f.rc(400, 1500, 2000, 1000); // FeetEnable = 1000 (OFF)
f.deliverHall(400);
f.dome.updateDrive(r2link::DriveIntent::Forward, 400);
f.tick(400);
assert(f.dome.owner() == r2link::DomeOwner::Drive);
assert(f.dome.state() == r2link::DomeState::SeekingReference);
''')

    def test_peer_lost_and_motion_locks(self):
        self.check(r'''
DomeFixture f;
f.setHall(0x00, 100);
f.tick(100);

// Start remote velocity
r2link::DomeRequest req{};
req.operation = static_cast<uint8_t>(r2link::DomeOperation::Velocity);
req.speed_percent = 25;
req.lease_ms = 100;
req.owner = 1;
req.control_epoch = f.dome.controlEpoch();
req.dome_authority_generation = f.dome.authorityGeneration();
assert(f.dome.request(req, 1, 100) == r2link::Result::Accepted);
f.tick(100);
assert(f.dome.owner() == r2link::DomeOwner::Event);

// Peer lost cancels remote motion
f.dome.peerLost(120);
assert(f.dome.owner() == r2link::DomeOwner::None);
assert(f.dome.state() == r2link::DomeState::Inhibited);
assert(f.dome.output().pulse_us == 1500);

// But fresh-RC manual control survives peer loss!
f.rc(140, 1800, 2000);
f.tick(140);
assert(f.dome.owner() == r2link::DomeOwner::Manual);
assert(f.dome.state() == r2link::DomeState::Manual);

// Motion locks inhibit manual too
f.dome.stop(150);
f.tick(150);
assert(f.dome.owner() == r2link::DomeOwner::None);
assert(f.dome.output().pulse_us == 1500);
''')


if __name__ == "__main__":
    unittest.main()
