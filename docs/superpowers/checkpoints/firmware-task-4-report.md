# Task 4 report — independent VESC UART links

## Status and scope

**Software implementation verified and committed. Hardware acceptance UNPERFORMED; motion remains disabled.** Only Task 4 was implemented. No nested agents, firmware compatibility whitelist, actual firmware defaults, EEPROM adapter, drive owner/arming logic, or other task was added.

Commit: `220e8cb0283e859518ebf1d510a05bc4f1376b04`

Message: `feat: add independent VESC UART drivers`

Trailer: `Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>`

Seven committed code/test files:

- `TEENSY_BODY_CONTROLLER/src/body/VescLink.h`
- `TEENSY_BODY_CONTROLLER/src/body/VescLink.cpp`
- `TEENSY_BODY_CONTROLLER/src/body/HardwareAdapters.h`
- `TEENSY_BODY_CONTROLLER/src/main.cpp`
- `tests/test_body_vesc.py`
- `tests/test_body_radio.py`
- `tests/radio_fakes/Arduino.h`

The pre-existing untracked `BODY_CONTROLLER_COMMISSIONING.md` was updated **in the workspace only**. Its new “Task 4 stationary USB firmware / raw-values capture” subsection covers dome removal/service access, elevated/inhibited capture, raw chunk reconstruction, VESC Tool comparison, explicit acceptance, and no rotating tether. There is no tracked baseline against which to isolate that subsection; the whole unrelated physical-documentation file was deliberately NOT staged in the code commit. Include/review it in a future dedicated physical-documentation commit. Other prior-phase documentation/workbook/visualizer modifications remain untouched and uncommitted. This report and TDD/build logs remain session/task workspace artifacts.

## Evidence / TDD chronology

1. Read the Task 4 brief, shared `BytePort.h`, current ConfigStore implementation/API, Task 2 report, current adapters/radio tests, and design sections 6/9 before implementation.
2. Added the requested concrete brake encoder assertion **before creating driver source**: count 10, short marker 2, payload length 5, command 7, positive big-endian `00 00 05 DC`, terminator 3. Added independently packed/CRC-generated synthetic fixtures and initial behavior tests.
3. RED: `python3 -m unittest discover -s tests -p test_body_vesc.py` → **9 failures**, `task-4-red.log`. Initial portable tests failed because `VescLink.cpp` did not exist; target-integration assertion failed because main had no VESC UARTs. This initial red is a missing-production-module compilation failure, not a runtime encoder assertion failure; it is not misrepresented as the latter.
4. Initial implementation exposed a test-harness dependency omission: ConfigStore needs shared `src/Codec.cpp` for its persistence CRC. Added that existing production source to the host build, not new tooling.
5. Additional feature/regression RED runs before corresponding fixes:
   - `task-4-main-capture-red.log`: filtered one-packet capture overload missing and actual main/Serial1/2 pin assertions not yet implemented.
   - `task-4-sensors-red.log`: missing paired sensor aggregation function.
   - `task-4-late-reply-red.log`: query expiry after RX let a stalled reply renew the old sample; moved expiry before RX.
   - `task-4-paired-red.log`: control priority when an unstarted query is blocked, plus missing paired aggregation. Unstarted queries now yield to live commands; a partially written query still finishes without interleaving.
   - `task-4-overlap-red.log`: a damaged CRC packet containing a plausible nested header masked a complete good suffix. Recovery now finds complete CRC/terminator-valid suffixes only after a known-invalid/expired candidate and preserves original header timestamps.
   - `task-4-repeat-demand-red.log`: repeated identical demand corrupted an in-flight partial command. Identical renewals now preserve that frame.
   - `task-4-brake-priority-red.log`: later duty cancelled a pending brake. Brake now has priority through completion; intervening duty is dropped, not delayed/replayed.
   - `task-4-unsent-query-red.log`: an unsolicited values packet freshened a query that had not reached the UART. Values cannot renew the cache until its query is completely written.
