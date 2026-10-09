# Task 3 — receiver decoding and half-duplex telemetry

Base: `8272d89`. Scope: Task 3 only. No nested agents, VESC implementation,
audio, arbitration, actuator objects, servo pulses, or dome-link UART startup.
Unrelated dirty physical documentation was left untouched and is not staged.

## Deliverables

- Portable production `body/IbusInput.{h,cpp}` and `body/IbusTelemetry.{h,cpp}`.
- Production `body/HardwareAdapters.h`, using the actual shared `BytePort` API.
- Radio-only startup in Teensy `src/main.cpp`, retaining disabled
  `LinkBootstrap(0)` on its `NullPort`.
- `tests/test_body_radio.py` and a narrow Arduino UART boundary fixture.
- Existing native-test helper changed only to put its self-cleaning build
  directory inside this repository, not system temporary directories.
- This ignored report and its raw command-output artifacts remain local.

## API and integration handoff

All parser operations are single-control-loop-context operations. Production
radio parsing has fixed storage and no allocation, waits, flush, delay, or
control-loop heap use.

### Receiver

```cpp
body::IbusInput input;
input.feed(uint8_t byte, uint32_t now_ms);
input.feed(const uint8_t* bytes, size_t length, uint32_t now_ms);
input.tick(uint32_t now_ms);
body::RcSnapshot rc = input.snapshot(uint32_t now_ms);
const body::IbusInputCounters& counts = input.counters();
```

`RcSnapshot` contains `uint16_t channels[10]`, `uint32_t sample_counter`,
`uint32_t sample_ms`, `bool valid`, and `uint16_t flags`.

- Both header bytes are checked. The 32-byte frame's checksum is
  `0xffff - sum(first 30 bytes)`, little-endian.
- All 14 raw channels must be in **900..2100 inclusive**, including CH14.
  Only after complete validation are the first ten channels clamped to
  **1000..2000 inclusive** and published together.
- A rejected frame never changes published channels, sample counter, or
  sample timestamp. Validity is calculated from the previous accepted sample.
- Partial candidates expire at **5ms**, measured from candidate start, using
  unsigned time subtraction; `feed()` also performs expiry before appending.
- A sample at 10ms is valid at 260ms and invalid at 261ms.
- A corrupt/truncated candidate retains an overlapping header suffix for
  recovery. Each byte's candidate check/suffix scan is bounded by 32 bytes.
- `flags`: bit0 fresh valid channel stream; bit1 CH6 >=1750; bit3 CH9 >=1750.
  Bit2 is never set by this parser: later neutral qualification owns it.
  Stale samples have flags zero. These flags do not claim RF link confirmation,
  actuator readiness, or permission to move.
- Zero-based mapping: `kSteering=0`, `kThrottle=1`, `kUnusedCh3=2`,
  `kManualDome=3`, `kDutyRate=4`, `kFeetEnable=5`, `kMood=6`,
  `kMacroTrigger=7`, `kAutoDome=8`, `kUnusedCh10=9`.
  There are no manual holo controls.
- Counters: `valid_frames`, `checksum_errors`, `channel_errors`,
  `header_errors`, `partial_timeouts`. Header errors count discarded
  non-header bytes/candidates; they are not a count of whole rejected frames.

### Sensor telemetry

```cpp
body::SensorSnapshot values{};
values.voltage_cV = 1280;
values.hottest_mosfet_dC = 250;
values.voltage_valid = true;
values.temperature_valid = true;
body::IbusTelemetry telemetry;
telemetry.setMeasurements(values);
telemetry.feed(uint8_t byte, uint32_t now_us);
telemetry.tick(uint32_t now_us, r2link::BytePort& port);
const body::IbusTelemetryCounters& counts = telemetry.counters();
```

The two numeric fields are signed `int32_t` so out-of-range source values are
validated before any narrowing or addition. A default snapshot has both
validity flags false.

- Address 1 is stock external voltage type **0x03**, data size 2,
  unsigned hundredths of a volt. Representable valid values: **0..65535**.
- Address 2 is stock temperature type **0x01**, data size 2,
  tenths of a degree C plus 400. Source representable range:
  **-400..65135 dC**. Bounds are checked before adding 400.
- `1280 cV` -> `00 05`; `250 dC` -> encoded 650 -> `8a 02`.
- Discovery `0x80`, type `0x90`, measurement `0xa0` commands support
  addresses 1/2 only. Polls and responses use the specified subtractive
  little-endian checksum. Unknown addresses/commands never receive replies.
- Discovery/type still respond when measurement validity is false.
  Measurement validity/value is rechecked immediately before the first write,
  including after waiting for full-response capacity.
