# Task 9 report — ESP32 body client and remote audio facade

## Status and scope

**Software implementation verified and reviewed.**
Task 9 delivers the portable ESP32 body link client (`BodyClient`) and typed remote audio adapter (`RemoteAudio`), connecting the ESP32 dome head executive with the Teensy 4.1 body controller over R2BodyLink without Arduino dependencies in logic.

Deliverables:
- Production `ASTROPIXELS_PLUS_UNIFIED/BodyClient.h`
- Production `ASTROPIXELS_PLUS_UNIFIED/BodyClient.cpp`
- Production `ASTROPIXELS_PLUS_UNIFIED/RemoteAudio.h`
- Production `ASTROPIXELS_PLUS_UNIFIED/RemoteAudio.cpp`
- Build config `ASTROPIXELS_PLUS_UNIFIED/platformio.ini` (`lib_extra_dirs = ../shared`, `-I../shared/R2BodyLink`)
- Unit tests in `tests/test_body_esp32.py` (8 tests)

## Implementation details and design rules

### 1. ESP32 Body Client (`BodyClient`)
- **Transport & Role:** Wraps `r2link::Endpoint` configured with role `kRoleDome` and dynamic session handshake. Uses static buffer storage for `Endpoint` without dynamic heap allocations.
- **Independent RC & Telemetry Freshness:**
  - `rcSnapshot(now_ms)`: Evaluates effective age `source_age_ms + (now_ms - rx_ms)`. Strictly expires at $\le 250\text{ ms}$; intermediate heartbeats do NOT alter arrival timestamp or delay expiration.
  - `vescStatus(wheel, now_ms)`: Stores independent records per wheel (`vesc_raw_[2]`). Evaluates per-wheel effective age with a $500\text{ ms}$ freshness deadline, expiring each wheel independently.
  - `bodyStatus(now_ms)`: Evaluates effective age with a $300\text{ ms}$ freshness deadline.
- **Link Loss & Generation Bump:** On peer session change or link timeout ($300\text{ ms}$ without heartbeat), `peerGeneration()` increments and all cached RC, VESC, and BodyStatus records are immediately invalidated.
- **Asynchronous Event Buffering:** Incoming `MessageType::Event` packets are decoded into an internal ring buffer and acknowledged via `endpoint_.reply(frame, Result::Accepted, 0)`. Applications pop events via `takeEvent(r2link::Event&)`.
- **Task 6b Autonomous Dome Integration:**
  - Implements `IDomeRequestSink::submit(const DomeRequest&, now_ms, sequence)`.
  - Implements `makeDomeBehaviourInput(now_ms, event_active)` supplying current RC and BodyStatus with explicit freshness booleans and preserving the true `dome_authority_generation`.
- **Hall Telemetry Publication:** `publishHall(valid_mask, active_mask, counter, now_ms)` encodes and transmits `HallState` streams to the body controller.

### 2. Typed Remote Audio Facade (`RemoteAudio`)
- Wraps `BodyClient` to provide clean, typed audio operation dispatches (`play`, `playFolder`, `stop`, `pause`, `resume`, `setVolume`, `status`).
- Clamps volume levels to the maximum permissible threshold ($30$).
- Enforces offline rejection: operations submitted when disconnected return unqueued handles (`RequestHandle{0, false}`) with a typed error code.
- Status queries return `AudioStatus` directly from the latest stream published by Teensy, including track number, state, and duration validity.

## Verification and test results

- `python3 -m unittest discover -s tests -p test_body_esp32.py -v`: 8 tests passed (3.82s).
- `python3 -m unittest discover tests -v`: 193 tests passed (62.35s, all green across entire repository).
