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
  - Watchdog supervision via `Watchdog_t4` (`WDT_T4<WDT1>` with 1.0s timeout) fed only when the previous scheduling pass took <=50ms.
- **Subsystem Orchestration:**
  - `DriveController`: Dual-wheel VESC mixing, ramp slew, reversal dwell, and failsafe braking.
  - `DomeController`: Continuous servo positioning, RC manual steering, drive-follow biasing, and Hall tracking.
  - `DomeCalibration`: Wireless commissioning state machine executing neutral trim, reference alignment, and 3-revolution CW/CCW rate calibration with keepalive and radio safety guards.
  - `DfPlayer`: Serial MP3 player interface with acknowledged track dispatch and completion event tracking.
  - `LinkEndpoint`: Shared SLIP protocol framing over Serial4 connecting to the ESP32 dome head.

## Control Epoch & Maintenance Locks

To guarantee safe coordination between local RC inputs and remote commands:
- **Motion Locks:** All physical motion (drive and dome) is inhibited when `motionLocked()` is true.
  - Lock bit 0 (value 1): STOP latched (`STOP_ALL`, or operator `LOCK`). Released by `RELEASE_STOP`/operator `UNLOCK`.
  - Lock bit 2 (value 4): Maintenance Lock (`LOCK` operation with explicit non-zero token). Released by `UNLOCK` with that token.
  - CH6 OFF is not a lock: it simply disarms the feet.
  - Releases are all-or-nothing and require fresh RC, CH6 OFF and sticks centred 500ms; a refused release (`NotReady`) changes nothing. `RECOVER_LOCKS` clears both bits behind the same gate.
- **Epoch Tracking:** Any state change (`STOP_ALL`, `RELEASE_STOP`, `LOCK`, `UNLOCK`, `RECOVER_LOCKS`) increments `control_epoch`. Stale motion commands carrying an older epoch are rejected with `Result::WrongEpoch`.
- **Commissioning-First Safety:** Actuators run only the *saved* profile (`activeProfile()`); edits are staged until Save, which applies without a reboot. The dome servo gets no pulses until servo neutral is accepted and saved; the feet stay disarmed until all eight VESC sign-offs are accepted and saved.

## USB Line CLI

The controller provides an interactive, bounded line-based USB serial CLI:
- `status`: Displays current drive state, motion locks, control epoch, faults, and voltages.
- `rc`: Displays latest radio input snapshot (channels 1-14, age, validity).
- `vesc`: Displays left and right VESC RPM, duty, current, and voltage.
- `profile show`: Shows saved and staged readiness and acceptance masks, plus the staged servo neutral and auto speed.
- `profile set FIELD VALUE`: Modifies a profile field (only permitted when CH6/CH9 are OFF and actuators are neutral).
- `profile save`: Persists the staged profile to EEPROM and makes it the active profile immediately (no reboot).
- `profile accept BIT`: Accepts a sign-off bit by name (`servo_neutral`, `front_reference`, `rear_reference`, `auto_timing`, `vesc_config_left`/`_right`, `timeout_brake_*`, `direction_*`, `reversal_*`), with the same evidence rules and gate as the wireless page (fresh RC, CH6 OFF, sticks centred, no motion lock, no test running). `timeout_brake_*`, `direction_*` and `reversal_*` need that wheel's passed automated wheel test, run from the `/drive` page.
- `profile baseline`: Stages the recommended baseline into every **unset** field (servo trims, auto speed, slew, VESC layout/currents/voltages/timeout/brake/reversal, and each wheel's firmware as observed over its UART). It never overwrites a set field and never accepts a bit; `profile save` to apply. Same gate as `profile accept`.
- `profile enable`: Reports the saved profile's readiness (drive, manual dome, auto dome); it does not change anything.
- `stop`: Immediately latches emergency stop and stops all actuators.

## Testing

Host unit tests compile Teensy controller sources directly with native clang/g++:

```bash
python3 -m unittest discover -s tests -p "test_body_*.py" -v
python3 -m unittest discover -s tests -p "test_dome_*.py" -v
```
