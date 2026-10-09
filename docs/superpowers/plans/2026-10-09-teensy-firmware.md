# Teensy and ESP32 Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Work directly unless the user requests delegation.

**Goal:** Build the Teensy body firmware, implement a shared two-way body/dome protocol, and migrate only the ESP32 hardware-control integration points.

**Architecture:** Teensy reads the receiver and owns both VESCs, dome servo, DFPlayer and handheld telemetry. ESP32 keeps ReelTwo, lighting, holo control, sound selection, macros, Wi-Fi and preferences. Portable codecs/state machines run against host fakes; thin Arduino adapters connect them to the real UARTs and GPIO.

**Tech Stack:** PlatformIO; Teensy Arduino; existing pinned ESP32/ReelTwo environment; C++ compatible with the existing ESP32 toolchain; Python unittest compiling production C++; existing Node diagram tests.

**Spec:** [Teensy body controller design](../specs/2026-10-09-teensy-body-controller-design.md)

**Planning revision:** Incorporates the approved two-Hall/one-magnet layout, autonomous dome priorities, CH9 Auto Dome, selective event override, Faint without motion locks, brake-before-reverse and wireless commissioning. User authorized implementation after plan consistency checks. Existing physical guides/graph require the second Hall and radio-policy updates in Task10.

## Global Constraints

- Complete the physical documentation plan before starting this plan.
- Nothing has been wired. Use the reviewed first-build pin map and power/programming arrangement from the physical phase. If that review changes a proposed assignment, update this plan and the shared design before implementing it.
- The body controller is a Teensy 4.1 in the ordered Treedix socketed screw-terminal carrier.
- Body and dome 5V positives remain separate. Grounds are common.
- Physical power distribution uses the owned screw-terminal blocks with selective inline branch fuses; no separate 5V fuse boxes.
- Approved physical fuses: B-SERVO body servo 5A; B-LOGIC body electronics 2A; D-SERVO dome holo-servos 5A; D-LOGIC dome electronics/lighting 3A. These are settled assembly values, not firmware current-limit defaults.
- User selected 5A for the dome PCA9685 V+ holo-servo branch; firmware does not reinterpret this as a servo current limit.
- Teensy uses a cut VUSB/VIN factory link, external body5V at VIN and normal USB for installed programming. USB alone does not power the board after modification; document this in upload/debug instructions.
- ESP32 Serial2 body link uses GPIO16 RX / GPIO17 TX. Front Hall19 and rear Hall18 use dome shifter channels2/3; GPIO5/4 are spare. PCA9685 SDA21/SCL22 are unchanged. Two rotating KY-003 modules180 degrees apart use one stationary magnet.
- Body and dome use selected Lonely Binary four-channel MOSFET converters from kit B0FFMLDYNY. The old converters are being returned; no TXS0108E or Pololu substitution.
- No oscilloscope or logic analyzer is available or required. Provide USB counters for valid/invalid frames, sample age, telemetry polls/responses, queue errors and firmware scheduling latency; commissioning uses these with a multimeter and handheld display.
- CH6 / SwA is drive enable. Receiver failsafe sets CH6 OFF.
- CH9 / SwD is Auto Dome enable, independent of CH6; failsafe CH9 OFF. CH3/10 are unused. Remove radio holo tilt/random controls.
- Dome priority: STOP/maintenance -> manual stick -> driving alignment -> event -> idle. Forward/reverse face their Hall reference; pivot holds current facing. Manual release during driving immediately resumes alignment.
- ESP32 selects idle/event motion; Teensy owns arbitration, reference seek and calibrated angle estimation. Idle begins20seconds after the last drive/manual/event activity, using approximate±45-degree sweeps with pauses and front re-referencing.
- Event takeover cancels only dome participation permanently for that event. Started audio/lights/holos continue. Leia awaiting front cancels on takeover/Auto Dome OFF; Leia triggered with Auto Dome OFF skips alignment. Faint never locks feet/dome.
- Normal drive duty is acceleration-ramped; neutral/safety stops request braking without that ramp. Opposite-sign drive requires fresh low-eRPM feedback and a calibrated dwell per wheel, not a guessed reversal delay.
- VESC voltage supplies handheld battery telemetry. FS-CVT01 is not installed in the active wiring. F6 becomes an empty spare.
- There is one actuator owner. ESP32 never writes VESC packets, DFPlayer packets, or dome servo pulses after migration.
- Keep ReelTwo, AstroPixels displays, shaders, I2C/PCA9685, holo limits, lighting commands, Wi-Fi credentials, routes, preference keys, and macro selections.
- Ship with `allow_remote_drive = false`. No existing macro gains foot movement.
- Shipping defaults keep motion disabled until the commissioned profile is saved.
- No long delays, response-wait loops, software UARTs, or heap allocation in the Teensy control loop.
- No assembled rotation/Hall/calibration test requires USB access on either board. Use a phone-connected ESP32 `/commissioning` page and typed body requests; USB is for initial flashing and stationary individual setup. All external programming cables are disconnected before rotation.
- Service access is by removable dome, not a cut body panel/external USB ports. Teensy/VESCs mount near the body top. Body-side ring leads have keyed detachable power/signal connectors; the stationary housing restraint disengages vertically with the dome/post assembly. Teensy reflashing requires dome removal; preserve wireless assembled testing.
- Write plain, declarative instructions. State errors explicitly in logs, link status and the dashboard.
- Do not port the new architecture into the historical FastLED or Nano sketches.
- Work in the current worktree. Commit task-sized changes with the required co-author trailer; do not push unless requested.

---

## File structure

Keep source files small and hardware ownership explicit:

```text
shared/R2BodyLink/
  library.json
  include/R2BodyLink/Bytes.h
  include/R2BodyLink/Messages.h
  include/R2BodyLink/Codec.h
  include/R2BodyLink/Endpoint.h
  src/Codec.cpp
  src/Endpoint.cpp

TEENSY_BODY_CONTROLLER/
  platformio.ini
  README.md
  config/tracks.csv
  src/main.cpp
  src/body/Pins.h
  src/body/HardwareAdapters.h
  src/body/ConfigStore.h
  src/body/ConfigStore.cpp
  src/body/IbusInput.h
  src/body/IbusInput.cpp
  src/body/IbusTelemetry.h
  src/body/IbusTelemetry.cpp
  src/body/VescLink.h
  src/body/VescLink.cpp
  src/body/DriveController.h
  src/body/DriveController.cpp
  src/body/DomeController.h
  src/body/DomeController.cpp
  src/body/DomePosition.h
  src/body/DomePosition.cpp
  src/body/DfPlayer.h
  src/body/DfPlayer.cpp
  src/body/BodyController.h
  src/body/BodyController.cpp

ASTROPIXELS_PLUS_UNIFIED/
  BodyClient.h
  BodyClient.cpp
  RemoteAudio.h
  RemoteAudio.cpp
  DomeBehaviour.h
  DomeBehaviour.cpp
  ASTROPIXELS_PLUS_UNIFIED.ino   # targeted integration changes
  MarcduinoSound.h              # preserve banks/parser; replace DF hardware calls
  MarcduinoSequence.h           # keep command names; use acknowledged operations
  WebPages.h                    # status, rejection feedback and maintenance gate
  platformio.ini                # shared library; remove obsolete DF driver dependency

tools/generate_track_catalog.py
tests/cpp_test_support.py
tests/body_fakes.h
tests/test_body_link.py
tests/test_body_radio.py
tests/test_body_vesc.py
tests/test_body_drive.py
tests/test_body_dome.py
tests/test_dome_behaviour.py
tests/test_body_audio.py
tests/test_body_controller.py
tests/test_body_esp32.py
tests/test_body_integration.py
tests/test_track_catalog.py
```

Generate `TEENSY_BODY_CONTROLLER/include/TrackCatalog.h` from the CSV during the build; do not commit the generated file. Add that exact generated path to `.gitignore`. Do not add a general ignore rule that hides other headers.

## Shared programming interfaces

Use namespace `r2link` for shared protocol and namespace `body` for body logic.

`BytePort` is the narrow UART abstraction:

```cpp
struct BytePort {
    virtual ~BytePort() {}
    virtual bool read(uint8_t& value) = 0; // false: no byte available
    virtual size_t writable() = 0;
    virtual size_t write(const uint8_t* bytes, size_t length) = 0;
};
```

