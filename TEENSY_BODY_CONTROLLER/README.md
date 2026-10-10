# Teensy body controller

Commissioning-first PlatformIO target for the Teensy 4.1 body controller
(see `docs/superpowers/specs/2026-10-09-teensy-body-controller-design.md`).

## Architecture & Executive Scheduler

The body executive is coordinated by `body::BodyController` (`src/body/BodyController.{h,cpp}`):
- **Deterministic non-blocking scheduler:**
  - Receiver sensor processing first (micros-accurate IBUS telemetry sensor response).
  - Bounded UART pump drains (at most 96 bytes per full-duplex port per pass).
  - Drive control updates on a 20ms cadence with paired wheel command dispatch.
  - VESC duplex telemetry polling at 100ms per wheel; raw diagnostic capture support.
  - RC snapshot updates and periodic `BODY_STATUS` publishing (200ms) and `AUDIO_STATUS` (500ms).
  - Watchdog supervision via `Watchdog_t4` (`WDT_T4<WDT1>` with 1.0s timeout) fed only when scheduling passes remain healthy (<25ms loop time).
- **Subsystem Orchestration:**
  - `DriveController`: Dual-wheel VESC mixing, ramp slew, reversal dwell, and failsafe braking.
  - `DomeController`: Continuous servo positioning, RC manual steering, drive-follow biasing, and Hall tracking.
  - `DomeCalibration`: Wireless commissioning state machine executing neutral trim, reference alignment, and 3-revolution CW/CCW rate calibration with keepalive and radio safety guards.
  - `DfPlayer`: Serial MP3 player interface with acknowledged track dispatch and completion event tracking.
  - `LinkEndpoint`: Shared SLIP protocol framing over Serial4 connecting to the ESP32 dome head.

## Control Epoch & Maintenance Locks

To guarantee safe coordination between local RC inputs and remote commands:
- **Motion Locks:** All physical motion (drive and dome) is inhibited when `motionLocked()` is true.
  - Lock reason 1: Emergency / Stop Latched (`STOP_ALL` operation).
  - Lock reason 2: Handheld Radio Inactive / Disarmed (CH6 OFF).
  - Lock reason 4: Maintenance Lock (`LOCK` operation with explicit non-zero token).
- **Epoch Tracking:** Any state change (`STOP_ALL`, `RELEASE_STOP`, `LOCK`, `UNLOCK`, `RECOVER_LOCKS`) increments `control_epoch`. Stale motion commands carrying an older epoch are rejected with `Result::WrongEpoch`.
- **Commissioning-First Safety:** Actuator outputs remain inert until a valid profile is saved to EEPROM and approved via `profile enable`.

## USB Line CLI

The controller provides an interactive, bounded line-based USB serial CLI:
- `status`: Displays current drive state, motion locks, control epoch, faults, and voltages.
- `rc`: Displays latest radio input snapshot (channels 1-14, age, validity).
- `vesc`: Displays left and right VESC RPM, duty, current, and voltage.
- `profile show`: Lists all commissioning profile fields and values.
- `profile set FIELD VALUE`: Modifies a profile field (only permitted when CH6/CH9 are OFF and actuators are neutral).
- `profile save`: Persists the current configuration profile to EEPROM.
- `profile enable`: Evaluates and arms readiness flags (drive, manual dome, auto dome) if prerequisite calibrations are met.
- `stop`: Immediately latches emergency stop and stops all actuators.

## Testing

Host unit tests compile Teensy controller sources directly with native clang/g++:

```bash
python3 -m unittest discover -s tests -p "test_body_*.py" -v
python3 -m unittest discover -s tests -p "test_dome_*.py" -v
```
