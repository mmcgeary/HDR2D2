# Task 6b report — ESP32 autonomous dome scheduler (DomeBehaviour)

## Status and scope

**Software implementation verified and reviewed.**
Task 6b defines the portable autonomous dome scheduler for ESP32 with no direct hardware dependencies. Integration with `BodyClient` occurs in Task 9 and the ESP32 main loop in Task 10.

Deliverables:
- Production `ASTROPIXELS_PLUS_UNIFIED/DomeBehaviour.h`
- Production `ASTROPIXELS_PLUS_UNIFIED/DomeBehaviour.cpp`
- Unit tests in `tests/test_dome_behaviour.py` (12 test methods covering inactivity timing, stick/drive/event postponement, full sweep lifecycles, random bounds, CH6/CH9 gating, peer loss, and fault latching)

## Implementation details and design rules

### 1. Abstract interfaces (`DomeBehaviour.h`)
- `IDomeRequestSink`: Virtual interface `submit(const r2link::DomeRequest& req, uint32_t now_ms, uint16_t& sequence) -> bool`.
- `IDomeRandom`: Virtual interface `pick(int32_t min_inclusive, int32_t max_exclusive) -> int32_t`.
- `DomeBehaviourInput`: Encapsulates `r2link::RcStatus rc`, `r2link::BodyStatus status`, `bool rc_fresh`, `bool status_fresh`, `bool event_active`.

### 2. State machine & lifecycle
- **`WaitingIdle` (20-second inactivity deadline):**
  - Requires fresh RC and status, Auto Dome switch ON (`CH9 >= 1750`), auto profile readiness (`profile_ready & 4`), zero motion locks, and no latched faults.
  - Postponed continuously during operator activity:
    - `input.event_active`
    - Steering, throttle, or manual dome stick deflected outside 1460–1540us deadband (even when feet are disarmed via CH6 OFF)
    - Drive moving (`drive_intent != Stationary`)
    - Dome owned by Manual, Drive alignment, Event, or Startup
  - When 20,000ms of continuous inactivity elapses, submits `SeekReference(Front)` and transitions to `Referencing`.
- **`Referencing`:**
  - Awaits front reference completion event.
  - On `EventKind::Completed`, transitions to `Pausing`.
- **`Pausing`:**
  - Injects random pause in `[2000, 6000]` ms between motions.
  - After pause at front: picks random target in `[-450, 450]` tenths of degrees (skipping targets equal to current estimate) and transitions to `Sweeping`.
  - After pause at excursion target: transitions to `Returning`.
- **`Sweeping`:**
  - Submits and renews `DomeOperation::Velocity` leases (`lease_ms = 100`, <=150ms) every 50ms at commissioned `auto_speed_percent`.
  - Rotates in the appropriate direction (CW or CCW) until estimate reaches target.
  - Stops renewal upon reaching target and transitions to `Pausing`.
- **`Returning`:**
  - Submits `SeekReference(Front)` to accurately eliminate accumulated timed movement drift after the excursion.
  - On `EventKind::Completed`, enters `Pausing` ready for the next sweep.

### 3. Fault latching & authority protection
- **Peer loss / Generation change:** Cancels active requests and returns to `WaitingIdle`. Tags new requests with the updated authority generation.
- **Manual / Drive takeover:** Resets to `WaitingIdle` without renewed commands.
- **Latched fault:** A seek timeout (`Detail::SeekTimeout`), hardware error, or `Result::Inhibited` reply latches a fault and halts repeated seeks until Auto Dome toggles OFF -> ON.

## Verification and test results

- `python3 -m unittest discover -s tests -p test_dome_behaviour.py -v`: 12 tests passed (3.9s).
- `python3 -m unittest discover tests`: 170 tests passed (53.9s, all green across entire repository).

## Public API

```cpp
class DomeBehaviour {
public:
    enum class State : uint8_t {
        WaitingIdle = 0,
        Referencing = 1,
        Pausing = 2,
        Sweeping = 3,
        Returning = 4
    };

    DomeBehaviour(IDomeRequestSink& sink, IDomeRandom& random, int16_t auto_speed_percent = 25);

    void tick(const DomeBehaviourInput& input, uint32_t now_ms);
    void onReply(uint16_t sequence, r2link::Result result);
    void onEvent(const r2link::Event& ev);
    void onPeerLost(uint32_t now_ms);

    State state() const;
    int16_t targetAngleDdeg() const;
    uint16_t activeSequence() const;
    bool faulted() const;
};
```