- **Supplier obligation for Task 4:** use the lower of two pack voltages and
  hottest of two MOSFET temperatures; both contributing VESC sources must be
  valid and age <=500ms. Invalidate each affected sensor when either source is
  older or invalid. `SensorSnapshot` intentionally has the brief's four fields,
  not invented VESC clocks. This module cannot infer source freshness from
  numeric values. Current `main` never sets valid measurements.
- A single bounded response slot is immutable after its first accepted byte.
  New polls while occupied are counted and cannot overwrite/interleave it.
- Schedule from receipt of the complete checksum-valid poll: do not start
  before **100us**; admit at **1000us inclusive**; drop an unstarted response
  at **1001us**. Full-response capacity is required before starting.
  There is at most one clipped write call per `tick()`.
- `BytePort` allows partial writes, so the committed suffix is retained and
  completed without interleaving another response. A deadline miss after a
  partial write is counted once. The production UART adapter has one writer
  and checks capacity: Teensy's buffer write normally accepts the complete
  response in one call without blocking.
- Complete packet echo matching avoids eating a real poll that shares a
  length/command prefix. Only a fully accepted local response can match.
  Type/measurement length-6 responses are ignored independently. The local
  echo guard expires after 1ms from the latest accepted write.
- Counters: `valid_polls`, `checksum_errors`, `header_errors`,
  `partial_timeouts`, `unsupported_polls`, `ignored_responses`, `echo_bytes`,
  `busy_polls`, `invalid_measurements`, `responses`, `partial_writes`,
  `deadline_misses`. `responses` means complete UART enqueue, not handheld
  delivery confirmation.

### Hardware

The exact shared interface used is:

```cpp
int r2link::BytePort::read(); // -1 when empty
size_t r2link::BytePort::writable() const;
size_t r2link::BytePort::write(const uint8_t*, size_t);
```

- `HardwareSerialPort` wraps `HardwareSerial&`; write length is clipped to
  positive `availableForWrite()`. Zero capacity performs no write call.
- Teensy-specific pin setup uses **HardwareSerialIMXRT&**, because Teensy 4's
  `setRX`/`setTX` methods are not declared on the `HardwareSerial` base.
- `ReceiverPort(Serial5).begin()` selects RX21 and 115200. Its write API always
  returns zero; TX20 is not wired to the receiver. Its `pump(input, now_ms)`
  consumes at most 64 bytes per call and runs input expiry.
- `TelemetryPort(Serial6).begin()` performs, in this order:
  `begin(115200, SERIAL_8N1_HALF_DUPLEX)` then `setTX(24, true)`.
  RX25 is not attached. Its `pump(telemetry, now_us)` consumes at most 16 bytes.
- Main services sensor input/tick first, receiver input and inert link second,
  then sensor tick again. It does not wait for USB on startup. Every five
  seconds it attempts one bounded, capacity-checked USB diagnostic line with
  CH1/2/4/6/8/9, age, flags and selected parser/scheduling counters.
- No FS-CVT01 responder or wiring was added.

## TDD command and output record

Commands below were executed from the repository root. For each named log,
**the complete unfiltered stdout and stderr** are saved next to this report.
The normal capture form was `COMMAND > LOG 2>&1; result=$?; cat LOG; exit $result`
(some display calls used `tail`; the stored log is still complete).