Arduino implementations use `HardwareSerial.available()`, `read()`, `availableForWrite()` and `write()`. A zero-capacity TX queue must return to the scheduler; partial writes retain their offset.

Tests supply fake ports with input/output buffers and fake time. Production uses fixed arrays; test fakes may use standard containers.

DriveState wire values: Boot=0, Disarmed=1, Qualifying=2, Armed=3, Fault=4, Locked=5. DomeState: Inhibited=0, Manual=1, RemoteVelocity=2, SeekingReference=3, HoldingReference=4. DriveIntent: Stationary=0, Forward=1, Reverse=2, Pivot=3. DomeOwner: None=0, Manual=1, Drive=2, Event=3, Idle=4, Startup=5. Define these once in `Messages.h`; serialize explicit fields, never native struct padding.

Capability bits: bit0 dual VESC feedback, bit1 audio status, bit2 Hall input, bit3 handheld telemetry, bit4 remote-drive interface. Body advertises 0/1/3/4; dome advertises 2. Capability 4 means requests are understood, not that remote drive is permitted.

Use the spec's message IDs, field types, limits, units, timing and ownership rules without renaming them between tasks.

The following types are shared contracts, not separate implementations:

| Type / owner | Fields or contract |
| --- | --- |
| `r2link::Result`, `Event`, `AudioStatus`, `DomeRequest`, `AudioRequest`, `DriveRequest`, `HallState`, `RcStatus`, `BodyStatus` / `Messages.h` | Exact typed fields from the spec's corresponding payloads; RcStatus is RC_STATUS; enum underlying types are fixed-width |
| `body::VescProfile` / `ConfigStore.h` | firmware_major/minor u8, layout_id u8, direction i8 (+1/-1), motor_limit_mA u32, battery_limit_mA u32, regen_limit_mA u32, brake_mA u32, cutoff_cV u16, overvoltage_cV u16, timeout_ms u16, timeout_brake_mA u32, reversal_erpm_limit u32, reversal_dwell_ms u16 |
| `body::CommissioningProfile` / `ConfigStore.h` | wheels[2] VescProfile, acceptance_flags u32, servo_neutral_us/min_us/max_us u16, pulse_sleep_verified bool, allow_remote_drive bool, duty_slew_permille_per_s u16, auto_speed_percent u8, cw_ddeg_per_s u16, ccw_ddeg_per_s u16 |
| `body::VescSample` / `VescLink.h` | wire VESC_STATUS fields plus sample_ms u32, valid bool and profile_match bool; source age is calculated on access |
| `body::SensorSnapshot` / `IbusTelemetry.h` | voltage_cV u16, hottest_mosfet_dC i16, voltage_valid bool, temperature_valid bool |
| `body::WheelCommand` / `DriveController.h` | mode enum Duty/Brake, duty_permille i16, brake_mA u32 |
| `body::WheelCommands` / `DriveController.h` | left and right WheelCommand |
| `BodyRcState` / `BodyClient.h` | RC_STATUS fields, received_ms u32, valid bool, effective_age_ms u32 |
| `BodyVescState` / `BodyClient.h` | VESC_STATUS fields, received_ms u32, valid bool, effective_age_ms u32 |
| `AudioPriority` / `RemoteAudio.h` | Ambient=0, Foreground=1; same values as protocol |
| `RequestHandle` / `BodyClient.h` | sequence u16, queued bool; queued false means no action was submitted |

The neutral window is 1460-1540us inclusive. A valid `RcSnapshot` contains sample time; callers calculate age with unsigned subtraction. Other freshness helpers also use unsigned subtraction and saturating wire ages.

Each controller receives its configuration by constructor: `DriveController(const CommissioningProfile&)`, `DomeController(const CommissioningProfile&)`, and `DfPlayer(BytePort&, const TrackInfo* catalog, size_t count)`. Keep a stable profile object; apply a validated replacement only while motion is inhibited.

Acceptance flags are individually named bits in `ConfigStore.h`: shifter communication, receiver failsafe including CH9, sensor telemetry, left/right VESC profiles and direction, timeout/full-pack braking, loaded stopping, reversal and ramp calibration, servo neutral, front/rear references and automatic timing calibration. Drive enable requires radio and wheel acceptance; manual dome requires radio and servo-neutral acceptance; automatic dome additionally requires both references and timing calibration. Do not make Hall calibration a prerequisite for foot-only commissioning. Shifter communication means concurrent RC/telemetry on the installed short harness. Pulse-sleep verification is optional.

Automatic moves use the one commissioned `auto_speed_percent` (1-25 inclusive) so clockwise/counterclockwise timing calibration has a defined speed. Record measured `cw_ddeg_per_s`/`ccw_ddeg_per_s` with the installed dome load. Zero calibration values disable automatic motion. Estimation at other manual speeds is invalidated until a Hall reference is reacquired; do not extrapolate a servo speed curve that was never measured. Estimate rounding retains fractional accumulation. Seek chooses a direction from a valid estimate, otherwise clockwise.

Use a fixed CSV header `track,duration_ms,completion_guard_ms`. All entries refer to folder1. Reject play requests outside folder1 or track1-255 rather than silently selecting another file.

## Task 1: Shared codec and safe Teensy project

**Files:**
- Create: `shared/R2BodyLink/library.json`, `Bytes.h`, `Messages.h`, `Codec.h`, `src/Codec.cpp`
- Create: `TEENSY_BODY_CONTROLLER/platformio.ini`, `src/main.cpp`, `src/body/Pins.h`, `README.md`
- Create: `tests/cpp_test_support.py`, `tests/test_body_link.py`
- Modify: `tests/test_plus_behavior.py` (import existing compilation helper)

**Interfaces:**
- Produces: `r2link::Frame`, `r2link::MessageType`, `r2link::Codec`, all typed payloads, encode/decode helpers and Teensy pin constants.
- `Frame` has version, type, flags, sequence, source_session, destination_session, length and `uint8_t payload[96]`.
- `Codec::encode(const Frame&, uint8_t*, size_t) -> size_t` returns zero on invalid arguments/capacity.
- `Codec::feed(uint8_t, uint32_t now_ms, Frame&) -> DecodeResult` returns None, FrameReady, or Error.
- `Codec::tick(uint32_t now_ms)` expires incomplete input.
- `crc16(const uint8_t*, size_t) -> uint16_t`; `newer16(uint16_t candidate, uint16_t previous) -> bool`.

- [ ] Extract the existing `run_cpp` helper into `tests/cpp_test_support.py`, adding optional `extra_sources` and `include_dirs` parameters. Preserve C++11 as its default and return `CompletedProcess`. Existing Plus tests import it and remain unchanged in behaviour.
- [ ] Write a failing production-code codec test:

```cpp
#include "R2BodyLink/Codec.h"
#include <cassert>
int main() {
    const uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    assert(r2link::crc16(check, sizeof(check)) == 0x31c3);
    r2link::Frame frame{};
    frame.version = 1;
    frame.type = r2link::MessageType::Heartbeat;
    frame.source_session = 1;
    frame.destination_session = 2;
    frame.length = 2;
    frame.payload[0] = 1;
    frame.payload[1] = 1;
    uint8_t wire[116]{};
    const size_t count = r2link::Codec::encode(frame, wire, sizeof(wire));
    assert(count && wire[count - 1] == 0);
    r2link::Codec parser;
    r2link::Frame decoded{};
    bool received = false;
    for (size_t i = 0; i < count; ++i)
        received |= parser.feed(wire[i], 10, decoded)
                    == r2link::DecodeResult::FrameReady;
    assert(received && decoded.source_session == 1 && decoded.length == 2);
    assert(r2link::newer16(0, 65535));
}
```

- [ ] Run `python3 -m unittest discover -s tests -p test_body_link.py -v`. Expect missing-header failure.
- [ ] Implement explicit little-endian field serialization, COBS, CRC, bounded input and the spec's 20ms parser timeout. Emit named error counters; malformed frames never become state.
- [ ] Add exact golden wire vectors, every possible split point, back-to-back frames, embedded zeros, wrong length/version/CRC, 96-byte payload, oversized input, timeout and timestamp/sequence rollover.
- [ ] Create the Teensy project:

```ini
[env:teensy41]
platform = teensy@6.0.0
board = teensy41
framework = arduino
monitor_speed = 115200
lib_extra_dirs = ../shared
build_flags = -DUSB_SERIAL
lib_deps =
    https://github.com/tonton81/WDT_T4#1910b57d24468cd3419959118a2e1e6192f5e7fa
```

