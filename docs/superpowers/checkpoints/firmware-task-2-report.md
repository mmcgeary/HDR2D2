# Task 2 report
Status: DONE (base 9d7817e).
Red: task-2-red.log (36 tests, 24 failures). Green: `cd tests && python3 -m unittest test_body_link` -> 36 OK; full suite `unittest discover -s tests` -> 83 OK.
Teensy: pio run -e teensy41 SUCCESS (RAM1 vars 3808, code 7744).
APIs: shared/R2BodyLink/{BytePort.h,Endpoint.h,src/Endpoint.cpp}; TEENSY_BODY_CONTROLLER/src/body/ConfigStore.{h,cpp}. Completion/reply polling API documented in Endpoint.h.
Decisions: framing accepts types 0x01..0x3F; out-of-range rejected+counted at framing/encode; in-range unknown -> endpoint Unsupported reply. Acceptance-bit prerequisites and layout id 1 placeholder are my definitions. Gate-closed save by default; no hardware defaults; allow_remote_drive=false.
Concerns: Task 1 red never captured; no Teensy EEPROM adapter; encode-failure paths untestable; Teensy build does not link Endpoint.cpp into the image (headers only via main.cpp).

## Fix round 1 (base afe7f18)

Commands/outputs:
- RED (98de807): `cd tests && python3 -m unittest test_body_link` -> 50 tests, 20 failures (log: task-2-fix1-red.log).
- GREEN: same command -> `Ran 50 tests ... OK`; full `python3 -m unittest discover -s tests` -> `Ran 97 tests ... OK`.
- Teensy: `pio run -e teensy41` (TEENSY_BODY_CONTROLLER) -> SUCCESS; `.pio/build/teensy41/libb62/R2BodyLink/src/Endpoint.cpp.o` exists; `nm -C firmware.elf | grep -c r2link::Endpoint` -> 20.

Changes:
- HELLO: equal session across opposite roles accepted; same-role loopback rejected (hello_rejected). Same-session HELLO is a no-op.
- Link timeout (`dropLink`): PeerLost for pending; clears stream seq, queues, replies, peer mode/ready; generation_++; unfinished cache entries become WrongEpoch(7) guards, completed entries keep replaying; no old replay after same-session handshake.
- EVENT ack only on `takeReceived()` (Accepted); events queued at loss are dropped unacked.
- Changed-content duplicate: never executed/replayed; sequence_conflicts + protocol_failures++; cached original result replied only if original done and flagged; if original still pending no reply (sender stays pending until TimedOut, outcome unknown).
- Partial TX: `dropWire()` sets need_delim_; txPump sends 0x00 first (retries if no room).
- ConfigStore: `acceptBit(p, bit, AcceptanceEvidence)` replaces 2-arg form; boot torn-vs-blank recovery; `faultMask`; layout enum; `LinkBootstrap` forces Endpoint into the Teensy image (no hardware).

API (headers are authoritative):
- Endpoint (shared/R2BodyLink/Endpoint.h): LinkStats adds `protocol_failures`; private `dropWire()`, `dropLink(uint32_t now)`; `generation()` increments on link loss; semantics documented above class.
- `enum class AcceptResult {Ok, UnsupportedBit, NotStationary, NotConfirmed, Prerequisite, TestEvidence, ConfigMismatch}`
- `enum class BootResult {NotAttempted, Ok, IoError, Corrupt, CounterExhausted}`; `ConfigStore::bootResult()`.
- `enum VescValuesLayout {kLayoutUnknown=0, kLayoutLegacyGetValues=1}`; `bool layoutSupported(uint8_t)` true only for 1. Task 4 MUST verify FW-specific offsets before drive.
- `const char* acceptanceBitName(uint8_t)`; bits: 0 servo_neutral, 1 front_reference, 2 rear_reference, 3 auto_timing, 4/5 vesc_config L/R, 6/7 timeout_brake L/R, 8/9 direction L/R, 10/11 reversal L/R.
- `struct AcceptanceEvidence` (all false/0 default): ch6_off, ch9_off, sticks_centered, stationary, operator_confirmed, test_completed, test_cancelled, cw_completed, ccw_completed, vesc_operator_observed, observed_run_id, commanded_run_id, config_digest.
- `uint32_t acceptanceDigest(const CommissioningProfile&, uint8_t bit)` (FNV-1a over the bit's fields; 0 for undefined bit).
- `AcceptResult acceptBit(CommissioningProfile&, uint8_t bit, const AcceptanceEvidence&)`; check order: bit valid -> gates (ch6/ch9 off, centered, stationary) -> operator_confirmed -> prerequisite -> test evidence (bits 0-3: completed, not cancelled, observed_run_id nonzero and == commanded_run_id; bit 3 also cw+ccw; bits 4-11: vesc_operator_observed) -> config_digest equals current digest. Only then the bit is set.
- Faults: `faultBits(ConfigResult)` Uncommissioned=1, Corrupt=2, IoError=2; `ConfigStore::faultMask(ConfigResult)` adds bit 4 (value 4, boot-session storage error) when boot result is IoError/Corrupt/CounterExhausted.
- Boot slot: 32 bytes, commit marker at index 31 (0xFF = torn). `nextBootSession`: valid slot -> increments; only Torn/Blank without a valid slot recovers to counter 1; any other invalid -> Corrupt, false.
- `body::LinkBootstrap(uint32_t session)`: `tick(now)`, `connected(now)`, `endpoint()`; NullPort; main.cpp ticks it. No Teensy EEPROM adapter yet.

## Fix round 2
Signature change: private `Endpoint::checkLinkTimeout(uint32_t now)` added (Endpoint.h); `tick()` calls it BEFORE `rxPump`, so a queued heartbeat after a >=300ms stall cannot mask the timeout; `housekeeping` still calls it. dropLink (generation++, pending PeerLost, queues/streams/replies/readiness cleared) fires first; the new heartbeat then reconnects as a fresh generation.
Red (before fix): `cd tests && python3 -m unittest test_body_link -k stalled -k fresh_traffic`
```
FAIL: test_stalled_tick_with_queued_heartbeat_still_drops_the_old_link (test_body_link.EndpointTests)
AssertionError: -6 != 0 : Assertion failed: (s.e.peerGeneration() == gen + 1 && s.e.stats().link_losses == 1), function main, file test.cpp, line 104.
Ran 2 tests ... FAILED (failures=1)   (fresh_traffic <300ms test passed)
```
Green: `python3 -m unittest test_body_link` -> `Ran 52 tests ... OK`.
Existing test_cache_expiry_and_receive_queue_full previously relied on a single 2100ms stall with a heartbeat; it now keeps the link fresh with 100ms heartbeats (a 2.1s stall is correctly a link loss).
Teensy: `pio-venv/bin/pio run -d TEENSY_BODY_CONTROLLER -e teensy41` -> [SUCCESS] (FLASH code:21256, RAM1 vars:11008).