6. Final GREEN, after all fixes:

   ```text
   cd tests && python3 -m unittest test_body_vesc test_body_radio test_body_link
   Ran 79 tests in 60.613s
   OK
   ```

   `task-4-final-tests.log` contains the complete output. These cover production C++ modules against injected transports, not Python decoder reimplementations.

7. Final Teensy target build:

   ```text
   /Users/matthew.mcgeary/.copilot/session-state/f45e25ad-38b0-48f9-b9fb-343a546b5c95/files/pio-venv/bin/pio \
       run -d TEENSY_BODY_CONTROLLER -e teensy41
   [SUCCESS] Took 1.03 seconds
   FLASH: code 56160, data 8460, headers 9104
   RAM1: variables 20800, code 53608, padding 11928
   RAM2: variables 12416
   ```

   `task-4-final-build.log` shows both `src/body/VescLink.cpp.o` and `src/main.cpp.o` being compiled and linked into the target. No new dependencies installed.

8. `git diff --check -- TEENSY_BODY_CONTROLLER tests` and `git diff --cached --check` passed. Staged file list was reviewed before committing; no commissioning/physical documentation was staged.

## Public API and readiness contract

The current shared transport remains **`r2link::BytePort`**, included as `"BytePort.h"`:

- `read() -> int`, `-1` when empty.
- `writable() const -> size_t`.
- `write(const uint8_t*, size_t) -> size_t`; partial writes are normal and never waited on.

Portable driver APIs in `body/VescLink.h`:

- `VescProfile()` creates unknown/unaccepted layout and zero fields.
- `VescProfile::fromSaved(const CommissioningProfile&, uint8_t wheel)` injects the existing saved profile and acceptance bits; invalid wheel/profile returns an unknown unaccepted profile. Its acceptance/version/layout members are private; there is no public “infer approval from packet length” constructor.
- `VescCodec::encodeDuty(int16_t permille, uint8_t*, size_t) -> size_t`: native command **5**, signed big-endian `permille * 100`, valid protocol range -1000..1000; zero/error length on invalid arguments/capacity.
- `VescCodec::encodeBrake(uint32_t current_mA, uint8_t*, size_t) -> size_t`: native command **7**, positive signed-32-representable magnitude in mA; rejects zero/overflow/capacity errors.
- `VescCodec::decodeValues(const uint8_t*, size_t, const VescProfile&, VescSample&) -> bool`: command-inclusive payload only, output unchanged on rejection; accepted named layout required. Observed firmware/freshness belongs to `VescLink`, not this payload decoder.
- `VescLink(r2link::BytePort&, uint8_t wheel)`, `tick(uint32_t now_ms)`, `setDuty(int16_t)`, `setBrake(uint32_t)`, `sample(uint32_t) -> VescSample`, `setProfile(const VescProfile&)`.
- `requestCapture(uint8_t command = 255)` and `takeCapture(VescCapture&) -> bool`: next one validated full raw wire frame, optionally restricted to firmware command 0 or values command 4; 255 means any native packet. Diagnostic capture works without an accepted profile. Re-arming replaces an unread capture.
- `counters()` reports frames, CRC/terminator errors, absolute frame expiries, oversized/zero-length frames, unsupported packets/profiles, invalid values, query timeouts, partial writes, aborted commands, and inhibited commands.
- `vescMeasurements(const VescSample& left, const VescSample& right) -> SensorSnapshot`: call with both `sample(now)` records. Healthy fresh approved paired records supply lower pack voltage and hotter MOSFET temperature; faults/stale/invalid sources invalidate handheld measurement flags. Voltage and temperature field masks remain separate.

`VescSample` matches the VESC_STATUS types:

