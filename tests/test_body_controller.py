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


class BodyControllerTests(unittest.TestCase):
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

    // 7. Right token unlock succeeds and clears maintenance lock
    r2link::ControlRequest right_unlock{};
    right_unlock.operation = 3; // UNLOCK
    right_unlock.reason = r2link::kReasonMaintenance;
    right_unlock.token = 0x1234;
    right_unlock.control_epoch = epoch_before_unlock;
    r2link::Frame rightUnlockFrame{};
    assert(r2link::encode(right_unlock, rightUnlockFrame, err) == r2link::Status::Ok);
    assert(controller.handle(rightUnlockFrame, now + 120) == r2link::Result::Accepted);
    assert(controller.status().control_epoch == uint16_t(epoch_before_unlock + 1));

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

    // 6. RECOVER_LOCKS with centered >= 500ms accepted!
    rc.sample_ms = 900;
    controller.updateRc(rc, 900); // 500ms centered elapsed (400 to 900)
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
