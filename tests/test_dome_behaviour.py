"""Task 6b tests: ESP32 autonomous dome scheduler (DomeBehaviour)."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
ASTRO = ROOT / "ASTROPIXELS_PLUS_UNIFIED"
SHARED = ROOT / "shared/R2BodyLink"

SOURCES = [ASTRO / "DomeBehaviour.cpp"]

PRELUDE = r'''
#include <cassert>
#include <vector>
#include <iostream>
#include "Messages.h"
#include "DomeBehaviour.h"

struct FakeDomeRequestSink : public IDomeRequestSink {
    std::vector<r2link::DomeRequest> requests;
    std::vector<uint32_t> timestamps;
    uint16_t next_seq{1};
    bool submit_result{true};

    bool submit(const r2link::DomeRequest& req, uint32_t now_ms, uint16_t& sequence) override {
        if (!submit_result) return false;
        sequence = next_seq++;
        requests.push_back(req);
        timestamps.push_back(now_ms);
        return true;
    }

    size_t count() const { return requests.size(); }
    const r2link::DomeRequest& last() const { return requests.back(); }
    void clear() { requests.clear(); timestamps.clear(); }
};

struct FakeDomeRandom : public IDomeRandom {
    std::vector<int32_t> next_picks;
    int32_t default_pick{3000};

    int32_t pick(int32_t min_inclusive, int32_t max_exclusive) override {
        if (!next_picks.empty()) {
            int32_t val = next_picks.front();
            next_picks.erase(next_picks.begin());
            return val;
        }
        return (default_pick >= min_inclusive && default_pick < max_exclusive)
            ? default_pick : min_inclusive;
    }
};

DomeBehaviourInput makeHealthyInput(uint32_t now_ms = 0) {
    (void)now_ms;
    DomeBehaviourInput in{};
    in.rc_fresh = true;
    in.status_fresh = true;
    in.event_active = false;
    for (int i = 0; i < 10; ++i) in.rc.channels[i] = 1500;
    in.rc.channels[5] = 2000; // CH6 Feet Enable
    in.rc.channels[8] = 2000; // CH9 Auto Dome Enable
    in.status.profile_ready = 7; // Drive, Manual, Auto
    in.status.drive_state = uint8_t(r2link::DriveState::Armed);
    in.status.drive_intent = uint8_t(r2link::DriveIntent::Stationary);
    in.status.dome_state = uint8_t(r2link::DomeState::HoldingReference);
    in.status.dome_owner = uint8_t(r2link::DomeOwner::None);
    in.status.angle_valid = 1;
    in.status.estimated_angle_ddeg = 0;
    in.status.control_epoch = 1;
    in.status.dome_authority_generation = 1;
    return in;
}
'''


class DomeBehaviourTests(unittest.TestCase):
    def test_no_command_before_20_seconds(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    // Fixture initially stationary, Auto Dome ON, front startup completed at 0.
    behaviour.tick(input, 19999);
    assert(sink.count() == 0);
    behaviour.tick(input, 20000);
    assert(sink.count() == 1);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    assert(sink.last().reference == uint8_t(r2link::DomeReference::Front));
    assert(sink.last().owner == uint8_t(r2link::DomeOwner::Idle));
    assert(sink.last().dome_authority_generation == 1);
    assert(sink.last().control_epoch == 1);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_active_event_postpones_deadline(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    input.event_active = true;
    behaviour.tick(input, 5000);
    behaviour.tick(input, 10000);
    assert(sink.count() == 0);

    // Event ends at 10000ms
    input.event_active = false;
    behaviour.tick(input, 10001);
    assert(sink.count() == 0);

    // 20s required after event end -> deadline is 30000ms
    behaviour.tick(input, 29999);
    assert(sink.count() == 0);

    behaviour.tick(input, 30000);
    assert(sink.count() == 1);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    assert(sink.last().reference == uint8_t(r2link::DomeReference::Front));
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_drive_intent_postpones_deadline(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    input.status.drive_intent = uint8_t(r2link::DriveIntent::Forward);
    behaviour.tick(input, 5000);
    behaviour.tick(input, 15000);
    assert(sink.count() == 0);

    // Drive stops at 15000ms
    input.status.drive_intent = uint8_t(r2link::DriveIntent::Stationary);
    behaviour.tick(input, 15001);
    assert(sink.count() == 0);

    // 20s after drive stops -> 35000ms
    behaviour.tick(input, 34999);
    assert(sink.count() == 0);

    behaviour.tick(input, 35000);
    assert(sink.count() == 1);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stick_deflection_postpones_deadline_even_when_disarmed(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    // Feet disarmed (CH6 = 1000)
    input.rc.channels[5] = 1000;
    input.status.drive_state = uint8_t(r2link::DriveState::Disarmed);

    // Deflect steering stick outside deadband (1700us)
    input.rc.channels[0] = 1700;
    behaviour.tick(input, 5000);
    behaviour.tick(input, 8000);
    assert(sink.count() == 0);

    // Center stick at 8000ms
    input.rc.channels[0] = 1500;
    behaviour.tick(input, 8001);
    assert(sink.count() == 0);

    behaviour.tick(input, 27999);
    assert(sink.count() == 0);

    behaviour.tick(input, 28000);
    assert(sink.count() == 1);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ch9_auto_dome_off_disables_idle_motion(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    // Auto Dome OFF (CH9 = 1000)
    input.rc.channels[8] = 1000;
    behaviour.tick(input, 20000);
    behaviour.tick(input, 50000);
    assert(sink.count() == 0);

    // Switch Auto Dome ON at 50000ms
    input.rc.channels[8] = 2000;
    behaviour.tick(input, 50000);
    assert(sink.count() == 0);

    // Requires 20s of healthy active Auto Dome
    behaviour.tick(input, 69999);
    assert(sink.count() == 0);

    behaviour.tick(input, 70000);
    assert(sink.count() == 1);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_full_sweep_lifecycle(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    // Set random pick sequence:
    // 1st pick: pause duration after initial reference (3000ms)
    // 2nd pick: target angle (+300 ddeg)
    // 3rd pick: pause duration at target (4000ms)
    // 4th pick: pause duration after return (2500ms)
    rng.next_picks = {3000, 300, 4000, 2500};

    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    // 1. Initial 20s wait
    behaviour.tick(input, 20000);
    assert(sink.count() == 1);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    assert(sink.last().reference == uint8_t(r2link::DomeReference::Front));
    assert(behaviour.state() == DomeBehaviour::State::Referencing);
    const uint16_t ref_seq = sink.requests[0].control_epoch; // seq assigned by sink

    // 2. Front reference completes at 20500ms
    behaviour.tick(input, 20500);
    r2link::Event ev{};
    ev.kind = uint8_t(r2link::EventKind::Completed);
    ev.request_type = uint8_t(r2link::MessageType::DomeRequest);
    ev.request_seq = 1; // first request
    behaviour.onEvent(ev);
    assert(behaviour.state() == DomeBehaviour::State::Pausing);

    // 3. Pausing for 3000ms (until 23500ms)
    behaviour.tick(input, 23499);
    assert(behaviour.state() == DomeBehaviour::State::Pausing);
    assert(sink.count() == 1);

    // 4. Pause expires -> transitions to Sweeping towards +300 ddeg
    behaviour.tick(input, 23500);
    assert(behaviour.state() == DomeBehaviour::State::Sweeping);
    assert(sink.count() == 2);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::Velocity));
    assert(sink.last().speed_percent == 25);
    assert(sink.last().lease_ms <= 150);

    // 5. While sweeping, leases are renewed every 50ms
    input.status.estimated_angle_ddeg = 100;
    behaviour.tick(input, 23550);
    assert(sink.count() == 3);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::Velocity));

    // 6. Target reached at 23800ms (+300 ddeg)
    input.status.estimated_angle_ddeg = 300;
    behaviour.tick(input, 23800);
    assert(behaviour.state() == DomeBehaviour::State::Pausing);

    // 7. Pausing at target for 4000ms (until 27800ms)
    behaviour.tick(input, 27799);
    assert(behaviour.state() == DomeBehaviour::State::Pausing);

    // 8. Pause expires -> transitions to Returning (SeekReference Front)
    behaviour.tick(input, 27800);
    assert(behaviour.state() == DomeBehaviour::State::Returning);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    assert(sink.last().reference == uint8_t(r2link::DomeReference::Front));

    // 9. Returning completes at 28500ms
    behaviour.tick(input, 28500);
    ev.request_seq = sink.requests.size(); // last seq
    behaviour.onEvent(ev);
    assert(behaviour.state() == DomeBehaviour::State::Pausing);

    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_manual_takeover_cancels_and_resets(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    rng.next_picks = {2000, 400};
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    behaviour.tick(input, 20000);
    assert(behaviour.state() == DomeBehaviour::State::Referencing);

    // Manual takeover via event
    r2link::Event ev{};
    ev.kind = uint8_t(r2link::EventKind::DomeTakeover);
    ev.request_type = uint8_t(r2link::MessageType::DomeRequest);
    ev.request_seq = 1;
    behaviour.onEvent(ev);

    assert(behaviour.state() == DomeBehaviour::State::WaitingIdle);

    // 20s required again
    behaviour.tick(input, 39999);
    assert(sink.count() == 1);

    behaviour.tick(input, 40000);
    assert(sink.count() == 2);
    assert(behaviour.state() == DomeBehaviour::State::Referencing);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_peer_lost_and_generation_change(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    behaviour.tick(input, 20000);
    assert(behaviour.state() == DomeBehaviour::State::Referencing);

    // Peer lost
    behaviour.onPeerLost(20100);
    assert(behaviour.state() == DomeBehaviour::State::WaitingIdle);

    // On reconnect, authority generation increments to 2
    input.status.dome_authority_generation = 2;
    behaviour.tick(input, 30000);
    assert(sink.count() == 1); // no new command before 20s from reconnect

    behaviour.tick(input, 40100);
    assert(sink.count() == 2);
    assert(sink.last().dome_authority_generation == 2);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fault_latches_and_prevents_repeated_seeks(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    behaviour.tick(input, 20000);
    assert(sink.count() == 1);

    // Seek timeout event arrives
    r2link::Event ev{};
    ev.kind = uint8_t(r2link::EventKind::Timeout);
    ev.detail = uint16_t(r2link::Detail::SeekTimeout);
    ev.request_seq = 1;
    behaviour.onEvent(ev);

    assert(behaviour.state() == DomeBehaviour::State::WaitingIdle);

    // Even after 30 seconds, no new seek should be repeatedly submitted while faulted
    behaviour.tick(input, 60000);
    assert(sink.count() == 1);

    // Cleared by Auto Dome OFF -> ON toggle
    input.rc.channels[8] = 1000;
    behaviour.tick(input, 61000);
    input.rc.channels[8] = 2000;
    behaviour.tick(input, 62000);

    // Now 20s of healthy inactivity will submit a new seek
    behaviour.tick(input, 81999);
    assert(sink.count() == 1);
    behaviour.tick(input, 82000);
    assert(sink.count() == 2);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_ch6_off_allows_idle(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    // CH6 feet switch OFF (1000us)
    input.rc.channels[5] = 1000;
    input.status.drive_state = uint8_t(r2link::DriveState::Disarmed);

    // CH9 Auto Dome is ON (2000us)
    input.rc.channels[8] = 2000;

    behaviour.tick(input, 19999);
    assert(sink.count() == 0);

    behaviour.tick(input, 20000);
    assert(sink.count() == 1);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_random_bounds_and_skip_equal_target(self):
        program = PRELUDE + r'''
struct BoundsCheckingRandom : public IDomeRandom {
    int32_t last_target{0};
    int pick_count{0};

    int32_t pick(int32_t min_inclusive, int32_t max_exclusive) override {
        ++pick_count;
        if (min_inclusive == -450 && max_exclusive == 451) {
            // Target angle pick: must be in [-450, 450]
            if (pick_count == 2) {
                // First attempt: return 0 (equal to current estimate)
                return 0;
            }
            // Second attempt: return valid non-zero target
            return 250;
        }
        if (min_inclusive == 2000 && max_exclusive == 6001) {
            // Pause pick: must be in [2000, 6000]
            return 3000;
        }
        assert(false); // unexpected bounds
        return 0;
    }
};

int main() {
    FakeDomeRequestSink sink;
    BoundsCheckingRandom rng;
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    // 1. Initial 20s
    behaviour.tick(input, 20000);
    assert(sink.count() == 1);

    // 2. Reference completes -> enters Pausing (rng pick #1: pause)
    behaviour.tick(input, 20500);
    r2link::Event ev{};
    ev.kind = uint8_t(r2link::EventKind::Completed);
    ev.request_type = uint8_t(r2link::MessageType::DomeRequest);
    ev.request_seq = 1;
    behaviour.onEvent(ev);

    // 3. Pause expires at 23500ms
    // rng pick #2 returns 0 (equal to current estimate 0) -> should be skipped!
    // rng pick #3 returns 250 -> used!
    behaviour.tick(input, 23500);
    assert(behaviour.state() == DomeBehaviour::State::Sweeping);
    assert(behaviour.targetAngleDdeg() == 250);
    assert(rng.pick_count >= 3);
    assert(sink.count() == 2);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::Velocity));
    assert(sink.last().speed_percent == 25); // CW towards +250
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_no_stale_movement_on_reconnect(self):
        program = PRELUDE + r'''
int main() {
    FakeDomeRequestSink sink;
    FakeDomeRandom rng;
    rng.next_picks = {2000, 300};
    DomeBehaviour behaviour(sink, rng, 25);
    DomeBehaviourInput input = makeHealthyInput();

    behaviour.tick(input, 20000); // Seek Front
    behaviour.tick(input, 20500);
    r2link::Event ev{};
    ev.kind = uint8_t(r2link::EventKind::Completed);
    ev.request_type = uint8_t(r2link::MessageType::DomeRequest);
    ev.request_seq = 1;
    behaviour.onEvent(ev); // Pausing for 2000ms

    behaviour.tick(input, 22500); // Sweeping towards +300
    assert(behaviour.state() == DomeBehaviour::State::Sweeping);
    assert(sink.count() == 2);

    // Peer lost while sweeping
    behaviour.onPeerLost(22600);
    assert(behaviour.state() == DomeBehaviour::State::WaitingIdle);

    // Reconnected: no new commands submitted every tick
    input.status.dome_authority_generation = 2;
    behaviour.tick(input, 22700);
    behaviour.tick(input, 22800);
    behaviour.tick(input, 25000);
    assert(sink.count() == 2); // no stale renewal commands!

    // Full 20s required from 22600 (until 42600)
    behaviour.tick(input, 42599);
    assert(sink.count() == 2);
    behaviour.tick(input, 42600);
    assert(sink.count() == 3);
    assert(sink.last().operation == uint8_t(r2link::DomeOperation::SeekReference));
    assert(sink.last().dome_authority_generation == 2);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=SOURCES, include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