- [ ] Define pins exactly: left RX0/TX1, right RX7/TX8, audio RX15/TX14, link RX16/TX17, RC RX21, telemetry single-wire24, servo2.
- [ ] Make the first `main.cpp` initialize USB and print `[BODY] UNCOMMISSIONED`. Do not send motor commands or non-neutral servo pulses.
- [ ] Run the link tests and existing Plus tests. Build `pio run -d TEENSY_BODY_CONTROLLER -e teensy41`. Expect PASS/SUCCESS.
- [ ] Commit with message `feat: add shared body protocol codec and Teensy target`.

## Task 2: Session handshake, acknowledged requests and configuration storage

**Files:**
- Create: `shared/R2BodyLink/include/R2BodyLink/Endpoint.h`, `src/Endpoint.cpp`
- Create: `TEENSY_BODY_CONTROLLER/src/body/ConfigStore.h`, `ConfigStore.cpp`
- Create: `tests/body_fakes.h`
- Modify: `tests/test_body_link.py`

**Interfaces:**
- Consumes: codec and message types from Task 1.
- Produces: `Endpoint(BytePort&, uint8_t role, uint32_t local_session)`.
- Methods: `tick(uint32_t now_ms)`, `connected(uint32_t now_ms) -> bool`, `peerSession() -> uint32_t`, `request(Frame&, uint32_t now_ms, uint16_t& request_seq) -> bool`, `publishLatest(const Frame&) -> bool`, `takeReceived(Frame&) -> bool`, `reply(const Frame&, Result, uint16_t detail) -> bool`.
- Produces: `ConfigStore::nextBootSession(uint32_t&) -> bool`, `load(CommissioningProfile&) -> ConfigResult`, `save(const CommissioningProfile&) -> bool`.
- `ConfigResult`: Uncommissioned, Ready, Corrupt, IoError. No generic false-success fallback.
- `CommissioningProfile` holds the per-wheel firmware/profile IDs, direction, current/brake/voltage/timeout values, passed-test flags, servo calibration and `allow_remote_drive=false`.

- [ ] Write a failing two-endpoint test with fake ports: request while disconnected fails; HELLO exchange establishes peer sessions; command retry produces one received action plus repeated cached replies.
- [ ] Add this restart case to the executable test:

```cpp
assert(bodyEndpoint.connected(now));
const uint32_t oldPeer = bodyEndpoint.peerSession();
// Construct a new dome Endpoint on the same test port with a new session.
assert(newDomeSession != oldPeer);
newDome.tick(now + 1);
bodyEndpoint.tick(now + 1);
assert(bodyEndpoint.peerSession() == newDomeSession);
assert(bodyEndpoint.pendingCount() == 0);
```

Define `pendingCount() -> size_t` as a read-only diagnostic. In the fixture, transfer each fake port's written bytes to its peer before ticking that peer.

- [ ] Run the link tests; confirm new cases fail.
- [ ] Implement role/capability HELLO checks, session matching, 100ms heartbeat, 300ms timeout, eight-item priority queue, streaming coalescing, 100ms retry/two-retry limit, 350ms deadline and 16-result duplicate cache. Reserve two queue slots for safety operations/events and use a separate bounded reply buffer. Discrete sequence IDs are assigned on queue acceptance; streaming IDs are assigned on transmission.
- [ ] Match a REPLY to expected type, sequence and session. Validate exact payload length and legal enum values before dispatch. Unknown types produce UNSUPPORTED replies, not successful parsing. Reject obsolete single-Hall payload length, reserved lock/reason1, and undefined reference/owner values.
- [ ] Add reliable EVENT delivery and replies without acknowledgement loops. Cached event receipts stop duplicate macro transitions.
- [ ] Add queue-full, wrong-session, changed-content duplicate, retransmitted velocity, wraparound and partial-TX tests.
- [ ] Coalesce VESC_STATUS by `(type, wheel)`, not type alone. Test that queued left/right snapshots both arrive under sustained traffic. Add queue residence time to source-age fields immediately before serialization, saturating at65535ms; test an old snapshot delayed behind safety messages remains stale.
- [ ] Store boot/profile records in separate double-buffered EEPROM regions with version, generation, CRC and commit marker. Increment the boot counter once per boot. A corrupt profile keeps motion disabled and reports CONFIG_CORRUPT.
- [ ] Keep a no-profile state distinct from corrupted storage. Allow profile provisioning only while CH6 OFF and actuators inactive.
- [ ] Use hardware-independent storage reads/writes in tests to simulate torn writes and failed saves.
- [ ] Run link tests and build Teensy. Commit `feat: add reliable controller link and persistent profiles`.

## Task 3: Receiver decoding and half-duplex telemetry

**Files:**
- Create: `body/IbusInput.h`, `.cpp`, `body/IbusTelemetry.h`, `.cpp`, `body/HardwareAdapters.h`
- Create: `tests/test_body_radio.py`
- Modify: Teensy `src/main.cpp`

**Interfaces:**
- Produces: `RcSnapshot` with channels[10], sample_counter, sample_ms and valid.
- `IbusInput::feed(uint8_t, uint32_t)`, `tick(uint32_t)`, `snapshot(uint32_t) -> RcSnapshot`.
- Produces: `SensorSnapshot` with voltage_cV, hottest_mosfet_dC, voltage_valid and temperature_valid.
- `IbusTelemetry::setMeasurements(const SensorSnapshot&)`; `feed(uint8_t, uint32_t now_us)`; `tick(uint32_t now_us, BytePort&)`.
- Produces: HardwareSerial-backed `BytePort` adapter and one Serial6 half-duplex adapter.

- [ ] Add tests that inject a checksum-valid frame with one invalid channel, including unused CH14; the previous snapshot and timestamp must remain unchanged.
- [ ] Test the exact boundary:

```cpp
input.feed(validFrameBytes, validFrameLength, 10);
assert(input.snapshot(260).valid);
assert(!input.snapshot(261).valid);
```

Provide `feed(const uint8_t*, size_t, uint32_t)` as a convenience overload of the byte parser. `validFrameBytes` is an actual 32-byte frame generated by the test's iBUS frame builder.

- [ ] Run radio tests and confirm failure.
- [ ] Move the existing iBUS validation contract into the portable parser; add overlapping-header recovery, 5ms partial-frame expiry and bounded processing.
- [ ] Configure Serial5 RX21 at 115200. Do not attach its TX20 to the receiver.
- [ ] Configure sensor UART:

```cpp
Serial6.begin(115200, SERIAL_8N1_HALF_DUPLEX);
Serial6.setTX(24, true);
```

- [ ] Implement sensor discovery 0x80, type 0x90 and measurement 0xA0 for addresses 1 and 2. Check checksum `0xffff - sum(bytes_except_checksum)` with little-endian checksum.
- [ ] Schedule the response 100us after receiving a complete valid poll; do not wait in a delay. Finish enqueueing its short response within 1ms. Reject unrecognized addresses/commands and ignore echo/response packets.
- [ ] Test voltage 1280 -> bytes 0x00,0x05 and temperature 25.0C -> encoded 650 -> bytes 0x8a,0x02. Test out-of-range values as invalid rather than overflowing.
- [ ] Stop responding to measurements when validity is false. Keep discovery/type responses available.
- [ ] Keep the FS-CVT01 out of the firmware and wire graph.
- [ ] Run radio tests and build Teensy. Commit `feat: add Teensy iBUS input and sensor telemetry`.

## Task 4: Two independent nonblocking VESC links

**Files:**
- Create: `body/VescLink.h`, `.cpp`
- Create: `tests/test_body_vesc.py`
- Modify: Teensy `src/main.cpp`
- Modify: `BODY_CONTROLLER_COMMISSIONING.md` (USB capture procedure)

**Interfaces:**
- `VescCodec::encodeDuty(int16_t permille, uint8_t*, size_t) -> size_t`.
- `VescCodec::encodeBrake(uint32_t current_mA, uint8_t*, size_t) -> size_t`.
- `VescCodec::decodeValues(const uint8_t*, size_t, const VescProfile&, VescSample&) -> bool`.
- `VescLink(BytePort&, uint8_t wheel)`, `tick(uint32_t)`, `setDuty(int16_t)`, `setBrake(uint32_t)`, `sample(uint32_t) -> VescSample`, `setProfile(const VescProfile&)`.
- `VescSample` includes valid, source age, firmware/profile match and typed fields matching VESC_STATUS.

