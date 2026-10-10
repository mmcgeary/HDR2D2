# Guided Commissioning — Design

Date: 2026-10-10. Status: proposed.

## 1. Intent

Commissioning becomes a guided ~15-minute flow on the wireless page. The
firmware runs the tests, gathers evidence and proposes values; the operator
confirms in batches. Success: from a blank profile the operator reaches
"drive + manual dome + auto dome ready and saved" using only the web page and
the transmitter (no USB/CLI).

Constraints kept from the current design:

- Every acceptance bit still needs an explicit operator click.
- Actuators run only the saved profile; edits are staged until Save.
- Both boards are flashed together (the link protocol changes).
- Everything is covered by host tests.

Decisions taken with the operator:

| Topic | Decision |
| --- | --- |
| Wheel tests | Web page only, CH6 OFF, sticks centred, "wheels are raised" confirmation, browser keepalive (the open `/drive` page sends a heartbeat every 150 ms; the ESP32 forwards Keepalive only while the last heartbeat is ≤500 ms old; dome tests keep the ESP32-driven keepalive), duty ≤10%, spin ≤2 s; CH6 ON / stick / keepalive loss brakes |
| Wheel sign-off evidence | Bits 6–11 require that wheel's completed automated test against the saved settings |
| CH9 | Not used by any commissioning test, Accept, SetField or Save. After a Save makes auto dome ready, auto dome waits for CH9 OFF→ON |
| Baseline | Fills unset fields only |
| Logic placement | Teensy runs tests and baseline; ESP32 sequences, mirrors and prompts |

## 2. Protocol changes (`shared/R2BodyLink/Messages.h`)

- `CommissionRequest.operation` 7 = **ApplyBaseline** (test/field/wheel/value 0).
- `CommissionRequest` Begin (op 1): `test` 1–8. Dome tests 1–5 keep
  wheel=0, value=0. Wheel tests: 6 **TimeoutBrake**, 7 **Direction**,
  8 **Reversal**, with `wheel` 0/1 and `value` = 1 ("wheels are raised";
  any other value is invalid).
- `CommissionStatus` adds: `staged_acceptance` (u16), `saved_acceptance`
  (u16), `unsaved` (u8, staged ≠ saved), and wheel results `wheel` (u8),
  `stop_ms` (u16), `peak_current_cA` (i16, 0.01 A), `peak_erpm` (i32),
  `vesc_fault` (u8). Wire size 36 → 51.
- `Diagnostics` subtype 1 (field read) adds `known` (u8): 1 when the field is set, so an unset field is not mistaken for 0. Wire size 11 → 12.
- Body publishes `VescStatus` whenever the VESC firmware has been observed;
  `valid_fields` = 0 when the sample is not valid, so the page can show
  detected firmware before commissioning.

## 3. Teensy

### 3.1 Baseline (`ConfigStore`)

`applyBaseline(profile, observed_fw)` sets only unset fields:

| Field | Baseline |
| --- | --- |
| servo_neutral / min / max | 1500 / 1000 / 2000 |
| auto_speed | 15 % |
| slew | 500 ‰/s |
| fw_major / fw_minor (per wheel) | observed VESC firmware, only if observed |
| layout | 1 |
| motor / battery / regen / brake | 12000 / 5000 / 2500 / 3000 mA |
| undervoltage / overvoltage | 1100 / 1480 cV |
| timeout_ms / timeout_brake_ma | 150 / 3000 |
| reversal_erpm / reversal_dwell | 300 / 200 ms |
| direction, cw_rate, ccw_rate | left unset (set by tests) |

Gate: stationary gate (3.2), no test running. CLI: `profile baseline`.

### 3.2 Gates (`DomeCalibration`)

Stationary gate = fresh RC, CH6 OFF, sticks (CH1/2/4) centred, no motion
lock. Used by Begin, SetField, Accept, Save and ApplyBaseline. CH9 is no
longer consulted. A running test is cancelled on CH6 ON, stick deflection,
stale RC, keepalive loss (>300 ms) or a motion lock.

### 3.3 Wheel tests (`WheelTest`, new)

Pure logic: input = wheel telemetry samples + time; output = command
{Disable, Duty(permille), Brake} for the tested wheel. The other wheel is
disabled. Constants: spin duty 100 ‰, spin-up 1000 ms, "turning" ≥ 100 eRPM
(all thresholds stay in eRPM, so no motor pole count is needed; a stopped
Hall-sensored motor reads ~0 eRPM), overall test limit 6 s.