- `wheel`, `fw_major`, `fw_minor`, `fault`: u8.
- `valid_fields`, saturated `source_age_ms`, `pack_cV`: u16.
- `motor_mA`, `input_mA`, `erpm`: i32.
- `mosfet_dC`, `motor_dC`, `duty_permille`: i16.
- `sample_ms`: u32 local receive timestamp.
- `valid`, `profile_match`, `stale`, `unsupported`, `motor_temperature_valid`: separate bools.

Freshness is valid through **500ms**, stale thereafter; absent data has age 65535 and is invalid. `valid` requires voltage/fault/eRPM (`0x49`) plus matched accepted firmware/layout and freshness; **a controller fault remains a separate raw `fault` field**, not conflated with stale/unsupported. `valid_fields = 0xDF` for a successfully decoded legacy packet: every defined field except unused motor temperature. Faulted data remains a fresh valid measurement record, but cannot permit duty and cannot feed healthy paired handheld measurements.

Layout **`body::kLayoutLegacyGetValues = 1`** is the existing ConfigStore enum, not a newly invented firmware list. Layout **0 / kLayoutUnknown** never becomes ready. `fromSaved` first uses the existing `validateProfile`, which checks field-presence/range/acceptance prerequisites. Telemetry profile match requires the per-wheel **`kAcceptVescConfig + wheel`** bit and exactly equal observed firmware major/minor. Actuator transmission additionally requires **`kAcceptTimeoutBrake`, `kAcceptDirection`, and `kAcceptReversal` + wheel**. These flags must come from the existing external commissioning evidence/save workflow, not decoding or building. The driver does not constitute Task 5's paired arming/radio/lock/slew/reversal owner.

Every `setProfile` invocation resets firmware observation, cached sample, and demand; an observed firmware change also resets the sample/demand. Outstanding unsafe command suffixes are invalidated. Fresh matching firmware and a new validated values query/reply are needed afterward.

## Decoder offset cross-check and provenance

Local prior-art: `ASTROPIXELS_PLUS_UNIFIED/ASTROPIXELS_PLUS_UNIFIED.ino`, `parseVescValuesPayload`. Its existing minimum payload length 54, MOSFET/current/eRPM/voltage/fault offsets match the legacy prefix. Unlike that older parser, this driver never left-shifts signed bytes or treats disconnected motor TEMP as valid.

Public reference inspected on 2026-10-09:

`https://github.com/vedderb/bldc/blob/e22727a69cbcb8de9b02e01cca2bb6d6807292c5/comm/commands.c`

Specifically `COMM_GET_VALUES` serialization (full nonselective mask). Upstream SHA was obtained from GitHub's master commit API at inspection time. This public protocol/source cross-check establishes the **shape**, not installed-controller compatibility. No upstream implementation was copied into production.

Offsets below count from the payload's command byte, not the short/long frame header:

| Payload bytes | Wire field/type | Driver representation |
| --- | --- | --- |
| 0 | COMM_GET_VALUES = 4, u8 | command check |
| 1..2 | FET temperature, signed BE i16, ×10 °C | i16 dC, unchanged |
| 3..4 | motor temperature, signed BE i16 | ignored; motor TEMP not wired, validity false |
| 5..8 | average motor current, signed BE i32, ×100 A | i32 mA = wire ×10 |
| 9..12 | average input current, signed BE i32, ×100 A | i32 mA = wire ×10 |
| 13..16 | average d-axis current, i32 | ignored; occupies four bytes |
| 17..20 | average q-axis current, i32 | ignored; occupies four bytes |
| 21..22 | duty, signed BE i16, ×1000 | i16 permille, unchanged |
| 23..26 | electrical RPM, signed BE i32 | i32 eRPM, unchanged |
| 27..28 | input voltage, signed BE i16, ×10 V | nonnegative u16 cV = wire ×10 |
| 29..44 | four amp-hour/watt-hour fields, i32 each | ignored; occupies 16 bytes |
| 45..48 | signed tachometer, i32 | ignored |
| 49..52 | absolute tachometer, i32 | ignored |
| 53 | raw fault code, u8 | u8 fault; any nonzero value inhibits duty |
| 54 onward | firmware-dependent extension | ignored only after explicit exact-version/layout acceptance |

