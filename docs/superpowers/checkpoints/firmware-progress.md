# SDD ledger — plan: docs/superpowers/plans/2026-10-09-teensy-firmware.md

User approved completion of engineering checks then implementation, 2026-10-09. Existing linked worktree, branch mmcgeary-r2-build-plan-review, baseline94bcf98. Existing physical documentation changes are intentionally uncommitted; never stage them in a code task except explicit related updates. 47 baseline tests pass.

## Preflight coverage / interface scan

| Task / pair | Contract inspected | Result |
| --- | --- | --- |
| 1 | Codec tests / target config / types | Concrete CRC/framing tests; all final payloads include commissioning and dual Hall |
| 2 | Endpoint / storage tests | Valid sessions, queue ages, separate corrupt/uncommissioned profiles |
| 3 | Radio freshness / half-duplex | All14 values validated; thresholds agree; open drain after begin |
| 4 | VESC framing / profile | Native independent links; unsupported firmware disarms; no guessed hardware acceptance |
| 5 | Mixer / drive state | Caps preserved; per-wheel reversal and slew added |
| 6 | Dome / position | Two Hall masks; CH9 gating; no radio holos; authority generation prevents resumed requests |
| 6b | Idle scheduler / client dependency | Portable sink and input defined before BodyClient |
| 7 | Audio / metadata | Status-confirmed start, explicit unknown duration and owner correlation |
| 8 | Body integration / CLI | Commissioning dispatch and typed diagnostics; readiness per subsystem |
| 9 | ESP client / freshness | Explicit dual Hall fields and status freshness300ms |
| 10 | ESP integration / physical updates | Remove old hardware writers; second Hall; current light/holo travel preserved |
| 11 | Macros / OTA | Faint no lock; selective dome cancellation; no update before maintenance ACK |
| 12 | Commissioning / acceptance | Exact payloads, save/readback, keepalive; staged profile; no WiFi foot-duty |
| 1-2-3-4-7-8-9 | BytePort / typed fields | Shared portable API, explicit endianness, no padded structure copies |
| 1-2-6-6b-8-9-11-12 | Messages / endpoint | Current revised spec binding; generation, masks, calibration payloads |
| 2-4-5-6-8-12 | Profile / acceptance | Distinct drive/manual/auto readiness; unsaved calibration cannot enable auto |
| 3-5-6-6b-8-9-10-11 | RC / permissions | CH3 unused; CH6feet and CH9auto independently; fresh stream not RF proof |
| 4-5-8-9-12 | VESC state | Required eRPM/fault/voltage validity; reversal test values not production defaults |
| 6-6b-8-9-10-11-12 | Dome / takeover | One actuator owner; event flag plus generation; measured references vs estimates |
| 7-8-9-10-11-12 | Audio ownership | One serializer, events not ACK timing, no restarted Leia from stale seek |
| 8-9-10-11-12 | Status / UI | Wireless diagnostic/calibration path mandatory; USB only stationary / flash |
| 9-10-11 | Shared ESP files | Sequential implementation, no parallel writers |
| 10-12 | Guides/tests | Preserve current signal colours and removed inspector banner; update Hall topology |

Ruling: Current revised specification governs numeric payloads and behaviour over older example names — necessary because planning examples predate approved changes — wrong mapping would require protocol/test rework.
Ruling: Neutral bootstrap permits only bounded trial pulses with explicit gear-disengaged confirmation, never automatic velocity — prevents circular need for accepted neutral before calibration — must validate loss/cancel behaviour before mounted tests.
Ruling: No USB-dependent assembled tests or WiFi wheel spin — matches operator constraints; radio runs wheels — costs extra typed commissioning implementation.

Task1: dispatched from94bcf98; no prior firmware implementation.

Task1 review: commit64612bb; gaps in CommissionRequest accept value / unused fields; static encoder counter ownership undocumented. Fix round1 opened.
Ruling: Acceptance value is a bit index0..31, validated further against named supported flags by the body controller — u32 acceptance mask requires bit indexing, not unconditional value0 — could need enum narrowing in integration.
Ruling: Keep signed dome angle[-1800,1800) and DFPlayer volume0..30, use explicit volume requests separately from play — approved angle contract and native DFPlayer semantics — adapters must normalise angles and send separate volume.
Task1: minor (deferred): Hello capability/revision mismatch is a counted error; endpoint must show incompatibility rather than freshness.
Task1: process defect: no captured TDD red run. Do not fabricate evidence. Further work must run tests before implementation.

Task 1: fix round 1/5 (3 addressed, 0 open; commits64612bb..9d7817e). Regression red then12 passing codec tests; documented counter ownership.
Task 1: complete (commits94bcf98..9d7817e, review clean).
Task 2: starting from9d7817e; endpoint and persistence.