- [ ] Write tests for native command IDs: FW_VERSION=0, GET_VALUES=4, SET_DUTY=5, SET_CURRENT_BRAKE=7. No test or encoder emits FORWARD_CAN=34.
- [ ] Include a concrete encoder assertion before implementing it:

```cpp
uint8_t packet[32]{};
const size_t count = body::VescCodec::encodeBrake(1500, packet, sizeof(packet));
assert(count == 10);
assert(packet[0] == 2 && packet[1] == 5 && packet[2] == 7);
assert(packet[3] == 0 && packet[4] == 0);
assert(packet[5] == 0x05 && packet[6] == 0xdc);
assert(packet[count - 1] == 3);
```
- [ ] Test duty -350 -> signed big-endian -35000 in VESC payload and brake 1500mA -> positive big-endian 1500. VESC framing stays big endian where its protocol specifies it; this differs from the body/dome framing.
- [ ] Run VESC tests and confirm failure.
- [ ] Implement short and long VESC frames with a bounded 256-byte payload buffer, CRC, terminator, per-frame timeout and explicit unsupported/oversized diagnostics.
- [ ] Query firmware before values. Poll each independent UART every 100ms with at most one outstanding query. Control writes have priority over telemetry queries.
- [ ] Decode the validated profile's signed fields with explicit byte readers. Do not left-shift signed bytes. Check length before every field.
- [ ] Define a named legacy values-layout profile from the existing packet format, including voltage, currents, eRPM, duty, MOSFET temperature and fault offset. Do not infer firmware compatibility solely from payload length.
- [ ] Obtain each installed firmware/version and raw GET_VALUES reply using the elevated, inhibited USB diagnostic procedure. Save byte fixtures and compare decoded fields against VESC Tool. Enable that version/profile in the commissioned record only after the comparison passes.
- [ ] Add signed-current, truncated-frame, CRC-error, unsupported-version and independent-left/right freshness tests. Unsupported profiles permit diagnostic capture but never arm drive.
- [ ] Mark motor temperature invalid because motor TEMP is not wired.
- [ ] Build Teensy and run VESC tests. Hardware profile acceptance is recorded separately; if hardware has not arrived, keep motion disabled and record that acceptance as unperformed.
- [ ] Commit `feat: add independent VESC UART drivers`.

## Task 5: Drive enable, normalized mixing and explicit braking

**Files:**
- Create: `body/DriveController.h`, `.cpp`
- Create: `tests/test_body_drive.py`
- Modify: Teensy `src/main.cpp`

**Interfaces:**
- `MixedDuty { int16_t left; int16_t right; }`.
- `mixDrive(uint16_t throttle_us, uint16_t steer_us, uint16_t rate_permille) -> MixedDuty`.
- `DriveController::update(const RcSnapshot&, const VescSample&, const VescSample&, const CommissioningProfile&, uint32_t now_ms)`.
- `driveState() -> r2link::DriveState`; `commands() -> WheelCommands`.
- `WheelCommands` contains per-wheel mode (Duty or Brake) and duty/brake magnitude.
- `stop(uint32_t now_ms)`, `setMotionLocks(uint8_t reasons)`, `submitRemote(const r2link::DriveRequest&, uint32_t now_ms) -> r2link::Result`.

- [ ] Write the cap regression:

```cpp
const body::MixedDuty diagonal = body::mixDrive(2000, 2000, 350);
assert(diagonal.left == 350);
assert(diagonal.right == 0);
const body::MixedDuty pivot = body::mixDrive(1500, 2000, 350);
assert(pivot.left == 350 && pivot.right == -350);
```

- [ ] Add boot deflection, ON without observed OFF, short neutral window, lost input, continuing failsafe CH6 OFF, stale left/right telemetry and fault recovery tests.
- [ ] Run drive tests and confirm failure.
- [ ] Implement clamped mapping, pair normalization and selected rate. Preserve 35/70/100 settings but enforce the actual bound on each wheel.
- [ ] Implement OFF -> ON -> 500ms neutral arming, 20ms commands and both-controller gating. A fault stops both and requires rearming.
- [ ] Generate Brake commands at neutral and stops. Use the commissioned positive brake magnitude; do not send duty zero as a substitute.
- [ ] Add per-wheel reversal states Tracking/Braking/Qualifying; test that target sign reversal produces Brake until `abs(erpm)<=reversal_erpm_limit` continuously for `reversal_dwell_ms`. Reset qualification when speed rises or data becomes invalid/stale. Preserve requested target changes during braking without bypassing the gate. Clearing a stop/fault resets prior direction and requires local rearming.
- [ ] Add elapsed-time duty slew with fractional accumulation; ramp from zero after reversal, preserve normalized target caps and do not step directly to a newly requested duty. Normal zero demand and safety stops bypass acceleration ramp into Brake, not a gradual residual duty.
- [ ] Add a production test fixture with `armDrive`, `deliverWheelSamples`, `tickDrive` helpers that drive real radio/sample APIs:

```cpp
// Fixture profile: reversal limit100eRPM, dwell100ms, slew500permille/s.
armDrive(1000);
deliverWheelSamples(1000, 500, 500);
tickDrive(1000, 2000, 1500); // forward target
tickDrive(1500, 2000, 1500);
assert(drive.commands().left.duty_permille <= 250);
deliverWheelSamples(1500, 500, 500);
tickDrive(1500, 1000, 1500); // reverse target
assert(drive.commands().left.mode == body::WheelMode::Brake);
deliverWheelSamples(1520, 80, 80);
tickDrive(1520, 1000, 1500);
deliverWheelSamples(1619, 80, 80);
tickDrive(1619, 1000, 1500);
assert(drive.commands().left.mode == body::WheelMode::Brake);
deliverWheelSamples(1620, 80, 80);
tickDrive(1620, 1000, 1500);
// Reverse is eligible now; slew starts at zero, never a full reverse step.
assert(drive.commands().left.duty_permille <= 0);
```

- [ ] Test one-wheel reversals during pivots, zero-crossing noise, fresh out-of-threshold samples, missing eRPM validity, milliseconds wrap and STOP during ramp. Fixture numbers are test data, not shipping motor settings.
- [ ] Produce `DriveController::intent() -> r2link::DriveIntent`; base it on permitted wheel targets and reversal state. During brake-before-reverse keep prior facing until opposite power is permitted. Publish intent changes immediately to dome arbitration and BODY_STATUS.
- [ ] Require a commissioned current/voltage/timeout/brake profile. Do not guess winding-current values.
- [ ] Implement remote request validation with `allow_remote_drive=false` by default. Test INHIBITED responses, no wheel commands from Wi-Fi, and manual priority if a test profile explicitly enables remote requests.
- [ ] Test thresholds exactly: input stale >250ms, telemetry stale >500ms, neutral qualification >=500ms, CH6/CH9 thresholds1250/1750, reversal dwell boundary and millis rollover. Remove nominal frame fields that imply CH3/CH9 still control holos.
- [ ] Run drive/VESC tests and build Teensy. Commit `feat: enforce local drive authority and braking`.

## Task 6: Dome arbitration, dual references and estimated position

**Files:**
- Create: `body/DomeController.h`, `.cpp`
- Create: `body/DomePosition.h`, `.cpp`
- Create: `tests/test_body_dome.py`
- Modify: Teensy `src/main.cpp`

**Interfaces:**
- `DomeController::updateRc(const RcSnapshot&, uint32_t)`.
- `updateHall(const r2link::HallState&, uint32_t received_ms)`.
- `updateDrive(r2link::DriveIntent, uint32_t now_ms)`.
- `owner() -> r2link::DomeOwner`, `authorityGeneration() -> uint32_t`.
- `DomePosition(const CommissioningProfile&)`, `updateHall(const HallState&, uint32_t)`, `integrate(int16_t speed_percent, uint32_t)`, `invalidate()`, `valid() -> bool`, `angleDdeg() -> int16_t`. Use wrap range[-1800,1800); rear reference is-1800 on the wire.
- `request(const r2link::DomeRequest&, uint16_t request_seq, uint32_t now_ms) -> r2link::Result`.
- `tick(uint32_t)`, `cancel(uint32_t)`, `setMotionLocks(uint8_t)`.
- `output() -> ServoCommand { bool pulses; uint16_t pulse_us; }`.
- `takeEvent(r2link::Event&) -> bool`.
- `peerLost(uint32_t)` cancels only remote-owned motion.

