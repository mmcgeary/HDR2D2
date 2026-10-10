"""Task 12 integration tests: End-to-end host rig with BodyController and BodyClient."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
INC = ROOT / "TEENSY_BODY_CONTROLLER/include"
ASTRO = ROOT / "ASTROPIXELS_PLUS_UNIFIED"
SHARED = ROOT / "shared/R2BodyLink"
TESTS_DIR = ROOT / "tests"

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
    BODY / "body/IbusInput.cpp",
    ASTRO / "BodyClient.cpp",
    ASTRO / "RemoteAudio.cpp",
    ASTRO / "DomeBehaviour.cpp",
    SHARED / "src/Endpoint.cpp",
    SHARED / "src/Codec.cpp",
]

INCLUDE_DIRS = [
    INC,
    BODY,
    ASTRO,
    SHARED,
    TESTS_DIR,
    ROOT / "shared",
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
#include "BodyClient.h"
#include "RemoteAudio.h"
#include "DomeBehaviour.h"
#include "body_fakes.h"

using Bytes = std::vector<uint8_t>;

struct PipePort : public r2link::BytePort {
    std::deque<uint8_t> rx;
    PipePort* peer{nullptr};
    bool dropped{false};

    int read() override {
        if (rx.empty()) return -1;
        uint8_t b = rx.front();
        rx.pop_front();
        return b;
    }
    size_t writable() const override { return 1024; }
    size_t write(const uint8_t* src, size_t len) override {
        if (!peer || dropped) return len;
        peer->rx.insert(peer->rx.end(), src, src + len);
        return len;
    }
};

static uint16_t crc16(const Bytes& b) {
    uint16_t c = 0;
    for (auto x : b) {
        c ^= uint16_t(x) << 8;
        for (int i = 0; i < 8; ++i) c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
    }
    return c;
}

static Bytes wireVesc(Bytes p) {
    Bytes b{2, uint8_t(p.size())};
    b.insert(b.end(), p.begin(), p.end());
    uint16_t c = crc16(p);
    b.push_back(c >> 8);
    b.push_back(c & 0xFF);
    b.push_back(3);
    return b;
}

static void putVesc(Bytes& p, size_t at, uint32_t v, size_t n) {
    for (size_t i = 0; i < n; ++i) p[at + i] = v >> ((n - i - 1) * 8);
}

static Bytes makeVescValues(int32_t erpm = 0, uint16_t pack_dV = 120) {
    Bytes p(54, 0);
    p[0] = 4; // COMM_GET_VALUES
    putVesc(p, 1, static_cast<uint16_t>(-125), 2);
    putVesc(p, 3, 999, 2);
    putVesc(p, 5, static_cast<uint32_t>(-12345), 4);
    putVesc(p, 9, 234, 4);
    putVesc(p, 13, 0x41424344, 4);
    putVesc(p, 17, 0x51525354, 4);
    putVesc(p, 21, static_cast<uint16_t>(-350), 2);
    putVesc(p, 23, static_cast<uint32_t>(erpm), 4);
    putVesc(p, 27, pack_dV, 2);
    p[53] = 0;
    return p;
}

struct ScriptedVescPort : public r2link::BytePort {
    Bytes tx;
    std::deque<uint8_t> rx;
    int32_t erpm{0};
    uint16_t pack_dV{120}; // 120 dV = 12.0 V = 1200 cV
    bool fail{false};

    int read() override {
        if (rx.empty()) return -1;
        int b = rx.front();
        rx.pop_front();
        return b;
    }
    size_t writable() const override { return 1024; }
    size_t write(const uint8_t* b, size_t n) override {
        tx.insert(tx.end(), b, b + n);
        if (fail) return n;
        for (size_t i = 0; i + 2 < n; ++i) {
            if (b[i] == 2 && b[i + 1] == 1) {
                uint8_t cmd = b[i + 2];
                if (cmd == 0) { // COMM_FW_VERSION
                    Bytes r = wireVesc({0, 42, 19});
                    rx.insert(rx.end(), r.begin(), r.end());
                } else if (cmd == 4) { // COMM_GET_VALUES
                    Bytes r = wireVesc(makeVescValues(erpm, pack_dV));
                    rx.insert(rx.end(), r.begin(), r.end());
                }
            }
        }
        return n;
    }
};

struct ScriptedAudioPort : public r2link::BytePort {
    Bytes tx;
    std::deque<uint8_t> rx;

    int read() override {
        if (rx.empty()) return -1;
        int b = rx.front();
        rx.pop_front();
        return b;
    }
    size_t writable() const override { return 1024; }
    size_t write(const uint8_t* b, size_t n) override {
        tx.insert(tx.end(), b, b + n);
        for (size_t i = 0; i + 9 < n; ++i) {
            if (b[i] == 0x7E && b[i + 9] == 0xEF) {
                uint8_t cmd = b[i + 3];
                if (cmd == 0x0C) { // reset: a real player reports online with its storage device
                    uint8_t resp[10];
                    body::DfPlayer::serializePacket(0x3F, 0, 0, 2, resp);
                    rx.insert(rx.end(), resp, resp + 10);
                } else if (cmd == 0x0F || cmd == 0x42) {
                    uint8_t resp[10];
                    body::DfPlayer::serializePacket(0x42, 0, 0, 1, resp); // Playing
                    rx.insert(rx.end(), resp, resp + 10);
                }
            }
        }
        return n;
    }

    void finishTrack() {
        uint8_t resp[10];
        body::DfPlayer::serializePacket(0x3D, 0, 0, 0, resp); // Completed
        rx.insert(rx.end(), resp, resp + 10);
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

static body::CommissioningProfile makeTestProfile() {
    body::CommissioningProfile p;
    // Servo dome
    assert(setField(p, body::kFieldServoNeutral, 0, 1500) == body::FieldResult::Ok);
    assert(setField(p, body::kFieldServoMin, 0, 1000) == body::FieldResult::Ok);
    assert(setField(p, body::kFieldServoMax, 0, 2000) == body::FieldResult::Ok);
    assert(setField(p, body::kFieldAutoSpeed, 0, 15) == body::FieldResult::Ok);
    assert(setField(p, body::kFieldCwRate, 0, 720) == body::FieldResult::Ok);
    assert(setField(p, body::kFieldCcwRate, 0, 720) == body::FieldResult::Ok);
    assert(setField(p, body::kFieldSlew, 0, 500) == body::FieldResult::Ok);
    p.acceptance |= (1u << body::kAcceptServoNeutral) | (1u << body::kAcceptFrontRef) |
                    (1u << body::kAcceptRearRef) | (1u << body::kAcceptAutoTiming);
    // Both wheels
    for (int w = 0; w < 2; ++w) {
        const int v[] = {1, 42, 19, 1, 1000, 1000, 0, 1500, 1050, 1500, 150, 1500, 100, 50};
        for (int id = 5; id <= 18; ++id) assert(setField(p, id, w, v[id - 5]) == body::FieldResult::Ok);
        p.acceptance |= (1u << (body::kAcceptVescConfig + w)) | (1u << (body::kAcceptTimeoutBrake + w)) |
                        (1u << (body::kAcceptDirection + w)) | (1u << (body::kAcceptReversal + w));
    }
    assert(validateProfile(p));
    return p;
}

static body::RcSnapshot makeRcSnapshot(uint32_t t, uint16_t ch6 = 1000, uint16_t ch9 = 2000,
                                      uint16_t steer = 1500, uint16_t throttle = 1500,
                                      uint16_t dome = 1500) {
    body::RcSnapshot rc{};
    rc.valid = true;
    rc.sample_ms = t;
    rc.sample_counter = 1;
    for (int i = 0; i < 10; ++i) rc.channels[i] = 1500;
    rc.channels[0] = steer;
    rc.channels[1] = throttle;
    rc.channels[3] = dome;
    rc.channels[5] = ch6;
    rc.channels[8] = ch9;
    // Same flag bits IbusInput::snapshot() sets on a valid frame.
    rc.flags = 1 | (ch6 >= 1750 ? 2 : 0) | (ch9 >= 1750 ? 8 : 0);
    return rc;
}

struct HostRig {
    DummyStorage storage;
    body::ConfigStore config_store;
    PipePort body_slip_port;
    PipePort dome_slip_port;
    ScriptedVescPort left_vesc_port;
    ScriptedVescPort right_vesc_port;
    ScriptedAudioPort audio_port;

    body::BodyController controller;
    BodyClient client;
    uint32_t now{1000};

    HostRig()
        : storage(),
          config_store(storage),
          controller(config_store, body_slip_port, left_vesc_port, right_vesc_port, audio_port),
          client()
    {
        body_slip_port.peer = &dome_slip_port;
        dome_slip_port.peer = &body_slip_port;

        body::CommissioningProfile prof = makeTestProfile();
        assert(config_store.trySave(prof, true, true) == body::SaveResult::Ok);

        controller.init(now);
        client.begin(dome_slip_port, 0xD0D0);
    }

    void step(uint32_t dt = 20) {
        now += dt;
        client.tick(now);
        controller.tick(now, now * 1000);
        client.tick(now);
    }

    void handshake() {
        for (int i = 0; i < 20; ++i) {
            controller.updateRc(makeRcSnapshot(now, 1000, 2000), now);
            step(20);
            if (controller.linkEndpoint().connected(now) && client.endpoint().connected(now)) {
                break;
            }
        }
        assert(controller.linkEndpoint().connected(now));
        assert(client.endpoint().connected(now));
    }
};
'''


class BodyIntegrationTests(unittest.TestCase):
    def check(self, code):
        program = PRELUDE + "\nint main() {\n" + code + "\n    return 0;\n}\n"
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=INCLUDE_DIRS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_complete_lifecycle(self):
        self.check(r'''
    HostRig rig;
    rig.handshake();

    // Step 25 times to allow VESC firmware and values queries to complete and stream
    for (int i = 0; i < 25; ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 2000), rig.now);
        rig.step(20);
    }

    // 1. Dual VESC telemetry received and healthy
    auto vl = rig.client.vescStatus(0, rig.now);
    auto vr = rig.client.vescStatus(1, rig.now);
    assert(vl.valid);
    assert(vr.valid);
    assert(vl.pack_cV == 1200);
    assert(vr.pack_cV == 1200);

    // 2. Body status fresh
    auto bstat = rig.client.bodyStatus(rig.now);
    assert(bstat.fresh);
    assert(bstat.value.profile_ready == 7);  // drive | manual dome | auto dome

    // 3. Observed CH6 OFF -> ON -> 500ms neutral -> Drive arms
    for (int i = 0; i < 30; ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 2000), rig.now);
        rig.step(20);
    }
    // Flip CH6 to ON (2000)
    for (int i = 0; i < 30; ++i) { // > 500ms neutral dwell
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(rig.controller.status().drive_state == static_cast<uint8_t>(r2link::DriveState::Armed));

    // 4. Drive intent deflection
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000, 1500, 1800), rig.now); // throttle forward
    rig.step(20);
    assert(rig.controller.drive().commands().left.mode == body::WheelMode::Duty);
    assert(rig.controller.status().drive_intent == static_cast<uint8_t>(r2link::DriveIntent::Forward));

    // Return drive sticks to neutral
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000, 1500, 1500), rig.now);
    rig.step(20);

    // 5. Manual dome stick deflection
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000, 1500, 1500, 1800), rig.now); // dome CW
    rig.step(20);
    assert(rig.controller.domeOutput().pulses);
    assert(rig.controller.domeOutput().pulse_us > 1500);

    // Return dome stick to neutral
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000, 1500, 1500, 1500), rig.now);
    rig.step(20);
    assert(rig.controller.domeOutput().pulse_us == 1500);

    // 6. Home dome: BodyClient submits DomeRequest(SeekReference, Front)
    rig.client.publishHall(0x03, 0x00, 10, rig.now);
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
    rig.step(20);

    r2link::DomeRequest dreq{};
    dreq.operation = static_cast<uint8_t>(r2link::DomeOperation::SeekReference);
    dreq.reference = static_cast<uint8_t>(r2link::DomeReference::Front);
    dreq.owner = 1; // 1 = Event priority
    dreq.control_epoch = rig.controller.status().control_epoch;
    dreq.dome_authority_generation = rig.controller.status().dome_authority_generation;
    RequestHandle dh = rig.client.requestDome(dreq, rig.now);
    assert(dh.queued);
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
    rig.step(20);
    assert(rig.controller.dome().state() == r2link::DomeState::SeekingReference);

    // Publish Hall Front active (bit 0 active)
    rig.client.publishHall(0x03, 0x01, 10, rig.now);
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
    rig.step(20);
    assert(rig.controller.dome().state() != r2link::DomeState::SeekingReference);

    // Verify Completion event delivered to client
    r2link::Event ev{};
    for (int i = 0; i < 5 && !rig.client.takeEvent(ev); ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(ev.kind == static_cast<uint8_t>(r2link::EventKind::Completed));
    assert(ev.request_type == static_cast<uint8_t>(r2link::MessageType::DomeRequest));

    // 7. Leia playback: AudioRequest track 109
    r2link::AudioRequest areq{};
    areq.operation = 0; // Play
    areq.folder = 1;
    areq.track = 109;
    areq.priority = 1;
    RequestHandle ah = rig.client.requestAudio(areq, rig.now);
    assert(ah.queued);
    for (int i = 0; i < 5 && rig.controller.audio().status(rig.now).state != static_cast<uint8_t>(r2link::AudioState::Playing); ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(rig.controller.audio().status(rig.now).state == static_cast<uint8_t>(r2link::AudioState::Playing));

    // Verify PlaybackStarted event delivered to client
    for (int i = 0; i < 5 && !rig.client.takeEvent(ev); ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(ev.kind == static_cast<uint8_t>(r2link::EventKind::PlaybackStarted));
    assert(ev.request_type == static_cast<uint8_t>(r2link::MessageType::AudioRequest));

    // Finish track
    rig.audio_port.finishTrack();
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
    rig.step(20);
    assert(rig.controller.audio().status(rig.now).state == static_cast<uint8_t>(r2link::AudioState::Idle));

    // Verify Audio Completion event delivered to client
    for (int i = 0; i < 5 && !rig.client.takeEvent(ev); ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(ev.kind == static_cast<uint8_t>(r2link::EventKind::Completed));
    assert(ev.request_type == static_cast<uint8_t>(r2link::MessageType::AudioRequest));

    // 8. STOP control request
    r2link::ControlRequest sreq{};
    sreq.operation = 0; // STOP_ALL
    sreq.reason = r2link::kReasonOperator;
    sreq.control_epoch = rig.controller.status().control_epoch;
    RequestHandle sh = rig.client.requestControl(sreq, rig.now);
    assert(sh.queued);
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
    rig.step(20);
    assert(rig.controller.motionLocked());
    assert(rig.controller.status().lock_reasons & 1);

    // 9. Recovery: with CH6 OFF and sticks neutral (> 500ms dwell)
    for (int i = 0; i < 30; ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 2000), rig.now);
        rig.step(20);
    }
    r2link::ControlRequest rreq{};
    rreq.operation = 4; // RECOVER_LOCKS
    rreq.reason = r2link::kReasonOperator;
    rreq.control_epoch = rig.controller.status().control_epoch;
    RequestHandle rh = rig.client.requestControl(rreq, rig.now);
    assert(rh.queued);
    for (int i = 0; i < 5 && rig.controller.motionLocked(); ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 2000), rig.now);
        rig.step(20);
    }
    assert(!rig.controller.motionLocked());
''')

    def test_fault_injection_and_link_loss(self):
        self.check(r'''
    HostRig rig;
    rig.handshake();

    // Step 25 times to allow VESC queries to complete
    for (int i = 0; i < 25; ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 2000), rig.now);
        rig.step(20);
    }

    // 1. Arm drive with live RC
    for (int i = 0; i < 30; ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 2000), rig.now);
        rig.step(20);
    }
    for (int i = 0; i < 30; ++i) {
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(rig.controller.status().drive_state == static_cast<uint8_t>(r2link::DriveState::Armed));

    // 2. Drop link: simulate slip-ring disconnect
    rig.body_slip_port.dropped = true;
    rig.dome_slip_port.dropped = true;
    for (int i = 0; i < 60; ++i) { // 1200ms > 1000ms link timeout
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(!rig.controller.linkEndpoint().connected(rig.now));

    // Manual feet STILL respond to healthy radio despite lost dome link!
    rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000, 1500, 1800), rig.now);
    rig.step(20);
    assert(rig.controller.drive().commands().left.mode == body::WheelMode::Duty);

    // Restore link
    rig.body_slip_port.dropped = false;
    rig.dome_slip_port.dropped = false;
    rig.handshake();

    // 3. VESC failure: simulate VESC failing to reply
    rig.left_vesc_port.fail = true;
    for (int i = 0; i < 35; ++i) { // 700ms > 500ms VESC expiry
        rig.controller.updateRc(makeRcSnapshot(rig.now, 2000, 2000), rig.now);
        rig.step(20);
    }
    assert(rig.controller.status().drive_state == static_cast<uint8_t>(r2link::DriveState::Disarmed) ||
           rig.controller.status().drive_state == static_cast<uint8_t>(r2link::DriveState::Fault));

    // 4. Radio loss: stop feeding RC
    rig.left_vesc_port.fail = false;
    for (int i = 0; i < 20; ++i) { // 400ms > 250ms RC expiry
        rig.step(20);
    }
    assert(rig.controller.status().faults & (1u << 3)); // Radio lost fault
''')

    def test_idle_dome_behaviour_reaches_the_body_and_sweeps(self):
        self.check(r'''
    struct MinRandom : IDomeRandom { int32_t pick(int32_t lo, int32_t) override { return lo; } } rnd;
    HostRig rig;
    DomeBehaviour behaviour(rig.client, rnd);
    rig.handshake();
    int seeks_completed = 0;
    bool saw_velocity = false;
    int16_t min_angle = 0;
    uint32_t hall_counter = 0;
    for (int i = 0; i < 2000; ++i) {                 // 40 s, CH6 OFF, CH9 ON, sticks centred
        rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 2000), rig.now);
        rig.client.publishHall(0x03, 0x01, ++hall_counter, rig.now);   // front magnet under sensor
        rig.step(20);
        r2link::Event ev;
        while (rig.client.takeEvent(ev)) {
            if (ev.kind == uint8_t(r2link::EventKind::Completed) &&
                ev.request_type == uint8_t(r2link::MessageType::DomeRequest)) ++seeks_completed;
            behaviour.onEvent(ev);
        }
        r2link::Completion c;
        while (rig.client.takeCompletion(c))
            if (c.type == r2link::MessageType::DomeRequest)
                behaviour.onReply(c.sequence, c.outcome == r2link::Outcome::Replied
                                  ? r2link::Result(c.result) : r2link::Result::NotReady);
        behaviour.tick(rig.client.makeDomeBehaviourInput(rig.now), rig.now);
        const auto& dome = rig.controller.dome();
        saw_velocity |= dome.state() == r2link::DomeState::RemoteVelocity;
        if (dome.position().valid() && dome.position().angleDdeg() < min_angle) min_angle = dome.position().angleDdeg();
    }
    assert(!behaviour.faulted());
    assert(seeks_completed >= 1);    // idle reference seek accepted and completed by the body
    assert(saw_velocity);            // velocity leases reached the body
    assert(min_angle <= -400);       // and the dead-reckoned sweep reached its -45 deg target
''')

    def test_commission_read_returns_the_requested_field(self):
        self.check(r'''
    HostRig rig;
    rig.handshake();
    for (int i = 0; i < 25; ++i) { rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 1000), rig.now); rig.step(20); }
    r2link::CommissionRequest read{};
    read.operation = 0;                       // Read
    read.field = 1;                           // subtype 1: one profile field
    read.value = body::kFieldBrakeMa;         // which field
    read.wheel = 1;                           // right wheel
    read.control_epoch = rig.client.bodyStatus(rig.now).value.control_epoch;
    assert(rig.client.requestCommission(read, rig.now).queued);
    for (int i = 0; i < 10; ++i) { rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 1000), rig.now); rig.step(20); }
    DiagnosticsSnapshot d = rig.client.diagnostics(rig.now);
    assert(d.fresh);
    assert(d.value.subtype == 1);
    assert(d.value.field == body::kFieldBrakeMa && d.value.wheel == 1);
    assert(d.value.value == 1500);
''')

    def test_audio_request_retry_idempotence(self):
        self.check(r'''
    HostRig rig;
    rig.handshake();
    for (int i = 0; i < 25; ++i) rig.step(20);

    // 1. Submit audio request track 101
    r2link::AudioRequest areq{};
    areq.operation = 0; // Play
    areq.folder = 1;
    areq.track = 101;
    areq.priority = 1;
    RequestHandle h = rig.client.requestAudio(areq, rig.now);
    assert(h.queued);
    for (int i = 0; i < 5 && rig.controller.audio().status(rig.now).state != static_cast<uint8_t>(r2link::AudioState::Playing); ++i) {
        rig.step(20);
    }

    // Verify audio started
    assert(rig.controller.audio().status(rig.now).state == static_cast<uint8_t>(r2link::AudioState::Playing));
    size_t written_cmds = rig.audio_port.tx.size();
    assert(written_cmds > 0);

    // 2. Retransmit identical request frame (same seq) directly into body link
    r2link::Frame frame = fakes::stamp(fakes::frameOf(areq), h.sequence, 0xD0D0,
                                      rig.controller.linkEndpoint().localSession(), 0);
    Bytes raw = fakes::wireOf(frame);
    rig.body_slip_port.rx.insert(rig.body_slip_port.rx.end(), raw.begin(), raw.end());
    rig.step(20);

    // Controller acknowledges duplicate without writing second command to DFPlayer
    assert(rig.audio_port.tx.size() == written_cmds);
''')

    def test_corrupted_link_bytes_recovery(self):
        self.check(r'''
    HostRig rig;
    rig.handshake();

    // Inject corrupted noise bytes into body RX queue
    uint8_t noise[] = { 0xFF, 0x00, 0x7E, 0x12, 0x34, 0x56, 0x7E, 0xAA };
    rig.body_slip_port.rx.insert(rig.body_slip_port.rx.end(), noise, noise + sizeof(noise));

    // Send valid Heartbeat after noise
    r2link::Heartbeat hb{1, 1};
    r2link::Frame f = fakes::stamp(fakes::frameOf(hb), 999, 0xD0D0,
                                   rig.controller.linkEndpoint().localSession(), 0);
    Bytes raw = fakes::wireOf(f);
    rig.body_slip_port.rx.insert(rig.body_slip_port.rx.end(), raw.begin(), raw.end());

    rig.step(20);

    // Body endpoint safely discards corrupt bytes and processes the valid heartbeat
    assert(rig.controller.linkEndpoint().connected(rig.now));
''')


if __name__ == "__main__":
    unittest.main()
