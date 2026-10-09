"""Task 7 tests: Bidirectional DFPlayer driver and track metadata."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
INC = ROOT / "TEENSY_BODY_CONTROLLER/include"
SHARED = ROOT / "shared/R2BodyLink"

SOURCES = [BODY / "body/DfPlayer.cpp"]

PRELUDE = r'''
#include <cassert>
#include <vector>
#include <cstring>
#include <iostream>
#include "BytePort.h"
#include "Messages.h"
#include "Endpoint.h"
#include "TrackCatalog.h"
#include "body/DfPlayer.h"

struct FakeAudioPort : public r2link::BytePort {
    std::vector<uint8_t> tx_bytes;
    std::vector<uint8_t> rx_bytes;

    int read() override {
        if (rx_bytes.empty()) return -1;
        uint8_t b = rx_bytes.front();
        rx_bytes.erase(rx_bytes.begin());
        return b;
    }

    size_t writable() const override { return 1024; }

    size_t write(const uint8_t* src, size_t len) override {
        tx_bytes.insert(tx_bytes.end(), src, src + len);
        return len;
    }

    void inject(const std::vector<uint8_t>& packet) {
        rx_bytes.insert(rx_bytes.end(), packet.begin(), packet.end());
    }

    void clearTx() { tx_bytes.clear(); }
};

std::vector<uint8_t> makeDfPacket(uint8_t cmd, uint8_t param_h, uint8_t param_l) {
    uint8_t p[10] = {0x7E, 0xFF, 0x06, cmd, 0x00, param_h, param_l, 0x00, 0x00, 0xEF};
    uint16_t sum = 0;
    for (int i = 1; i < 7; ++i) sum += p[i];
    sum = -sum;
    p[7] = uint8_t(sum >> 8);
    p[8] = uint8_t(sum & 0xFF);
    return std::vector<uint8_t>(p, p + 10);
}
'''


class BodyAudioTests(unittest.TestCase):
    def test_dfplayer_command_serialization_and_spacing(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);

    // Initialized port
    player.initSimulated(100);

    // Request play folder 1, track 110 at t=100
    r2link::AudioRequest play{};
    play.operation = uint8_t(r2link::AudioOperation::Play);
    play.folder = 1;
    play.track = 110;
    play.priority = uint8_t(r2link::AudioPriority::Foreground);
    assert(player.request(play, 1, 100) == r2link::Result::Accepted);

    port.clearTx();
    player.tick(100);

    // Verify 10-byte packet for folder 1, track 110
    // 7E FF 06 0F 00 01 6E FE 7D EF
    assert(port.tx_bytes.size() == 10);
    assert(port.tx_bytes[0] == 0x7E);
    assert(port.tx_bytes[1] == 0xFF);
    assert(port.tx_bytes[2] == 0x06);
    assert(port.tx_bytes[3] == 0x0F); // Folder play command
    assert(port.tx_bytes[4] == 0x00);
    assert(port.tx_bytes[5] == 0x01); // Folder 1
    assert(port.tx_bytes[6] == 0x6E); // Track 110
    assert(port.tx_bytes[7] == 0xFE); // Checksum H
    assert(port.tx_bytes[8] == 0x7D); // Checksum L
    assert(port.tx_bytes[9] == 0xEF);

    // Verify 100ms spacing: next command at t=150 should be queued, not transmitted
    r2link::AudioRequest vol{};
    vol.operation = uint8_t(r2link::AudioOperation::SetVolume);
    vol.volume = 25;
    assert(player.request(vol, 2, 150) == r2link::Result::Accepted);
    port.clearTx();
    player.tick(150);
    assert(port.tx_bytes.empty()); // Spaced!

    // At t=200 (>= 100ms elapsed since 100), command transmits
    player.tick(200);
    assert(port.tx_bytes.size() == 10);
    assert(port.tx_bytes[3] == 0x06); // Set volume
    assert(port.tx_bytes[6] == 25);   // Volume 25
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_request_acceptance_and_starting_status(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);
    player.initSimulated(0);

    const uint32_t now = 500;
    r2link::AudioRequest play{};
    play.operation = uint8_t(r2link::AudioOperation::Play);
    play.folder = 1;
    play.track = 110;
    play.priority = uint8_t(r2link::AudioPriority::Foreground);

    assert(player.request(play, 42, now) == r2link::Result::Accepted);
    player.tick(now);
    assert(player.status(now).state == uint8_t(r2link::AudioState::Starting));
    assert(!(player.status(now).validity & 1)); // duration is 0 (unknown)
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_playback_started_and_completed_lifecycle(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);
    player.initSimulated(0);

    r2link::AudioRequest play{};
    play.operation = uint8_t(r2link::AudioOperation::Play);
    play.folder = 1;
    play.track = 110;
    play.priority = uint8_t(r2link::AudioPriority::Foreground);
    assert(player.request(play, 42, 100) == r2link::Result::Accepted);
    player.tick(100);

    // Status query is sent, inject status reply indicating playing (status 0x01)
    port.inject(makeDfPacket(0x42, 0x00, 0x01));
    player.tick(150);

    // PLAYBACK_STARTED event emitted for request 42
    r2link::Event ev{};
    assert(player.takeEvent(ev));
    assert(ev.kind == uint8_t(r2link::EventKind::PlaybackStarted));
    assert(ev.request_type == uint8_t(r2link::MessageType::AudioRequest));
    assert(ev.request_seq == 42);

    assert(player.status(150).state == uint8_t(r2link::AudioState::Playing));
    assert(player.status(150).owner_request_seq == 42);

    // Track finishes: inject 0x3D (track finished) for track 110
    port.inject(makeDfPacket(0x3D, 0x00, 110));
    player.tick(5000);

    // COMPLETED event emitted for request 42
    assert(player.takeEvent(ev));
    assert(ev.kind == uint8_t(r2link::EventKind::Completed));
    assert(ev.request_seq == 42);
    assert(player.status(5000).state == uint8_t(r2link::AudioState::Idle));
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_completion_guard_timeout(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);
    player.initSimulated(0);

    // Track 102 guard is 6500ms
    r2link::AudioRequest play{};
    play.operation = uint8_t(r2link::AudioOperation::Play);
    play.folder = 1;
    play.track = 102;
    play.priority = uint8_t(r2link::AudioPriority::Foreground);
    assert(player.request(play, 10, 100) == r2link::Result::Accepted);
    player.tick(100);

    // Confirm playback start at 200ms
    port.inject(makeDfPacket(0x42, 0x00, 0x01));
    player.tick(200);
    r2link::Event ev{};
    assert(player.takeEvent(ev)); // PlaybackStarted

    // No completion received from DFPlayer before guard expires (200 + 6500 = 6700ms)
    player.tick(6600);
    assert(player.status(6600).state == uint8_t(r2link::AudioState::Playing));
    assert(player.status(6699).state == uint8_t(r2link::AudioState::Playing));

    // At 6700ms, completion guard expires!
    port.clearTx();
    player.tick(6700);
    assert(player.takeEvent(ev));
    assert(ev.kind == uint8_t(r2link::EventKind::Timeout));
    assert(ev.detail == uint16_t(r2link::Detail::AudioGuardExpired));
    assert(ev.request_seq == 10);
    assert(player.status(6700).state == uint8_t(r2link::AudioState::Idle));
    assert(port.tx_bytes.size() == 10);
    assert(port.tx_bytes[3] == 0x16); // Stop command sent to player
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_foreground_supersedes_ambient(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);
    player.initSimulated(0);

    // Start ambient track 1
    r2link::AudioRequest amb{};
    amb.operation = uint8_t(r2link::AudioOperation::Play);
    amb.folder = 1;
    amb.track = 1;
    amb.priority = uint8_t(r2link::AudioPriority::Ambient);
    assert(player.request(amb, 1, 100) == r2link::Result::Accepted);
    player.tick(100);
    port.inject(makeDfPacket(0x42, 0x00, 0x01));
    player.tick(150);
    assert(player.status(150).state == uint8_t(r2link::AudioState::Playing));

    // Foreground request arrives: accepted and supersedes ambient
    r2link::AudioRequest fg{};
    fg.operation = uint8_t(r2link::AudioOperation::Play);
    fg.folder = 1;
    fg.track = 102;
    fg.priority = uint8_t(r2link::AudioPriority::Foreground);
    assert(player.request(fg, 2, 200) == r2link::Result::Accepted);

    // While foreground is active, another ambient request is inhibited!
    r2link::AudioRequest amb2{};
    amb2.operation = uint8_t(r2link::AudioOperation::Play);
    amb2.folder = 1;
    amb2.track = 2;
    amb2.priority = uint8_t(r2link::AudioPriority::Ambient);
    assert(player.request(amb2, 3, 250) == r2link::Result::Inhibited);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stop_cancels_playback(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);
    player.initSimulated(0);

    r2link::AudioRequest play{};
    play.operation = uint8_t(r2link::AudioOperation::Play);
    play.folder = 1;
    play.track = 106;
    play.priority = uint8_t(r2link::AudioPriority::Foreground);
    assert(player.request(play, 5, 100) == r2link::Result::Accepted);
    player.tick(100);
    port.inject(makeDfPacket(0x42, 0x00, 0x01));
    player.tick(150);

    // Stop request arrives
    r2link::AudioRequest stop{};
    stop.operation = uint8_t(r2link::AudioOperation::Stop);
    assert(player.request(stop, 6, 500) == r2link::Result::Accepted);
    player.tick(500);

    r2link::Event ev{};
    assert(player.takeEvent(ev)); // PlaybackStarted
    assert(player.takeEvent(ev)); // Cancelled
    assert(ev.kind == uint8_t(r2link::EventKind::Cancelled));
    assert(ev.request_seq == 5); // identifies cancelled request
    assert(player.status(500).state == uint8_t(r2link::AudioState::Idle));
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pause_resume_elapsed_bookkeeping(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);
    player.initSimulated(0);

    r2link::AudioRequest play{};
    play.operation = uint8_t(r2link::AudioOperation::Play);
    play.folder = 1;
    play.track = 106;
    play.priority = uint8_t(r2link::AudioPriority::Foreground);
    player.request(play, 1, 100);
    player.tick(100);
    port.inject(makeDfPacket(0x42, 0x00, 0x01));
    player.tick(200); // confirmed start at 200

    player.tick(1200);
    assert(player.status(1200).elapsed_ms == 1000); // 1000ms elapsed

    // Pause
    r2link::AudioRequest pause{};
    pause.operation = uint8_t(r2link::AudioOperation::Pause);
    player.request(pause, 2, 1300);
    player.tick(1300);
    assert(player.status(1300).state == uint8_t(r2link::AudioState::Paused));

    // While paused, elapsed freezes
    player.tick(2000);
    assert(player.status(2000).elapsed_ms == 1100);

    // Resume
    r2link::AudioRequest resume{};
    resume.operation = uint8_t(r2link::AudioOperation::Resume);
    player.request(resume, 3, 2100);
    player.tick(2100);
    assert(player.status(2100).state == uint8_t(r2link::AudioState::Playing));

    player.tick(3100);
    assert(player.status(3100).elapsed_ms == 2100); // resumed advancing
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_startup_offline_after_timeout(self):
        program = PRELUDE + r'''
int main() {
    FakeAudioPort port;
    body::DfPlayer player(port, kTrackCatalog, kTrackCatalogCount);

    // Initial tick at 0 starts init
    player.tick(0);
    assert(player.status(0).state == uint8_t(r2link::AudioState::Offline));

    // 3 seconds elapse with no DFPlayer response
    player.tick(3000);
    assert(player.status(3000).state == uint8_t(r2link::AudioState::Offline));

    // Play request is not accepted while offline
    r2link::AudioRequest play{};
    play.operation = uint8_t(r2link::AudioOperation::Play);
    play.folder = 1;
    play.track = 102;
    assert(player.request(play, 1, 3100) == r2link::Result::NotReady);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ROOT / "tests/radio_fakes", BODY, SHARED, INC])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
