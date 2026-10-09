# Task 1 report: shared codec and safe Teensy project — DONE_WITH_CONCERNS

## Changed files (committed)
shared/R2BodyLink/{library.json,Bytes.h,Messages.h,Codec.h,src/Codec.cpp};
TEENSY_BODY_CONTROLLER/{platformio.ini,README.md,src/main.cpp,src/body/Pins.h};
tests/{cpp_test_support.py,test_body_link.py}; tests/test_plus_behavior.py (imports shared run_cpp).
Dirty docs/graph/BOM files were not staged.

## TDD / commands
- Red: `python3 -m unittest discover -s tests -p test_body_link.py -v` (headers absent) — missing-header failure was expected; note that I wrote the implementation before the tests were run, so the red step was not captured with output.
  Subsequent test-side errors (golden bytes, offsets) were fixed in tests only.
- Green: link tests 11/11 OK (C++11, -Wall -Wextra, compiled against production Codec.cpp).
- Full suite: `python3 -m unittest discover -s tests -v` — 58 tests OK.
- Build: `<pio> run -d TEENSY_BODY_CONTROLLER -e teensy41` — SUCCESS (R2BodyLink/Codec.cpp.o linked; flash code 10292, RAM1 vars 3808).

## Signatures (namespace r2link; C++11)
- `struct Frame {version,type,flags,sequence,source_session,destination_session,length,payload[96]}`; `enum class MessageType`, `DecodeResult{None,FrameReady,Error}`, `Status{Ok,NullArgument,BadLength,BadCapacity,BadEnum,BadReserved,BadRange,BadType}`.
- `uint16_t crc16(const uint8_t*, size_t)`; `bool newer16(uint16_t cand, uint16_t prev)`.
- `Codec::encode(const Frame&, uint8_t*, size_t) -> size_t` (static; 0 on error, counted in `Codec::encodeCounters()`).
- `DecodeResult Codec::feed(uint8_t, uint32_t now_ms, Frame&)`; `void Codec::tick(uint32_t)`; `const ErrorCounters& Codec::counters() const`.
- `ErrorCounters {crc,length,version,reserved,type,cobs,overflow,timeout,session,queue,null_argument,capacity,payload_length,enum_value,reserved_bits,range; clear()}` (session/queue are for the link layer).
- Typed, per struct T (Hello, Heartbeat, RcStatus, VescStatus, BodyStatus, HallState, DomeRequest, AudioRequest, DriveRequest, ControlRequest, CommissionRequest, Reply, AudioStatus, Event, CommissionStatus, Diagnostics), each with `static MessageType T::type()`:
  - `Status encode(const T&, Frame&, ErrorCounters&)`; `Status decode(const Frame&, T&, ErrorCounters&)`
  - `Status encodePayload(const T&, uint8_t* out, size_t cap, size_t& len, ErrorCounters&)`; `Status decodePayload(const uint8_t*, size_t len, T&, ErrorCounters&)`
  - `Status validate(const T&)`, `size_t wireSize(const T&)`. Destination untouched on any failure; each rejection increments one counter.
- Wire sizes: Hello 7, Heartbeat 2, RC 32, VESC 28, Body 19, Hall 8, Dome 13, Audio 6, Drive 8, Control 6, CommissionRequest 14, Reply 6, AudioStatus 16, Event 6, CommissionStatus 36, Diagnostics 37 (subtype0) / 11 (subtype1).
- Pins: `body_pins::` kLeftVescRx0/Tx1, kRightVescRx7/Tx8, kAudioRx15/Tx14, kDomeLinkRx16/Tx17, kReceiverRx21, kTelemetrySingleWire24, kDomeServo2.

## Concerns / decisions to review
- The spec leaves some field rules open; I chose strict ones: Hello safety_revision must be 1 and capability mask 0x1F; Dome velocity speed ±100, lease 1-150, seek/cancel require speed 0 and lease 0, cancel reference 0; Drive ±1000 permille, lease 1-150; Audio volume ≤30, folder/track nonzero for play, unused fields zero; Hall active ⊆ valid; BODY angle in [-1800,1799] and zero when invalid; commission trial neutral 0 or 1400-1600us, speed ≤100; CommissionRequest unused field/wheel/value zero unless set_field, begin needs test≠0, read needs test 0. Relax in later tasks if the endpoint needs it.
- Parser order: CRC checked before version/length/flags/type. Overflow (>115 encoded bytes) returns Error once then discards to delimiter; consecutive delimiters are ignored. Timeout is >=20ms and counted only for non-discarded partials.
- Session counter exists but is untouched by Codec (link layer, later task). Encode failures use a static global counter set.
- Teensy main.cpp includes Codec.h but does not call it; main only prints UNCOMMISSIONED, configures no pins. Library headers are included as `Codec.h` on Teensy and `R2BodyLink/Codec.h` in host tests.
- Not verified: WDT_T4 dependency is declared but unused in the image.

## Round 1 review fixes

### Fixed

- `shared/R2BodyLink/Messages.h:244-275`: Commission operation 6 (accept) now permits `value` as a named acceptance-bit index from 0 through 31, with `test`, `field`, and `wheel` zero. The codec validates the wire-level index range; the endpoint must still reject unsupported named acceptance checks.
- `shared/R2BodyLink/Messages.h:244-275`: Enforced the operation schema from design spec 9: read=0, begin=1, keepalive=2, cancel=3, set_field=4, save=5, accept=6. Keepalive, cancel, set_field, save, and accept require `test=0`; begin still requires a nonzero test; read requires `test=0`.
- Read diagnostics preserve both request forms: `field=0` selects the counters record and requires `wheel=value=0`; `field=1` selects a profile record and uses `wheel` plus `value` (profile field 0-20) as the lookup key. The previous blanket non-set-field zero check prevented profile reads. Set-field `field` and returned profile diagnostic `field` are bounded to 0-20.
- Unused `field`, `wheel`, and `value` are required to be zero for operations that do not use them. Accept is the exception for `value` (0-31); set-field uses `field` (0-20) and `value`; profile reads use `wheel` and `value`.
- `shared/R2BodyLink/Codec.h:32-44`: Documented that the static encode counters are diagnostic-only and unsynchronized. Encode and counter reads/clears are restricted by contract to the single control-loop context; adapters snapshot, send, and clear there.
- Added regression coverage in `tests/test_body_link.py:375-418`; updated the existing set-field round-trip fixture at line239 to use the now-required `test=0`. The approved signed angle range, audio volume cap and explicit volume operation were not changed.

### Red/green and validation

- RED: `python3 -m unittest discover -s tests -p test_body_link.py -v` — expected failure in `test_commission_request_operation_schema`; the previous read validator rejected the valid subtype-1 profile read.
- GREEN: `python3 -m unittest discover -s tests -p test_body_link.py -v` — **Ran 12 tests; OK** (7.228s). Tests exercise valid operation round trips, test/unused-field restrictions, acceptance indices, profile field bounds, and the read-by-profile-field request.
- No Teensy build was rerun: this round changes payload validation and documents the existing API; it does not change the firmware API.

### Diff range

- `shared/R2BodyLink/Messages.h:244-275, 306-307`
- `shared/R2BodyLink/Codec.h:32-44`
- `tests/test_body_link.py:239, 375-418`