- [ ] Add tests for front/rear already-active seek, masks/age validation, simultaneous detection rejection, manual override, 150ms lease expiry, duplicate lease rejection and10-second timeout. Use real generation/epoch fields in requests.
- [ ] Include a cancellation case:

```cpp
dome.cancel(100);
dome.updateHall(homeActive, 110);
dome.tick(110);
assert(dome.output().pulse_us == profile.servo_neutral_us);
assert(!dome.takeEvent(completedHome)); // no delayed success after cancellation
```

The fixture initializes `profile`, `homeActive` and a previously accepted seek. Cancel events may be consumed before testing that there is no completion.

- [ ] Run dome tests and confirm failure.
- [ ] Implement STOP/maintenance/manual/drive/event/idle priority; CH9 rather than CH6 gates automation. Startup Auto Dome enable searches front while stationary; driving searches front/rear instead and does not delay feet. A pivot cancels remote motion and applies neutral without initiating a seek.
- [ ] Attach bundled `Servo` to pin2. It drives body shifter LV3. Do not reuse the ESP32 LEDC duty constants.
- [ ] Implement reference seek at the saved low automatic speed, either direction when estimate is valid, clockwise when unknown; target active completes without movement. Publish correlated completion/fault/cancellation. Track held seek ownership so later manual/drive takeover cancels event participation even if the seek previously completed.
- [ ] Increment generation at takeover, disable and session change; publish it immediately. Reject queued old-generation event commands after manual release. A CANCEL request is owner-scoped and must never cancel higher-priority manual/drive alignment; STOP uses CONTROL_REQUEST.
- [ ] Integrate calibrated fixed-speed timed movement in DomePosition and re-anchor on either sensor. Manual output at uncalibrated speed invalidates estimates until reference. Invalid simultaneous references or seek failure inhibit automation until healthy CH9 OFF->ON, without disabling manual/feet.
- [ ] Test override then immediate centring while driving, CH6 OFF with CH9 ON, CH9 OFF/mid-value, pivot holding, changing drive direction mid-seek, full-turn wrap, sensor chatter, unknown position, estimated shortest-route tie resolved clockwise, stale Hall and lost takeover events.
- [ ] Add exact generation regression using fixture helpers that submit actual typed requests:

```cpp
const uint32_t oldGeneration = dome.authorityGeneration();
submitEventSeekFront(oldGeneration, 42, 100);
setDomeStick(1800, 120);
assert(dome.owner() == r2link::DomeOwner::Manual);
setDomeStick(1500, 140);
assert(submitEventVelocity(oldGeneration, 43, 140)
       == r2link::Result::Inhibited);
setAutoDome(false, 160);
assert(dome.output().pulse_us == profile.servo_neutral_us);
```
- [ ] Keep pulse-sleep disabled by default. Test neutral-before-detach and enable it only after the installed signal-loss test is recorded.
- [ ] Test manual dome control survives ordinary dome-link loss while remote motion stops; locks still stop manual motion.
- [ ] Run dome tests and build Teensy. Commit `feat: move dome actuator and homing control to Teensy`.

### Task 6b: ESP32 autonomous dome scheduler

**Files:** Create `ASTROPIXELS_PLUS_UNIFIED/DomeBehaviour.h`, `.cpp`, `tests/test_dome_behaviour.py`. Integrate with BodyClient in Task9 and the main loop in Task10.

**Interfaces:** Define `DomeBehaviourInput` in `DomeBehaviour.h`: `r2link::RcStatus rc`, `r2link::BodyStatus status`, `bool rc_fresh`, `bool status_fresh`, `bool event_active`; the adapter in Task9 computes freshness. `DomeBehaviour::tick(const DomeBehaviourInput&, uint32_t now_ms)`, `onReply(uint16_t, Result)`, `onEvent(const Event&)`, `onPeerLost(uint32_t)`. Constructor receives a request sink and injected bounded random provider. Define sink interface `submit(const DomeRequest&, uint32_t now_ms, uint16_t& sequence) -> bool` and random interface `pick(int32_t min_inclusive, int32_t max_exclusive) -> int32_t` here so this portable task compiles before BodyClient exists. Status carries generation, owner, intent and valid estimated angle. This scheduler never writes a servo and never performs a second motor-side seek.

- [ ] Write a fake-clock test with actual DomeBehaviour: no command before20seconds, active event/drive/stick deflection postpones the deadline, and completion/cancellation resets it.

```cpp
// Fixture initially stationary, Auto Dome ON, front startup completed at0.
behaviour.tick(input, 19999);
assert(sink.count() == 0);
behaviour.tick(input, 20000);
assert(sink.last().operation == r2link::DomeOperation::SeekReference);
assert(sink.last().reference == r2link::DomeReference::Front);
```

- [ ] Run `python3 -m unittest discover -s tests -p test_dome_behaviour.py -v`; confirm missing implementation failure.
- [ ] Implement states WaitingIdle/Referencing/Pausing/Sweeping/Returning; each accepts correlated responses/events, never blocking waits. Pick targets in[-450,450] tenths of degrees and pauses2000-6000ms using injected randomness. Skip targets equal to current estimate; motion uses saved automatic speed and <=150ms renewed leases.
- [ ] Seek front before the first sweep and after each out-and-back excursion. Missing status/estimate, errors, CH9 OFF, drive/manual takeover or peer change cancels outstanding idle requests and stops renewal. Do not repeatedly submit seeks during a fault; wait for body automation availability.
- [ ] Test event end at10000ms -> no idle before30000ms; drive/manual end behave likewise. Test no accumulated stale movement on reconnect, latest generation tagging, bound enforcement, pauses, CH6 OFF idle and no mutation of event/audio states.
- [ ] Run the scheduler and dome tests. Commit `feat: schedule autonomous dome idle behaviour`.

## Task 7: Bidirectional DFPlayer driver and track metadata

**Files:**
- Create: `body/DfPlayer.h`, `.cpp`, `config/tracks.csv`
- Create: `tools/generate_track_catalog.py`
- Create: `tests/test_body_audio.py`, `tests/test_track_catalog.py`
- Modify: Teensy `platformio.ini`, `.gitignore`, `src/main.cpp`

**Interfaces:**
- `DfPlayer(BytePort&, const TrackInfo* catalog, size_t count)`, `tick(uint32_t)`, `request(const r2link::AudioRequest&, uint16_t owner_seq, uint32_t) -> r2link::Result`.
- `status(uint32_t) -> r2link::AudioStatus`, `takeEvent(r2link::Event&) -> bool`, `peerLost(uint32_t)`.
- `lookupTrack(uint8_t folder, uint16_t track) -> TrackInfo`.
- `TrackInfo { uint32_t duration_ms; uint32_t completion_guard_ms; }`; duration zero is unknown, not zero-length audio.
- Generator `generate(csv_path: Path, output_path: Path) -> None` rejects duplicate/out-of-range entries and writes a deterministic C++ table.

- [ ] Test actual folder-play command bytes for folder1/track110, volume bounds, checksum, 100ms spacing and split incoming packets.
- [ ] Use a fake UART and a request owner to exercise the production serializer:

```cpp
r2link::AudioRequest play{};
play.operation = r2link::AudioOperation::Play;
play.folder = 1;
play.track = 110;
play.priority = r2link::AudioPriority::Foreground;
assert(player.request(play, 42, now) == r2link::Result::Accepted);
player.tick(now);
assert(player.status(now).state == r2link::AudioState::Starting);
assert(!(player.status(now).validity & 1));
```