Every decoded field uses a bounds-checked byte reader and unsigned shift/assembly, with explicit mathematical signed conversion. Every i32 current multiplication is checked for i32 overflow; voltage is checked for nonnegativity/u16 conversion overflow, and duty for the protocol's ±1000 range. These are **representation/protocol checks, not guessed physical maximum currents**. Failed decoding leaves the entire old sample/timestamp unchanged.

Test fixture provenance: `tests/test_body_vesc.py` constructs payloads and frames in test code; its independent reference CRC uses polynomial 0x1021, initial 0. These are **synthetic**, not installed-device captures. Firmware **42.19** is deliberately arbitrary synthetic test metadata, never a compatibility whitelist entry or shipping accepted profile. The synthetic values record has MOSFET -125dC, motor current -123450mA, input current +2340mA, duty -350permille, eRPM -98765, input voltage 1280cV, and fault 0; motor temperature bytes are filled with 999 to verify they do not grant validity. Additional synthetic tests cover faults, truncation, malformed fields, current overflow, and long 256-byte payloads.

## Framing, scheduling and command safety

- Native query IDs: **FW_VERSION 0**, **GET_VALUES 4**. Only **SET_DUTY 5** and **SET_CURRENT_BRAKE 7** actuator encoders exist; no CAN forwarding/command 34 or controller-ID wrapper is emitted.
- Short marker 2/one-byte length and long marker 3/two-byte big-endian length, CRC-16 and terminator 3 are validated. Maximum payload 256 bytes; maximum captured wire frame 263 bytes. Zero/oversized lengths and unknown native commands have diagnostics.
- Absolute frame expiry **50ms from the candidate header**, not the last byte. Overlap recovery preserves individual header arrival times; corrupted/trickled packets cannot extend their expiry or freshen a sample. Complete valid suffix recovery is enabled only after known corruption/expiry, not inside an intact live frame.
- One independent parser/capture/cache/query state per wheel. Each tick consumes at most **64 RX bytes** and makes at most **one <=16-byte TX write**; no flush, wait-for-data, delay, or hardware wait loop.
- Firmware first, then query cadence **100ms**, at most one outstanding query. Firmware rechecked at least once per 1000ms of successful observation during ongoing polls. Completed-query response timeout **150ms**, checked before queued RX. Invalid/unsupported/late/unsolicited values do not renew the old sample. A values reply cannot renew an uncompleted TX request.
- Commands take precedence over new queries; a wholly unstarted query yields, but a partial query must finish atomically before a command. Never mix frame prefixes/suffixes.
- Latest nonzero duty demand coalesces, unrenewed duty expires after **20ms** using the last loop tick timestamp; callers must tick regularly and renew live intent. A command is gated again at TX time by accepted matching profile, freshness/required fields, and fault status.
- Brake wins over intervening duty until brake completion; intervening duty requests are dropped rather than replayed later. Brake magnitude must equal the saved accepted profile's positive brake current; zero duty requests that approved brake, not a guessed current.
- A brake supersedes an old queued/partial nonzero command. An unstarted old command is discarded. A committed prefix cannot be unsent, so an unsent CRC byte (or final terminator at the last partial offset) is poisoned; the old frame can never complete as a valid nonzero command after that brake request. The bounded invalid suffix finishes before the valid brake starts. Tests exercise **all partial offsets 1..9**. Already fully transmitted duty cannot be retracted.
- Identical demand renewals preserve a partial frame, avoiding starvation/corruption when the owner repeats live demand during a partial write.
- On stale data, unsupported firmware/profile, or loss of acceptance, no guessed actuator command is sent. External VESC timeout/braking configuration and its hardware acceptance remain essential.

