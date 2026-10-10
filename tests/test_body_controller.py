"""Task 8 tests: Integrate the Teensy scheduler, locks and diagnostics."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
INC = ROOT / "TEENSY_BODY_CONTROLLER/include"
SHARED = ROOT / "shared/R2BodyLink"

SOURCES = [
    BODY / "body/BodyController.cpp",
    BODY / "body/DriveController.cpp",
    BODY / "body/DomePosition.cpp",
    BODY / "body/DomeController.cpp",
    BODY / "body/DfPlayer.cpp",
    BODY / "body/DomeCalibration.cpp",
    BODY / "body/WheelTest.cpp",
    BODY / "body/VescLink.cpp",
    BODY / "body/ConfigStore.cpp",
    BODY / "body/IbusTelemetry.cpp",
    BODY / "body/LinkBootstrap.cpp",
    SHARED / "src/Endpoint.cpp",
    SHARED / "src/Codec.cpp",
]

PRELUDE = r'''
#include <cassert>
#include <vector>
#include <deque>
#include <cstring>
#include <iostream>
#include "BytePort.h"
#include "Messages.h"
#include "Endpoint.h"
#include "TrackCatalog.h"
#include "body/ConfigStore.h"
#include "body/BodyController.h"

struct FakePort : public r2link::BytePort {
    std::vector<uint8_t> tx_bytes;
    std::deque<uint8_t> rx_bytes;

    int read() override {
        if (rx_bytes.empty()) return -1;
        uint8_t b = rx_bytes.front();
        rx_bytes.pop_front();
        return b;
    }
    size_t writable() const override { return 1024; }
    size_t write(const uint8_t* src, size_t len) override {
        tx_bytes.insert(tx_bytes.end(), src, src + len);
        return len;
    }
};

struct DummyStorage : public body::RawStorage {
    uint8_t mem[512];
    DummyStorage() { std::memset(mem, 0xFF, sizeof(mem)); }
    size_t size() const override { return sizeof(mem); }
    body::StorageResult read(size_t address, uint8_t* destination, size_t length) override {
        if (address + length > sizeof(mem)) return body::StorageResult::OutOfRange;
        std::memcpy(destination, mem + address, length);
        return body::StorageResult::Ok;
    }
    body::StorageResult write(size_t address, const uint8_t* source, size_t length) override {
        if (address + length > sizeof(mem)) return body::StorageResult::OutOfRange;
        std::memcpy(mem + address, source, length);
        return body::StorageResult::Ok;
    }
};
'''


LOCK_RIG = r'''
// Ticks the real scheduler with a fresh RC frame every millisecond, like main.cpp.
struct LockRig {
    DummyStorage storage;
    body::ConfigStore config_store{storage};
    FakePort link_port, left_vesc_port, right_vesc_port, audio_port;
    body::BodyController controller{config_store, link_port, left_vesc_port, right_vesc_port, audio_port};
    uint32_t now = 1000;
    // Optional dome-side endpoint: while it is linked the body sees a live link,
    // so commissioning runs are not cancelled as link loss.
    FakePort peer_port;
    r2link::Endpoint peer{peer_port, r2link::kRoleDome, 0xD0D0};
    bool linked = false;
    LockRig() {
        controller.init(now);
        // Manual dome commissioned so a released dome can prove it moves again.
        body::CommissioningProfile& p = controller.profile();
        assert(body::setField(p, body::kFieldServoNeutral, 0, 1500) == body::FieldResult::Ok);
        assert(body::setField(p, body::kFieldServoMin, 0, 1000) == body::FieldResult::Ok);
        assert(body::setField(p, body::kFieldServoMax, 0, 2000) == body::FieldResult::Ok);
        p.acceptance |= 1u << body::kAcceptServoNeutral;
        assert(body::readiness(p).manual_dome);
        save();
    }
    void save() {
        char buf[256];
        assert(controller.processCli("profile save", buf, sizeof buf, now));
        assert(std::strstr(buf, "OK saved") != nullptr);
    }
    void run(uint32_t ms, uint16_t ch4 = 1500, uint16_t ch6 = 1000) {
        for (uint32_t i = 0; i < ms; ++i) {
            ++now;
            body::RcSnapshot rc{};
            for (auto& c : rc.channels) c = 1500;
            rc.channels[3] = ch4; rc.channels[5] = ch6; rc.channels[8] = 1000;
            rc.valid = true; rc.sample_ms = now; rc.sample_counter = now; rc.flags = 1;
            controller.updateRc(rc, now);
            if (linked) pumpLink();
            controller.tick(now, now * 1000);
        }
    }
    void pumpLink() {
        peer_port.rx_bytes.insert(peer_port.rx_bytes.end(), link_port.tx_bytes.begin(), link_port.tx_bytes.end());
        link_port.tx_bytes.clear();
        peer.tick(now);
        r2link::Frame fr{}; uint32_t ms = 0;
        while (peer.takeReceived(fr, ms)) peer.reply(fr, r2link::Result::Accepted, 0);
        link_port.rx_bytes.insert(link_port.rx_bytes.end(), peer_port.tx_bytes.begin(), peer_port.tx_bytes.end());
        peer_port.tx_bytes.clear();
    }
    void connect() {
        linked = true;
        for (int i = 0; i < 100 && !(controller.linkEndpoint().connected(now) && peer.connected(now)); ++i) run(10);
        assert(controller.linkEndpoint().connected(now) && peer.connected(now));
    }
    r2link::Result control(uint8_t op, uint8_t reason, uint16_t token = 0) {
        r2link::ControlRequest q{};
        q.operation = op; q.reason = reason; q.token = token;
        q.control_epoch = controller.status().control_epoch;
        r2link::Frame f{}; r2link::ErrorCounters e{};
        assert(r2link::encode(q, f, e) == r2link::Status::Ok);
        return controller.handle(f, now);
    }
    bool driveLocked() const { return controller.drive().driveState() == r2link::DriveState::Locked; }
};

// Drive fields for both wheels with the VESC acceptance bits injected (test-only,
// not a commissioning observation). FW 6.2, brake 1500mA.
static void readyDrive(body::CommissioningProfile& p) {
    assert(body::setField(p, body::kFieldSlew, 0, 500) == body::FieldResult::Ok);
    for (uint8_t w = 0; w < 2; ++w) {
        const int32_t v[] = {1, 6, 2, 1, 1000, 1000, 0, 1500, 1050, 1500, 150, 1500, 100, 50};
        for (uint8_t id = 5; id <= 18; ++id) assert(body::setField(p, id, w, v[id - 5]) == body::FieldResult::Ok);
        p.acceptance |= (1u << (body::kAcceptVescConfig + w)) | (1u << (body::kAcceptTimeoutBrake + w)) |
                        (1u << (body::kAcceptDirection + w)) | (1u << (body::kAcceptReversal + w));
    }
    assert(body::readiness(p).drive);
}

// Independent VESC framing (XMODEM CRC), not the production codec.
static std::vector<uint8_t> vescFrame(const std::vector<uint8_t>& payload) {
    uint16_t c = 0;
    for (uint8_t x : payload) {
        c ^= uint16_t(x) << 8;
        for (int i = 0; i < 8; ++i) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
    }
    std::vector<uint8_t> f{2, uint8_t(payload.size())};
    f.insert(f.end(), payload.begin(), payload.end());
    f.push_back(uint8_t(c >> 8)); f.push_back(uint8_t(c)); f.push_back(3);
    return f;
}
'''


class BodyControllerTests(unittest.TestCase):
    def run_lock_rig(self, body):
        program = PRELUDE + LOCK_RIG + "int main() {\n" + body + "\nreturn 0;\n}\n"
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_release_stop_clears_drive_and_dome_latches(self):
        self.run_lock_rig(r'''
LockRig g; g.run(1000);
assert(g.control(0, r2link::kReasonOperator) == r2link::Result::Accepted);
g.run(600);
assert(g.controller.drive().stopLatched() && g.driveLocked());
assert(g.control(1, r2link::kReasonOperator) == r2link::Result::Accepted);
g.run(20);
assert(!g.controller.drive().stopLatched());
assert(!g.driveLocked());
assert(g.controller.status().lock_reasons == 0);
g.run(50, 1900);  // the released dome follows the manual stick again
assert(g.controller.dome().state() == r2link::DomeState::Manual);
''')

    def test_recover_after_repeated_stop_clears_drive_and_dome_latches(self):
        self.run_lock_rig(r'''
LockRig g; g.run(1000);
assert(g.control(0, r2link::kReasonOperator) == r2link::Result::Accepted);
g.run(10);
assert(g.control(0, r2link::kReasonOperator) == r2link::Result::Accepted);  // STOP pressed twice
g.run(600);
assert(g.control(4, r2link::kReasonOperator) == r2link::Result::Accepted);
g.run(20);
assert(!g.controller.drive().stopLatched());
assert(!g.driveLocked());
assert(g.controller.status().lock_reasons == 0);
g.run(50, 1900);
assert(g.controller.dome().state() == r2link::DomeState::Manual);
''')

    def test_unlock_refused_without_release_gate_keeps_state_consistent(self):
        self.run_lock_rig(r'''
LockRig g; g.run(100, 1900);  // dome stick held off-centre: release gate closed
assert(g.control(2, r2link::kReasonOperator) == r2link::Result::Accepted);
g.run(50, 1900);
const uint16_t epoch = g.controller.status().control_epoch;
assert(g.control(3, r2link::kReasonOperator) == r2link::Result::NotReady);
g.run(600);  // centred long enough; the earlier refusal must not have unlocked anything
assert(g.controller.status().lock_reasons == 1);
assert(g.driveLocked());
assert(g.controller.status().control_epoch == epoch);
assert(g.control(3, r2link::kReasonOperator) == r2link::Result::Accepted);
g.run(20);
assert(g.controller.status().lock_reasons == 0);
assert(!g.driveLocked());
''')

    def test_maintenance_unlock_refused_until_release_gate(self):
        self.run_lock_rig(r'''
LockRig g; g.run(1000);
assert(g.control(2, r2link::kReasonMaintenance, 0x1234) == r2link::Result::Accepted);
g.run(50, 1500, 2000);  // CH6 ON closes the gate
assert(g.control(3, r2link::kReasonMaintenance, 0x1234) == r2link::Result::NotReady);
assert(g.controller.status().lock_reasons == 4);
g.run(600);
assert(g.control(3, r2link::kReasonMaintenance, 0x1234) == r2link::Result::Accepted);
g.run(20);
assert(g.controller.status().lock_reasons == 0);
assert(!g.driveLocked());
''')

    def test_saved_profile_reaches_actuators_without_reboot_and_unsaved_edits_do_not(self):
        self.run_lock_rig(r'''
LockRig g; g.run(10);
readyDrive(g.controller.profile());
g.run(30);
assert(g.controller.drive().commands().left.brake_mA == 0);   // staged only: actuators untouched
g.save();
g.run(30);
assert(g.controller.drive().commands().left.brake_mA == 1500);
// The VESC link now runs the saved profile: a matching FW reply is accepted, no reboot.
const std::vector<uint8_t> fw = vescFrame({0, 6, 2});
g.left_vesc_port.rx_bytes.insert(g.left_vesc_port.rx_bytes.end(), fw.begin(), fw.end());
g.run(5);
assert(g.controller.leftVesc().sample(g.now).profile_match);
// A later unsaved edit does not change what the actuators use.
assert(body::setField(g.controller.profile(), body::kFieldBrakeMa, 0, 2500) == body::FieldResult::Ok);
g.run(30);
assert(g.controller.drive().commands().left.brake_mA == 1500);
''')

    def test_wireless_save_activates_profile(self):
        self.run_lock_rig(r'''
LockRig g; g.run(10);
readyDrive(g.controller.profile());
g.run(30);
assert(g.controller.drive().commands().left.brake_mA == 0);
r2link::CommissionRequest save{};
save.operation = 5; save.control_epoch = g.controller.status().control_epoch;
r2link::Frame f{}; r2link::ErrorCounters e{};
assert(r2link::encode(save, f, e) == r2link::Status::Ok);
assert(g.controller.handle(f, g.now) == r2link::Result::Accepted);
g.run(30);
assert(g.controller.drive().commands().left.brake_mA == 1500);
const std::vector<uint8_t> fw = vescFrame({0, 6, 2});
g.right_vesc_port.rx_bytes.insert(g.right_vesc_port.rx_bytes.end(), fw.begin(), fw.end());
g.run(5);
assert(g.controller.rightVesc().sample(g.now).profile_match);
''')

    def test_cli_accepts_vesc_bits_behind_the_stationary_gate(self):
        self.run_lock_rig(r'''
LockRig g; g.run(10);
body::CommissioningProfile& p = g.controller.profile();
readyDrive(p);
p.acceptance &= ~(1u << body::kAcceptVescConfig);   // left VESC config not yet signed off
char buf[256];
g.run(10, 1500, 2000);  // CH6 ON
assert(g.controller.processCli("profile accept vesc_config_left", buf, sizeof buf, g.now));
assert(std::strstr(buf, "ERROR") != nullptr);
assert(!(p.acceptance & (1u << body::kAcceptVescConfig)));
g.run(10);
assert(g.controller.processCli("profile accept vesc_config_left", buf, sizeof buf, g.now));
assert(std::strstr(buf, "OK accepted vesc_config_left") != nullptr);
assert(p.acceptance & (1u << body::kAcceptVescConfig));
assert(g.controller.processCli("profile accept no_such_bit", buf, sizeof buf, g.now));
assert(std::strstr(buf, "ERROR unknown") != nullptr);
''')

    def test_wheel_test_drives_only_the_tested_vesc_with_low_duty(self):
        self.run_lock_rig(r'''
LockRig g; g.run(10);
body::CommissioningProfile& p = g.controller.profile();
readyDrive(p);
p.acceptance &= ~((0x3u << body::kAcceptTimeoutBrake) | (0x3u << body::kAcceptDirection) | (0x3u << body::kAcceptReversal));
g.save();                                              // vesc_config only, saved
// Scripted VESC replies: firmware 6.2 and a GET_VALUES reply on demand.
auto values = [](int32_t erpm) {
    std::vector<uint8_t> v(54, 0); v[0] = 4; v[27] = 0; v[28] = 128;
    v[23] = uint8_t(erpm >> 24); v[24] = uint8_t(erpm >> 16); v[25] = uint8_t(erpm >> 8); v[26] = uint8_t(erpm);
    return vescFrame(v);
};
auto feed = [&](int rounds) {
    for (int i = 0; i < rounds; ++i) {
        for (FakePort* port : {&g.left_vesc_port, &g.right_vesc_port}) {
            const auto fw = vescFrame({0, 6, 2}); const auto vals = values(0);
            port->rx_bytes.insert(port->rx_bytes.end(), fw.begin(), fw.end());
            port->rx_bytes.insert(port->rx_bytes.end(), vals.begin(), vals.end());
        }
        g.run(20);
    }
};
feed(30);
assert(g.controller.leftVesc().commissioningReady(g.now));
r2link::CommissionRequest begin{}; begin.operation = 1; begin.test = 7; begin.wheel = 0; begin.value = 1;
begin.run_id = 77; begin.control_epoch = g.controller.status().control_epoch;
r2link::Frame f{}; r2link::ErrorCounters e{};
auto duties = [](const std::vector<uint8_t>& tx, int16_t& max_permille) {
    int n = 0; max_permille = 0;
    for (size_t i = 0; i + 9 < tx.size(); ++i)
        if (tx[i] == 2 && tx[i + 1] == 5 && tx[i + 2] == 5) {
            const int32_t v = int32_t(uint32_t(tx[i+3]) << 24 | uint32_t(tx[i+4]) << 16 | uint32_t(tx[i+5]) << 8 | tx[i+6]);
            if (v / 100 > max_permille) max_permille = int16_t(v / 100);
            ++n;
        }
    return n;
};
int16_t left_max = 0, right_max = 0;
// No live link: the run is cancelled on the first pass and the wheel only brakes out.
assert(r2link::encode(begin, f, e) == r2link::Status::Ok);
g.left_vesc_port.tx_bytes.clear(); g.right_vesc_port.tx_bytes.clear();
assert(g.controller.handle(f, g.now) == r2link::Result::Accepted);
g.run(50);
assert(g.controller.calibration().status().state == uint8_t(body::CommissionState::Cancelled));
assert(duties(g.left_vesc_port.tx_bytes, left_max) == 0 && duties(g.right_vesc_port.tx_bytes, right_max) == 0);
g.connect();
feed(30);                                              // brake-out finished, telemetry fresh
assert(!g.controller.calibration().wheelTestBusy());
assert(g.controller.leftVesc().commissioningReady(g.now));
begin.run_id = 78; begin.control_epoch = g.controller.status().control_epoch;
assert(r2link::encode(begin, f, e) == r2link::Status::Ok);
g.left_vesc_port.tx_bytes.clear(); g.right_vesc_port.tx_bytes.clear();
assert(g.controller.handle(f, g.now) == r2link::Result::Accepted);
g.run(50);
assert(duties(g.left_vesc_port.tx_bytes, left_max) > 0 && left_max == 100);
assert(duties(g.right_vesc_port.tx_bytes, right_max) == 0);
''')

    def test_boot_initialises_the_real_dfplayer(self):
        self.run_lock_rig(r'''
LockRig g; g.run(200);
auto sent = [&](uint8_t cmd, int param_l) {
    const std::vector<uint8_t>& tx = g.audio_port.tx_bytes;
    for (size_t i = 0; i + 9 < tx.size(); ++i)
        if (tx[i] == 0x7E && tx[i + 3] == cmd && (param_l < 0 || tx[i + 6] == param_l)) return true;
    return false;
};
assert(sent(0x0C, -1));                                   // reset at boot
assert(g.controller.audio().status(g.now).state == uint8_t(r2link::AudioState::Offline));
uint8_t online[10];
body::DfPlayer::serializePacket(0x3F, 0, 0, 2, online);  // player reports SD card online
g.audio_port.rx_bytes.insert(g.audio_port.rx_bytes.end(), online, online + 10);
g.run(400);
assert(g.controller.audio().status(g.now).state == uint8_t(r2link::AudioState::Idle));
assert(sent(0x06, 10));                                   // configured volume, not the player default
''')

    def test_body_status_stays_encodable_after_dome_angle_is_lost(self):
        self.run_lock_rig(r'''
LockRig g; g.run(100);
r2link::HallState rear{0x03, 0x02, 1, 0};
r2link::Frame hall{}; r2link::ErrorCounters e{};
assert(r2link::encode(rear, hall, e) == r2link::Status::Ok);
assert(g.controller.handle(hall, g.now) == r2link::Result::Accepted);
g.run(5);
assert(g.controller.status().angle_valid == 1);
assert(g.controller.status().estimated_angle_ddeg == -1800);
g.run(50, 1900);  // manual stick invalidates dead reckoning
r2link::BodyStatus s = g.controller.status();
assert(s.angle_valid == 0);
assert(s.estimated_angle_ddeg == 0);
r2link::Frame out{};
assert(r2link::encode(s, out, e) == r2link::Status::Ok);
''')

    def test_stop_maintenance_lock_lifecycle_and_stale_epoch(self):
        program = PRELUDE + r'''
int main() {
    DummyStorage storage;
    body::ConfigStore config_store(storage);
    FakePort link_port, left_vesc_port, right_vesc_port, audio_port;

    body::BodyController controller(config_store, link_port, left_vesc_port, right_vesc_port, audio_port);
    controller.init(100);

    const uint32_t now = 200;
    const uint16_t oldEpoch = controller.status().control_epoch;

    // 1. Build stopFrame carrying oldEpoch
    r2link::ControlRequest stop_req{};
    stop_req.operation = 0; // STOP_ALL
    stop_req.reason = r2link::kReasonOperator;
    stop_req.token = 0;
    stop_req.control_epoch = oldEpoch;
    r2link::Frame stopFrame{};
    r2link::ErrorCounters err{};
    assert(r2link::encode(stop_req, stopFrame, err) == r2link::Status::Ok);

    // 2. Build staleVelocityFrame carrying oldEpoch
    r2link::DomeRequest stale_vel{};
    stale_vel.operation = 1; // velocity
    stale_vel.speed_percent = 20;
    stale_vel.lease_ms = 100;
    stale_vel.control_epoch = oldEpoch;
    stale_vel.owner = 1; // event
    stale_vel.reference = 0;
    stale_vel.dome_authority_generation = controller.status().dome_authority_generation;
    r2link::Frame staleVelocityFrame{};
    assert(r2link::encode(stale_vel, staleVelocityFrame, err) == r2link::Status::Ok);

    // Execute exact assertion mandated by plan
    assert(controller.handle(stopFrame, now) == r2link::Result::Accepted);
    assert(controller.status().control_epoch == uint16_t(oldEpoch + 1));
    assert(controller.motionLocked());
    assert(controller.handle(staleVelocityFrame, now) == r2link::Result::WrongEpoch);

    // 3. Reject reserved Faint lock reason 1
    r2link::Frame faintFrame{};
    faintFrame.type = r2link::MessageType::ControlRequest;
    faintFrame.sequence = 123;
    faintFrame.length = 6;
    faintFrame.payload[0] = 2; // LOCK
    faintFrame.payload[1] = r2link::kReasonReserved; // 1 (reserved)
    faintFrame.payload[2] = 0; // token H
    faintFrame.payload[3] = 10; // token L
    const uint16_t cur_epoch = controller.status().control_epoch;
    faintFrame.payload[4] = static_cast<uint8_t>(cur_epoch >> 8);
    faintFrame.payload[5] = static_cast<uint8_t>(cur_epoch & 0xFF);
    assert(controller.handle(faintFrame, now) != r2link::Result::Accepted);

    // 4. Maintenance lock with token 0x1234
    const uint16_t epoch_before_lock = controller.status().control_epoch;
    r2link::ControlRequest lock_req{};
    lock_req.operation = 2; // LOCK
    lock_req.reason = r2link::kReasonMaintenance; // 2
    lock_req.token = 0x1234;
    lock_req.control_epoch = epoch_before_lock;
    r2link::Frame lockFrame{};
    assert(r2link::encode(lock_req, lockFrame, err) == r2link::Status::Ok);
    assert(controller.handle(lockFrame, now) == r2link::Result::Accepted);
    assert(controller.status().control_epoch == uint16_t(epoch_before_lock + 1));
    assert(controller.motionLocked());

    // 5. Matching lock survives simulated disconnect
    controller.onLinkDisconnected(now + 100);
    assert(controller.motionLocked());

    // 6. Wrong token unlock is rejected
    const uint16_t epoch_before_unlock = controller.status().control_epoch;
    r2link::ControlRequest wrong_unlock{};
    wrong_unlock.operation = 3; // UNLOCK
    wrong_unlock.reason = r2link::kReasonMaintenance;
    wrong_unlock.token = 0x9999;
    wrong_unlock.control_epoch = epoch_before_unlock;
    r2link::Frame wrongUnlockFrame{};
    assert(r2link::encode(wrong_unlock, wrongUnlockFrame, err) == r2link::Status::Ok);
    assert(controller.handle(wrongUnlockFrame, now + 110) == r2link::Result::Inhibited);

    // 7. Right token unlock succeeds once the local release gate has been observed
    //    (fresh RC, CH6 OFF, sticks centred 500ms) by the running scheduler.
    uint32_t t = now + 110;
    for (; t <= now + 720; ++t) {
        body::RcSnapshot rc{};
        rc.valid = true; rc.sample_ms = t;
        rc.channels[0] = rc.channels[1] = rc.channels[3] = 1500;
        rc.channels[5] = 1000; rc.channels[8] = 1000;
        controller.updateRc(rc, t);
        controller.tick(t, t * 1000);
    }
    r2link::ControlRequest right_unlock{};
    right_unlock.operation = 3; // UNLOCK
    right_unlock.reason = r2link::kReasonMaintenance;
    right_unlock.token = 0x1234;
    right_unlock.control_epoch = epoch_before_unlock;
    r2link::Frame rightUnlockFrame{};
    assert(r2link::encode(right_unlock, rightUnlockFrame, err) == r2link::Status::Ok);
    assert(controller.handle(rightUnlockFrame, t) == r2link::Result::Accepted);
    assert(controller.status().control_epoch == uint16_t(epoch_before_unlock + 1));
    assert(controller.status().lock_reasons == 1);  // the earlier STOP is still latched

    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_recover_locks_guards(self):
        program = PRELUDE + r'''
int main() {
    DummyStorage storage;
    body::ConfigStore config_store(storage);
    FakePort link_port, left_vesc_port, right_vesc_port, audio_port;

    body::BodyController controller(config_store, link_port, left_vesc_port, right_vesc_port, audio_port);
    controller.init(100);

    // Apply STOP lock
    r2link::ControlRequest stop_req{};
    stop_req.operation = 0; // STOP_ALL
    stop_req.reason = r2link::kReasonOperator;
    stop_req.token = 0;
    stop_req.control_epoch = controller.status().control_epoch;
    r2link::Frame stopFrame{};
    r2link::ErrorCounters err{};
    assert(r2link::encode(stop_req, stopFrame, err) == r2link::Status::Ok);
    assert(controller.handle(stopFrame, 200) == r2link::Result::Accepted);
    assert(controller.motionLocked());

    // 1. RECOVER_LOCKS with wrong reason rejected
    r2link::ControlRequest rec_bad_reason{};
    rec_bad_reason.operation = 4; // RECOVER_LOCKS
    rec_bad_reason.reason = r2link::kReasonMaintenance; // not operator!
    rec_bad_reason.token = 0;
    rec_bad_reason.control_epoch = controller.status().control_epoch;
    r2link::Frame recBadReasonFrame{};
    assert(r2link::encode(rec_bad_reason, recBadReasonFrame, err) == r2link::Status::Ok);
    assert(controller.handle(recBadReasonFrame, 300) != r2link::Result::Accepted);

    // 2. RECOVER_LOCKS with non-zero token rejected
    r2link::ControlRequest rec_bad_tok{};
    rec_bad_tok.operation = 4; // RECOVER_LOCKS
    rec_bad_tok.reason = r2link::kReasonOperator;
    rec_bad_tok.token = 123; // must be 0!
    rec_bad_tok.control_epoch = controller.status().control_epoch;
    r2link::Frame recBadTokFrame{};
    assert(r2link::encode(rec_bad_tok, recBadTokFrame, err) == r2link::Status::Ok);
    assert(controller.handle(recBadTokFrame, 300) != r2link::Result::Accepted);

    // 3. RECOVER_LOCKS with stale RC rejected
    body::RcSnapshot rc{};
    rc.valid = true;
    rc.sample_ms = 100; // > 250ms old at 400
    rc.channels[0] = rc.channels[1] = rc.channels[3] = 1500; // centered
    rc.channels[5] = 1000; // CH6 OFF
    rc.channels[8] = 1000; // CH9 OFF
    controller.updateRc(rc, 100);

    r2link::ControlRequest rec{};
    rec.operation = 4; // RECOVER_LOCKS
    rec.reason = r2link::kReasonOperator;
    rec.token = 0;
    rec.control_epoch = controller.status().control_epoch;
    r2link::Frame recFrame{};
    assert(r2link::encode(rec, recFrame, err) == r2link::Status::Ok);
    assert(controller.handle(recFrame, 400) == r2link::Result::Inhibited);

    // 4. RECOVER_LOCKS with CH6 ON rejected
    rc.sample_ms = 400;
    rc.channels[5] = 1800; // CH6 ON!
    controller.updateRc(rc, 400);
    assert(controller.handle(recFrame, 400) == r2link::Result::Inhibited);

    // 5. RECOVER_LOCKS with centered < 500ms rejected
    rc.channels[5] = 1000; // CH6 OFF
    rc.sample_ms = 400;
    controller.updateRc(rc, 400); // centered starts at 400
    assert(controller.handle(recFrame, 500) == r2link::Result::Inhibited); // only 100ms centered

    // 6. RECOVER_LOCKS with centered >= 500ms accepted once the scheduler has run it.
    for (uint32_t t = 400; t <= 900; ++t) {
        rc.sample_ms = t;
        controller.updateRc(rc, t); // 500ms centered elapsed (400 to 900)
        controller.tick(t, t * 1000);
    }
    const uint16_t epoch_before_rec = controller.status().control_epoch;
    rec.control_epoch = epoch_before_rec;
    assert(r2link::encode(rec, recFrame, err) == r2link::Status::Ok);
    assert(controller.handle(recFrame, 900) == r2link::Result::Accepted);
    assert(!controller.motionLocked());
    assert(controller.status().control_epoch == uint16_t(epoch_before_rec + 1));

    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cli_commands(self):
        program = PRELUDE + r'''
int main() {
    DummyStorage storage;
    body::ConfigStore config_store(storage);
    FakePort link_port, left_vesc_port, right_vesc_port, audio_port;

    body::BodyController controller(config_store, link_port, left_vesc_port, right_vesc_port, audio_port);
    controller.init(100);

    body::RcSnapshot rc{};
    rc.valid = true;
    rc.sample_ms = 100;
    rc.channels[0] = rc.channels[1] = rc.channels[3] = 1500;
    rc.channels[5] = 1000; // CH6 OFF
    rc.channels[8] = 1000; // CH9 OFF
    controller.updateRc(rc, 100);

    char buf[256];

    // status command
    assert(controller.processCli("status", buf, sizeof(buf), 100));
    assert(std::strstr(buf, "STATUS") != nullptr);

    // rc command
    assert(controller.processCli("rc", buf, sizeof(buf), 100));
    assert(std::strstr(buf, "RC valid=1") != nullptr);

    // vesc command
    assert(controller.processCli("vesc", buf, sizeof(buf), 100));
    assert(std::strstr(buf, "VESC L:") != nullptr);

    // profile show
    assert(controller.processCli("profile show", buf, sizeof(buf), 100));
    assert(std::strstr(buf, "PROFILE") != nullptr);

    // profile set servo_neutral 1510
    assert(controller.processCli("profile set servo_neutral 1510", buf, sizeof(buf), 100));
    assert(std::strstr(buf, "OK set") != nullptr);
    assert(controller.profile().servo_neutral == 1510);

    // profile set rejected when CH6 is ON
    rc.channels[5] = 1800; // CH6 ON
    controller.updateRc(rc, 100);
    assert(controller.processCli("profile set servo_neutral 1520", buf, sizeof(buf), 100));
    assert(std::strstr(buf, "ERROR gate closed") != nullptr);
    assert(controller.profile().servo_neutral == 1510); // unchanged

    // stop command
    assert(controller.processCli("stop", buf, sizeof(buf), 100));
    assert(controller.motionLocked());

    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