- **TimeoutBrake**: spin 1000 ms (must reach turning speed, else Failed
  "did not turn"), then stop sending commands. Pass when |eRPM| falls below
  10% of the spin speed within 1500 ms (the VESC's own 150 ms timeout brake).
  Records stop_ms and peak |motor current| after the cut-off. Still turning at
  1500 ms → Failed "VESC timeout brake not active".
- **Direction**: spin 1500 ms at raw +100 ‰, then brake 300 ms. Pass when it
  reached turning speed. The page then asks "did this foot roll forward?";
  the answer stages `direction` ±1. Accept then needs that answer.
- **Reversal**: spin +100 ‰ for 1000 ms, then brake at the saved brake current
  until |eRPM| ≤ reversal_erpm for reversal_dwell, then −100 ‰ for 1000 ms,
  then brake. Pass when the eRPM sign reversed at turning speed and no fault.
  Records the brake-to-low-speed time and peak current.
- Any VESC fault, stale telemetry, or abort condition → brake 300 ms, then
  Failed/Cancelled with the fault code.

`VescLink` commissioning mode: while a wheel test runs, duty ≤100 ‰ and the
saved brake current are permitted once that wheel's `vesc_config` is
accepted **and saved**, its firmware matches and telemetry is fresh (timeout,
direction and reversal bits are not yet required). While commissioning mode
is on, duty above 100 ‰ is refused even for a fully control-accepted wheel.

### 3.4 Evidence

Each completed test records a digest of the saved settings it ran against:

| Bit | Test | Digest fields |
| --- | --- | --- |
| 0 | Neutral | servo trims, auto speed |
| 1 / 2 | FrontRef / RearRef | servo trims, auto speed |
| 3 | TimingCw + TimingCcw | servo trims, auto speed |
| 6/7 | TimeoutBrake (wheel) | wheel fields 6–16 |
| 8/9 | Direction (wheel) | wheel fields 6–14 (the `direction` answer is staged after the test and must be set) |
| 10/11 | Reversal (wheel) | wheel fields 6–14, 17, 18 |

Accept succeeds only when the staged values of those fields equal the
recorded digest. Dome tests keep their existing staged-config evidence
(they run on staged trims); wheel tests run on saved settings.

### 3.5 Auto-dome re-arm (`DomeController`)

`activateSavedProfile()` notifies the dome controller; when the activation
newly makes auto dome ready, auto behaviour (startup alignment, seeks, idle
velocity) stays inhibited until CH9 is observed OFF and then ON. Boot with
CH9 already ON keeps today's behaviour.

## 4. ESP32

- **`ProfileMirror`**: one Read in flight, round-robin over the 7 global and
  2×14 wheel fields while the link is up (≈3 s per full pass); caches
  value + known flag per field. Used by the field table and checklist.
- **`CommissionWizard`**: "Calibrate dome" runs FrontRef → RearRef →
  TimingCw → TimingCcw, each Begin + wait for Completed of that run id;
  stops on the first failure with its error. "Accept all & Save" sends
  Accept 1, 2, 3 then Save. Neutral is its own step with **Nudge −5 / +5 µs**
  (SetField servo_neutral, restart the 3 s hold) and **Accept neutral**.
- **Wheel flow**: "Accept VESC config & Save" (bits 4/5 + Save) after the
  operator compares VESC Tool; then per wheel **Run timeout**, **Run
  direction** (+ Forward/Backward answer), **Run reversal**; then
  "Accept wheel tests & Save" (bits 6–11 that have evidence + Save).
- **`RadioCheck`**: prompts in order, each passing when the condition is
  seen within 15 s: right stick up (CH2 > 1750), right stick right
  (CH1 > 1750), left stick right (CH4 > 1750) — the three stick prompts say
  "with SwA UP" and count only while CH6 < 1250 — SwA down (CH6 > 1750),
  SwC through 3 positions (CH5 low/mid/high), SwB down (CH8 > 1750),
  SwD down (CH9 > 1750; the prompt warns that a saved auto dome may turn),
  knob sweep (CH7 < 1100 then > 1900). Transmitter macros (SwB) do not fire
  while the check is prompting; a SwB still DOWN when it ends needs a fresh
  flip. Prompt text holds no quotes or backslashes (ReelTwo puts it into
  JavaScript strings unescaped). Failsafe:
  "turn the transmitter off" — pass when frames keep arriving with
  CH1/2/4 centred, CH6/8/9 ≤ 1250; frames stopping is reported as "no
  failsafe frames (receiver stops output)" — drive still disarms on staleness.
- **`AudioCheck`**: play track 255; pass on PlaybackStarted within 3 s.
- **Pages**: `/commissioning` = checklist, baseline, dome steps, radio and
  audio checks. `/drive` = detected VESC firmware, named field table
  (edit-in-place, staged vs saved), wheel tests and their results.
- **Checklist**: baseline filled · VESC config L/R · timeout L/R ·
  direction L/R · reversal L/R · neutral · front/rear refs · timing · radio ·
  failsafe · audio · saved (no unsaved changes). Radio/failsafe/audio are
  session results, not acceptance bits.

## 5. Testing

- Teensy unit tests: baseline fill-only and ranges; gates without CH9;
  `WheelTest` per test with scripted telemetry (pass, did-not-turn, timeout
  inactive, fault, abort on CH6/stick/keepalive); VescLink commissioning
  permission; evidence and digest mismatch; auto-dome re-arm; protocol
  validation of the new fields/ops.
- ESP32 host tests: ProfileMirror, CommissionWizard, RadioCheck, AudioCheck
  against fakes / real BodyClient over a pipe.
- Two-board integration test (success criterion): blank profile → baseline →
  VESC accept & save → wheel tests (scripted VESC that spins and responds to
  timeout) → dome wizard (scripted Hall) → accept & save → profile_ready == 7,
  using only CommissionRequests from the dome side.

## 6. Out of scope

Reading VESC configuration (mcconf) to verify limits; automatic servo-neutral
search; wiring/power checks.
