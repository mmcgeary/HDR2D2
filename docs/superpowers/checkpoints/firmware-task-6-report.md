# Task 6 report — dome arbitration, dual references, position estimation, and servo output

## Status and scope

**Software implementation verified and reviewed. Hardware acceptance UNPERFORMED; dome continuous servo motion remains neutral/disabled until commissioning acceptance is saved.**
Only Task 6 was reviewed, refined, and verified. ESP32 autonomous dome scheduler, DFPlayer, and body integration CLI remain separate subsequent tasks.

Deliverables:
- Production `TEENSY_BODY_CONTROLLER/src/body/DomePosition.h` and `.cpp`
- Production `TEENSY_BODY_CONTROLLER/src/body/DomeController.h` and `.cpp`
- Production updates in `TEENSY_BODY_CONTROLLER/src/main.cpp`
- Protocol definitions in `shared/R2BodyLink/Messages.h` (`DomeState`, `DomeOwner`, `DomeOperation`, `DomeReference`, `EventKind`, `Detail`)
- Native testing shim in `tests/radio_fakes/Servo.h`
- Unit tests in `tests/test_body_dome.py` (10 test suites covering arbitration, dual references, position estimation, shortest route, timeouts, and servo pulses)
- Test updates in `tests/test_body_drive.py` and `tests/test_body_radio.py`

## Implementation details and design rules

### 1. Position tracking and dual references (`DomePosition`)
- **Signed normalization:** Normalizes angles to `[-1800, 1800)` deci-degrees (0.1° resolution).
- **Dual Hall sensors:**
  - Front sensor at 0 deci-degrees.
  - Rear sensor at -1800 deci-degrees.
  - Anchors on rising edge (entering magnet window); does not re-pin during broad magnet window.
  - Multiple simultaneous active references are flagged as sensor fault, invalidating position estimate.
- **Dead reckoning:** Accumulates position when moving using calibrated `cw_ddeg_per_s` and `ccw_ddeg_per_s` with fractional milli-ddeg remainder across 20ms steps.
- **Shortest route seeking:** Calculates delta in `[-1800, 1800)` ddeg. Direct 180° tie resolves CW. Unknown position defaults to CW seek until anchor is encountered.

### 2. Authority hierarchy and arbitration (`DomeController`)
- **Strict priority:**
  1. Operator STOP / motion locks (pins neutral 1500us, reports `DomeState::Stopped`).
  2. Manual RC stick (CH4 outside 1460–1540us deadband) overrides all automatic/remote control.
  3. Drive alignment (if enabled in profile, Forward aligns to 0 ddeg, Reverse aligns to -1800 ddeg, Pivot aligns forward).
  4. Remote event / macro requests.
  5. Remote idle requests (lowest remote priority).
  6. Startup homing (if enabled and unhomed).
  7. Default stationary neutral.
- **Authority generation:** Incremented on manual takeover, drive alignment takeover, Auto Dome disable (CH9 OFF), and peer loss.
- **Event dispatch:** Emits `EventKind::DomeTakeover` (kind 5) to notify remote client of authority revocation.
- **Remote lease timeouts:** Remote velocity leases expire after <=150ms without renewal; seek requests timeout after 10s if target reference is not reached.
- **Continuous servo pulse mapping:**
  - Maps desired velocity (-1000..+1000 permille) to calibrated pulse width via profile `servo_min_us`, `servo_neutral_us`, `servo_max_us`, and `servo_deadband_us`.
  - Stays neutral when disarmed or uncommissioned.

## Verification and test results

- `python3 -m unittest discover -s tests -p 'test_body_dome.py'`: 10 tests passed (4.4s).
- `python3 -m unittest discover -s tests -p 'test_body_drive.py'`: 18 tests passed (7.1s).
- `python3 -m unittest discover -s tests -p 'test_body_radio.py'`: 12 tests passed (3.9s).
- `python3 -m unittest discover tests`: 158 tests passed (46.4s, entire repository test suite).

## Public API and contracts

### DomePosition
```cpp
class DomePosition {
public:
    explicit DomePosition(const CommissioningProfile* profile = nullptr);
    void bindProfile(const CommissioningProfile* profile);
    void updateHall(const r2link::HallState& hall, uint32_t now_ms);
    void step(int16_t commanded_permille, uint32_t dt_ms);
    void markInvalid();
    bool valid() const;
    int16_t angleDdeg() const;
    r2link::DomeReference reference() const;
    r2link::SeekDirection shortestRoute(int16_t target_ddeg) const;
    static int16_t normalizeDdeg(int32_t ddeg);
};
```

### DomeController
```cpp
class DomeController {
public:
    DomeController();
    void bindProfile(const CommissioningProfile* profile);
    void update(const RcSnapshot& rc, uint32_t now_ms,
                r2link::DriveIntent drive_intent = r2link::DriveIntent::Stationary);
    void updateHall(const r2link::HallState& hall, uint32_t now_ms);
    r2link::Result request(const r2link::DomeRequest& req, uint16_t sequence, uint32_t now_ms);
    void stop(uint32_t now_ms);
    r2link::Result releaseStop(uint16_t current_epoch, uint32_t now_ms);
    void setMotionLocks(uint8_t reasons);
    void peerLost(uint32_t now_ms);
    ServoCommand output() const;
    r2link::DomeState state() const;
    r2link::DomeOwner owner() const;
    uint16_t authorityGeneration() const;
    bool angleValid() const;
    int16_t estimatedAngleDdeg() const;
    bool takeEvent(r2link::Event& ev);
};
```
