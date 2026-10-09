# Task 5 report — drive enable, normalized mixing, per-wheel reversal and explicit braking

## Status and scope

**Software implementation verified and reviewed. Hardware acceptance UNPERFORMED; motion remains disabled until commissioned profile is saved.**
Only Task 5 was reviewed, refined, and verified. No dome arbitration, continuous servo pulse output, DFPlayer, or ESP32 client code was added.

Deliverables:
- Production `TEENSY_BODY_CONTROLLER/src/body/DriveController.h` and `.cpp`
- Production update in `TEENSY_BODY_CONTROLLER/src/main.cpp`
- New shared enum types in `shared/R2BodyLink/Messages.h` (`DriveState`, `DriveIntent`)
- Unit tests in `tests/test_body_drive.py` (18 test methods covering all drive requirements)
- Regression coverage in `tests/test_body_radio.py` and `tests/test_body_vesc.py`

## Review findings and resolution

The Task 5 code preserved in `dd779b0` was audited against [`docs/superpowers/plans/2026-10-09-teensy-firmware.md`](../plans/2026-10-09-teensy-firmware.md) and [`docs/superpowers/specs/2026-10-09-teensy-body-controller-design.md`](../specs/2026-10-09-teensy-body-controller-design.md):

1. **Reversal dwell start timestamp underflow bug:**
   - *Finding:* In `DriveController::permit()`, `w.low_ms` was initialized to `now` instead of `s.sample_ms`. In real operation, VESC telemetry samples are polled at 100ms intervals or arrive asynchronously, so `s.sample_ms <= now`. When `s.sample_ms < now`, `uint32_t(s.sample_ms - w.low_ms)` underflowed to ~4,294,967,290 in unsigned 32-bit arithmetic. Because this exceeded `p.reversal_dwell_ms`, the reversal dwell gate immediately qualified on the very first low-speed sample, bypassing the required dwell period.
   - *Resolution:* Initialized `w.low_ms = s.sample_ms`. Both operands are now consistently in the sample time domain, preventing underflow and accurately requiring samples spanning at least `reversal_dwell_ms`.
2. **Missing `reversalState()` test assertions:**
   - *Finding:* `DriveController::reversalState(wheel)` was implemented but never asserted in `tests/test_body_drive.py`.
   - *Resolution:* Added explicit assertions in `test_reversal_measured_dwell_boundary_and_neutral_history` tracking the full lifecycle: `Tracking` -> `Braking` -> `Qualifying` -> `Tracking`.
3. **Asynchronous sample arrival regression test:**
   - *Finding:* Existing synthetic tests always delivered samples with `s.sample_ms == now`, masking the underflow bug.
   - *Resolution:* Added a test case where `s.sample_ms = 1135` and tick occurs at `now = 1140`, asserting that Braking and Forward intent persist until `t = 1235` (1135 + 100ms dwell).

## Verification and test results

- `python3 -m unittest discover tests -p test_body_drive.py`: 18 tests passed (all green).
- `python3 -m unittest discover tests`: 148 tests passed (entire project test suite).

## Public API and contracts

### Mixing and stick deadband
```cpp
MixedDuty mixDrive(uint16_t throttle_us, uint16_t steer_us, uint16_t rate_permille);
```
- Deadband: 1460–1540us maps to 0.
- Clamped endpoints: 1000–2000us mapped to -1000..+1000.
- Normalized mixing: `divisor = max(1000, abs(l), abs(r))`; output scaled by `rate / divisor`.
- Rates: 350 (35%), 700 (70%), 1000 (100%).
- Final per-wheel duty clamped to 950 (95%).

### Drive controller
```cpp
class DriveController {
public:
    DriveController();
    void update(const RcSnapshot& rc, const VescSample& left, const VescSample& right,
                const CommissioningProfile& profile, uint32_t now_ms);
    r2link::DriveState driveState() const;
    const WheelCommands& commands() const;
    r2link::DriveIntent intent() const;
    ReversalState reversalState(uint8_t wheel) const;
    void stop(uint32_t now_ms);
    r2link::Result setMotionLocks(uint8_t reasons);
    r2link::Result releaseStop(uint16_t current_epoch, uint32_t now_ms);
    r2link::Result submitRemote(const r2link::DriveRequest& req, uint32_t now_ms);
    bool stopLatched() const;
    uint8_t motionLocks() const;
    uint16_t controlEpoch() const;
    uint32_t deadlineMisses() const;
    uint32_t commandRevision() const;
};
```

### Safety and state rules enforced
- **Arming gate:** Requires observing CH6 OFF (<=1250us), then ON (>=1750us), then >=500ms centered sticks (CH1/CH2) and low speed (<= `reversal_erpm_limit`) before transitioning from `Disarmed`/`Qualifying` to `Armed`.
- **Positive braking:** Commanded on neutral, stops, and during reversals using commissioned `brake_mA`. Never substitutes duty zero. Uncommissioned profile uses `WheelMode::Disabled`.
- **Per-wheel reversal gate:** Opposing target sign commands Brake until measured eRPM is within `reversal_erpm_limit` continuously for `reversal_dwell_ms` across newly received samples.
- **Duty slew:** Elapsed-time slew with milli-permille fractional accumulation at `duty_slew_permille_per_s`. Reversals reset slew to start from zero. Neutral/stop requests bypass slew ramp into immediate Brake.
- **Drive intent:** Derived from permitted wheel targets. Stationary when no wheels permitted or disarmed; Forward when net forward; Reverse when net reverse; Pivot when equal-opposite. Retains prior travel facing during brake-before-reverse until opposite power is permitted.
- **Remote drive:** `allow_remote_drive = false` by default; `submitRemote()` returns `r2link::Result::Inhibited`.
- **Stop and locks:** `stop()` latches operator stop and increments epoch. `releaseStop()` and `setMotionLocks()` require fresh RC and CH6 OFF + centered sticks for >=500ms.