The fixture completes initialization first, empties its captured output and passes a one-track catalog to `DfPlayer`. Inject a checksum-valid playing-state reply only after a status query for request42 was transmitted; then assert PLAYBACK_STARTED identifies request42.
- [ ] Test ACK changes only request acceptance; a status response indicating playing produces PLAYBACK_STARTED; a matching finished response produces COMPLETED.
- [ ] Run audio tests and confirm failure.
- [ ] Implement startup discovery/reset and a 3-second initialization deadline without blocking. Initialize EQ/volume before accepting playback. Audio absence reports OFFLINE but does not disable manual feet.
- [ ] Use one queue/serializer for all DFPlayer packets. Stop supersedes queued play; foreground supersedes ambient; ambient cannot interrupt foreground.
- [ ] Validate 10-byte DFPlayer replies, status, ACK, finished/error and card events. Poll current state every 500ms without monopolizing Serial3.
- [ ] Do not correlate completion using track alone. Use the current request owner, observed start, serial ordering and matching device data. Flush obsolete receive state when replacing a track; reject late pre-start completion.
- [ ] Test one-way UART failures separately: commands sent without valid feedback report unconfirmed/offline playback, not a confirmed start.
- [ ] Test pause/resume elapsed bookkeeping. Label elapsed as Teensy's estimate from confirmed transitions, not a DFPlayer playback cursor.
- [ ] Seed the CSV with current macro/startup tracks. Use duration 0 until actual file lengths are supplied; use explicit guard_ms values for the current choreography.
- [ ] Set the guard for unknown ordinary tracks to 600000ms; define macro guards as current choreography time plus 2000ms while duration is unknown. Measured track duration plus 2000ms replaces that guard when metadata is supplied.
- [ ] Add `extra_scripts = pre:../tools/generate_track_catalog.py`. Separate its PlatformIO hook from its importable `generate` function so host tests do not import SCons.
- [ ] Test malformed CSV, unknown duration, known duration, deterministic output and compile the generated table in a host test.
- [ ] Remove no sound files and add no copyrighted audio.
- [ ] Run audio/catalog tests and build Teensy. Commit `feat: add acknowledged audio playback and track catalog`.

## Task 8: Integrate the Teensy scheduler, locks and diagnostics

**Files:**
- Create: `body/BodyController.h`, `.cpp`
- Modify: `src/main.cpp`, `body/HardwareAdapters.h`, `ConfigStore.*`, Teensy `README.md`
- Create: `tests/test_body_controller.py`

**Interfaces:**
- `BodyController::tick(uint32_t now_ms, uint32_t now_us)`.
- `handle(const r2link::Frame&, uint32_t) -> r2link::Result`.
- `status() -> r2link::BodyStatus`, `motionLocked() -> bool`, `deadlineHealthy() -> bool`.
- Produces: typed routing between shared Endpoint and body device controllers, telemetry aggregation, lock tokens/epochs, configuration CLI and watchdog feed policy.

- [ ] Write a simulated STOP/maintenance test: STOP increments epoch; stale motion is rejected; a matching lock survives disconnect; wrong-token unlock is rejected. Reject reserved Faint lock reason1.
- [ ] Use this state assertion inside the fixture's full-frame command test:

```cpp
const uint16_t oldEpoch = controller.status().control_epoch;
assert(controller.handle(stopFrame, now) == r2link::Result::Accepted);
assert(controller.status().control_epoch == uint16_t(oldEpoch + 1));
assert(controller.motionLocked());
assert(controller.handle(staleVelocityFrame, now)
       == r2link::Result::WrongEpoch);
```

Build `stopFrame` and `staleVelocityFrame` with the real typed encoders and current sessions; the velocity carries `oldEpoch`.
- [ ] Run body-controller tests and confirm failure.
- [ ] Implement the scheduler with telemetry service at least once per 1ms, bounded UART drains, drive every20ms, VESC polls100ms, RC publish20ms, status200ms and protocol timers.
- [ ] Use at most 96 received bytes per full-duplex port per pass, no blocking TX flush and latest-sample queues. Instrument maximum loop duration and missed deadlines.
- [ ] Set receiver sensor processing first, then RC/VESC RX, body-link dispatch, actuator decisions, audio work, and low-priority status. Do not service network code on Teensy.
- [ ] Implement STOP/lock/release validation from the spec, including token-checked UNLOCK and guarded RECOVER_LOCKS after a dome restart. Increment the epoch on successful lock-state changes and publish BODY_STATUS immediately. Reply ACCEPTED only after state changes. Mechanical stopping is verified separately.
- [ ] Aggregate handheld voltage/temperature from two valid <=500ms records; mark stale readings invalid and stop answering their measurement polls.
- [ ] Add a bounded line-based USB CLI with commands `status`, `rc`, `vesc`, `profile show`, `profile set FIELD VALUE`, `profile save`, `profile enable`, and `stop`.
- [ ] Specify editable fields in a table: per-wheel direction, firmware/profile, motor/battery/brake limits, voltage, timeout/brake, reversal eRPM/dwell; duty slew; servo neutral/min/max; auto speed and CW/CCW calibration; subsystem acceptance flags. Validate field widths and automatic speed1-25, require CH6 OFF/CH9 OFF and actuators neutral for writes. Unknown fields produce an error. `profile enable` checks readiness separately for drive, manual dome and automatic dome; missing Hall calibration cannot enable automation or prevent an otherwise valid foot-only profile.
- [ ] Separate real accepted flags from test fixtures. A synthetic host profile must never become the production shipping default.
- [ ] Include `Watchdog_t4.h` and configure `WDT_T4<WDT1>` with zero-initialized `WDT_timings_t`, `timeout=1.0f`, `pin=0` and no callback. Enable it once synchronous GPIO/UART/storage initialization finishes, before asynchronous device discovery. Feed only after a complete healthy scheduling pass. Do not feed from timer interrupts. VESC's 150ms input timeout handles command loss before a watchdog reset.
- [ ] Test CPU stall/deadline fault, full UART queues, diagnostic flooding, invalid config, corrupted EEPROM and source-time rollover. Do not wait for USB to connect before starting control.
- [ ] Build Teensy and run all body tests. Commit `feat: integrate body control scheduler and maintenance locks`.

## Task 9: ESP32 body client and remote audio adapter

**Files:**
- Create: `ASTROPIXELS_PLUS_UNIFIED/BodyClient.h`, `.cpp`, `RemoteAudio.h`, `.cpp`
- Modify: ESP32 `platformio.ini`
- Create: `tests/test_body_esp32.py`

**Interfaces:**
- `BodyClient::begin(BytePort&, uint32_t local_session)`, `tick(uint32_t)`.
- `rcSnapshot(uint32_t) -> BodyRcState` with channels/valid/source age.
- `vescStatus(uint8_t wheel, uint32_t) -> BodyVescState`; stale values have validity false.
- `requestDome(...)`, `requestAudio(...)`, `requestControl(...) -> RequestHandle`.
- `RequestHandle { uint16_t sequence; bool queued; }`; `takeEvent(r2link::Event&)`.
- `publishHall(uint8_t valid_mask, uint8_t active_mask, uint32_t sample_counter, uint32_t now_ms)`.
- `bodyStatus(uint32_t) -> BodyStatusSnapshot { r2link::BodyStatus value; bool fresh; uint32_t effective_age_ms; }`; status freshness deadline300ms.
- `RemoteAudio::play(uint16_t track, AudioPriority) -> RequestHandle`; `stop()`, `pause()`, `resume()`, `setVolume(uint8_t)`, `status(uint32_t)`.
- `lastError() -> typed error/rejection record` on both adapters.

- [ ] Write tests proving source age plus local age expires RC independently of incoming heartbeats and expires each VESC separately.
- [ ] Inject a valid RC_STATUS with source_age_ms200 at local time1000, then a heartbeat at time1040:

```cpp
assert(client.rcSnapshot(1050).valid);
assert(!client.rcSnapshot(1051).valid);
```

The fixture delivers the real encoded RC_STATUS through its fake port and calls `tick(1000)`. The heartbeat does not alter its sample counter or receive timestamp.
- [ ] Run ESP32 adapter tests and confirm failure.
- [ ] Implement the client without Arduino dependencies in its logic. Put Serial2 and Preferences boot-counter access in the sketch/hardware adapter.
- [ ] Decode only known payload shapes; invalid messages do not refresh state. Store per-controller records, not a single global VESC value.
- [ ] Add the shared library path to ESP32 `platformio.ini`; preserve its existing platform/ReelTwo/FastLED versions.
- [ ] Implement request handles, events, typed rejection messages and offline behaviour. Retry/session rules come from Endpoint; do not add a second reliability layer.
- [ ] Implement RemoteAudio as a typed facade, not as a byte stream that tunnels legacy DFPlayer packets.
- [ ] Adapt BodyClient to the Task6b sink without altering its result semantics; a rejected queue submission is false with a visible diagnostic. Build DomeBehaviourInput from current RC/status freshness and event state; current generation comes from fresh BODY_STATUS, never a guessed zero.
- [ ] Test queue-full, lost reply, offline play, restart, duration validity and duplicate completion.
- [ ] Build ESP32 with these files linked but not yet replacing old ownership. This intermediate firmware is not ready for the first-build harness; do not begin combined hardware testing.
- [ ] Commit `feat: add ESP32 body client and remote audio facade`.

