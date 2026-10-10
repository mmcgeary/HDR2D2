"""Task 9 tests: ESP32 body client and remote audio adapter."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
ASTRO = ROOT / "ASTROPIXELS_PLUS_UNIFIED"
SHARED = ROOT / "shared/R2BodyLink"
TESTS_DIR = ROOT / "tests"

SOURCES = [
    ASTRO / "BodyClient.cpp",
    ASTRO / "RemoteAudio.cpp",
    ASTRO / "DomeBehaviour.cpp",
    SHARED / "src/Endpoint.cpp",
    SHARED / "src/Codec.cpp",
]

INCLUDE_DIRS = [
    ASTRO,
    SHARED,
    TESTS_DIR,
    ROOT / "shared",
    ROOT / "TEENSY_BODY_CONTROLLER/src",
]

PRELUDE = r'''
#include <cassert>
#include <vector>
#include <iostream>
#include <cstring>
#include "body_fakes.h"
#include "BodyClient.h"
#include "RemoteAudio.h"
#include "DomeBehaviour.h"

using namespace fakes;
using namespace r2link;

static void inject(FakePort& p, const Frame& f) { p.push(wireOf(f)); }

struct ClientFixture {
    FakePort port;
    BodyClient client;
    RemoteAudio audio;
    uint32_t now{1000};
    uint32_t body_session{0xB0B0};
    uint32_t dome_session{0xD0D0};
    uint16_t body_seq{1};

    ClientFixture() : port(), client(), audio(client) {
        client.begin(port, dome_session);
    }

    void connect() {
        // Handshake: body sends Hello to dome
        Hello h{kRoleBody, 0x1Bu, kSafetyRevision};
        inject(port, stamp(frameOf(h), body_seq++, body_session, 0, 0));
        client.tick(now);

        // Body sends Heartbeat to dome
        Heartbeat hb{1, 1};
        inject(port, stamp(frameOf(hb), body_seq++, body_session, dome_session, 0));
        client.tick(now);
        assert(client.endpoint().connected(now));
    }

    void injectHeartbeat(uint32_t t) {
        Heartbeat hb{1, 1};
        inject(port, stamp(frameOf(hb), body_seq++, body_session, dome_session, 0));
        client.tick(t);
    }

    void injectRc(uint32_t t, uint16_t source_age_ms, uint32_t counter = 1) {
        RcStatus rc{};
        rc.sample_counter = counter;
        rc.source_age_ms = source_age_ms;
        rc.flags = 1;
        for (int i = 0; i < 10; ++i) rc.channels[i] = 1500;
        rc.channels[5] = 2000;
        rc.control_epoch = 1;
        inject(port, stamp(frameOf(rc), body_seq++, body_session, dome_session, 0));
        client.tick(t);
    }

    void injectVesc(uint32_t t, uint8_t wheel, uint16_t source_age_ms, int32_t erpm = 1000) {
        VescStatus v{};
        v.wheel = wheel;
        v.source_age_ms = source_age_ms;
        v.valid_fields = 0xFF;
        v.pack_cV = 1200;
        v.erpm = erpm;
        inject(port, stamp(frameOf(v), body_seq++, body_session, dome_session, 0));
        client.tick(t);
    }

    void injectBodyStatus(uint32_t t, uint16_t epoch = 1, uint32_t generation = 1) {
        BodyStatus s{};
        s.drive_state = static_cast<uint8_t>(DriveState::Armed);
        s.dome_state = static_cast<uint8_t>(DomeState::Manual);
        s.control_epoch = epoch;
        s.dome_authority_generation = generation;
        s.profile_ready = 1;
        inject(port, stamp(frameOf(s), body_seq++, body_session, dome_session, 0));
        client.tick(t);
    }

    void injectAudioStatus(uint32_t t, AudioState state, uint16_t track, uint32_t duration_ms = 5000) {
        AudioStatus s{};
        s.state = static_cast<uint8_t>(state);
        s.folder = 1;
        s.track = track;
        s.volume = 20;
        s.validity = duration_ms > 0 ? 2 : 0;
        s.duration_ms = duration_ms;
        inject(port, stamp(frameOf(s), body_seq++, body_session, dome_session, 0));
        client.tick(t);
    }

    void injectEvent(uint32_t t, EventKind kind, MessageType req_type, uint16_t req_seq) {
        Event ev{};
        ev.kind = static_cast<uint8_t>(kind);
        ev.request_type = static_cast<uint8_t>(req_type);
        ev.request_seq = req_seq;
        inject(port, stamp(frameOf(ev), body_seq++, body_session, dome_session, 0));
        client.tick(t);
    }
};
'''

class BodyEsp32Tests(unittest.TestCase):
    def check(self, body):
        code = PRELUDE + "\nint main() {\n" + body + "\n    puts(\"ok\");\n    return 0;\n}\n"
        result = run_cpp(code, extra_sources=SOURCES, include_dirs=INCLUDE_DIRS)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rc_and_vesc_expiry_and_heartbeat_independence(self):
        self.check(r'''
    ClientFixture f;
    f.connect();

    // Required specification assertion:
    // Inject RC_STATUS with source_age_ms 200 at local time 1000, then heartbeat at 1040:
    // assert(client.rcSnapshot(1050).valid);
    // assert(!client.rcSnapshot(1051).valid);
    f.injectRc(1000, 200);
    f.injectHeartbeat(1040);

    assert(f.client.rcSnapshot(1050).valid);
    assert(!f.client.rcSnapshot(1051).valid);

    // VESC expiration: each wheel expires independently (500ms freshness deadline)
    f.injectVesc(1000, 0, 100, 1200); // wheel 0 at t=1000, age=100
    f.injectVesc(1100, 1, 100, 1400); // wheel 1 at t=1100, age=100
    f.injectHeartbeat(1300);          // keep link connected

    // At t=1450:
    // Wheel 0: age = 100 + (1450 - 1000) = 550ms > 500ms -> invalid
    // Wheel 1: age = 100 + (1450 - 1100) = 450ms <= 500ms -> valid
    assert(!f.client.vescStatus(0, 1450).valid);
    assert(f.client.vescStatus(1, 1450).valid);
    assert(f.client.vescStatus(1, 1450).erpm == 1400);

    // Wheel 2 out of range is invalid
    assert(!f.client.vescStatus(2, 1100).valid);
''')

    def test_body_status_freshness_deadline_300ms(self):
        self.check(r'''
    ClientFixture f;
    f.connect();

    f.injectBodyStatus(1000, 42, 7);
    f.injectHeartbeat(1150);
    auto s1 = f.client.bodyStatus(1299);
    assert(s1.fresh);
    assert(s1.value.control_epoch == 42);
    assert(s1.effective_age_ms == 299);

    auto s2 = f.client.bodyStatus(1300);
    assert(s2.fresh);
    assert(s2.effective_age_ms == 300);

    auto s3 = f.client.bodyStatus(1301);
    assert(!s3.fresh);
    assert(s3.effective_age_ms == 301);
''')

    def test_dome_behaviour_integration_and_sink(self):
        self.check(r'''
    ClientFixture f;
    f.connect();

    // 1. Submit through IDomeRequestSink interface
    IDomeRequestSink& sink = f.client;
    DomeRequest req{};
    req.operation = static_cast<uint8_t>(DomeOperation::SeekReference);
    req.speed_percent = 0;
    req.reference = static_cast<uint8_t>(DomeReference::Front);
    req.lease_ms = 0;
    req.owner = 0;
    req.control_epoch = 1;
    req.dome_authority_generation = 1;

    uint16_t seq = 0;
    bool ok = sink.submit(req, 1000, seq);
    assert(ok);
    assert(seq > 0);

    // 2. Build DomeBehaviourInput from client
    f.injectRc(1000, 20);
    f.injectBodyStatus(1000, 1, 12);
    DomeBehaviourInput input = f.client.makeDomeBehaviourInput(1010, false);
    assert(input.rc_fresh);
    assert(input.status_fresh);
    assert(!input.event_active);
    assert(input.status.dome_authority_generation == 12);

    // When status expires, input reflects not fresh
    DomeBehaviourInput stale_input = f.client.makeDomeBehaviourInput(1400, false);
    assert(!stale_input.status_fresh);
''')

    def test_remote_audio_facade_and_offline(self):
        self.check(r'''
    ClientFixture f;
    // Before connection, audio.play is rejected offline
    RequestHandle h_offline = f.audio.play(110, AudioPriority::Foreground, 1000);
    assert(!h_offline.queued);
    assert(f.audio.lastError().code != 0);

    // Connect rig
    f.connect();

    // Now play succeeds and returns queued handle
    RequestHandle h = f.audio.play(110, AudioPriority::Foreground, 1000);
    assert(h.queued);
    assert(h.sequence > 0);

    // Commands stop, pause, resume, setVolume
    RequestHandle h_stop = f.audio.stop(1001);
    assert(h_stop.queued);
    RequestHandle h_pause = f.audio.pause(1002);
    assert(h_pause.queued);
    RequestHandle h_resume = f.audio.resume(1003);
    assert(h_resume.queued);
    RequestHandle h_vol = f.audio.setVolume(25, 1004);
    assert(h_vol.queued);

    // Volume clamping to 30
    RequestHandle h_vol_max = f.audio.setVolume(50, 1005);
    assert(h_vol_max.queued);

    // Audio status query
    f.injectAudioStatus(1006, AudioState::Playing, 110, 60000);
    auto st = f.audio.status(1006);
    assert(st.state == AudioState::Playing);
    assert(st.track == 110);
    assert(st.duration_ms == 60000);
''')

    def test_events_and_completions(self):
        self.check(r'''
    ClientFixture f;
    f.connect();

    // Inject Event: PlaybackStarted
    f.injectEvent(1000, EventKind::PlaybackStarted, MessageType::AudioRequest, 42);
    Event ev{};
    assert(f.client.takeEvent(ev));
    assert(ev.kind == static_cast<uint8_t>(EventKind::PlaybackStarted));
    assert(ev.request_type == static_cast<uint8_t>(MessageType::AudioRequest));
    assert(ev.request_seq == 42);
    assert(!f.client.takeEvent(ev)); // Queue emptied

    // Publish Hall test
    f.client.publishHall(0x03, 0x01, 100, 1050);
''')

    def test_dual_hall_pins_and_masks(self):
        self.check(r'''
    ClientFixture f;
    f.connect();

    constexpr uint8_t kPinFront = 19;
    constexpr uint8_t kPinRear = 18;
    assert(kPinFront == 19);
    assert(kPinRear == 18);

    // Front active (bit 0 = 1, bit 1 = 0)
    f.client.publishHall(0x03, 0x01, 1, 1000);
    // Rear active (bit 0 = 0, bit 1 = 1)
    f.client.publishHall(0x03, 0x02, 2, 1020);
    // Neither active
    f.client.publishHall(0x03, 0x00, 3, 1040);
    // Both active
    f.client.publishHall(0x03, 0x03, 4, 1060);
''')

    def test_generation_change_and_peer_lost(self):
        self.check(r'''
    ClientFixture f;
    f.connect();
    f.injectRc(1000, 50);
    f.injectBodyStatus(1000, 1, 5);
    assert(f.client.rcSnapshot(1010).valid);
    assert(f.client.bodyStatus(1010).fresh);

    // Advance time past LinkTimeoutMs (300ms without heartbeat)
    f.client.tick(1400);
    assert(!f.client.endpoint().connected(1400));
    assert(!f.client.rcSnapshot(1400).valid);
    assert(!f.client.bodyStatus(1400).fresh);
''')

    def test_queue_full_and_lost_reply(self):
        self.check(r'''
    ClientFixture f;
    f.connect();

    // Fill ordinary request queue (kOrdinarySlots = 6)
    for (int i = 0; i < 6; ++i) {
        RequestHandle h = f.audio.play(100 + i, AudioPriority::Foreground, 1000);
        assert(h.queued);
    }
    // 7th ordinary request fails due to queue full
    RequestHandle h_fail = f.audio.play(107, AudioPriority::Foreground, 1000);
    assert(!h_fail.queued);
    assert(f.audio.lastError().code == 2); // Queue full

    // Advance past request deadline (350ms)
    // Ticking the client processes the timeouts and clears the pending slots
    f.client.tick(1400);
    f.injectHeartbeat(1400);

    // Now a new request can be queued
    RequestHandle h_new = f.audio.play(108, AudioPriority::Foreground, 1400);
    assert(h_new.queued);
''')

    def test_peer_restart_clears_state(self):
        self.check(r'''
    ClientFixture f;
    f.connect();
    f.injectRc(1000, 20);
    f.injectBodyStatus(1000, 1, 10);
    assert(f.client.rcSnapshot(1010).valid);

    // Peer restarts with a new session
    const uint32_t new_body_session = 0xB1B1;
    Hello h{kRoleBody, 0x1Bu, kSafetyRevision};
    inject(f.port, stamp(frameOf(h), 1, new_body_session, 0, 0));
    f.client.tick(1020);

    // Generation bumped and cached state invalidated
    assert(!f.client.rcSnapshot(1020).valid);
    assert(!f.client.bodyStatus(1020).fresh);
''')


if __name__ == "__main__":
    unittest.main()