| Phase | Exact validation command | Exit/result | Full output artifact |
| --- | --- | --- | --- |
| Initial RED | `python3 -m unittest discover -s tests -p test_body_radio.py -v` | 1, 9 failures: production modules absent | [task-3-red.log](task-3-red.log) |
| Initial GREEN | `python3 -m unittest discover -s tests -p test_body_radio.py -v` | 0, 9 passed in 6.663s | [task-3-green-initial.log](task-3-green-initial.log) |
| Actual-main RED | `PYTHONPATH=tests python3 -m unittest test_body_radio.BodyRadioTests.test_actual_main_starts_radio_only_without_waiting_or_actuators -v` | 1, old main has no radio instances/startup and retains USB wait | [task-3-main-red.log](task-3-main-red.log) |
| Echo regression RED | `PYTHONPATH=tests python3 -m unittest test_body_radio.BodyRadioTests.test_echo_shared_prefix_does_not_eat_real_poll_and_recovery_keeps_suffix -v` | 1, shared-prefix real poll lost | [task-3-echo-red.log](task-3-echo-red.log) |
| Main regression run | `python3 -m unittest discover -s tests -p test_body_radio.py -v` | 1, 10 passed, new echo regression failed | [task-3-main-green.log](task-3-main-green.log) |
| Suffix regression RED after echo fix | `PYTHONPATH=tests python3 -m unittest test_body_radio.BodyRadioTests.test_echo_shared_prefix_does_not_eat_real_poll_and_recovery_keeps_suffix -v` | 1, recovered poll discarded next header byte | [task-3-suffix-red.log](task-3-suffix-red.log) |
| Radio GREEN | `python3 -m unittest discover -s tests -p test_body_radio.py -v` | 0, 11 passed; final per-radio run 9.417s | [task-3-radio-green.log](task-3-radio-green.log) |
| Link regression | `python3 -m unittest discover -s tests -p test_body_link.py -v` | 0, 52 passed in 39.866s | [task-3-link-regression.log](task-3-link-regression.log) |
| Target RED | `/Users/matthew.mcgeary/.copilot/session-state/f45e25ad-38b0-48f9-b9fb-343a546b5c95/files/pio-venv/bin/pio run -d TEENSY_BODY_CONTROLLER -e teensy41` | 1, `HardwareSerial` base lacks pin methods | [task-3-teensy-build.log](task-3-teensy-build.log) |
| Target API fixture RED | `PYTHONPATH=tests python3 -m unittest test_body_radio.BodyRadioTests.test_hardware_startup_rx_only_bounded_pump_and_write_clipping -v` | 1, native fixture now reproduces same target API error | [task-3-uart-type-red.log](task-3-uart-type-red.log) |
| Target GREEN | `/Users/matthew.mcgeary/.copilot/session-state/f45e25ad-38b0-48f9-b9fb-343a546b5c95/files/pio-venv/bin/pio run -d TEENSY_BODY_CONTROLLER -e teensy41` | 0, success in 5.99s | [task-3-teensy-build-green.log](task-3-teensy-build-green.log) |
| Final combined verification | `PYTHONPATH=tests python3 -m unittest test_body_radio test_body_link -v` | 0, **63 passed in 41.845s** | [task-3-final-tests.log](task-3-final-tests.log) |
| Final target verification | `/Users/matthew.mcgeary/.copilot/session-state/f45e25ad-38b0-48f9-b9fb-343a546b5c95/files/pio-venv/bin/pio run -d TEENSY_BODY_CONTROLLER -e teensy41` | 0, **success in 0.75s** | [task-3-final-build.log](task-3-final-build.log) |

Representative exact RED outputs:

```text
clang++: error: no such file or directory: '.../body/IbusInput.cpp'
clang++: error: no such file or directory: '.../body/IbusTelemetry.cpp'
Ran 9 tests in 0.501s
FAILED (failures=9)

Assertion failed: (p.tx==Bytes({4,0x81,0x7a,0xff,4,0x82,0x79,0xff})), function main, file test.cpp, line 55.
Ran 1 test in 1.039s
FAILED (failures=1)

Assertion failed: (u.counters().valid_polls==2 && u.counters().busy_polls==1), function main, file test.cpp, line 65.
Ran 1 test in 0.674s
FAILED (failures=1)

src/body/HardwareAdapters.h:31:17: error: 'class HardwareSerial' has no member named 'setRX'
src/body/HardwareAdapters.h:52:17: error: 'class HardwareSerial' has no member named 'setTX'
========================== [FAILED] Took 2.33 seconds ==========================
```

Final radio test output:

```text
test_actual_main_starts_radio_only_without_waiting_or_actuators ... ok
test_all_or_none_capacity_deadline_and_partial_writes_no_mix ... ok
test_bad_measurements_never_overflow_and_validity_independent ... ok
test_checksum_headers_and_flags_use_only_ch6_and_ch9 ... ok
test_checksum_unknown_poll_echo_and_response_echo ... ok
test_echo_shared_prefix_does_not_eat_real_poll_and_recovery_keeps_suffix ... ok
test_golden_sensors_and_scheduler_boundaries ... ok
test_hardware_startup_rx_only_bounded_pump_and_write_clipping ... ok
test_invalid_all_fourteen_fields_preserve_snapshot_and_time ... ok
test_overlap_partial_expiry_and_rollover ... ok
test_telemetry_overlap_partial_expiry_and_loopback_each_partial_write ... ok
Ran 11 tests in 9.417s
OK
```

Final combined test and target output:

```text
Ran 63 tests in 41.845s
OK

teensy_size: Memory Usage on Teensy 4.1:
teensy_size:   FLASH: code:52512, data:8460, headers:8656   free for files:8056836
teensy_size:    RAM1: variables:16608, code:49960, padding:15576   free for local variables:442144
teensy_size:    RAM2: variables:12416  free for malloc/new:511872
========================= [SUCCESS] Took 0.75 seconds =========================
```