## Task 10: Replace only ESP32 hardware integration

**Files:**
- Modify: `ASTROPIXELS_PLUS_UNIFIED.ino`
- Modify: `MarcduinoSound.h`
- Modify: `tests/test_plus_behavior.py`, `tests/plus_macro_fakes.h`
- Modify: `tests/test_body_esp32.py`
- Modify: ESP32 `platformio.ini` (remove DFRobot hardware dependency)

**Interfaces:**
- Consumes: BodyClient, RemoteAudio and RC snapshots.
- Produces: the existing high-level helpers backed by body requests rather than local hardware.
- Keep `playDFPlayerTrack(uint16_t)`, `stopDomeMotion()`, `startDomeHoming(R2Macro)`, `startR2Macro(R2Macro)`, `cancelR2Macro()` as integration-facing entry points.
- Add `MarcSound::beginRemote(RemoteAudio&, int startup_track)`; preserve bank selection, command parser, volume and ambient scheduler APIs.

- [ ] Add failing integration-contract tests forbidding the primary sketch from containing `DfPlayerSerial`, `sendVescDuty`, `COMM_FORWARD_CAN`, `WRITE_DOME_SERVO`, `ledcAttachPin` or direct DFPlayer hardware initialization.
- [ ] Add the literal retired-owner test:

```python
def test_primary_sketch_has_no_retired_hardware_writers(self):
    source = SKETCH.read_text()
    for token in ("DfPlayerSerial", "sendVescDuty", "COMM_FORWARD_CAN",
                  "WRITE_DOME_SERVO", "ledcAttachPin", "dfPlayer.begin"):
        with self.subTest(token=token):
            self.assertNotIn(token, source)
```
- [ ] Run ESP32 tests and confirm failure.
- [ ] Replace pin roles with body RX16/TX17 on Serial2. Remove Serial1 iBUS setup, sound bit-bang stream, VESC mixer/parser and LEDC setup. Ensure BodyClient is the only Serial2 reader; disable legacy MarcDuino byte parsing on that UART, retaining Web/internal command processing.
- [ ] Replace `processIBusFrames` with body snapshot ingestion before transmitter/holo processing. Keep channels and RC mapping names so the retained logic remains bounded.
- [ ] Define front Hall GPIO19/rear GPIO18, publish normalized active/valid masks every20ms and on changes. Initialize both input levels before valid flags; no serial work inside ISR. Add both sensor pins to adapter tests.
- [ ] Remove ESP32 motor-side `processDomeRotation` and homing seek loops. Keep local pending-home/macro correlation, not a second homing state machine.
- [ ] Route sound operations in the kDFMini branch of MarcSound through RemoteAudio. Remove `DFRobotDFPlayerMini` member/include. Keep MP3Trigger/HCR code paths as reference alternatives; reject local DF initialization explicitly if called.
- [ ] Keep sound banks and reserved-track exclusions. Initialize bank indexes through a common helper used by `beginRemote` and retained alternative `begin`.
- [ ] Remove the DFPlayer library dependency only after no primary source includes it.
- [ ] Replace old source-extracted VESC/audio/iBUS tests with production-module tests from Tasks3-7. Keep equivalent end-to-end behaviours, not assertions that retired function names still exist.
- [ ] Remove RC_CH_HOLO_TILT/RC_CH_HOLO_ENABLE, `manual_holo_active`, stick-driven tilt,3-second hold and SwD random gating. Retain calibrated holo travel, autonomous random settings and event servo dispatch. CH3 must not alter any holo; CH9 alters only dome automation. Tick DomeBehaviour after BodyClient updates, without another angle controller.
- [ ] Update `BODY_CONTROLLER_WIRING.md`, `DOME_WIRING_DIAGRAM.md`, `SYSTEM_ARCHITECTURE.md`, `POWER_HARNESS_GUIDE.md`, both READMEs, `Master_R2D2_BOM.xls`, `wiring_visualizer.html` and both documentation/harness tests for two Hall sensors/one magnet, dome shifter ch3 and GPIO18, CH3unused/CH9Auto Dome, and no Faint lock. Keep the user's removed inspector migration banner removed.
- [ ] Build ESP32, run migrated Plus and ESP32 adapter tests, then both target builds. Commit `refactor: delegate body hardware from AstroPixels to Teensy`.

## Task 11: Acknowledged macros, STOP and both OTA paths

**Files:**
- Modify: ESP32 sketch, `MarcduinoSequence.h`, `WebPages.h`, `BodyClient.*`
- Modify: `tests/test_plus_behavior.py`, `tests/test_body_esp32.py`

**Interfaces:**
- Produces: `MacroPhase` = Idle, WaitingHome, WaitingAudio, Running, Finishing. Maintenance lock preparation is separate.
- Produces: `MaintenanceState` = Idle, Requested, Locked, Updating, Failed.
- `prepareMaintenance()`, `maintenanceReady() -> bool`, `releaseMaintenance()`.
- All body-changing Web/MarcDuino/radio entry points use these shared helpers.

- [ ] Add tests for all entry points: Web STOP, `:DMS`, `:SE00`, Faint, ordinary macro, Leia, preference reset, Wi-Fi save/reboot, explicit reboot, ArduinoOTA and web upload.
- [ ] Include an event-order test: home ACCEPTED alone must not start Leia; home COMPLETED -> play request; audio ACCEPTED alone must not start choreography; PLAYBACK_STARTED -> Running.
- [ ] Exercise `startR2Macro(R2_LEIA)` with fake acknowledged body requests; assert this ordering:

```cpp
startR2Macro(R2_LEIA);
assert(macro.phase == MacroPhase::WaitingHome);
deliverReply(homeSequence, r2link::Result::Accepted);
assert(macro.phase == MacroPhase::WaitingHome);
deliverEvent(r2link::EventKind::Completed, homeSequence);
assert(macro.phase == MacroPhase::WaitingAudio);
deliverReply(audioSequence, r2link::Result::Accepted);
assert(macro.phase == MacroPhase::WaitingAudio);
deliverEvent(r2link::EventKind::PlaybackStarted, audioSequence);
assert(macro.phase == MacroPhase::Running);
```

Define `MacroState macro` with phase, home_sequence, audio_sequence, dome_cancelled bool, dome_generation u32 and guard_deadline_ms in the sketch. A maintenance token belongs to MaintenanceState, not Faint. Test fakes implement `deliverReply(uint16_t, Result)` and `deliverEvent(EventKind, uint16_t)` by sending real packets through BodyClient, then invoking the macro processing tick; they never set the phase directly.
- [ ] Run integration/Plus tests and confirm failure.
- [ ] Faint requests audio and runs lighting/holo choreography without LOCK/UNLOCK packets. Test feet and manual dome remain available throughout its five-second effect.
- [ ] Use completion/cancellation/error plus explicit guard times to end macros. Late events from another sequence/session cannot finish a new routine.
- [ ] Manual/drive takeover sets `dome_cancelled` permanently for the event and ceases its future dome requests. Keep started audio/lights/holos running; no event cleanup may issue global dome cancel that suppresses drive alignment.
- [ ] Leia Auto Dome OFF at trigger goes directly WaitingAudio, no seek. Auto Dome ON goes WaitingHome only while body grants event authority. Manual/drive/CH9 OFF during WaitingHome cancels the pending message and invalidates its seek sequence; delayed completion never submits PLAY. If already driving/manual at trigger, reject alignment visibly.
- [ ] Test selective takeover after PLAYBACK_STARTED, takeover after home completes but before audio submission, Auto Dome OFF trigger, OFF during seek, obsolete scheduled event request after generation change and STOP cancelling everything. WaitingAudio means play was submitted; STOP may cancel it, but dome takeover does not replay or cancel already-submitted audio.
- [ ] Route STOP_ALL through body control and show "Pending", "Stopped", or "Body stop unconfirmed". Stop local holo motions immediately but do not claim body acknowledgement from that local change.
- [ ] Add the `/body` status page with both VESC records, state/age, audio, locks and rejection reason. Show unavailable fields as unavailable, not zero. Add an explicit "Recover body locks" action implementing RECOVER_LOCKS; show its CH6 OFF/neutral requirements and never invoke it automatically.
- [ ] Preserve `/dome`, `/sound`, `/logics`, `/wifi`, `/serial` and `/firmware`. Replace misleading labels about local UART/audio control.
- [ ] Add "Prepare update" to `/firmware`. Request and acknowledge the maintenance lock before enabling ArduinoOTA service. Its onStart handler verifies the existing lock; it does not try to create one inside a blocking update.
- [ ] Gate web upload before `Update.begin`. Reject unprepared upload, show an error and leave motion locked on update failure.
- [ ] Convert reboot/preference-reset/Wi-Fi-save reboot into asynchronous maintenance preparation. Do not retain `delay(1000)` as a substitute for body acknowledgement.
- [ ] On a failed/unconfirmed maintenance request, do not start an update or reboot. Provide USB/offline recovery instructions.
- [ ] Test lost locks/replies, peer reboot during update, failed upload, pending STOP, unchanged lighting/holo controls and macro trigger rearming after reconnect.
- [ ] Build ESP32 and run relevant tests. Commit `feat: acknowledge macros and gate OTA with body maintenance`.