Native VESC query replies have no sequence/session ID. The driver rejects unsolicited, expired and pre-transmission values, but an old reply arriving during a later completely transmitted same-command query is indistinguishable on this protocol. Hardware timing/configuration acceptance and the higher-level drive owner remain necessary; do not claim cryptographic/session-correlated telemetry.

## Real target integration and capture procedure

`body::VescPort` wraps the existing nonblocking adapter:

- **Left Serial1**: RX **0**, TX **1**, 115200 8N1.
- **Right Serial2**: RX **7**, TX **8**, 115200 8N1.

Main instantiates both actual adapters and both portable links, configures them in setup, and ticks them in the real loop. It calls paired sensor aggregation before servicing sensor replies. There is no ESP32 pin substitution and no main `setDuty`/`setBrake` call. Profiles remain default zero/unaccepted; startup only sends firmware/value requests. Existing radio UARTs and disabled NullPort body/dome bootstrap remain unchanged in role.

USB capture commands are one character: **L/R** capture that wheel's next FW_VERSION reply; **l/r** capture its next GET_VALUES reply. Reporting uses bounded `VESC_RAW wheel=… offset=… length=… HEX…` lines, at most 16 raw bytes per loop and only when USB has room. No automatic raw stream, actuator CLI, wait-for-host or flush. Capture is raw wire data, including header/length/CRC/terminator, and remains accessible when the profile is unsupported/unaccepted. Wait for the complete report before another request. Missing hardware may produce no report; it is not a pass.

The workspace commissioning subsection requires dome removal for USB access, elevated wheels, inhibited feet/dome, full raw fixture provenance, matching VESC Tool comparison, separate per-wheel acceptance, and removal of every external tether before rotation/reassembly. Stationary near-zero-current data alone is not proof of signed-current scaling; unresolved checks remain unperformed.

## Remaining hardware and integration gates

| Required observation | Left | Right |
| --- | --- | --- |
| Installed firmware major/minor and controller identity | **UNPERFORMED** | **UNPERFORMED** |
| Real FW_VERSION / GET_VALUES full byte fixture capture | **UNPERFORMED** | **UNPERFORMED** |
| Firmware-specific legacy-layout verification | **UNPERFORMED** | **UNPERFORMED** |
| Decoded fields compared against VESC Tool, including current sign/scaling | **UNPERFORMED** | **UNPERFORMED** |
| Operator-confirmed saved firmware/layout/current-profile acceptance | **UNPERFORMED** | **UNPERFORMED** |
| Applied VESC Tool current/voltage/150ms timeout/brake settings | **UNPERFORMED** | **UNPERFORMED** |
| Timeout braking / direction / reversal external acceptance | **UNPERFORMED** | **UNPERFORMED** |
| Electrical UART operation, sustained RC/sensor concurrency and installed timing | **UNPERFORMED** | **UNPERFORMED** |

No hardware was available. No raw hardware fixture, installed firmware version or compatibility approval is fabricated. Build/test success does not enable either wheel or set acceptance. Future integration must load only the genuinely saved/accepted profile, preserve paired-drive inhibition and owner safety rules, and perform the remaining physical checks. Physical USB buffer behavior and VESC rejection/recovery of deliberately invalidated partial frames still require installed verification; the 150ms external timeout remains the fallback, not a software assertion of measured stopping distance.

## Fix round 1 (review of 220e8cb)

Scope: only `VescLink.h/.cpp` and `tests/test_body_vesc.py`. No other drivers, docs, tasks or nested agents.

**Important 1 — stale telemetry suppressed safety brake.** Brake (start and in-flight) previously required a fresh valid sample. Now `brakePermitted()` = actuator-accepted profile (`kAcceptVescConfig/TimeoutBrake/Direction/Reversal` + wheel) **and** observed firmware exactly matching the profile **and** magnitude equal to the saved `brake_ma`. Stale/missing RX no longer blocks brake; duty keeps the fresh/valid/unfaulted/20ms gate at stage and every TX tick. Profile change to unaccepted (`setProfile`) or observed firmware change poisons any partial brake and emits nothing; never-observed firmware emits nothing. A staged/partial old duty is still poisoned (CRC/terminator) then replaced by the brake, including when the duty went stale mid-frame (offsets 1..9 tested).