Task2 review ofafe7f18: critical equal boot sessions reject HELLO; important acceptance evidence missing and peer-loss reset incomplete. Fix round1 opened; also recover partial transmit delimiter, event receipt/cache loss, first-ever boot torn write, error mapping and target compilation.
Ruling: Boot sessions are local counter namespaces, not globally unique; equal sessions across opposite roles are valid — both boards share power and normally increment together — role check must prevent self-peer instead.
Ruling: Acceptance numbering is implementation-owned named enum shared with UI, not placeholder. Require explicit stationary operator evidence and completed matching runs where applicable — profile field validity is not hardware evidence — task12 must bind actual observations and expose named checks.
Ruling: Define supported VESC values-layout enum as Unknown0 / legacy COMM_GET_VALUES1 with explicit documented target offsets; unknown firmware remains ineligible until Task4 verifies compatibility — layout number is protocol shape, not invented firmware compatibility — decoder/profile must reject unverified versions.
Ruling: Stale peer clears authority/readiness/queues/stream sequence/cache and increments link generation; preserve only bounded completed-request sequence guard if needed to avoid replay until renewed handshake — stale state must not authorize future actions — clients must reinitialise on generation change.

Task2 fix round1 actual commits98de807..db1ed7d (agent response misnamed commit; git log authoritative).50link/97full tests pass, target Endpoint object+symbols verified by implementer.
Task 2: fix round 1/5 (9 addressed, 1 open — timeout processing after RX can revive stalled link without generation reset).
Task2 round2: evaluate timeout before RX; regression stall>=300ms plus queued heartbeat.
Ruling: Preserve protocol fault bit1 as configuration corrupt-or-unreadable; retain precise ConfigResult::IoError in bounded diagnostics and boot storage bit2 separately — catalog has no dedicated profile-I/O bit — dashboard must not call unreadable profile blank or saved.

Task 2: fix round 2/5 (1 addressed, 0 open, commit8272d89;52link tests and target build).
Task 2: complete (commits9d7817e..8272d89, review clean).
Task3 starting8272d89. Shared phase complete; profile EEPROM hardware adapter deferred to body integration Task8 (portable store implemented).

Task3 implemented083af28:63radio/link tests and target build. Review Important: overlapping recovered frame inherits rejected header start timestamp and expires early. Fix round1 opened, preserve recovered header arrival.

Task 3: fix round 1/5 (1 addressed, 0 open, commit2faf224;12radio tests and target build).
Task 3: complete (commits8272d89..2faf224, review clean).
Task4 starting2faf224; VESC compatibility must be verified before readiness.

Task4 implemented220e8cb:79tests+targetbuild; commissioned VESC doc updated untracked (preserve). Hardware FW verification unperformed.
Task4 review: Important brake suppressed when RX sample stale; control renewal can starve100ms polling.
Ruling: Brake remains permissible under stale telemetry if exact firmware and explicitly accepted configured brake/profile match; powered duty still requires fresh sample — otherwise RX failure suppresses safety braking despite working TX — cannot prove braking if TX/device failed, VESC150ms local timeout remains necessary.
Ruling: Treat polling starvation as Important despite review Minor label; preserve control priority while guaranteeing due query under sustained renewals — drive must run longer than freshness500ms — bounded UART scheduling must not delay urgent brake.

Task 4: fix round 1/5 (2 addressed, 0 open; commitb4314a3,83targetedtests/targetbuild).
Task 4: complete (commits2faf224..b4314a3, review clean).
Task 5 starting b4314a3; drive mixer/braking/arbitration.

Task 5 review of dd779b0: Important reversal dwell start timestamp underflow bug in permit() when s.sample_ms < now; missing reversalState() assertions and asynchronous sample arrival test. Fix round 1: initialize w.low_ms to s.sample_ms, add lifecycle assertions and async sample regression test.
Task 5: fix round 1/5 (3 addressed, 0 open; 18 drive tests, 148 full tests pass).
Task 5: complete (commit 31b4880).
Task 6: implemented and verified. DomePosition (dual Hall anchors at 0 and -1800 ddeg, continuous normalization, shortest route seek), DomeController (authority hierarchy STOP -> CH4 -> Drive -> Event -> Idle -> Homing, authority generation bump and takeover event, continuous servo pulses). 10 dome tests, 158 full tests pass.
Task 6: complete (commit ecdf1bb).
Task 6b: implemented and verified. DomeBehaviour (WaitingIdle 20s deadline postponed by stick/drive/event, Referencing, Pausing 2000-6000ms, Sweeping [-450, 450] ddeg at auto speed, Returning via front reference; fault latching and generation protection). 12 behaviour tests, 170 full tests pass.
Task 6b: complete (commit e391bad).
Task 7: implemented and verified. DfPlayer (100ms spacing, foreground/ambient priority, startup reset, 500ms status polling, request-correlated PlaybackStarted/Completed/Cancelled/Timeout events, pause/resume elapsed bookkeeping, completion guards). TrackCatalog generator from CSV into ignored include/TrackCatalog.h. Teensy main.cpp integration with AudioPort and publishAudioStatus. 4 catalog tests, 8 audio tests, 182 full tests pass.
Task 7: complete.
Task 8: starting; integrate the Teensy scheduler, locks and diagnostics (BodyController).