## Task 12: End-to-end acceptance and documentation release

**Added required deliverable: wireless commissioning.** Before hardware acceptance, implement the spec's `/commissioning` workflow in `WebPages.h` and `BodyClient.*`, with a portable body-side `DomeCalibration.h/.cpp` and `tests/test_dome_calibration.py`. Add typed calibration operations/status to `Messages.h`, shared codec and client tests before integrating the page; do not use unrestricted CLI forwarding. This is a prerequisite for assembled testing, not an optional follow-up.

Use exact COMMISSION_REQUEST0x24(14bytes), COMMISSION_STATUS0x33(36bytes) and DIAGNOSTICS0x34 records from design section9, including field IDs/bounds and read subtypes. Add types/codecs in Task1 and reliable dispatch in Task8; implement body calibration before ESP32 page integration. Results are bounded local records returned on status requests; retries of Begin never launch a second run. A run requires fresh radio/Hall/link, CH6 OFF, CH9 ON, neutral sticks, no locks, and inhibits feet/idle/events. Timing tests bypass missing timing readiness, never accepted neutral/STOP gates. The isolated neutral bootstrap permits only trial1400-1600us pulses and disables pulses on cancellation when no neutral has been accepted. Keepalive100ms/expiry300ms; per-reference timeout10seconds. Any manual/enable/permission/loss interruption cancels and discards partial results.

- [ ] Write failing portable tests: disconnect browser keepalive during motion -> neutral at300ms; CH6 ON/manual/CH9 OFF cancels; STOP remains absolute; repeated Start has one run; stale Hall never completes; no motor duty is emitted.
- [ ] Measure three full revolutions per direction using successive front detection edges, requiring rear detection between them. Test artificial magnet active windows do not shorten the measured360-degree duration. Report median-derived speed, individual times and errors; never auto-save an interrupted run.
- [ ] Implement explicit save with CH6/CH9 OFF and actuators neutral, strict field validation, EEPROM success/read-back acknowledgement and visible page errors. Do not mark unrelated acceptance flags passed.
- [ ] Add page/adapter tests for Hall indicators, pending run/save, lost replies, visible timeout, cancellation, no stale success and saved-value read-back.
- [ ] Rewrite installed commissioning steps to use this page for receiver/counters, Hall checks, neutral/reference tests and timing runs. Retain USB only for bare flashes and stationary VESC setup. No laptop tether during rotation.

**Files:**
- Create: `tests/test_body_integration.py`
- Modify: both firmware READMEs, `README.md`, `BODY_CONTROLLER_COMMISSIONING.md`
- Modify: physical guides only where implemented behaviour differs from the approved spec

**Interfaces:**
- Consumes: real shared/body/client modules and fake UART devices.
- Produces: a passing integration matrix and explicit hardware acceptance status.

- [ ] Create a host rig with body/dome endpoints, actual state machines, synthetic iBUS frames and scripted VESC/DFPlayer replies. Compile the production `.cpp` files using the shared C++ helper.
- [ ] Run a complete scenario: boot offline -> handshake -> healthy dual telemetry -> observed CH6 OFF -> ON -> 500ms neutral -> drive -> manual dome -> home -> Leia playback -> completion -> STOP -> recovery.
- [ ] Inject dropped/duplicated/corrupted bytes on each link direction, old-session frames, full queues, stale telemetry, either VESC failure, continued receiver failsafe frames and unplugged input.
- [ ] Verify ordinary dome-link loss preserves healthy manual feet but cancels automatic actions; operator/maintenance locks remain latched. Faint has no lock.
- [ ] Add combined scenarios for dual Hall front/rear, calibrated estimated idle sweeps,20-second restart after events, CH9 OFF with manual control, forward/reverse alignment, pivot holding, event dome takeover without audio interruption and per-wheel ramped brake-before-reverse.
- [ ] Test retry idempotence with the same track twice as two distinct requests: each intentional request plays once; retransmission never plays it twice.
- [ ] Run:

```sh
python3 -m unittest discover -s tests -v
pio run -d TEENSY_BODY_CONTROLLER -e teensy41
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus
git diff --check
```

- [ ] Check build output for the expected pin map, target, pinned dependencies and no warnings about unsupported telemetry timing.
- [ ] Update firmware status in the guides after both builds and simulated integration pass. Do not restore the inspector migration warning the user removed. State hardware commissioning is still required until recorded.
- [ ] Follow `BODY_CONTROLLER_COMMISSIONING.md` on the installed hardware: Lonely Binary short-harness communication and concurrent sensor/RC bus, audio feedback, manual override, full dome rotations, homing timing, elevated wheels, braking, slow floor run and both OTA methods. Diagnose communication failures using multimeter checks, USB counters, isolated short-harness tests and a spare selected module. Test a deliberate CPU stall with the dome unloaded; record watchdog reset delay and dome stopping behaviour rather than applying the VESC timeout claim to the servo.
- [ ] Require measured Hall-to-neutral <=50ms under normal installed operation; remote velocity stops within its150ms lease plus one20ms actuator cycle after renewal loss. Record mechanical overshoot rather than assuming zero stopping distance.
- [ ] Record both Hall reference positions, clockwise/counterclockwise timing at saved automatic speed, drive slew rate, per-wheel reversal eRPM/dwell and full-pack brake behaviour. Keep uncalibrated automatic positioning/reversal disabled and test staged manual-only operation separately.
- [ ] Run a ten-minute concurrent bench test with receiver input, both VESC feedback streams, audio, lighting and dashboard activity. Record no missed20ms drive output deadline, sensor replies enqueued within1ms, and CRC/error counters while rotating the dome.
- [ ] If a hardware check fails, fix its driver/wiring root cause and repeat that check before enabling its profile flag. Do not weaken the test to hide the failure.
- [ ] Keep commissioning disabled when hardware is absent. Report software readiness and unperformed hardware acceptance separately.
- [ ] Commit `test: verify body and dome integration and publish build guides`.

## Commit convention

Use task-specific file lists, not `git add .`. Every implementation commit includes:

```text
Co-authored-by: Copilot App <223556219+Copilot@users.noreply.github.com>
```

Do not amend unrelated commits. Do not push or create a PR unless requested.

## Plan coverage gate

Before declaring the migration complete, confirm:

- Direct full-duplex control and feedback from **both** VESCs.
- Receiver channel input **and** two-way sensor-bus telemetry.
- Body-local dome servo and DFPlayer with no dedicated slip-ring signals.
- Shared versioned protocol, freshness, sessions, acknowledgements and completion events.
- CH6 foot enable / CH9 Auto Dome independent, unused CH3, neutral rearming, rate caps, acceleration slew and per-wheel feedback-gated reversal.
- Two dome Hall inputs/one magnet; Teensy reference seek/estimate has one owner, ESP32 idle/event scheduler never writes actuators.
- Startup alignment, front/rear driving alignment, pivot hold,20-second idle resets and selective permanent event-dome cancellation.
- Autonomous/choreographed holos only, Faint without motion locks, Leia OFF-mode and pre-start cancellation rules.
- No duplicated ambient scheduler or competing hardware writer.
- Existing lighting/holo/Wi-Fi surfaces remain intact.
- Web and Arduino OTA both acquire the same body lock first.
- Physical graph, guides, BOM and actual firmware pins agree.

Execution starts only after the user reviews and approves the design and both plans.