**Important 2 — per-tick control starved polling.** New TX timing contract per `tick()`:
- Byte budget `kTxBudget = 32` per tick (~2.8ms of line time at 115200 8N1; a 10-byte command or 6-byte query is <1ms). Finish any partial frame, then start at most **two** new frames. After the first write of a tick, a further frame starts only if it fits whole in `min(writable(), remaining budget)`; no needless partial frames.
- Selection order: **brake > waiting query > duty > due query**. A due query goes out in the same tick right after control when room allows. If control took the room, the query is marked waiting and goes ahead of the next duty renewal (delay ≤6 bytes); it never goes ahead of brake.
- Brake displaces an unstarted query; duty no longer cancels an unstarted query. A partially written query still completes first (≤6 bytes) — never interleaved.
- Query reply timeout (150ms) is now evaluated whenever the query itself is not in TX, even while a command occupies TX.
- Guarantee: with regular ticks, a poll opportunity arises at least every 100ms + ≤2 loop periods regardless of duty renewal rate; poll/firmware cadence otherwise unchanged (100ms, FW recheck ≥1000ms, one outstanding).

Contract change reflected in one existing assertion: a brake issued when a query is due is now followed by that query in the same tick (`test_control_priority…`).

**Tests added** (production C++ against fakes; reference CRC/frame parser in test code):
- `test_live_duplex_continuous_duty_never_starves_polls`: duplex fake drains TX FIFO at 11.52 bytes/ms into a reference VESC that replies FW/GET_VALUES after 3ms. For loop/renewal (1ms/every tick, 1/20, 5/every, 7/every, 5/20) over 2.1s of continuous duty: sample valid and age ≤250ms every tick, poll gaps ≤100+2·loop+3ms, duty gaps ≤ renewal+loop+3ms through the end, zero query timeouts and zero aborted commands.
- `test_live_duplex_small_fifo_brake_urgent_and_due_query_both_progress`: 12-byte FIFO, duty every 1ms tick, five urgent brakes on the wire within 4ms of request, polls gap ≤106ms; same-tick brake+due query order; partial capacity brake first then query next tick; fairness with 10-byte room per tick.
- `test_stale_rx_brake_still_sent_but_duty_blocked`: stale sample → duty blocked, `setBrake`/`setDuty(0)` brake sent; brake with no values ever received; stale partial duty poisoned then brake; stale partial brake completes intact.
- `test_brake_requires_accepted_profile_and_matching_firmware`: FW change, unaccepted profile mid-brake, never-observed FW, missing reversal acceptance → no brake frame.

**RED**: `cd tests && python3 -m unittest -v test_body_vesc` → `Ran 19 tests … FAILED (failures=5)` (continuous duty loops 1/5/7ms: sample stale; small-FIFO/fairness; stale brake `count(p.tx,7)==1`). The brake-gating test passed pre-fix (existing gates already held) and is kept as a regression. Log: `task-4-fix1-red.log`.

**GREEN**: `cd tests && python3 -m unittest -v test_body_vesc test_body_radio test_body_link` → `Ran 83 tests in 66.124s OK`. Log: `task-4-fix1-green.log`.

**Build**: `…/files/pio-venv/bin/pio run -d TEENSY_BODY_CONTROLLER -e teensy41` → `[SUCCESS]`, VescLink.cpp recompiled; FLASH code 56608, RAM1 variables 20800. Log: `task-4-fix1-build.log`. `git diff --check` clean.

Hardware acceptance remains UNPERFORMED; motion remains disabled (main still never calls `setDuty`/`setBrake`).
