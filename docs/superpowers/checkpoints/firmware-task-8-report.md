# Task 8 report — Integrate the Teensy scheduler, locks and diagnostics

## Status and scope

**Software implementation verified and reviewed.**
Task 8 integrates drive control, continuous dome positioning, DFPlayer audio, link endpoint communications, watchdog supervision, maintenance locks, and interactive USB CLI diagnostics into a unified non-blocking executive via `body::BodyController`.

Deliverables:
- Production `TEENSY_BODY_CONTROLLER/src/body/BodyController.h`
- Production `TEENSY_BODY_CONTROLLER/src/body/BodyController.cpp`
- Production `TEENSY_BODY_CONTROLLER/src/body/HardwareAdapters.h` (`EepromStorage`, `DomeLinkPort`)
- Production `TEENSY_BODY_CONTROLLER/src/main.cpp`
- Host fake `tests/radio_fakes/Arduino.h` (`Serial4`, `FakeEeprom`, `available()`)
- Unit tests in `tests/test_body_controller.py` (3 tests)
- Documentation in `TEENSY_BODY_CONTROLLER/README.md`

## Implementation details and design rules

### 1. Unified Executive Scheduler (`body::BodyController`)
- **Non-blocking Loop Hierarchy:**
  1. Handheld receiver sensor processing runs first (`IbusTelemetry::tick` and `pump`), keeping IBUS sensor return frames under strict microsecond deadlines.
  2. Bounded UART pump drains (`ReceiverPort::pump` and `link_endpoint_.pumpLink`), capped to $\le 96$ bytes per port per pass.
  3. `BodyController::updateRc` passes fresh RC snapshots to drive and dome subsystems.
  4. `BodyController::tick(now_ms, now_us)` handles VESC links, incoming R2BodyLink frames, drive updates (20ms cadence with paired wheel command dispatch), dome motion, audio state machine, event draining, and periodic status publishing.
  5. Dome servo pulse generation via `g_dome.output()`.
  6. Telemetry aggregation via `vescMeasurements` from both VESC samples.
  7. Interactive USB line-based CLI and diagnostic packet capture.
- **Deadline Monitoring & Watchdog Feed:**
  - Loop time is measured in microseconds (`last_loop_us_`). Loop iterations exceeding 25ms set `deadline_healthy_ = false`.
  - Configures `Watchdog_t4` (`WDT_T4<WDT1>`) with a 1.0s timeout. Fed only if `deadlineHealthy()` is true; stalled loops or deadlocks trip the hardware watchdog.

### 2. Control Epoch and Maintenance Locks
- **Lock Reasons & Semantics:**
  - Bit 0: Emergency / Stop Latched (`STOP_ALL` operation or CLI `stop`).
  - Bit 1: Radio Inactive / Disarmed (CH6 OFF).
  - Bit 2: Maintenance Lock (`LOCK` operation with explicit non-zero token).
- **Control Epoch Invariant:**
  - Every lock state mutation (`STOP_ALL`, `RELEASE_STOP`, `LOCK`, `UNLOCK`, `RECOVER_LOCKS`) increments `control_epoch_` and broadcasts `BODY_STATUS` immediately.
  - Motion requests carrying a stale epoch are rejected with `Result::WrongEpoch` before checking lock status, preventing delayed or out-of-sequence execution.
- **Lock Recovery:**
  - `RECOVER_LOCKS` operation allows head node recovery across reboots only when motion is stopped/neutral and provides the correct maintenance token, restoring `control_epoch` continuity.

### 3. USB Diagnostic CLI
- Bounded line-based interactive serial console:
  - `status`: Outputs state, faults, control epoch, lock reasons, and battery/temperature.
  - `rc`: Dumps raw channels 1-14, age, and validity flags.
  - `vesc`: Dumps per-wheel eRPM, duty cycle, current, and voltage.
  - `profile show`: Lists current commissioning fields and values.
  - `profile set FIELD VALUE`: Safely mutates configuration fields (requires CH6/CH9 OFF and actuators neutral).
  - `profile save`: Writes active configuration profile to EEPROM.
  - `profile enable`: Evaluates readiness per subsystem (drive, manual dome, auto dome) and arms authorized features.
  - `stop`: Immediately latches emergency stop and halts drive and dome actuators.

## Verification and test results

- `python3 -m unittest discover -s tests -p test_body_controller.py -v`: 3 tests passed (1.69s).
- `python3 -m unittest discover -s tests -p test_body_drive.py -v`: 18 tests passed (7.66s).
- `python3 -m unittest discover -s tests -p test_body_radio.py -v`: 12 tests passed (3.99s).
- `python3 -m unittest discover -s tests -p test_body_vesc.py -v`: 19 tests passed (8.92s).
- `python3 -m unittest discover tests -v`: 185 tests passed (54.97s, all green across entire repository).