`git diff --check` also completed successfully before staging. The only build
environment warning was PlatformIO's existing urllib3/LibreSSL warning, not a
firmware compiler error. Actual-main native compilation also surfaced the
existing unused `used` variable warning in shared `Endpoint.cpp`; it was not
changed.

## Test coverage and explicit limits

The tests compile/link **actual production .cpp files**, not parallel parsers.
They cover every raw channel's invalid values (0,899,2101,65535), including
unused CH14; unchanged previous channels/time/count; accepted raw endpoints;
checksum/header rejection; switch flags/mapping; exact age and partial expiry
boundaries; overlapping headers; uint32 rollover; all six literal telemetry
response vectors; exact 99/100/999/1000/1001us scheduling; false/invalidated
measurements and extreme signed inputs; capacity below full response size;
queued timeout; partial writes and stalled suffix ownership; malformed
checksums; unsupported commands/addresses; duplicate receiver polls; local
discovery/type echo; non-echo polls sharing response prefixes; unsolicited
responses; bounded UART pumps; clipping/zero-capacity writes; and actual-main
radio-only startup with unavailable USB.

Remaining physical/integration concerns (not reasons to instantiate actuators):

1. No hardware upload or handheld/shifter test was performed. UART enqueue
   timing counters are not electrical timing-margin measurements.
2. Discovery reply bytes are identical to discovery poll bytes. Within the
   short local echo guard an identical packet cannot be distinguished from a
   local loopback without UART-origin metadata. Different polls sharing a
   prefix are correctly retained; the next identical poll after expiry works.
3. A generic transport that accepts a prefix and then stalls cannot satisfy
   both an absolute 1ms completion guarantee and intact framing. The fallback
   counts the miss and preserves only that suffix, rejecting new polls until
   completion. Production single-writer HardwareSerial capacity clipping
   prevents the normal cause of this condition. This fallback is tested rather
   than claiming a timing guarantee under arbitrary transport failure.
4. Task 4 must supply source freshness/aggregation and invalidate each stale
   sensor. Until then discovery/type are available but main's measurements
   stay invalid; no fake zeros are published as valid data.

## Commit

Commit **`083af28`** (parent **`8272d89`**), nine code/test files only:
`feat: add Teensy iBUS input and sensor telemetry`

Includes `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`.
No physical documentation or local report/log artifacts are included.

Post-commit status contains only the unrelated physical-documentation changes
present at task start. No Task 3 production/test changes remain uncommitted.

## Task 3 round 1 — recovered receiver-header age

The recovered overlapping 0x20 0x40 header previously inherited the original
candidate's `started_ms_`. In the reported trace, that made a valid header
arriving at 2ms appear 5ms old when its final seven bytes arrived at 5ms.
`IbusInput` now keeps a fixed 32-entry arrival-time array parallel to its byte
buffer. Candidate suffix recovery and expiry shift both arrays together;
expiry removes stale candidate bytes while retaining a younger overlapping
header with its original timestamp. Later bytes never renew that timestamp.
Unsigned subtraction is retained for millis rollover.

| Phase | Exact validation command | Exit/result | Full output artifact |
| --- | --- | --- | --- |
| RED | `PYTHONPATH=tests python3 -m unittest test_body_radio.BodyRadioTests.test_recovered_overlap_keeps_header_arrival_time -v` | 1, failed at expected sample assertion for recovered frame | [task-3-overlap-timestamp-red.log](task-3-overlap-timestamp-red.log) |
| GREEN | `PYTHONPATH=tests python3 -m unittest test_body_radio.BodyRadioTests.test_recovered_overlap_keeps_header_arrival_time -v` | 0, 1 passed | [task-3-overlap-timestamp-green.log](task-3-overlap-timestamp-green.log) |
| Radio regression suite | `PYTHONPATH=tests python3 -m unittest test_body_radio -v` | 0, 12 passed | [task-3-round1-radio.log](task-3-round1-radio.log) |
| Teensy 4.1 target build | `/Users/matthew.mcgeary/.copilot/session-state/f45e25ad-38b0-48f9-b9fb-343a546b5c95/files/pio-venv/bin/pio run -d TEENSY_BODY_CONTROLLER -e teensy41` | 0, success in 0.81s | [task-3-round1-teensy-build.log](task-3-round1-teensy-build.log) |

The regression includes the exact 0/2/4/5ms trace, an overlapping candidate
expiring across uint32 rollover while retaining its younger valid header, and
a later-byte case proving a retained candidate still expires based on its
header's original arrival rather than being kept fresh. Task 3 round 1 changed
only `IbusInput.{h,cpp}`, `tests/test_body_radio.py`, and this local report.
