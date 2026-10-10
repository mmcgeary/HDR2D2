# Guided Commissioning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn commissioning into a guided wireless flow: baseline fill, firmware auto-fill, named live fields, a dome calibration wizard, automated wheel tests (timeout-brake, direction, reversal), radio/failsafe and audio checks, and a progress checklist — with no CH9 flipping.

**Architecture:** The Teensy owns every test that moves hardware (dome tests, new `WheelTest`), the baseline, the gates and the acceptance evidence; it reports results in an extended `CommissionStatus`. The ESP32 owns operator-facing orchestration: `ProfileMirror` (field values), `CommissionWizard` (sequencing + accept/save), `RadioCheck`, `AudioCheck` and a pure `CommissionChecklist` formatter, wired into the ReelTwo web pages.

**Tech Stack:** C++11 firmware (PlatformIO: Teensy 4.1 `teensy@6.0.0`, ESP32 `espressif32 v5.2.0` + ReelTwo 23.5.3), shared `R2BodyLink` protocol library, Python `unittest` host tests compiling C++ with `c++ -std=c++11` via `tests/cpp_test_support.py:run_cpp`.

**Spec:** `docs/superpowers/specs/2026-10-10-guided-commissioning-design.md`

## Global Constraints

- Every acceptance bit still needs an explicit operator Accept; nothing auto-accepts.
- Actuators run only the saved (active) profile; edits are staged until Save.
- CH9 is not consulted by any commissioning Begin, SetField, Accept, Save or ApplyBaseline.
- Stationary gate = fresh RC (≤250 ms), CH6 < 1250 µs, CH1/CH2/CH4 within 1460–1540 µs, no motion lock.
- Wheel tests: `value` = 1 ("wheels are raised"), duty ≤ 100 ‰, spin ≤ 2 s, "turning" ≥ 100 eRPM; CH6 ON, stick deflection, stale RC, keepalive loss (>300 ms) or motion lock → brake 300 ms then stop.
- Wheel tests need that wheel's `vesc_config` accepted **and saved**, matching firmware and fresh telemetry.
- Bits 6–11 require that wheel's completed test whose digest of the saved settings equals the staged settings at Accept.
- After a Save newly makes auto dome ready, auto dome waits for CH9 OFF→ON.
- ApplyBaseline fills unset fields only, with the spec's baseline table values.
- `CommissionStatus` wire size 51; `Diagnostics` subtype 1 wire size 12.
- Run all host tests with `python3 -m unittest discover -s tests -p "test_*.py"` from the repo root; build checks with `pio run` in each firmware directory (use `PLATFORMIO_BUILD_DIR=<scratch>` to avoid touching the tree).

## Review Focus

1. A wheel test begun while that wheel is still coasting (|eRPM| ≥ 100) — expect `Inhibited`, no spin. Test in Task 6.
2. Keepalive lost mid-spin — expect brake for 300 ms, then no further commands, state `Cancelled`. Test in Task 6.
3. Save or SetField pressed while a wheel test is still braking out — expect `Busy`. Test in Task 6.
4. Baseline with no VESC firmware observed — expect firmware fields left unset, everything else filled. Test in Task 2.
5. Radio failsafe step started with switches already in their failsafe positions — must not pass without first seeing them armed. Test in Task 10.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `shared/R2BodyLink/Messages.h` (modify) | New ops/tests, `CommissionStatus` fields, Diagnostics `known` |
| `TEENSY_BODY_CONTROLLER/src/body/ConfigStore.{h,cpp}` (modify) | `applyBaseline`, `fieldDigest`, `sameProfile` |
| `TEENSY_BODY_CONTROLLER/src/body/WheelTest.{h,cpp}` (create) | Pure wheel-test state machine |
| `TEENSY_BODY_CONTROLLER/src/body/VescLink.{h,cpp}` (modify) | Commissioning permission, `fw_known` |
| `TEENSY_BODY_CONTROLLER/src/body/DomeCalibration.{h,cpp}` (modify) | Gates without CH9, ApplyBaseline, wheel-test lifecycle, evidence, status fields |
| `TEENSY_BODY_CONTROLLER/src/body/DomeController.{h,cpp}` (modify) | Auto-dome re-arm after activation |
| `TEENSY_BODY_CONTROLLER/src/body/BodyController.{h,cpp}` (modify) | Wiring: samples/firmware to calibration, wheel commands to VESC links, VescStatus publish, CLI `profile baseline` |
| `ASTROPIXELS_PLUS_UNIFIED/ProfileMirror.{h,cpp}` (create) | Round-robin field reads cache |
| `ASTROPIXELS_PLUS_UNIFIED/CommissionWizard.{h,cpp}` (create) | Test sequencing, accept-and-save batches |
| `ASTROPIXELS_PLUS_UNIFIED/RadioCheck.{h,cpp}` (create) | Prompted channel + failsafe check |
| `ASTROPIXELS_PLUS_UNIFIED/AudioCheck.{h,cpp}` (create) | Startup-track playback check |
| `ASTROPIXELS_PLUS_UNIFIED/CommissionChecklist.{h,cpp}` (create) | Pure checklist formatter |
| `ASTROPIXELS_PLUS_UNIFIED/BodyClient.{h,cpp}` (modify) | Implements `IFieldReader`, `ICommissionSink` |
| `ASTROPIXELS_PLUS_UNIFIED/ASTROPIXELS_PLUS_UNIFIED.ino`, `WebPages.h` (modify) | Wiring + `/commissioning` and `/drive` pages |
| `tests/test_commissioning_*.py` (create), existing tests (modify) | Coverage |
| Guides (modify) | Commissioning workflow docs |

---

### Task 1: Protocol additions

**Files:**
- Modify: `shared/R2BodyLink/Messages.h`
- Test: `tests/test_body_link.py`

**Interfaces:**
- Produces: `CommissionRequest` op 7 (ApplyBaseline); Begin tests 6/7/8 with `wheel` 0/1 and `value` 1; `CommissionStatus` fields `uint16_t staged_acceptance, saved_acceptance; uint8_t unsaved, wheel; uint16_t stop_ms; int16_t peak_current_cA; int32_t peak_erpm; uint8_t vesc_fault;` (appended in that order after `saved`); `Diagnostics::known` (u8, subtype 1 only, appended after `value`).

- [ ] **Step 1: Write the failing tests** — append to `TypedPayloadTests` in `tests/test_body_link.py`:

```python
    def test_guided_commissioning_wire_contract(self):
        self.check(r'''
    ErrorCounters c; uint8_t buf[96]; size_t n = 0;
    CommissionRequest base = {7, 0, 0, 0, 0, 0, 3};
    assert(encodePayload(base, buf, sizeof buf, n, c) == Status::Ok);
    CommissionRequest wheel = {1, 6, 9, 0, 1, 1, 3};          // TimeoutBrake, right wheel, raised
    assert(encodePayload(wheel, buf, sizeof buf, n, c) == Status::Ok);
    wheel.test = 8; assert(encodePayload(wheel, buf, sizeof buf, n, c) == Status::Ok);
    wheel.value = 0; assert(encodePayload(wheel, buf, sizeof buf, n, c) != Status::Ok);   // not raised
    wheel.value = 1; wheel.test = 9; assert(encodePayload(wheel, buf, sizeof buf, n, c) != Status::Ok);
    CommissionRequest dome = {1, 2, 9, 0, 1, 0, 3};           // dome test with a wheel number
    assert(encodePayload(dome, buf, sizeof buf, n, c) != Status::Ok);
    CommissionStatus cs = {};
    cs.test = 8; cs.staged_acceptance = 0x0FFF; cs.saved_acceptance = 0x0010; cs.unsaved = 1;
    cs.wheel = 1; cs.stop_ms = 640; cs.peak_current_cA = -312; cs.peak_erpm = -1234; cs.vesc_fault = 3;
    assert(encodePayload(cs, buf, sizeof buf, n, c) == Status::Ok && n == 51);
    CommissionStatus back = {};
    assert(decodePayload(buf, n, back, c) == Status::Ok);
    assert(back.staged_acceptance == 0x0FFF && back.saved_acceptance == 0x0010 && back.unsaved == 1);
    assert(back.wheel == 1 && back.stop_ms == 640 && back.peak_current_cA == -312);
    assert(back.peak_erpm == -1234 && back.vesc_fault == 3);
    cs.staged_acceptance = 0x1000; assert(encodePayload(cs, buf, sizeof buf, n, c) == Status::BadReserved);
    cs.staged_acceptance = 0; cs.wheel = 2; assert(encodePayload(cs, buf, sizeof buf, n, c) == Status::BadEnum);
    Diagnostics d = {}; d.subtype = 1; d.field = 12; d.wheel = 1; d.value = 1500; d.known = 1;
    assert(encodePayload(d, buf, sizeof buf, n, c) == Status::Ok && n == 12);
    Diagnostics db = {}; assert(decodePayload(buf, n, db, c) == Status::Ok && db.known == 1 && db.value == 1500);
    d.known = 2; assert(encodePayload(d, buf, sizeof buf, n, c) == Status::BadEnum);
    Diagnostics d0 = {}; d0.known = 1; assert(encodePayload(d0, buf, sizeof buf, n, c) == Status::BadReserved);
''')
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests && python3 -m unittest test_body_link.TypedPayloadTests.test_guided_commissioning_wire_contract`
Expected: FAIL — compile error, `no member named 'staged_acceptance'`.

- [ ] **Step 3: Implement** in `shared/R2BodyLink/Messages.h`:

Replace the `CommissionStatus` and `Diagnostics` structs:

```cpp
struct CommissionStatus {
    uint32_t run_id; uint8_t state, test; uint16_t error; uint32_t flags;
    uint16_t trial_neutral_us; uint8_t trial_speed_percent;
    uint16_t proposed_cw_ddeg_s, proposed_ccw_ddeg_s; uint32_t revolution_ms[3];
    uint32_t config_generation; uint8_t saved;
    uint16_t staged_acceptance, saved_acceptance; uint8_t unsaved;
    uint8_t wheel; uint16_t stop_ms; int16_t peak_current_cA; int32_t peak_erpm; uint8_t vesc_fault;
    static MessageType type() { return MessageType::CommissionStatus; }
};
struct Diagnostics {
    uint8_t subtype; uint32_t sample_counter;
    uint32_t counters[8];            // subtype 0 only
    uint8_t field, wheel; int32_t value;  // subtype 1 only
    uint8_t known;                   // subtype 1 only: 1 when the field is set
    static MessageType type() { return MessageType::Diagnostics; }
};
```

Wire sizes:

```cpp
inline size_t wireSize(const CommissionStatus&) { return 51; }
inline size_t wireSize(const Diagnostics& d) { return d.subtype == 0 ? 37 : 12; }
```

Replace `validate(const CommissionRequest&)`:

```cpp
inline Status validate(const CommissionRequest& m) {
    R2_CHECK(enumLE(m.operation, 7));
    R2_CHECK(enumLE(m.test, 8));
    R2_CHECK(ok01(m.wheel));
    if (m.operation == 1) {
        if (m.test == 0) return Status::BadRange;
        R2_CHECK(zero(m.field));
        if (m.test >= 6) return inRange(m.value == 1);   // wheel tests: value 1 = wheels raised
        R2_CHECK(zero(m.wheel));
        return zero(static_cast<uint32_t>(m.value));
    }
    if (m.operation == 0) {
        if (m.test != 0) return Status::BadRange;
        R2_CHECK(enumLE(m.field, 1));
        if (m.field == 0) {
            R2_CHECK(zero(m.wheel));
            return zero(static_cast<uint32_t>(m.value));
        }
        return inRange(m.value >= 0 && m.value <= 20);
    }
    R2_CHECK(zero(m.test));
    if (m.operation == 4) return inRange(m.field <= 20);
    if (m.operation == 6) {
        R2_CHECK(zero(m.field));
        R2_CHECK(zero(m.wheel));
        return inRange(m.value >= 0 && m.value <= 31);
    }
    R2_CHECK(zero(m.field));
    R2_CHECK(zero(m.wheel));
    return zero(static_cast<uint32_t>(m.value));
}
```

Replace `validate(const CommissionStatus&)` and `validate(const Diagnostics&)`:

```cpp
inline Status validate(const CommissionStatus& m) {
    R2_CHECK(enumLE(m.state, 5));
    R2_CHECK(enumLE(m.test, 8));
    R2_CHECK(ok01(m.saved));
    R2_CHECK(enumLE(m.trial_speed_percent, 100));
    R2_CHECK(maskOnly(m.staged_acceptance, 0x0FFF));
    R2_CHECK(maskOnly(m.saved_acceptance, 0x0FFF));
    R2_CHECK(ok01(m.unsaved));
    R2_CHECK(ok01(m.wheel));
    return inRange(m.trial_neutral_us == 0 || (m.trial_neutral_us >= 1400 && m.trial_neutral_us <= 1600));
}
inline Status validate(const Diagnostics& m) {
    R2_CHECK(enumLE(m.subtype, 1));
    if (m.subtype == 0) {
        R2_CHECK(zero(m.field));
        R2_CHECK(zero(m.wheel));
        R2_CHECK(zero(m.known));
        return zero(static_cast<uint32_t>(m.value));
    }
    for (int i = 0; i < 8; ++i) R2_CHECK(zero(m.counters[i]));
    R2_CHECK(ok01(m.wheel));
    R2_CHECK(ok01(m.known));
    return inRange(m.field <= 20);
}
```

Serialization — append to `put`/`get` of `CommissionStatus` after `saved`:

```cpp
// put(Writer& w, const CommissionStatus& m), after w.u8(m.saved);
    w.u16(m.staged_acceptance); w.u16(m.saved_acceptance); w.u8(m.unsaved);
    w.u8(m.wheel); w.u16(m.stop_ms); w.i16(m.peak_current_cA); w.i32(m.peak_erpm); w.u8(m.vesc_fault);
// get(Reader& r, CommissionStatus& m), after m.saved = r.u8();
    m.staged_acceptance = r.u16(); m.saved_acceptance = r.u16(); m.unsaved = r.u8();
    m.wheel = r.u8(); m.stop_ms = r.u16(); m.peak_current_cA = r.i16(); m.peak_erpm = r.i32(); m.vesc_fault = r.u8();
```

Diagnostics `put`/`get` subtype-1 branch:

```cpp
    else { w.u8(m.field); w.u8(m.wheel); w.i32(m.value); w.u8(m.known); }
// get:
    m.field = m.wheel = 0; m.value = 0; m.known = 0;
    ...
    else { m.field = r.u8(); m.wheel = r.u8(); m.value = r.i32(); m.known = r.u8(); }
```

`decodePayload` length gate: replace `length != 37 && length != 11` with `length != 37 && length != 12`.

- [ ] **Step 4: Update the existing wire-size tests that pin the old sizes** in `tests/test_body_link.py`: `RT(CommissionStatus, cs, 36)` → `51`; `RT(Diagnostics, d1, 11)` → `12` with `Diagnostics d1 = {1, 6, {0}, 3, 1, -9, 1};`; the CommissionStatus wire-order assertion's `buf[35] == 1` is unchanged; the length-validation lines `sf = makeFrame(1, 35, 0)` → `makeFrame(1, 50, 0)` and `sf.length = 37` → `52`; `cs.test = 7; BAD(...)` → `cs.test = 9`; in the decode-rejects test `uint8_t cs[36]` / `decodePayload(cs, 36, st, c)` → `51`; `decodePayload(dg, 11, d, c)` → `12`. In `test_body_integration.py` no change. Also update the request REJECT list line `request = {6, 1, 0, 0, 0, 1, 0}; REJECT(request);` stays valid (op 6 with test 1 still rejected).

- [ ] **Step 5: Run the protocol and full suites**

Run: `cd tests && python3 -m unittest test_body_link` then `cd .. && python3 -m unittest discover -s tests -p "test_*.py"`
Expected: PASS. If `BodyController.cpp`/`DomeCalibration.cpp` fail to compile because `Diagnostics`/`CommissionStatus` aggregate initializers changed, they use value-init (`{}`) and should compile; fix any positional initializer by switching it to named-field assignment.

- [ ] **Step 6: Commit**

```bash
git add shared/R2BodyLink/Messages.h tests/test_body_link.py
git commit -m "feat(link): commissioning baseline op, wheel tests, extended status, field-known flag"
```

---

### Task 2: Baseline, field digest and profile comparison (`ConfigStore`)

**Files:**
- Modify: `TEENSY_BODY_CONTROLLER/src/body/ConfigStore.h`, `ConfigStore.cpp`
- Test: create `tests/test_commissioning_baseline.py`

**Interfaces:**
- Produces:
  - `struct ObservedFirmware { bool valid; uint8_t major, minor; };`
  - `uint8_t applyBaseline(CommissioningProfile& p, const ObservedFirmware fw[2]);` — fills unset fields only, returns the number filled.
  - `uint32_t fieldDigest(const CommissioningProfile& p, uint8_t wheel, const uint8_t* ids, size_t n);` — FNV-1a over (id, set flag, value) of those fields; never 0.
  - `bool sameProfile(const CommissioningProfile& a, const CommissioningProfile& b);` — all fields' set flags and values, `acceptance` and `allow_remote_drive` equal.

- [ ] **Step 1: Write the failing test** — create `tests/test_commissioning_baseline.py`:

```python
"""Guided commissioning: baseline fill, digests and profile comparison."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
SHARED = ROOT / "shared/R2BodyLink"
SOURCES = [BODY / "body/ConfigStore.cpp", SHARED / "src/Codec.cpp"]
PRELUDE = r'''
#include <cassert>
#include "body/ConfigStore.h"
using namespace body;
static int32_t get(const CommissioningProfile& p, uint8_t id, uint8_t w) {
    int32_t v = -999; return getField(p, id, w, v) ? v : -999;
}
'''


class BaselineTests(unittest.TestCase):
    def check(self, body):
        result = run_cpp(PRELUDE + "int main() {\n" + body + "\nreturn 0;\n}\n",
                         extra_sources=SOURCES, include_dirs=[BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_baseline_fills_every_unset_field_with_the_spec_values(self):
        self.check(r'''
    CommissioningProfile p;
    const ObservedFirmware fw[2] = {{true, 6, 2}, {true, 6, 2}};
    assert(applyBaseline(p, fw) == 5 + 2 * 13);
    assert(get(p, kFieldServoNeutral, 0) == 1500 && get(p, kFieldServoMin, 0) == 1000);
    assert(get(p, kFieldServoMax, 0) == 2000 && get(p, kFieldAutoSpeed, 0) == 15 && get(p, kFieldSlew, 0) == 500);
    for (uint8_t w = 0; w < 2; ++w) {
        assert(get(p, kFieldFwMajor, w) == 6 && get(p, kFieldFwMinor, w) == 2 && get(p, kFieldLayout, w) == 1);
        assert(get(p, kFieldMotorMa, w) == 12000 && get(p, kFieldBatteryMa, w) == 5000);
        assert(get(p, kFieldRegenMa, w) == 2500 && get(p, kFieldBrakeMa, w) == 3000);
        assert(get(p, kFieldUndervoltage, w) == 1100 && get(p, kFieldOvervoltage, w) == 1480);
        assert(get(p, kFieldTimeoutMs, w) == 150 && get(p, kFieldTimeoutBrakeMa, w) == 3000);
        assert(get(p, kFieldReversalErpm, w) == 300 && get(p, kFieldReversalDwell, w) == 200);
        assert(get(p, kFieldDirection, w) == -999);   // set by the direction test
    }
    assert(get(p, kFieldCwRate, 0) == -999 && get(p, kFieldCcwRate, 0) == -999);
    assert(validateProfile(p));
''')

    def test_baseline_never_overwrites_and_skips_unknown_firmware(self):
        self.check(r'''
    CommissioningProfile p;
    assert(setField(p, kFieldBrakeMa, 1, 4200) == FieldResult::Ok);
    assert(setField(p, kFieldServoNeutral, 0, 1512) == FieldResult::Ok);
    const ObservedFirmware fw[2] = {{false, 0, 0}, {true, 5, 3}};
    applyBaseline(p, fw);
    assert(get(p, kFieldBrakeMa, 1) == 4200 && get(p, kFieldServoNeutral, 0) == 1512);
    assert(get(p, kFieldFwMajor, 0) == -999 && get(p, kFieldFwMinor, 0) == -999);
    assert(get(p, kFieldFwMajor, 1) == 5 && get(p, kFieldFwMinor, 1) == 3);
    assert(applyBaseline(p, fw) == 0);   // idempotent
''')

    def test_field_digest_and_same_profile(self):
        self.check(r'''
    CommissioningProfile a, b;
    const ObservedFirmware fw[2] = {{true, 6, 2}, {true, 6, 2}};
    applyBaseline(a, fw); b = a;
    const uint8_t ids[] = {kFieldBrakeMa, kFieldTimeoutMs};
    assert(fieldDigest(a, 0, ids, 2) == fieldDigest(b, 0, ids, 2) && fieldDigest(a, 0, ids, 2) != 0);
    assert(sameProfile(a, b));
    assert(setField(b, kFieldBrakeMa, 0, 3100) == FieldResult::Ok);
    assert(fieldDigest(a, 0, ids, 2) != fieldDigest(b, 0, ids, 2));
    assert(fieldDigest(a, 1, ids, 2) == fieldDigest(b, 1, ids, 2));   // other wheel untouched
    assert(!sameProfile(a, b));
    b = a; b.acceptance |= 1u << kAcceptVescConfig; assert(!sameProfile(a, b));
''')


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests && python3 -m unittest test_commissioning_baseline`
Expected: FAIL — compile error, `unknown type name 'ObservedFirmware'`.

- [ ] **Step 3: Implement.** In `ConfigStore.h` after `uint8_t faultBits(ConfigResult r);` add:

```cpp
// Firmware a VESC reported over UART; valid=false when none was observed.
struct ObservedFirmware { bool valid; uint8_t major, minor; };
// Fills only unset fields with the recommended baseline (guided commissioning
// spec section 3.1); firmware fields only from observed firmware. Returns the
// number of fields filled. Never touches acceptance bits directly.
uint8_t applyBaseline(CommissioningProfile& p, const ObservedFirmware fw[2]);
// FNV-1a over (id, set flag, value) for the listed fields of one wheel; never 0.
uint32_t fieldDigest(const CommissioningProfile& p, uint8_t wheel, const uint8_t* ids, size_t n);
// True when every field (set flag and value), acceptance and allow_remote_drive match.
bool sameProfile(const CommissioningProfile& a, const CommissioningProfile& b);
```

In `ConfigStore.cpp` (outside the anonymous namespace, after `readiness`):

```cpp
uint8_t applyBaseline(CommissioningProfile& p, const ObservedFirmware fw[2]) {
    struct B { uint8_t id; int32_t value; };
    static const B kGlobal[] = {{kFieldServoNeutral, 1500}, {kFieldServoMin, 1000}, {kFieldServoMax, 2000},
                                {kFieldAutoSpeed, 15}, {kFieldSlew, 500}};
    static const B kWheel[] = {{kFieldLayout, 1}, {kFieldMotorMa, 12000}, {kFieldBatteryMa, 5000},
                               {kFieldRegenMa, 2500}, {kFieldBrakeMa, 3000}, {kFieldUndervoltage, 1100},
                               {kFieldOvervoltage, 1480}, {kFieldTimeoutMs, 150}, {kFieldTimeoutBrakeMa, 3000},
                               {kFieldReversalErpm, 300}, {kFieldReversalDwell, 200}};
    uint8_t filled = 0;
    for (const B& b : kGlobal)
        if (!isSet(p, b.id, 0) && setField(p, b.id, 0, b.value) == FieldResult::Ok) ++filled;
    for (uint8_t w = 0; w < 2; ++w) {
        for (const B& b : kWheel)
            if (!isSet(p, b.id, w) && setField(p, b.id, w, b.value) == FieldResult::Ok) ++filled;
        if (fw[w].valid) {
            if (!isSet(p, kFieldFwMajor, w) && setField(p, kFieldFwMajor, w, fw[w].major) == FieldResult::Ok) ++filled;
            if (!isSet(p, kFieldFwMinor, w) && setField(p, kFieldFwMinor, w, fw[w].minor) == FieldResult::Ok) ++filled;
        }
    }
    return filled;
}

uint32_t fieldDigest(const CommissioningProfile& p, uint8_t wheel, const uint8_t* ids, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t v = static_cast<uint32_t>(rawGet(p, ids[i], wheel));
        const uint8_t bytes[6] = {ids[i], static_cast<uint8_t>(isSet(p, ids[i], wheel)),
                                  static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                                  static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
        for (uint8_t b : bytes) h = (h ^ b) * 16777619u;
    }
    return h == 0 ? 1 : h;
}

bool sameProfile(const CommissioningProfile& a, const CommissioningProfile& b) {
    if (a.acceptance != b.acceptance || a.allow_remote_drive != b.allow_remote_drive) return false;
    for (size_t i = 0; i < kSpecCount; ++i) {
        const Spec& s = kSpecs[i];
        for (uint8_t w = 0; w < (s.wheel ? 2 : 1); ++w) {
            if (isSet(a, s.id, w) != isSet(b, s.id, w)) return false;
            if (isSet(a, s.id, w) && rawGet(a, s.id, w) != rawGet(b, s.id, w)) return false;
        }
    }
    return true;
}
```

The baseline counts: 5 globals + 11 per-wheel fields + 2 firmware fields = 13 per wheel → `5 + 2*13` in the test.

- [ ] **Step 4: Run tests**

Run: `cd tests && python3 -m unittest test_commissioning_baseline test_body_link`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add TEENSY_BODY_CONTROLLER/src/body/ConfigStore.h TEENSY_BODY_CONTROLLER/src/body/ConfigStore.cpp tests/test_commissioning_baseline.py
git commit -m "feat(config): baseline fill, field digest and profile comparison"
```

---

### Task 3: Commissioning gates without CH9, ApplyBaseline op, status masks

**Files:**
- Modify: `TEENSY_BODY_CONTROLLER/src/body/DomeCalibration.h`, `DomeCalibration.cpp`
- Test: `tests/test_dome_calibration.py`

**Interfaces:**
- Consumes: `applyBaseline`, `sameProfile`, `ObservedFirmware` (Task 2); new `CommissionStatus` fields (Task 1).
- Produces:
  - `CommissionOp::ApplyBaseline = 7`; `CommissionTest::WheelTimeout = 6, WheelDirection = 7, WheelReversal = 8` (replaces `VescTimeout`).
  - `void DomeCalibration::setSavedProfile(const CommissioningProfile& saved);` (defaults to the staged profile).
  - `void DomeCalibration::setObservedFirmware(uint8_t wheel, bool valid, uint8_t major, uint8_t minor);`
  - `status()` now fills `staged_acceptance`, `saved_acceptance`, `unsaved` on every `tick()` and after every `handleRequest()`.

- [ ] **Step 1: Write the failing tests** — add to `DomeCalibrationTests`:

```python
    def test_dome_tests_accept_and_save_never_need_ch9(self):
        self.check(r'''
    CalibrationFixture f;
    f.rc.channels[8] = 1000; f.tick(f.now + 20);          // CH9 OFF throughout
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoNeutral, 0, 1500), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoMin, 0, 1000), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoMax, 0, 2000), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::Neutral), 3), f.now) == Result::Accepted);
    f.advance(f.now + 3100);
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    assert(f.cal.handleRequest(f.makeAccept(kAcceptServoNeutral), f.now) == Result::Accepted);
    f.rc.channels[8] = 2000; f.tick(f.now + 20);          // and CH9 ON is fine too
    CommissionRequest save{}; save.operation = static_cast<uint8_t>(CommissionOp::Save); save.control_epoch = 1;
    assert(f.cal.handleRequest(save, f.now) == Result::Accepted);
    // CH6 ON still closes the gate.
    f.rc.channels[5] = 2000; f.tick(f.now + 20);
    assert(f.cal.handleRequest(f.makeSetField(kFieldServoNeutral, 0, 1505), f.now) == Result::Inhibited);
''')

    def test_apply_baseline_op_and_acceptance_masks_in_status(self):
        self.check(r'''
    CalibrationFixture f;
    CommissioningProfile saved;
    f.cal.setSavedProfile(saved);
    f.cal.setObservedFirmware(0, true, 6, 2);
    f.cal.setObservedFirmware(1, false, 0, 0);
    CommissionRequest b{}; b.operation = 7; b.control_epoch = 1;
    assert(f.cal.handleRequest(b, f.now) == Result::Accepted);
    int32_t v = 0;
    assert(getField(f.profile, kFieldFwMajor, 0, v) && v == 6);
    assert(!getField(f.profile, kFieldFwMajor, 1, v));
    assert(getField(f.profile, kFieldBrakeMa, 1, v) && v == 3000);
    f.profile.acceptance = 0x011;
    f.tick(f.now + 20);
    assert(f.cal.status().staged_acceptance == 0x011 && f.cal.status().saved_acceptance == 0);
    assert(f.cal.status().unsaved == 1);
    saved = f.profile; f.tick(f.now + 20);
    assert(f.cal.status().unsaved == 0 && f.cal.status().saved_acceptance == 0x011);
    f.rc.channels[5] = 2000; f.tick(f.now + 20);
    assert(f.cal.handleRequest(b, f.now) == Result::Inhibited);   // stationary gate
    // Field reads say whether the field is set.
    assert(f.cal.diagnostics(1, kFieldBrakeMa, 1, 0).known == 1 && f.cal.diagnostics(1, kFieldBrakeMa, 1, 0).value == 3000);
    assert(f.cal.diagnostics(1, kFieldDirection, 0, 0).known == 0);
''')
```

Also change the existing `test_ch6_on_manual_or_ch9_off_cancels` (it pins the removed CH9 rule): rename it `test_ch6_on_or_manual_cancels_and_ch9_off_does_not`, keep its CH6-ON and manual-stick cancellation sections, and replace its CH9-OFF section with:

```cpp
    // CH9 OFF does not cancel a running test any more.
    assert(f.cal.handleRequest(f.makeBegin(static_cast<uint8_t>(CommissionTest::TimingCw), 3), f.now) == Result::Accepted);
    f.rc.channels[8] = 1000;
    f.advance(f.now + 200);
    assert(f.cal.active());
```

(Read the existing test first; keep its CH6/manual assertions verbatim.)

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests && python3 -m unittest test_dome_calibration`
Expected: FAIL — `no member named 'setSavedProfile'`.

- [ ] **Step 3: Implement** in `DomeCalibration.h`:

```cpp
enum class CommissionOp : uint8_t {
    Read = 0, Begin = 1, Keepalive = 2, Cancel = 3, SetField = 4, Save = 5, Accept = 6, ApplyBaseline = 7
};
enum class CommissionTest : uint8_t {
    None = 0, Neutral = 1, FrontRef = 2, RearRef = 3, TimingCw = 4, TimingCcw = 5,
    WheelTimeout = 6, WheelDirection = 7, WheelReversal = 8
};
```

Public additions:

```cpp
    // Actuators run the saved profile; status() compares staged against it.
    void setSavedProfile(const CommissioningProfile& saved) { saved_ = &saved; }
    void setObservedFirmware(uint8_t wheel, bool valid, uint8_t major, uint8_t minor);
```

Private additions: `const CommissioningProfile* saved_{nullptr}; ObservedFirmware observed_fw_[2]{}; void refreshStatus();`

In `DomeCalibration.cpp`:

- `stationaryGate()` becomes `return rc_.valid && rc_.channels[5] < 1250 && isSticksNeutral() && !motion_locked_;` (RC freshness is folded into `rc_.valid` by the snapshot).
- Begin: delete the `if (test != CommissionTest::VescTimeout) { CH9 ... }` block and replace with `if (!isSticksNeutral()) return r2link::Result::Inhibited;`; change the range check `static_cast<uint8_t>(test) > 6` to `> 5` for now (wheel tests arrive in Task 6; until then Begin 6–8 returns `InvalidArgument`).
- `tick()`: replace the `if (test != CommissionTest::VescTimeout) { if (rc_.channels[8] < 1750 || !isSticksNeutral()) ... }` block with `if (!isSticksNeutral()) { cancel(now_ms); return; }`.
- Add the ApplyBaseline handler before `if (op == CommissionOp::Read)`:

```cpp
    if (op == CommissionOp::ApplyBaseline) {
        if (status_.state == static_cast<uint8_t>(CommissionState::Running)) return r2link::Result::Busy;
        if (!stationaryGate()) return r2link::Result::Inhibited;
        applyBaseline(profile_, observed_fw_);
        refreshStatus();
        return r2link::Result::Accepted;
    }
```

- Add:

```cpp
void DomeCalibration::setObservedFirmware(uint8_t wheel, bool valid, uint8_t major, uint8_t minor) {
    if (wheel > 1) return;
    observed_fw_[wheel].valid = valid;
    observed_fw_[wheel].major = major;
    observed_fw_[wheel].minor = minor;
}

void DomeCalibration::refreshStatus() {
    const CommissioningProfile& saved = saved_ ? *saved_ : profile_;
    status_.staged_acceptance = static_cast<uint16_t>(profile_.acceptance & 0x0FFFu);
    status_.saved_acceptance = static_cast<uint16_t>(saved.acceptance & 0x0FFFu);
    status_.unsaved = sameProfile(profile_, saved) ? 0 : 1;
}
```

- In `diagnostics()`, set `d.known = 1` when `getField(...)` succeeds (leave 0 otherwise):

```cpp
        if (getField(profile_, field, wheel, val)) { d.value = val; d.known = 1; }
```

- Call `refreshStatus()` at the very start of `tick()` and on every return path of `handleRequest()` (wrap: rename the current body to `handleRequestImpl` and make `handleRequest` call it then `refreshStatus()`).

- [ ] **Step 4: Run tests** — `cd tests && python3 -m unittest test_dome_calibration test_body_controller test_body_integration`. Expected: PASS. If a controller/integration test relied on CH9 gating, read it and confirm it asserts commissioning behaviour rather than auto dome before changing it.

- [ ] **Step 5: Commit**

```bash
git add TEENSY_BODY_CONTROLLER/src/body/DomeCalibration.* tests/test_dome_calibration.py
git commit -m "feat(commissioning): no CH9 gating, ApplyBaseline op, acceptance masks in status"
```

---

### Task 4: `WheelTest` state machine

**Files:**
- Create: `TEENSY_BODY_CONTROLLER/src/body/WheelTest.h`, `WheelTest.cpp`
- Test: create `tests/test_commissioning_wheel.py`

**Interfaces:**
- Consumes: `VescSample` (`VescLink.h`).
- Produces (namespace `body`):

```cpp
enum class WheelTestKind : uint8_t { TimeoutBrake = 6, Direction = 7, Reversal = 8 };
enum class WheelTestPhase : uint8_t { Idle, Running, Passed, Failed };
struct WheelTestCommand { enum Mode : uint8_t { Disable, Duty, Brake } mode; int16_t duty_permille; };
struct WheelTestResult { uint16_t stop_ms; int16_t peak_current_cA; int32_t peak_erpm; uint8_t fault; uint16_t error; };
class WheelTest {
public:
    static const int16_t kSpinPermille = 100;
    static const int32_t kTurningErpm = 100;
    static const uint32_t kSpinMs = 1000, kDirectionSpinMs = 1500, kStopLimitMs = 1500;
    static const uint32_t kBrakeHoldMs = 300, kTestLimitMs = 6000;
    // error codes reported in WheelTestResult::error / CommissionStatus::error
    static const uint16_t kErrNotTurning = 7, kErrTimeoutBrakeInactive = 8, kErrVescFault = 9,
                          kErrTelemetry = 10, kErrNoReversal = 11, kErrTimeLimit = 13;
    void begin(WheelTestKind kind, uint16_t reversal_erpm, uint16_t reversal_dwell_ms, uint32_t now_ms);
    void update(const VescSample& sample, uint32_t now_ms);  // once per loop with the tested wheel's sample
    void abort(uint32_t now_ms);                             // brake out, then Failed with error 0
    WheelTestCommand command() const;
    WheelTestPhase phase() const;
    bool busy() const;                                       // Running, including the final brake hold
    WheelTestKind kind() const;
    const WheelTestResult& result() const;
};
```

- [ ] **Step 1: Write the failing tests** — create `tests/test_commissioning_wheel.py`:

```python
"""Guided commissioning: WheelTest state machine with scripted telemetry."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
BODY = ROOT / "TEENSY_BODY_CONTROLLER/src"
SHARED = ROOT / "shared/R2BodyLink"
SOURCES = [BODY / "body/WheelTest.cpp"]
PRELUDE = r'''
#include <cassert>
#include "body/WheelTest.h"
using namespace body;
// A raised wheel: follows duty with a first-order lag, decays at `decay` erpm/ms
// when braked (or when the VESC timeout brake engages 150ms after commands stop).
struct Wheel {
    double erpm = 0; int32_t fault = 0; bool timeout_brake = true; double decay = 2.0;
    uint32_t last_cmd_ms = 0; bool stale = false;
    void step(const WheelTestCommand& c, uint32_t now) {
        if (c.mode == WheelTestCommand::Duty) { erpm += (c.duty_permille * 8.0 - erpm) * 0.01; last_cmd_ms = now; }
        else if (c.mode == WheelTestCommand::Brake) { erpm -= (erpm > 0 ? 1 : -1) * decay * (std::abs(erpm) > decay); last_cmd_ms = now; }
        else if (timeout_brake && now - last_cmd_ms > 150) erpm -= (erpm > 0 ? 1 : -1) * decay * (std::abs(erpm) > decay);
        else erpm *= 0.9995;   // coast
    }
    VescSample sample(uint32_t now) const {
        VescSample s{}; s.valid = !stale; s.stale = stale; s.erpm = int32_t(erpm); s.fault = uint8_t(fault);
        s.motor_mA = int32_t(-std::abs(erpm)); s.sample_ms = now; return s;
    }
};
static WheelTestPhase run(WheelTest& t, Wheel& w, uint32_t from, uint32_t to) {
    for (uint32_t now = from; now <= to && t.busy(); ++now) { t.update(w.sample(now), now); w.step(t.command(), now); }
    return t.phase();
}
'''


class WheelTestTests(unittest.TestCase):
    def check(self, body):
        result = run_cpp(PRELUDE.replace("#include <cassert>", "#include <cassert>\n#include <cmath>\n#include <cstdlib>")
                         + "int main() {\n" + body + "\nreturn 0;\n}\n",
                         extra_sources=SOURCES, include_dirs=[BODY, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_timeout_brake_passes_when_the_vesc_stops_the_wheel(self):
        self.check(r'''
    WheelTest t; Wheel w;
    t.begin(WheelTestKind::TimeoutBrake, 300, 200, 0);
    assert(t.command().mode == WheelTestCommand::Duty && t.command().duty_permille == 100);
    assert(run(t, w, 0, 6000) == WheelTestPhase::Passed);
    assert(t.result().stop_ms > 150 && t.result().stop_ms < 1500);
    assert(t.result().peak_erpm >= 100 && t.command().mode == WheelTestCommand::Disable);
''')

    def test_timeout_brake_fails_when_the_wheel_keeps_turning(self):
        self.check(r'''
    WheelTest t; Wheel w; w.timeout_brake = false;
    t.begin(WheelTestKind::TimeoutBrake, 300, 200, 0);
    assert(run(t, w, 0, 6000) == WheelTestPhase::Failed);
    assert(t.result().error == WheelTest::kErrTimeoutBrakeInactive);
''')

    def test_a_wheel_that_never_turns_fails(self):
        self.check(r'''
    WheelTest t; Wheel w; w.decay = 1e9;   // seized: erpm stays 0
    t.begin(WheelTestKind::Direction, 300, 200, 0);
    for (uint32_t now = 0; now <= 3000 && t.busy(); ++now) { t.update(VescSample{true,0,0,0,0,0,0,0,0,0,0,0,0,now,true,true,false,false,false}, now); }
    assert(t.phase() == WheelTestPhase::Failed && t.result().error == WheelTest::kErrNotTurning);
''')

    def test_direction_spins_positive_then_brakes(self):
        self.check(r'''
    WheelTest t; Wheel w;
    t.begin(WheelTestKind::Direction, 300, 200, 0);
    assert(run(t, w, 0, 6000) == WheelTestPhase::Passed);
    assert(t.result().peak_erpm >= 100);   // signed: positive for a positive raw command
''')

    def test_reversal_brakes_dwells_and_reverses(self):
        self.check(r'''
    WheelTest t; Wheel w;
    t.begin(WheelTestKind::Reversal, 300, 200, 0);
    bool saw_brake = false, saw_reverse = false;
    for (uint32_t now = 0; now <= 6000 && t.busy(); ++now) {
        t.update(w.sample(now), now);
        saw_brake |= t.command().mode == WheelTestCommand::Brake && w.erpm > 300;
        saw_reverse |= t.command().mode == WheelTestCommand::Duty && t.command().duty_permille == -100;
        if (saw_reverse) assert(std::abs(w.erpm) <= 300 || w.erpm < 0);   // never reversed at speed
        w.step(t.command(), now);
    }
    assert(t.phase() == WheelTestPhase::Passed && saw_brake && saw_reverse);
''')

    def test_fault_stale_abort_and_time_limit(self):
        self.check(r'''
    { WheelTest t; Wheel w; t.begin(WheelTestKind::TimeoutBrake, 300, 200, 0);
      run(t, w, 0, 500); w.fault = 4;
      assert(run(t, w, 501, 6000) == WheelTestPhase::Failed);
      assert(t.result().error == WheelTest::kErrVescFault && t.result().fault == 4); }
    { WheelTest t; Wheel w; t.begin(WheelTestKind::Direction, 300, 200, 0);
      run(t, w, 0, 500); w.stale = true;
      t.update(w.sample(501), 501);
      assert(t.command().mode == WheelTestCommand::Brake);          // brake out first
      assert(run(t, w, 502, 6000) == WheelTestPhase::Failed && t.result().error == WheelTest::kErrTelemetry); }
    { WheelTest t; Wheel w; t.begin(WheelTestKind::Reversal, 300, 200, 0);
      run(t, w, 0, 700); t.abort(701);
      assert(t.command().mode == WheelTestCommand::Brake && t.busy());
      assert(run(t, w, 702, 1100) == WheelTestPhase::Failed && t.result().error == 0);
      assert(t.command().mode == WheelTestCommand::Disable); }
''')


if __name__ == "__main__":
    unittest.main()
```

Note for the implementer: `VescSample` field order is `wheel, fw_major, fw_minor, valid_fields, source_age_ms, pack_cV, motor_mA, input_mA, erpm, mosfet_dC, motor_dC, duty_permille, fault, sample_ms, valid, profile_match, stale, unsupported, motor_temperature_valid` (plus `fw_known` after Task 5). In `test_a_wheel_that_never_turns_fails` replace the aggregate with explicit assignments if the order differs: `VescSample s{}; s.valid = true; s.sample_ms = now;`.

- [ ] **Step 2: Run to verify it fails**

Run: `cd tests && python3 -m unittest test_commissioning_wheel`
Expected: FAIL — `'body/WheelTest.h' file not found`.

- [ ] **Step 3: Implement** `TEENSY_BODY_CONTROLLER/src/body/WheelTest.h` (interface exactly as in **Interfaces** above, plus private members):

```cpp
private:
    enum class Step : uint8_t { Idle, SpinUp, CoastWatch, BrakeToLow, Dwell, ReverseSpin, BrakeOut, Done };
    void finish(WheelTestPhase phase, uint16_t error, uint32_t now_ms);
    WheelTestKind kind_{WheelTestKind::TimeoutBrake};
    WheelTestPhase phase_{WheelTestPhase::Idle}, final_{WheelTestPhase::Idle};
    Step step_{Step::Idle};
    WheelTestResult result_{};
    uint16_t reversal_erpm_{0}, dwell_ms_{0};
    uint32_t begin_ms_{0}, step_ms_{0}, low_since_ms_{0};
    int32_t spin_erpm_{0};
    bool low_started_{false};
```

`WheelTest.cpp`:

```cpp
#include "body/WheelTest.h"
#include <stdlib.h>

namespace body {
namespace {
int32_t mag(int32_t v) { return v < 0 ? -v : v; }
}

void WheelTest::begin(WheelTestKind kind, uint16_t reversal_erpm, uint16_t reversal_dwell_ms, uint32_t now) {
    kind_ = kind; reversal_erpm_ = reversal_erpm; dwell_ms_ = reversal_dwell_ms;
    result_ = WheelTestResult{};
    phase_ = WheelTestPhase::Running; final_ = WheelTestPhase::Running;
    step_ = Step::SpinUp; begin_ms_ = step_ms_ = now; spin_erpm_ = 0; low_started_ = false;
}

void WheelTest::finish(WheelTestPhase phase, uint16_t error, uint32_t now) {
    final_ = phase; result_.error = error;
    step_ = Step::BrakeOut; step_ms_ = now;   // always leave the wheel braked, then released
}

void WheelTest::abort(uint32_t now) {
    if (!busy() || step_ == Step::BrakeOut) return;
    finish(WheelTestPhase::Failed, 0, now);
}

void WheelTest::update(const VescSample& s, uint32_t now) {
    if (!busy()) return;
    if (step_ == Step::BrakeOut) {
        if (now - step_ms_ >= kBrakeHoldMs) { step_ = Step::Done; phase_ = final_; }
        return;
    }
    if (now - begin_ms_ >= kTestLimitMs) { finish(WheelTestPhase::Failed, kErrTimeLimit, now); return; }
    if (s.fault) { result_.fault = s.fault; finish(WheelTestPhase::Failed, kErrVescFault, now); return; }
    if (!s.valid || s.stale) { finish(WheelTestPhase::Failed, kErrTelemetry, now); return; }
    if (mag(s.erpm) > mag(result_.peak_erpm)) result_.peak_erpm = s.erpm;
    const uint32_t in_step = now - step_ms_;
    switch (step_) {
    case Step::SpinUp: {
        const uint32_t spin = kind_ == WheelTestKind::Direction ? kDirectionSpinMs : kSpinMs;
        if (mag(s.erpm) > mag(spin_erpm_)) spin_erpm_ = s.erpm;
        if (in_step < spin) return;
        if (mag(spin_erpm_) < kTurningErpm) { finish(WheelTestPhase::Failed, kErrNotTurning, now); return; }
        if (kind_ == WheelTestKind::Direction) { finish(WheelTestPhase::Passed, 0, now); return; }
        step_ = kind_ == WheelTestKind::TimeoutBrake ? Step::CoastWatch : Step::BrakeToLow;
        step_ms_ = now;
        return;
    }
    case Step::CoastWatch: {
        const int32_t current_cA = int32_t(mag(s.motor_mA) / 10);
        if (current_cA > mag(result_.peak_current_cA)) result_.peak_current_cA = int16_t(-current_cA);
        if (mag(s.erpm) * 10 <= mag(spin_erpm_)) {
            result_.stop_ms = uint16_t(in_step);
            step_ = Step::Done; phase_ = WheelTestPhase::Passed;   // already stopped: no brake needed
            return;
        }
        if (in_step >= kStopLimitMs) finish(WheelTestPhase::Failed, kErrTimeoutBrakeInactive, now);
        return;
    }
    case Step::BrakeToLow: {
        const int32_t current_cA = int32_t(mag(s.motor_mA) / 10);
        if (current_cA > mag(result_.peak_current_cA)) result_.peak_current_cA = int16_t(-current_cA);
        if (mag(s.erpm) <= reversal_erpm_) {
            result_.stop_ms = uint16_t(in_step);
            step_ = Step::Dwell; step_ms_ = now; low_since_ms_ = now;
        } else if (in_step >= kStopLimitMs) {
            finish(WheelTestPhase::Failed, kErrNoReversal, now);
        }
        return;
    }
    case Step::Dwell:
        if (mag(s.erpm) > reversal_erpm_) low_since_ms_ = now;
        if (now - low_since_ms_ >= dwell_ms_) { step_ = Step::ReverseSpin; step_ms_ = now; }
        return;
    case Step::ReverseSpin:
        if (in_step < kSpinMs) return;
        if ((spin_erpm_ > 0 ? s.erpm <= -kTurningErpm : s.erpm >= kTurningErpm))
            finish(WheelTestPhase::Passed, 0, now);
        else
            finish(WheelTestPhase::Failed, kErrNoReversal, now);
        return;
    default:
        return;
    }
}

WheelTestCommand WheelTest::command() const {
    switch (step_) {
    case Step::SpinUp: return {WheelTestCommand::Duty, kSpinPermille};
    case Step::ReverseSpin: return {WheelTestCommand::Duty, int16_t(-kSpinPermille)};
    case Step::BrakeToLow: case Step::Dwell: case Step::BrakeOut: return {WheelTestCommand::Brake, 0};
    default: return {WheelTestCommand::Disable, 0};   // Idle, CoastWatch (no commands), Done
    }
}

WheelTestPhase WheelTest::phase() const { return phase_; }
bool WheelTest::busy() const { return step_ != Step::Idle && step_ != Step::Done; }
WheelTestKind WheelTest::kind() const { return kind_; }
const WheelTestResult& WheelTest::result() const { return result_; }

}  // namespace body
```

- [ ] **Step 4: Run tests** — `cd tests && python3 -m unittest test_commissioning_wheel`. Expected: PASS. If the scripted `Wheel` model's constants make a pass/fail borderline, adjust the model (not the thresholds) so that a working timeout brake stops within ~500 ms and coasting does not.

- [ ] **Step 5: Add `WheelTest.cpp` to every test source list that compiles `DomeCalibration.cpp`** (`test_dome_calibration.py`, `test_body_controller.py`, `test_body_integration.py`, `test_body_drive.py` main-rig source list, `test_body_radio.py` if it compiles `main.cpp`) — this is needed once Task 6 includes it; do it in Task 6 Step 3 instead if no test fails now.

- [ ] **Step 6: Commit**

```bash
git add TEENSY_BODY_CONTROLLER/src/body/WheelTest.* tests/test_commissioning_wheel.py
git commit -m "feat(commissioning): WheelTest state machine for timeout-brake, direction and reversal"
```

---

### Task 5: VescLink commissioning permission and `fw_known`

**Files:**
- Modify: `TEENSY_BODY_CONTROLLER/src/body/VescLink.h`, `VescLink.cpp`
- Test: `tests/test_body_vesc.py`

**Interfaces:**
- Produces:
  - `bool VescSample::fw_known;` (true once a FW_VERSION reply was seen).
  - `void VescLink::setCommissioning(bool enabled);`
  - `bool VescLink::commissioningReady(uint32_t now) const;` — `config_accepted_ && match() && sample(now).valid && !fault`.
  - `static const int16_t VescLink::kCommissionDutyLimit = 100;`

- [ ] **Step 1: Write the failing test** — add to `VescTests`:

```python
    def test_commissioning_mode_allows_low_duty_with_only_config_accepted(self):
        self.check(r'''
    CommissioningProfile p = saved();
    p.acceptance = (1u << kAcceptVescConfig) | (1u << (kAcceptVescConfig + 1));   // config only
    assert(validateProfile(p));
    Port q; VescLink l(q, 0);
    l.setProfile(VescProfile::fromSaved(p, 0)); l.tick(0);
    feed(q, l, wire({0, 42, 19}), 1); l.tick(100); feed(q, l, wire(values()), 101);
    assert(l.sample(101).fw_known && l.commissioningReady(101));
    q.tx.clear(); l.setDuty(80); l.tick(102);
    assert(count(q.tx, 5) == 0);                         // normal mode: not control-accepted
    l.setCommissioning(true);
    l.setDuty(80); l.tick(103); assert(count(q.tx, 5) == 1);
    q.tx.clear(); l.setDuty(150); l.tick(104); assert(count(q.tx, 5) == 0);   // over the 10% limit
    l.setBrake(1500); l.tick(105); assert(count(q.tx, 7) == 1);
    l.setCommissioning(false);
    q.tx.clear(); l.tick(130); l.setDuty(80); l.tick(131); assert(count(q.tx, 5) == 0);
    // Unaccepted config: never ready.
    Port r; VescLink m(r, 0); CommissioningProfile none = saved(); none.acceptance = 0;
    m.setProfile(VescProfile::fromSaved(none, 0)); m.tick(0);
    feed(r, m, wire({0, 42, 19}), 1);
    assert(m.sample(1).fw_known && !m.commissioningReady(1));
''')
```

- [ ] **Step 2: Run to verify it fails** — `cd tests && python3 -m unittest test_body_vesc.VescTests.test_commissioning_mode_allows_low_duty_with_only_config_accepted`. Expected: FAIL — `no member named 'fw_known'`.

- [ ] **Step 3: Implement.** `VescLink.h`: add `bool fw_known;` to `VescSample` (after `motor_temperature_valid`); public `void setCommissioning(bool enabled) { commissioning_ = enabled; }`, `bool commissioningReady(uint32_t now_ms) const;`, `static const int16_t kCommissionDutyLimit = 100;`; private `bool commissioning_ = false;` (initialize in the constructor list as `commissioning_(false)` after `capture_{}`... add it at the end of the member list and initializer list).

`VescLink.cpp`:

```cpp
// sample(): after s.wheel = wheel_; ...
    s.fw_known = have_firmware_;

bool VescLink::commissioningReady(uint32_t now) const {
    const VescSample s = sample(now);
    return profile_.config_accepted_ && match() && s.valid && !s.fault;
}
bool VescLink::brakePermitted() const {
    const bool allowed = profile_.control_accepted_ || (commissioning_ && profile_.config_accepted_);
    return allowed && match() && brake_ && brake_ == profile_.brake_ma_;
}
bool VescLink::dutyPermitted(uint32_t now) const {
    const VescSample s = sample(now);
    const bool commission = commissioning_ && profile_.config_accepted_ &&
        duty_ >= -kCommissionDutyLimit && duty_ <= kCommissionDutyLimit;
    return (profile_.control_accepted_ || commission) && s.valid && !s.fault && uint32_t(now - demand_ms_) <= 20;
}
```

- [ ] **Step 4: Run** `cd tests && python3 -m unittest test_body_vesc test_body_drive`. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add TEENSY_BODY_CONTROLLER/src/body/VescLink.* tests/test_body_vesc.py
git commit -m "feat(vesc): commissioning mode and firmware-known flag"
```

---

### Task 6: Wheel tests in the commissioning lifecycle + BodyController wiring

**Files:**
- Modify: `DomeCalibration.{h,cpp}`, `BodyController.{h,cpp}`
- Test: `tests/test_dome_calibration.py`, `tests/test_body_controller.py`

**Interfaces:**
- Consumes: `WheelTest` (Task 4), `VescLink::setCommissioning/commissioningReady`, `VescSample::fw_known` (Task 5), `fieldDigest` (Task 2).
- Produces (DomeCalibration public):
  - `void setWheelReady(uint8_t wheel, bool ready);`
  - `void updateWheelSample(const VescSample& sample, uint32_t now_ms);` (tested wheel only)
  - `bool wheelTestBusy() const;` `uint8_t wheelUnderTest() const;` `WheelTestCommand wheelCommand() const;`
  - Error code 12 = wheel not ready (config not accepted+saved, firmware mismatch or stale telemetry); 14 = wheel still turning at Begin.

- [ ] **Step 1: Write failing tests** — add to `DomeCalibrationTests` (fixture already has `makeBegin`; add a helper in the PRELUDE struct):

```cpp
    CommissionRequest makeWheelBegin(CommissionTest test, uint8_t wheel, uint32_t run_id, int32_t raised = 1) {
        CommissionRequest req = makeBegin(static_cast<uint8_t>(test), run_id, raised);
        req.wheel = wheel;
        return req;
    }
    VescSample wheelSample(int32_t erpm, uint32_t t) {
        VescSample s{}; s.valid = true; s.erpm = erpm; s.sample_ms = t; return s;
    }
```

(Add `#include "body/VescLink.h"` to the PRELUDE and `BODY / "body/WheelTest.cpp"`, `BODY / "body/VescLink.cpp"` to `SOURCES`.)

```python
    def test_wheel_test_lifecycle_and_gates(self):
        self.check(r'''
    CalibrationFixture f;
    f.rc.channels[8] = 1000; f.tick(f.now + 20);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 5), f.now) == Result::NotReady);
    assert(f.cal.status().error == 12);
    f.cal.setWheelReady(0, true);
    f.cal.updateWheelSample(f.wheelSample(400, f.now), f.now);          // still coasting
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 5), f.now) == Result::Inhibited);
    assert(f.cal.status().error == 14);
    f.cal.updateWheelSample(f.wheelSample(0, f.now), f.now);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 6, 0), f.now) == Result::InvalidArgument);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelDirection, 0, 6), f.now) == Result::Accepted);
    assert(f.cal.wheelTestBusy() && f.cal.wheelUnderTest() == 0);
    assert(f.cal.wheelCommand().mode == WheelTestCommand::Duty && f.cal.wheelCommand().duty_permille == 100);
    // Keepalive lost mid-spin: brake 300ms, then nothing; state Cancelled.
    for (uint32_t t = f.now + 1; t <= f.now + 400; ++t) { f.cal.updateWheelSample(f.wheelSample(600, t), t); f.tick(t); }
    assert(f.cal.status().state != static_cast<uint8_t>(CommissionState::Running));
    assert(f.cal.wheelCommand().mode == WheelTestCommand::Brake && f.cal.wheelTestBusy());
    CommissionRequest save{}; save.operation = static_cast<uint8_t>(CommissionOp::Save); save.control_epoch = 1;
    assert(f.cal.handleRequest(save, f.now) == Result::Busy);            // still braking out
    assert(f.cal.handleRequest(f.makeSetField(kFieldBrakeMa, 0, 3000), f.now) == Result::Busy);
    for (uint32_t t = f.now + 1; t <= f.now + 400; ++t) { f.cal.updateWheelSample(f.wheelSample(0, t), t); f.tick(t); }
    assert(!f.cal.wheelTestBusy() && f.cal.wheelCommand().mode == WheelTestCommand::Disable);
''')

    def test_wheel_sign_offs_need_a_passed_test_on_the_saved_settings(self):
        self.check(r'''
    CalibrationFixture f;
    CommissioningProfile saved = saved_profile_for_tests();
    f.profile = saved; f.cal.setSavedProfile(saved);
    f.rc.channels[8] = 1000; f.tick(f.now + 20);
    f.cal.setWheelReady(1, true);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake + 1), f.now) == Result::Inhibited);   // no test yet
    f.cal.updateWheelSample(f.wheelSample(0, f.now), f.now);
    assert(f.cal.handleRequest(f.makeWheelBegin(CommissionTest::WheelTimeout, 1, 9), f.now) == Result::Accepted);
    // Spin 1s at 800 erpm, then the VESC stops the wheel 300ms after commands stop.
    uint32_t cut = 0;
    for (uint32_t t = f.now + 1; t <= f.now + 3000 && f.cal.wheelTestBusy(); ++t) {
        const bool spinning = f.cal.wheelCommand().mode == WheelTestCommand::Duty;
        if (!spinning && !cut) cut = t;
        const int32_t erpm = spinning ? 800 : (t - cut < 300 ? 800 : 0);
        f.cal.updateWheelSample(f.wheelSample(erpm, t), t);
        f.tick(t);
        if (t % 100 == 0 && f.cal.active()) f.cal.handleRequest(f.makeKeepalive(9), t);
    }
    assert(f.cal.status().state == static_cast<uint8_t>(CommissionState::Completed));
    assert(f.cal.status().wheel == 1 && f.cal.status().stop_ms >= 290 && f.cal.status().stop_ms <= 400);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake + 1), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake), f.now) == Result::Inhibited);       // left not tested
    // A staged change to a tested field voids the evidence.
    f.profile.acceptance &= ~(1u << (kAcceptTimeoutBrake + 1));
    assert(f.cal.handleRequest(f.makeSetField(kFieldTimeoutBrakeMa, 1, 2500), f.now) == Result::Accepted);
    assert(f.cal.handleRequest(f.makeAccept(kAcceptTimeoutBrake + 1), f.now) == Result::Inhibited);
''')
```

Add to the PRELUDE (test utility, copies `test_body_vesc.py`'s `saved()` values):

```cpp
static CommissioningProfile saved_profile_for_tests() {
    CommissioningProfile p;
    for (int w = 0; w < 2; ++w) {
        const int v[] = {1, 42, 19, 1, 1000, 1000, 0, 1500, 1050, 1500, 150, 1500, 100, 50};
        for (int id = 5; id <= 18; ++id) assert(setField(p, id, w, v[id - 5]) == FieldResult::Ok);
        p.acceptance |= 1u << (kAcceptVescConfig + w);
    }
    assert(validateProfile(p));
    return p;
}
```

And in `tests/test_body_controller.py` (uses `LockRig`, `readyDrive`, `vescFrame`):

```python
    def test_wheel_test_drives_only_the_tested_vesc_with_low_duty(self):
        self.run_lock_rig(r'''
LockRig g; g.run(10);
body::CommissioningProfile& p = g.controller.profile();
readyDrive(p);
p.acceptance &= ~((0x3u << body::kAcceptTimeoutBrake) | (0x3u << body::kAcceptDirection) | (0x3u << body::kAcceptReversal));
g.save();                                              // vesc_config only, saved
// Scripted VESC replies: firmware 6.2 and a GET_VALUES reply on demand.
auto values = [](int32_t erpm) {
    std::vector<uint8_t> v(54, 0); v[0] = 4; v[27] = 0; v[28] = 128;
    v[23] = uint8_t(erpm >> 24); v[24] = uint8_t(erpm >> 16); v[25] = uint8_t(erpm >> 8); v[26] = uint8_t(erpm);
    return vescFrame(v);
};
for (int i = 0; i < 30; ++i) {
    for (FakePort* port : {&g.left_vesc_port, &g.right_vesc_port}) {
        const auto fw = vescFrame({0, 6, 2}); const auto vals = values(0);
        port->rx_bytes.insert(port->rx_bytes.end(), fw.begin(), fw.end());
        port->rx_bytes.insert(port->rx_bytes.end(), vals.begin(), vals.end());
    }
    g.run(20);
}
assert(g.controller.leftVesc().commissioningReady(g.now));
r2link::CommissionRequest begin{}; begin.operation = 1; begin.test = 7; begin.wheel = 0; begin.value = 1;
begin.run_id = 77; begin.control_epoch = g.controller.status().control_epoch;
r2link::Frame f{}; r2link::ErrorCounters e{};
assert(r2link::encode(begin, f, e) == r2link::Status::Ok);
g.left_vesc_port.tx_bytes.clear(); g.right_vesc_port.tx_bytes.clear();
assert(g.controller.handle(f, g.now) == r2link::Result::Accepted);
g.run(50);
auto duties = [](const std::vector<uint8_t>& tx, int16_t& max_permille) {
    int n = 0; max_permille = 0;
    for (size_t i = 0; i + 9 < tx.size(); ++i)
        if (tx[i] == 2 && tx[i + 1] == 5 && tx[i + 2] == 5) {
            const int32_t v = int32_t(uint32_t(tx[i+3]) << 24 | uint32_t(tx[i+4]) << 16 | uint32_t(tx[i+5]) << 8 | tx[i+6]);
            if (v / 100 > max_permille) max_permille = int16_t(v / 100);
            ++n;
        }
    return n;
};
int16_t left_max = 0, right_max = 0;
assert(duties(g.left_vesc_port.tx_bytes, left_max) > 0 && left_max == 100);
assert(duties(g.right_vesc_port.tx_bytes, right_max) == 0);
''')
```

- [ ] **Step 2: Run to verify failure** — `cd tests && python3 -m unittest test_dome_calibration test_body_controller`. Expected: FAIL — `no member named 'setWheelReady'`.

- [ ] **Step 3: Implement `DomeCalibration`.** Header: `#include "body/WheelTest.h"` and `#include "body/VescLink.h"`; public methods from **Interfaces**; private:

```cpp
    WheelTest wheel_test_;
    uint8_t wheel_{0};
    bool wheel_ready_[2]{false, false};
    VescSample wheel_sample_{};
    uint32_t wheel_done_run_[3][2]{};      // [kind-6][wheel]
    uint32_t wheel_done_digest_[3][2]{};
    static uint32_t wheelDigest(const CommissioningProfile& p, uint8_t kind_index, uint8_t wheel);
```

`DomeCalibration.cpp` additions:

```cpp
namespace {
const uint8_t kTimeoutIds[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const uint8_t kDirectionIds[] = {6, 7, 8, 9, 10, 11, 12, 13, 14};
const uint8_t kReversalIds[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18};
}

uint32_t DomeCalibration::wheelDigest(const CommissioningProfile& p, uint8_t k, uint8_t w) {
    if (k == 0) return fieldDigest(p, w, kTimeoutIds, sizeof kTimeoutIds);
    if (k == 1) return fieldDigest(p, w, kDirectionIds, sizeof kDirectionIds);
    return fieldDigest(p, w, kReversalIds, sizeof kReversalIds);
}

void DomeCalibration::setWheelReady(uint8_t wheel, bool ready) { if (wheel < 2) wheel_ready_[wheel] = ready; }
void DomeCalibration::updateWheelSample(const VescSample& s, uint32_t now_ms) {
    wheel_sample_ = s;
    if (wheel_test_.busy()) wheel_test_.update(s, now_ms);
}
bool DomeCalibration::wheelTestBusy() const { return wheel_test_.busy(); }
uint8_t DomeCalibration::wheelUnderTest() const { return wheel_; }
WheelTestCommand DomeCalibration::wheelCommand() const { return wheel_test_.command(); }
```

Begin changes (in `handleRequestImpl`, op Begin): range check `test > 8` → InvalidArgument; for tests ≥ 6, after the stationary gate:

```cpp
        if (static_cast<uint8_t>(test) >= 6) {
            if (req.value != 1 || req.wheel > 1) return r2link::Result::InvalidArgument;
            if (status_.state == static_cast<uint8_t>(CommissionState::Running) || wheel_test_.busy())
                return req.run_id == status_.run_id ? r2link::Result::Accepted : r2link::Result::Busy;
            if (!wheel_ready_[req.wheel]) { status_.error = 12; return r2link::Result::NotReady; }
            if (wheel_sample_.erpm <= -WheelTest::kTurningErpm || wheel_sample_.erpm >= WheelTest::kTurningErpm) {
                status_.error = 14; return r2link::Result::Inhibited;
            }
            const CommissioningProfile& saved = saved_ ? *saved_ : profile_;
            wheel_ = req.wheel;
            wheel_done_run_[req.test - 6][wheel_] = 0;
            status_ = r2link::CommissionStatus{};   // keep acceptance fields; refreshStatus() refills them
            status_.run_id = req.run_id; status_.test = req.test; status_.wheel = wheel_;
            status_.state = static_cast<uint8_t>(CommissionState::Running);
            status_.config_generation = store_.generation();
            keepalive_deadline_ms_ = now_ms + 300;
            wheel_test_.begin(static_cast<WheelTestKind>(req.test),
                              saved.wheel[wheel_].reversal_erpm_limit, saved.wheel[wheel_].reversal_dwell_ms, now_ms);
            return r2link::Result::Accepted;
        }
```

Busy gates: SetField, Save, Accept and ApplyBaseline also return `Busy` when `wheel_test_.busy()`.

`tick()` — before the dome-test logic, handle a wheel test:

```cpp
    if (status_.state == static_cast<uint8_t>(CommissionState::Running) && status_.test >= 6) {
        const bool abort = motion_locked_ || now_ms >= keepalive_deadline_ms_ || !rc_.valid ||
                           (now_ms - rc_.sample_ms > 250) || rc_.channels[5] >= 1250 || !isSticksNeutral();
        if (abort) {
            wheel_test_.abort(now_ms);
            status_.state = static_cast<uint8_t>(now_ms >= keepalive_deadline_ms_ ? CommissionState::TimedOut
                                                                                  : CommissionState::Cancelled);
            return;
        }
        if (!wheel_test_.busy()) {
            const WheelTestResult& r = wheel_test_.result();
            status_.stop_ms = r.stop_ms; status_.peak_current_cA = r.peak_current_cA;
            status_.peak_erpm = r.peak_erpm; status_.vesc_fault = r.fault;
            if (wheel_test_.phase() == WheelTestPhase::Passed) {
                const CommissioningProfile& saved = saved_ ? *saved_ : profile_;
                const uint8_t k = status_.test - 6;
                wheel_done_run_[k][wheel_] = status_.run_id;
                wheel_done_digest_[k][wheel_] = wheelDigest(saved, k, wheel_);
                status_.state = static_cast<uint8_t>(CommissionState::Completed);
            } else {
                status_.state = static_cast<uint8_t>(CommissionState::Failed);
                status_.error = r.error;
            }
        }
        return;
    }
```

`cancel()` also calls `wheel_test_.abort(now_ms)`. `active()` stays "state == Running".

Accept — replace the `if (bit >= 4 && bit <= 11) ev.vesc_operator_observed = true;` block:

```cpp
        if (bit == kAcceptVescConfig || bit == kAcceptVescConfig + 1) {
            ev.vesc_operator_observed = true;   // operator compared VESC Tool with the staged record
        } else if (bit >= kAcceptTimeoutBrake && bit < kAcceptBitCount) {
            const uint8_t k = static_cast<uint8_t>((bit - kAcceptTimeoutBrake) / 2);   // 0 timeout, 1 direction, 2 reversal
            const uint8_t w = bit & 1;
            ev.vesc_operator_observed = wheel_done_run_[k][w] != 0 &&
                                        wheel_done_digest_[k][w] == wheelDigest(profile_, k, w);
        }
```

`acceptBit` returns `TestEvidence` when `vesc_operator_observed` is false → mapped to `Inhibited` as today.

- [ ] **Step 4: Implement `BodyController` wiring.** In the constructor body: `calibration_.setSavedProfile(active_);`. In `tick()` replace the actuator block:

```cpp
    // 3. Actuator updates
    for (uint8_t w = 0; w < 2; ++w) {
        VescLink& link = w ? right_vesc_ : left_vesc_;
        const VescSample s = link.sample(now_ms);
        calibration_.setObservedFirmware(w, s.fw_known, s.fw_major, s.fw_minor);
        calibration_.setWheelReady(w, link.commissioningReady(now_ms));
    }
    if (calibration_.wheelTestBusy()) {
        const uint8_t w = calibration_.wheelUnderTest();
        VescLink& link = w ? right_vesc_ : left_vesc_;
        calibration_.updateWheelSample(link.sample(now_ms), now_ms);
    } else {
        calibration_.updateWheelSample((calibration_.status().wheel ? right_vesc_ : left_vesc_).sample(now_ms), now_ms);
    }
    calibration_.tick(now_ms);
    if (calibration_.wheelTestBusy()) {
        const uint8_t w = calibration_.wheelUnderTest();
        VescLink& tested = w ? right_vesc_ : left_vesc_;
        VescLink& other = w ? left_vesc_ : right_vesc_;
        tested.setCommissioning(true);
        const WheelTestCommand c = calibration_.wheelCommand();
        if (c.mode == WheelTestCommand::Duty) tested.setDuty(c.duty_permille);
        else if (c.mode == WheelTestCommand::Brake) tested.setBrake(active_.wheel[w].brake_ma);
        else tested.disableControl();
        other.disableControl();
    } else {
        left_vesc_.setCommissioning(false);
        right_vesc_.setCommissioning(false);
        if (!calibration_.active()) {
            drive_.update(rc_snapshot_, left_vesc_.sample(now_ms), right_vesc_.sample(now_ms), active_, now_ms);
            applyWheelCommands(drive_.commands(), left_vesc_, right_vesc_);
        } else {
            WheelCommands neut{};
            applyWheelCommands(neut, left_vesc_, right_vesc_);
        }
    }
```

Note: the "Begin requires a stationary wheel" check reads `wheel_sample_`, which only tracks the last-tested wheel. Before Begin, `handleCommissionRequest` must feed the requested wheel: in `BodyController::handleCommissionRequest`, before `calibration_.handleRequest(req, now_ms)`, add `if (req.operation == 1 && req.test >= 6 && req.wheel < 2) calibration_.updateWheelSample((req.wheel ? right_vesc_ : left_vesc_).sample(now_ms), now_ms);`.

VescStatus publishing in `publishPeriodic()`: change `if (sl.valid) {` to `if (sl.fw_known) {` and set `vs.valid_fields = sl.valid ? sl.valid_fields : 0;` (keep the other fields; they are zeros when invalid).

CLI: add before `snprintf(out, out_max, "ERROR unknown command\n")`:

```cpp
    if (std::strcmp(line, "profile baseline") == 0) {
        r2link::CommissionRequest req{};
        req.operation = static_cast<uint8_t>(CommissionOp::ApplyBaseline);
        req.control_epoch = control_epoch_;
        const r2link::Result res = calibration_.handleRequest(req, now_ms);
        snprintf(out, out_max, res == r2link::Result::Accepted ? "OK baseline staged (profile save to apply)\n"
                                                               : "ERROR baseline refused code=%u\n", static_cast<unsigned>(res));
        return true;
    }
```

Add `BODY / "body/WheelTest.cpp"` to the SOURCES lists of `test_body_controller.py`, `test_body_integration.py`, and the main-rig source lists in `test_body_drive.py` / `test_body_radio.py` (any list that already contains `DomeCalibration.cpp`), and to `TEENSY_BODY_CONTROLLER` nothing (PlatformIO compiles `src/body/*` automatically).

- [ ] **Step 5: Run** `python3 -m unittest discover -s tests -p "test_*.py"` from the repo root. Expected: PASS.

- [ ] **Step 6: Build check** `cd TEENSY_BODY_CONTROLLER && PLATFORMIO_BUILD_DIR=/tmp/teensy_wheel pio run`. Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add TEENSY_BODY_CONTROLLER/src/body tests
git commit -m "feat(commissioning): automated wheel tests with saved-settings evidence"
```

---

### Task 7: Auto-dome re-arm after a Save

**Files:**
- Modify: `DomeController.{h,cpp}`, `BodyController.cpp`
- Test: `tests/test_body_dome.py`, `tests/test_body_controller.py`

**Interfaces:**
- Produces: `void DomeController::profileActivated(bool auto_was_ready);` — when the active profile is now auto-ready and was not, auto behaviour waits for CH9 OFF→ON.

- [ ] **Step 1: Failing test** in `test_body_dome.py`:

```python
    def test_save_that_enables_auto_dome_waits_for_a_ch9_flip(self):
        self.check(r'''
DomeFixture f;
f.profile.acceptance = 1u << kAcceptServoNeutral;        // auto dome not ready yet
f.rc(0, 1500, 2000, 1000); f.setHall(0x00, 0); f.tick(20);
f.acceptAutoDome();                                       // the Save made it ready...
f.dome.profileActivated(false);
for (uint32_t t = 40; t <= 400; t += 20) { f.deliverHall(t); f.tick(t); }
assert(f.dome.state() != r2link::DomeState::SeekingReference);   // ...but CH9 was already ON
f.rc(420, 1500, 1000, 1000); f.tick(420);                       // CH9 OFF
f.rc(440, 1500, 2000, 1000); f.deliverHall(440); f.tick(440);   // CH9 ON again
f.deliverHall(460); f.tick(460);
assert(f.dome.state() == r2link::DomeState::SeekingReference);  // startup alignment now runs
''')
```

- [ ] **Step 2: Run** — `cd tests && python3 -m unittest test_body_dome`. Expected: FAIL — `no member named 'profileActivated'`.

- [ ] **Step 3: Implement.** `DomeController.h`: public `void profileActivated(bool auto_was_ready);`; private `bool auto_rearm_ = false;` (add to constructor initializer list as `auto_rearm_(false)`).

`DomeController.cpp`:

```cpp
void DomeController::profileActivated(bool auto_was_ready) {
    if (!auto_was_ready && profile_ && readiness(*profile_).auto_dome) auto_rearm_ = true;
}
```

In `tick()`, replace the first line computing `auto_dome_on`:

```cpp
    const bool ch9_on = isRcFresh(now_ms) && rc_.channels[kAutoDome] >= 1750;
    if (isRcFresh(now_ms) && rc_.channels[kAutoDome] < 1250) auto_rearm_ = false;
    const bool auto_dome_on = ch9_on && !auto_rearm_;
```

In `request()`, after the CH9 check add: `if (auto_rearm_) return r2link::Result::Inhibited;`.

`BodyController::activateSavedProfile()`:

```cpp
void BodyController::activateSavedProfile() {
    const bool auto_was_ready = readiness(active_).auto_dome;
    active_ = profile_;
    left_vesc_.setProfile(VescProfile::fromSaved(active_, 0));
    right_vesc_.setProfile(VescProfile::fromSaved(active_, 1));
    dome_.profileActivated(auto_was_ready);
}
```

(`init()` calls `activateSavedProfile()` with `active_` blank, so a boot with an auto-ready saved profile would set `auto_rearm_`; boot must keep today's behaviour — in `init()` call `dome_.profileActivated(true)` semantics by passing a flag: add a parameter `void activateSavedProfile(bool from_boot = false);` and call `dome_.profileActivated(from_boot || auto_was_ready);`; `init()` passes `true`.)

- [ ] **Step 4: Run** full suite. Expected: PASS.

- [ ] **Step 5: Commit** `git commit -am "feat(dome): auto dome waits for a CH9 flip after a Save enables it"`

---

### Task 8: ESP32 `ProfileMirror` and BodyClient interfaces

**Files:**
- Create: `ASTROPIXELS_PLUS_UNIFIED/ProfileMirror.h`, `ProfileMirror.cpp`
- Modify: `ASTROPIXELS_PLUS_UNIFIED/BodyClient.h`, `BodyClient.cpp`
- Test: create `tests/test_commissioning_dome.py`

**Interfaces:**
- Produces:

```cpp
// ProfileMirror.h
class IFieldReader {
public:
    virtual ~IFieldReader() = default;
    virtual bool requestRead(uint8_t field, uint8_t wheel, uint32_t now_ms) = 0;
};
class ICommissionSink {
public:
    virtual ~ICommissionSink() = default;
    virtual bool sendCommission(const r2link::CommissionRequest& req, uint32_t now_ms, uint16_t& seq) = 0;
};
class ProfileMirror {
public:
    static const uint8_t kSlots = 35;           // 7 global + 2 x 14 wheel fields
    static const uint32_t kReadTimeoutMs = 250, kReadSpacingMs = 50;
    explicit ProfileMirror(IFieldReader& reader);
    void tick(uint32_t now_ms, bool link_up, const r2link::Diagnostics& latest, uint32_t latest_rx_ms);
    bool value(uint8_t field, uint8_t wheel, int32_t& out) const;   // false: unknown or unset
    bool known(uint8_t field, uint8_t wheel) const;                 // a read reply arrived
    void invalidate(uint8_t field, uint8_t wheel);                  // re-read this one next
};
```

- `BodyClient` implements both: `class BodyClient : public IDomeRequestSink, public IFieldReader, public ICommissionSink`; `DiagnosticsSnapshot` gains `uint32_t rx_ms`.

- [ ] **Step 1: Failing test** — create `tests/test_commissioning_dome.py`:

```python
"""Guided commissioning: dome-side mirror, wizard, radio/audio checks, checklist."""
import unittest
from pathlib import Path
from cpp_test_support import run_cpp

ROOT = Path(__file__).resolve().parents[1]
ASTRO = ROOT / "ASTROPIXELS_PLUS_UNIFIED"
SHARED = ROOT / "shared/R2BodyLink"
PRELUDE = r'''
#include <cassert>
#include <vector>
#include "Messages.h"
#include "ProfileMirror.h"
struct Reader : IFieldReader {
    std::vector<std::pair<uint8_t, uint8_t>> asked; bool ok = true;
    bool requestRead(uint8_t f, uint8_t w, uint32_t) override { if (ok) asked.push_back({f, w}); return ok; }
};
static r2link::Diagnostics reply(uint8_t f, uint8_t w, int32_t v, uint8_t known = 1) {
    r2link::Diagnostics d{}; d.subtype = 1; d.field = f; d.wheel = w; d.value = v; d.known = known; return d;
}
'''


class DomeCommissioningTests(unittest.TestCase):
    def check(self, body, sources=()):
        result = run_cpp(PRELUDE + "int main() {\n" + body + "\nreturn 0;\n}\n",
                         extra_sources=[ASTRO / "ProfileMirror.cpp"] + list(sources), include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_mirror_reads_every_field_once_per_pass_and_caches_replies(self):
        self.check(r'''
    Reader r; ProfileMirror m(r);
    r2link::Diagnostics none{}; uint32_t now = 1000;
    m.tick(now, true, none, 0);
    assert(r.asked.size() == 1 && r.asked[0].first == 0 && r.asked[0].second == 0);   // servo_neutral first
    m.tick(now + 10, true, reply(0, 0, 1512), now + 10);                             // reply arrives
    int32_t v = 0; assert(m.value(0, 0, v) && v == 1512);
    for (uint32_t t = now + 60; t < now + 60 + 35 * 60; t += 60) {
        const auto& last = r.asked.back();
        m.tick(t, true, reply(last.first, last.second, 7, last.first == 5 ? 0 : 1), t);
    }
    assert(r.asked.size() >= 35);
    assert(!m.value(5, 1, v) && m.known(5, 1));                                       // direction unset
    assert(m.value(18, 1, v) && v == 7);
''')

    def test_mirror_times_out_pauses_without_link_and_rereads_invalidated(self):
        self.check(r'''
    Reader r; ProfileMirror m(r); r2link::Diagnostics none{};
    m.tick(1000, false, none, 0); assert(r.asked.empty());
    m.tick(1000, true, none, 0); assert(r.asked.size() == 1);
    m.tick(1100, true, none, 0); assert(r.asked.size() == 1);           // waiting for reply
    m.tick(1300, true, none, 0); assert(r.asked.size() == 2);           // 250ms timeout, moved on
    m.invalidate(12, 1);
    m.tick(1600, true, none, 0);
    assert(r.asked.back().first == 12 && r.asked.back().second == 1);
''')


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run** `cd tests && python3 -m unittest test_commissioning_dome`. Expected: FAIL — `'ProfileMirror.h' file not found`.

- [ ] **Step 3: Implement** `ProfileMirror.h` (interfaces above; `#include <cstdint>`, `"Messages.h"`), private:

```cpp
private:
    struct Slot { uint8_t field, wheel; bool known, set; int32_t value; };
    int find(uint8_t field, uint8_t wheel) const;
    IFieldReader& reader_;
    Slot slots_[kSlots];
    uint8_t next_{0};
    int outstanding_{-1};
    int priority_{-1};
    uint32_t sent_ms_{0};
```

`ProfileMirror.cpp`:

```cpp
#include "ProfileMirror.h"

ProfileMirror::ProfileMirror(IFieldReader& reader) : reader_(reader), slots_{} {
    static const uint8_t kGlobal[] = {0, 1, 2, 3, 4, 19, 20};
    uint8_t i = 0;
    for (uint8_t f : kGlobal) slots_[i++] = Slot{f, 0, false, false, 0};
    for (uint8_t w = 0; w < 2; ++w)
        for (uint8_t f = 5; f <= 18; ++f) slots_[i++] = Slot{f, w, false, false, 0};
}

int ProfileMirror::find(uint8_t field, uint8_t wheel) const {
    for (uint8_t i = 0; i < kSlots; ++i)
        if (slots_[i].field == field && slots_[i].wheel == wheel) return i;
    return -1;
}

void ProfileMirror::tick(uint32_t now, bool link_up, const r2link::Diagnostics& d, uint32_t rx_ms) {
    if (!link_up) { outstanding_ = -1; return; }
    if (outstanding_ >= 0) {
        Slot& s = slots_[outstanding_];
        if (d.subtype == 1 && d.field == s.field && d.wheel == s.wheel && rx_ms >= sent_ms_ && rx_ms != 0) {
            s.known = true; s.set = d.known != 0; s.value = d.value;
            outstanding_ = -1;
        } else if (now - sent_ms_ >= kReadTimeoutMs) {
            outstanding_ = -1;
        } else {
            return;
        }
    }
    if (now - sent_ms_ < kReadSpacingMs && sent_ms_ != 0) return;
    const int slot = priority_ >= 0 ? priority_ : next_;
    if (!reader_.requestRead(slots_[slot].field, slots_[slot].wheel, now)) return;
    outstanding_ = slot; sent_ms_ = now;
    if (priority_ >= 0) priority_ = -1;
    else next_ = static_cast<uint8_t>((next_ + 1) % kSlots);
}

bool ProfileMirror::value(uint8_t field, uint8_t wheel, int32_t& out) const {
    const int i = find(field, wheel);
    if (i < 0 || !slots_[i].known || !slots_[i].set) return false;
    out = slots_[i].value; return true;
}
bool ProfileMirror::known(uint8_t field, uint8_t wheel) const {
    const int i = find(field, wheel); return i >= 0 && slots_[i].known;
}
void ProfileMirror::invalidate(uint8_t field, uint8_t wheel) {
    const int i = find(field, wheel);
    if (i >= 0) { slots_[i].known = false; priority_ = i; }
}
```

In the first test the first `tick` at 1000 sends slot 0 with `sent_ms_ = 1000`; the reply tick at 1010 caches it; the spacing rule then waits until 1050. Adjust the test's loop start (`now + 60`) if needed so each tick both receives and sends.

- [ ] **Step 4: `BodyClient`.** `BodyClient.h`: `#include "ProfileMirror.h"`; class line `class BodyClient : public IDomeRequestSink, public IFieldReader, public ICommissionSink {`; add `uint32_t rx_ms{0};` to `DiagnosticsSnapshot`; declare `bool requestRead(uint8_t field, uint8_t wheel, uint32_t now_ms) override;` and `bool sendCommission(const r2link::CommissionRequest& req, uint32_t now_ms, uint16_t& seq) override;`.

`BodyClient.cpp`:

```cpp
bool BodyClient::requestRead(uint8_t field, uint8_t wheel, uint32_t now_ms) {
    r2link::CommissionRequest req{};
    req.operation = 0; req.field = 1; req.wheel = wheel; req.value = field;
    req.control_epoch = bodyStatus(now_ms).value.control_epoch;
    return requestCommission(req, now_ms).queued;
}
bool BodyClient::sendCommission(const r2link::CommissionRequest& req, uint32_t now_ms, uint16_t& seq) {
    const RequestHandle h = requestCommission(req, now_ms);
    seq = h.sequence;
    return h.queued;
}
// diagnostics(): set res.rx_ms = diagnostics_rx_ms_;
```

Add `ASTRO / "ProfileMirror.cpp"` to every test SOURCES list that compiles `BodyClient.cpp` (`test_body_integration.py`, `test_plus_behavior.py`'s MarcSound test).

- [ ] **Step 5: Run** the full suite. Expected: PASS.

- [ ] **Step 6: Commit** `git add ASTROPIXELS_PLUS_UNIFIED tests && git commit -m "feat(dome): profile mirror and commissioning sink interfaces"`

---

### Task 9: `CommissionWizard`

**Files:**
- Create: `ASTROPIXELS_PLUS_UNIFIED/CommissionWizard.h`, `CommissionWizard.cpp`
- Test: `tests/test_commissioning_dome.py`

**Interfaces:**
- Consumes: `ICommissionSink` (Task 8), `r2link::CommissionStatus`, `r2link::Completion`.
- Produces:

```cpp
class CommissionWizard {
public:
    enum class State : uint8_t { Idle, Running, Done, Failed };
    explicit CommissionWizard(ICommissionSink& sink);
    bool startDomeCalibration(uint32_t now_ms);                         // FrontRef, RearRef, TimingCw, TimingCcw
    bool startWheelTest(uint8_t test, uint8_t wheel, bool raised, uint32_t now_ms);   // test 6/7/8
    bool startNeutral(uint32_t now_ms);                                 // Neutral hold
    bool nudgeNeutral(int16_t delta_us, int32_t current_us, uint32_t now_ms); // SetField 0 then Neutral
    bool acceptAndSave(const uint8_t* bits, uint8_t count, uint32_t now_ms);
    bool applyBaseline(uint32_t now_ms);
    void cancel(uint32_t now_ms);
    void tick(uint32_t now_ms, const r2link::CommissionStatus& status, bool status_fresh, uint16_t epoch);
    void onCompletion(const r2link::Completion& c);
    State state() const; uint8_t currentTest() const;
    uint16_t lastError() const;   // CommissionStatus.error of a failed test
    uint8_t lastResult() const;   // r2link::Result of a refused request
};
```

- [ ] **Step 1: Failing tests** — append to `test_commissioning_dome.py` (extend `PRELUDE` with `#include "CommissionWizard.h"` and a sink):

```cpp
#include "CommissionWizard.h"
struct Sink : ICommissionSink {
    std::vector<r2link::CommissionRequest> sent; uint16_t seq = 0;
    bool sendCommission(const r2link::CommissionRequest& r, uint32_t, uint16_t& s) override {
        sent.push_back(r); s = ++seq; return true;
    }
};
static r2link::CommissionStatus status(uint32_t run, uint8_t test, uint8_t state, uint16_t error = 0) {
    r2link::CommissionStatus s{}; s.run_id = run; s.test = test; s.state = state; s.error = error; return s;
}
static r2link::Completion done(uint16_t seq, r2link::Result r) {
    r2link::Completion c{}; c.type = r2link::MessageType::CommissionRequest; c.sequence = seq;
    c.outcome = r2link::Outcome::Replied; c.result = uint8_t(r); return c;
}
```

```python
    def test_wizard_runs_the_dome_sequence_and_stops_on_failure(self):
        self.check(r'''
    Sink s; CommissionWizard w(s);
    w.tick(0, status(0, 0, 0), true, 9);
    assert(w.startDomeCalibration(10));
    const uint8_t order[] = {2, 3, 4, 5};
    for (uint8_t i = 0; i < 4; ++i) {
        const r2link::CommissionRequest& b = s.sent.back();
        assert(b.operation == 1 && b.test == order[i] && b.control_epoch == 9 && b.run_id != 0);
        w.onCompletion(done(s.seq, r2link::Result::Accepted));
        w.tick(20 + i, status(b.run_id, b.test, 1), true, 9);           // running
        assert(w.state() == CommissionWizard::State::Running && s.sent.size() == size_t(i + 1));
        w.tick(30 + i, status(b.run_id, b.test, 2), true, 9);           // completed -> next
    }
    assert(w.state() == CommissionWizard::State::Done);
    Sink f; CommissionWizard x(f); x.tick(0, status(0, 0, 0), true, 9);
    x.startDomeCalibration(10);
    x.tick(20, status(f.sent.back().run_id, 2, 4, 2), true, 9);           // Failed, hall stale
    assert(x.state() == CommissionWizard::State::Failed && x.lastError() == 2 && f.sent.size() == 1);
    Sink g; CommissionWizard y(g); y.tick(0, status(0, 0, 0), true, 9);
    y.startDomeCalibration(10);
    y.onCompletion(done(g.seq, r2link::Result::Inhibited));               // Begin refused
    assert(y.state() == CommissionWizard::State::Failed && y.lastResult() == uint8_t(r2link::Result::Inhibited));
''')

    def test_wizard_accept_and_save_batch_and_single_steps(self):
        self.check(r'''
    Sink s; CommissionWizard w(s); w.tick(0, status(0, 0, 0), true, 4);
    const uint8_t bits[] = {1, 2, 3};
    assert(w.acceptAndSave(bits, 3, 10));
    assert(s.sent.size() == 4 && s.sent[0].operation == 6 && s.sent[0].value == 1 && s.sent[2].value == 3);
    assert(s.sent[3].operation == 5 && s.sent[3].control_epoch == 4);
    for (uint16_t q = 1; q <= 3; ++q) w.onCompletion(done(q, r2link::Result::Accepted));
    assert(w.state() == CommissionWizard::State::Running);
    w.onCompletion(done(4, r2link::Result::Accepted));
    assert(w.state() == CommissionWizard::State::Done);
    Sink r; CommissionWizard v(r); v.tick(0, status(0, 0, 0), true, 4);
    v.acceptAndSave(bits, 3, 10);
    v.onCompletion(done(2, r2link::Result::Inhibited));
    assert(v.state() == CommissionWizard::State::Failed && v.lastResult() == uint8_t(r2link::Result::Inhibited));
    Sink n; CommissionWizard u(n); u.tick(0, status(0, 0, 0), true, 4);
    assert(u.nudgeNeutral(5, 1500, 10));
    assert(n.sent[0].operation == 4 && n.sent[0].field == 0 && n.sent[0].value == 1505);
    assert(n.sent[1].operation == 1 && n.sent[1].test == 1);
    assert(!u.nudgeNeutral(-200, 1500, 20));                               // 1300 out of 1400-1600
    Sink t; CommissionWizard z(t); z.tick(0, status(0, 0, 0), true, 4);
    assert(!z.startWheelTest(6, 0, false, 10) && t.sent.empty());          // must confirm raised
    assert(z.startWheelTest(6, 1, true, 10));
    assert(t.sent[0].test == 6 && t.sent[0].wheel == 1 && t.sent[0].value == 1);
    assert(z.applyBaseline(20) && t.sent.back().operation == 7);
''')
```

Change the `check()` call to pass `sources=[ASTRO / "CommissionWizard.cpp"]` for these tests (or add it to the default list).

- [ ] **Step 2: Run** — Expected: FAIL, `'CommissionWizard.h' file not found`.

- [ ] **Step 3: Implement** `CommissionWizard.h` (interface above, `#include "ProfileMirror.h"` for `ICommissionSink`, `#include "Endpoint.h"` for `Completion`), private:

```cpp
private:
    bool begin(uint8_t test, uint8_t wheel, int32_t value, uint32_t now_ms);
    bool send(r2link::CommissionRequest req, uint32_t now_ms);
    void fail(uint16_t error, uint8_t result);
    ICommissionSink& sink_;
    State state_{State::Idle};
    uint8_t queue_[4]{}; uint8_t queue_len_{0}, queue_pos_{0};
    uint8_t test_{0};
    uint32_t run_id_{0}, run_counter_{0};
    uint16_t epoch_{0};
    uint16_t pending_[13]{}; uint8_t pending_count_{0};   // sequences awaiting replies
    uint16_t last_error_{0}; uint8_t last_result_{0};
    bool waiting_status_{false};
```

`CommissionWizard.cpp`:

```cpp
#include "CommissionWizard.h"

CommissionWizard::CommissionWizard(ICommissionSink& sink) : sink_(sink) {}

bool CommissionWizard::send(r2link::CommissionRequest req, uint32_t now) {
    req.control_epoch = epoch_;
    uint16_t seq = 0;
    if (pending_count_ >= sizeof pending_ / sizeof pending_[0] || !sink_.sendCommission(req, now, seq)) return false;
    pending_[pending_count_++] = seq;
    return true;
}

bool CommissionWizard::begin(uint8_t test, uint8_t wheel, int32_t value, uint32_t now) {
    r2link::CommissionRequest req{};
    req.operation = 1; req.test = test; req.wheel = wheel; req.value = value;
    if (++run_counter_ == 0) run_counter_ = 1;
    req.run_id = (now << 8) ^ run_counter_;
    if (req.run_id == 0) req.run_id = 1;
    if (!send(req, now)) return false;
    test_ = test; run_id_ = req.run_id; waiting_status_ = true; state_ = State::Running;
    return true;
}

bool CommissionWizard::startDomeCalibration(uint32_t now) {
    const uint8_t order[] = {2, 3, 4, 5};
    for (uint8_t i = 0; i < 4; ++i) queue_[i] = order[i];
    queue_len_ = 4; queue_pos_ = 0; last_error_ = 0; last_result_ = 0;
    return begin(queue_[queue_pos_++], 0, 0, now);
}
bool CommissionWizard::startNeutral(uint32_t now) { queue_len_ = queue_pos_ = 0; return begin(1, 0, 0, now); }
bool CommissionWizard::startWheelTest(uint8_t test, uint8_t wheel, bool raised, uint32_t now) {
    if (!raised || test < 6 || test > 8 || wheel > 1) return false;
    queue_len_ = queue_pos_ = 0;
    return begin(test, wheel, 1, now);
}
bool CommissionWizard::nudgeNeutral(int16_t delta, int32_t current, uint32_t now) {
    const int32_t next = current + delta;
    if (next < 1400 || next > 1600) return false;
    r2link::CommissionRequest set{}; set.operation = 4; set.field = 0; set.value = next;
    if (!send(set, now)) return false;
    return startNeutral(now);
}
bool CommissionWizard::acceptAndSave(const uint8_t* bits, uint8_t count, uint32_t now) {
    queue_len_ = queue_pos_ = 0; last_result_ = 0; test_ = 0; waiting_status_ = false;
    for (uint8_t i = 0; i < count; ++i) {
        r2link::CommissionRequest acc{}; acc.operation = 6; acc.value = bits[i];
        if (!send(acc, now)) return false;
    }
    r2link::CommissionRequest save{}; save.operation = 5;
    if (!send(save, now)) return false;
    state_ = State::Running;
    return true;
}
bool CommissionWizard::applyBaseline(uint32_t now) {
    r2link::CommissionRequest b{}; b.operation = 7;
    if (!send(b, now)) return false;
    state_ = State::Running; waiting_status_ = false; test_ = 0;
    return true;
}
void CommissionWizard::cancel(uint32_t now) {
    r2link::CommissionRequest c{}; c.operation = 3; c.run_id = run_id_;
    send(c, now);
    queue_len_ = queue_pos_ = 0; waiting_status_ = false; state_ = State::Idle;
}
void CommissionWizard::fail(uint16_t error, uint8_t result) {
    last_error_ = error; last_result_ = result; state_ = State::Failed;
    queue_len_ = queue_pos_ = 0; waiting_status_ = false; pending_count_ = 0;
}
void CommissionWizard::onCompletion(const r2link::Completion& c) {
    if (c.type != r2link::MessageType::CommissionRequest) return;
    for (uint8_t i = 0; i < pending_count_; ++i) {
        if (pending_[i] != c.sequence) continue;
        for (uint8_t j = i + 1; j < pending_count_; ++j) pending_[j - 1] = pending_[j];
        --pending_count_;
        const bool ok = c.outcome == r2link::Outcome::Replied && c.result == uint8_t(r2link::Result::Accepted);
        if (!ok) { fail(0, c.outcome == r2link::Outcome::Replied ? c.result : uint8_t(r2link::Result::NotReady)); return; }
        if (state_ == State::Running && !waiting_status_ && pending_count_ == 0) state_ = State::Done;
        return;
    }
}
void CommissionWizard::tick(uint32_t now, const r2link::CommissionStatus& s, bool fresh, uint16_t epoch) {
    epoch_ = epoch;
    if (state_ != State::Running || !waiting_status_ || !fresh || s.run_id != run_id_) return;
    if (s.state == 1) return;                                           // Running
    if (s.state == 2) {                                                 // Completed
        waiting_status_ = false;
        if (queue_pos_ < queue_len_) { begin(queue_[queue_pos_++], 0, 0, now); return; }
        if (pending_count_ == 0) state_ = State::Done;
        return;
    }
    fail(s.error, 0);                                                   // Cancelled / Failed / TimedOut
}
CommissionWizard::State CommissionWizard::state() const { return state_; }
uint8_t CommissionWizard::currentTest() const { return test_; }
uint16_t CommissionWizard::lastError() const { return last_error_; }
uint8_t CommissionWizard::lastResult() const { return last_result_; }
```

- [ ] **Step 4: Run** — Expected: PASS.

- [ ] **Step 5: Commit** `git add ASTROPIXELS_PLUS_UNIFIED/CommissionWizard.* tests/test_commissioning_dome.py && git commit -m "feat(dome): commissioning wizard sequencing and accept-and-save"`

---

### Task 10: `RadioCheck` and `AudioCheck`

**Files:**
- Create: `ASTROPIXELS_PLUS_UNIFIED/RadioCheck.{h,cpp}`, `AudioCheck.{h,cpp}`
- Test: `tests/test_commissioning_dome.py`

**Interfaces:**
- Consumes: `BodyRcState` (`BodyClient.h` — move the struct into a new tiny header `BodyRcState.h` included by `BodyClient.h`, so these classes don't pull in the Endpoint).
- Produces:

```cpp
class RadioCheck {
public:
    enum class State : uint8_t { Idle, Prompting, Passed, Failed };
    enum class Failsafe : uint8_t { Unknown, FrameValues, NoFrames, BadValues };
    static const uint32_t kStepTimeoutMs = 15000, kHoldMs = 1000;
    static const uint8_t kSteps = 9;   // 8 channel prompts + failsafe
    void start(uint32_t now_ms);
    void tick(const BodyRcState& rc, uint32_t now_ms);
    State state() const; uint8_t step() const; const char* prompt() const; Failsafe failsafe() const;
};
class AudioCheck {
public:
    enum class State : uint8_t { Idle, Waiting, Passed, Failed };
    static const uint32_t kTimeoutMs = 3000;
    void begin(uint16_t sequence, bool queued, uint32_t now_ms);   // after RemoteAudio::play(255, ...)
    void onEvent(const r2link::Event& ev);
    void onCompletion(const r2link::Completion& c);
    void tick(uint32_t now_ms);
    State state() const;
};
```

- [ ] **Step 1: Failing tests** (append; PRELUDE adds `#include "RadioCheck.h"`, `#include "AudioCheck.h"`, and a helper):

```cpp
static BodyRcState rc(std::initializer_list<std::pair<int, uint16_t>> set, bool valid = true) {
    BodyRcState s{}; s.valid = valid; for (auto& c : s.channels) c = 1500;
    s.channels[5] = s.channels[7] = s.channels[8] = 1000; s.channels[4] = 1000; s.channels[6] = 1000;
    for (auto& kv : set) s.channels[kv.first] = kv.second; return s;
}
```

```python
    def test_radio_check_walks_every_prompt_and_detects_failsafe_frames(self):
        self.check(r'''
    RadioCheck r; r.start(0); uint32_t t = 0;
    auto feed = [&](BodyRcState s) { t += 20; r.tick(s, t); };
    feed(rc({{1, 1900}}));                      // right stick up
    feed(rc({{0, 1900}}));                      // right stick right
    feed(rc({{3, 1900}}));                      // left stick right
    feed(rc({{5, 1900}}));                      // SwA down
    feed(rc({{4, 1000}})); feed(rc({{4, 1500}})); feed(rc({{4, 2000}}));   // SwC three positions
    feed(rc({{7, 1900}}));                      // SwB down
    feed(rc({{8, 1900}}));                      // SwD down
    feed(rc({{6, 1000}})); feed(rc({{6, 2000}}));                          // knob sweep
    assert(r.step() == 8 && r.state() == RadioCheck::State::Prompting);    // failsafe prompt
    // Switches already in failsafe positions must not pass on their own.
    for (int i = 0; i < 100; ++i) feed(rc({}));
    assert(r.state() == RadioCheck::State::Prompting);
    feed(rc({{5, 1900}, {7, 1900}, {8, 1900}}));                           // armed first
    for (int i = 0; i < 60; ++i) feed(rc({}));                             // TX off: failsafe frames
    assert(r.state() == RadioCheck::State::Passed && r.failsafe() == RadioCheck::Failsafe::FrameValues);
''', sources=[ASTRO / "RadioCheck.cpp"])

    def test_radio_check_reports_missing_frames_bad_values_and_timeouts(self):
        self.check(r'''
    { RadioCheck r; r.start(0); r.tick(rc({}), 20000);
      assert(r.state() == RadioCheck::State::Failed && r.step() == 0); }
    auto toFailsafe = [](RadioCheck& r, uint32_t& t) {
        const std::initializer_list<std::pair<int, uint16_t>> steps[] = {
            {{1, 1900}}, {{0, 1900}}, {{3, 1900}}, {{5, 1900}}, {{4, 1000}}, {{4, 1500}}, {{4, 2000}},
            {{7, 1900}}, {{8, 1900}}, {{6, 1000}}, {{6, 2000}}};
        for (auto& s : steps) { t += 20; r.tick(rc(s), t); }
        t += 20; r.tick(rc({{5, 1900}, {7, 1900}, {8, 1900}}), t);
    };
    { RadioCheck r; r.start(0); uint32_t t = 0; toFailsafe(r, t);
      for (int i = 0; i < 20; ++i) { t += 20; r.tick(rc({}, false), t); }
      assert(r.state() == RadioCheck::State::Passed && r.failsafe() == RadioCheck::Failsafe::NoFrames); }
    { RadioCheck r; r.start(0); uint32_t t = 0; toFailsafe(r, t);
      for (int i = 0; i < 60; ++i) { t += 20; r.tick(rc({{3, 1800}}), t); }   // CH4 held off-centre
      assert(r.state() == RadioCheck::State::Failed && r.failsafe() == RadioCheck::Failsafe::BadValues); }
''', sources=[ASTRO / "RadioCheck.cpp"])

    def test_audio_check(self):
        self.check(r'''
    { AudioCheck a; a.begin(7, true, 100);
      r2link::Event ev{}; ev.kind = uint8_t(r2link::EventKind::PlaybackStarted);
      ev.request_type = uint8_t(r2link::MessageType::AudioRequest); ev.request_seq = 7;
      a.onEvent(ev); assert(a.state() == AudioCheck::State::Passed); }
    { AudioCheck a; a.begin(7, true, 100); a.tick(3200); assert(a.state() == AudioCheck::State::Failed); }
    { AudioCheck a; a.begin(0, false, 100); assert(a.state() == AudioCheck::State::Failed); }
    { AudioCheck a; a.begin(7, true, 100);
      r2link::Completion c{}; c.type = r2link::MessageType::AudioRequest; c.sequence = 7;
      c.outcome = r2link::Outcome::Replied; c.result = uint8_t(r2link::Result::NotReady);
      a.onCompletion(c); assert(a.state() == AudioCheck::State::Failed); }
''', sources=[ASTRO / "AudioCheck.cpp"])
```

- [ ] **Step 2: Run** — Expected: FAIL, headers missing.

- [ ] **Step 3: Implement.** `BodyRcState.h`: move `struct BodyRcState {...}` out of `BodyClient.h` verbatim; `BodyClient.h` includes it.

`RadioCheck.h`: interface above plus private `State state_{State::Idle}; uint8_t step_{0}; uint8_t seen_{0}; uint32_t step_ms_{0}, hold_ms_{0}; bool armed_{false}; Failsafe failsafe_{Failsafe::Unknown};`.

`RadioCheck.cpp`:

```cpp
#include "RadioCheck.h"

namespace {
const char* const kPrompts[RadioCheck::kSteps] = {
    "Push the right stick UP", "Push the right stick RIGHT", "Push the left stick RIGHT",
    "Flip SwA DOWN", "Move SwC through all three positions", "Flip SwB DOWN", "Flip SwD DOWN",
    "Turn the VrA knob fully both ways",
    "Set SwA, SwB and SwD DOWN, then turn the transmitter OFF"};
bool centred(uint16_t v) { return v >= 1460 && v <= 1540; }
}

void RadioCheck::start(uint32_t now) {
    state_ = State::Prompting; step_ = 0; seen_ = 0; step_ms_ = now; hold_ms_ = 0; armed_ = false;
    failsafe_ = Failsafe::Unknown;
}

void RadioCheck::tick(const BodyRcState& rc, uint32_t now) {
    if (state_ != State::Prompting) return;
    if (now - step_ms_ > kStepTimeoutMs) { state_ = State::Failed; return; }
    const uint16_t* ch = rc.channels;
    bool done = false;
    if (step_ < 8 && rc.valid) {
        switch (step_) {
        case 0: done = ch[1] > 1750; break;
        case 1: done = ch[0] > 1750; break;
        case 2: done = ch[3] > 1750; break;
        case 3: done = ch[5] > 1750; break;
        case 4:
            if (ch[4] < 1250) seen_ |= 1;
            if (ch[4] >= 1400 && ch[4] <= 1600) seen_ |= 2;
            if (ch[4] > 1750) seen_ |= 4;
            done = seen_ == 7; break;
        case 5: done = ch[7] > 1750; break;
        case 6: done = ch[8] > 1750; break;
        case 7:
            if (ch[6] < 1100) seen_ |= 8;
            done = (seen_ & 8) && ch[6] > 1900; break;
        }
    } else if (step_ == 8) {
        if (!armed_) {
            armed_ = rc.valid && ch[5] > 1750 && ch[7] > 1750 && ch[8] > 1750;
            return;
        }
        if (!rc.valid) {                     // frames stopped: Teensy disarms on staleness
            failsafe_ = Failsafe::NoFrames; state_ = State::Passed; return;
        }
        if (ch[5] > 1750 || ch[7] > 1750 || ch[8] > 1750) { hold_ms_ = 0; return; }   // still armed: TX on
        if (!hold_ms_) hold_ms_ = now;
        if (now - hold_ms_ < kHoldMs) return;
        const bool good = centred(ch[0]) && centred(ch[1]) && centred(ch[3]) &&
                          ch[5] <= 1250 && ch[7] <= 1250 && ch[8] <= 1250;
        failsafe_ = good ? Failsafe::FrameValues : Failsafe::BadValues;
        state_ = good ? State::Passed : State::Failed;
        return;
    }
    if (done) { ++step_; seen_ = 0; step_ms_ = now; }
}

RadioCheck::State RadioCheck::state() const { return state_; }
uint8_t RadioCheck::step() const { return step_; }
const char* RadioCheck::prompt() const { return step_ < kSteps ? kPrompts[step_] : ""; }
RadioCheck::Failsafe RadioCheck::failsafe() const { return failsafe_; }
```

`AudioCheck.cpp`:

```cpp
#include "AudioCheck.h"

void AudioCheck::begin(uint16_t seq, bool queued, uint32_t now) {
    seq_ = seq; start_ms_ = now; state_ = queued ? State::Waiting : State::Failed;
}
void AudioCheck::onEvent(const r2link::Event& ev) {
    if (state_ != State::Waiting || ev.request_type != uint8_t(r2link::MessageType::AudioRequest) ||
        ev.request_seq != seq_) return;
    if (ev.kind == uint8_t(r2link::EventKind::PlaybackStarted)) state_ = State::Passed;
    else if (ev.kind != uint8_t(r2link::EventKind::Completed)) state_ = State::Failed;
}
void AudioCheck::onCompletion(const r2link::Completion& c) {
    if (state_ != State::Waiting || c.type != r2link::MessageType::AudioRequest || c.sequence != seq_) return;
    if (c.outcome != r2link::Outcome::Replied || c.result != uint8_t(r2link::Result::Accepted)) state_ = State::Failed;
}
void AudioCheck::tick(uint32_t now) {
    if (state_ == State::Waiting && now - start_ms_ >= kTimeoutMs) state_ = State::Failed;
}
AudioCheck::State AudioCheck::state() const { return state_; }
```

(`AudioCheck.h` private: `State state_{State::Idle}; uint16_t seq_{0}; uint32_t start_ms_{0};`, includes `"Messages.h"` and `"Endpoint.h"`.)

- [ ] **Step 4: Run** — Expected: PASS.

- [ ] **Step 5: Commit** `git add ASTROPIXELS_PLUS_UNIFIED tests && git commit -m "feat(dome): radio/failsafe and audio checks"`

---

### Task 11: Checklist formatter, sketch wiring and web pages

**Files:**
- Create: `ASTROPIXELS_PLUS_UNIFIED/CommissionChecklist.{h,cpp}`
- Modify: `ASTROPIXELS_PLUS_UNIFIED/ASTROPIXELS_PLUS_UNIFIED.ino`, `WebPages.h`
- Test: `tests/test_commissioning_dome.py`; build with `pio run`

**Interfaces:**
- Produces:

```cpp
struct ChecklistInput {
    uint16_t saved_acceptance;   // CommissionStatus.saved_acceptance
    bool unsaved, baseline_filled, radio_passed, failsafe_passed, audio_passed, status_fresh;
};
// Writes one line per item ("[x] VESC config L/R" / "[ ] ...") separated by '\n'; returns bytes written.
size_t formatChecklist(const ChecklistInput& in, char* out, size_t capacity);
// The next incomplete item's name, or "All done" when complete.
const char* nextChecklistStep(const ChecklistInput& in);
```

- [ ] **Step 1: Failing test:**

```python
    def test_checklist_lines_and_next_step(self):
        self.check(r'''
    ChecklistInput in{}; in.status_fresh = true;
    char buf[512];
    formatChecklist(in, buf, sizeof buf);
    assert(std::strstr(buf, "[ ] Baseline filled") && std::strstr(buf, "[ ] VESC config L"));
    assert(std::string(nextChecklistStep(in)) == "Baseline filled");
    in.baseline_filled = true; in.saved_acceptance = 0x0FFF; in.radio_passed = in.failsafe_passed = in.audio_passed = true;
    formatChecklist(in, buf, sizeof buf);
    assert(!std::strstr(buf, "[ ]") && std::string(nextChecklistStep(in)) == "All done");
    in.unsaved = true; assert(std::string(nextChecklistStep(in)) == "Save profile");
    in.unsaved = false; in.saved_acceptance = 0x0FFF & ~(1u << 9);
    assert(std::string(nextChecklistStep(in)) == "Direction R");
    assert(formatChecklist(in, buf, 8) <= 7 && std::strlen(buf) <= 7);   // truncates safely
''', sources=[ASTRO / "CommissionChecklist.cpp"])
```

(PRELUDE adds `#include <cstring>`, `#include <string>`, `#include "CommissionChecklist.h"`.)

- [ ] **Step 2: Run** — Expected: FAIL, header missing.

- [ ] **Step 3: Implement** `CommissionChecklist.cpp`:

```cpp
#include "CommissionChecklist.h"
#include <cstdio>

namespace {
struct Item { const char* name; int bit; };   // bit -1: non-bit item handled by index
const Item kItems[] = {
    {"Baseline filled", -1}, {"VESC config L", 4}, {"VESC config R", 5},
    {"Timeout brake L", 6}, {"Timeout brake R", 7}, {"Direction L", 8}, {"Direction R", 9},
    {"Reversal L", 10}, {"Reversal R", 11}, {"Dome neutral", 0}, {"Front reference", 1},
    {"Rear reference", 2}, {"Dome timing", 3}, {"Radio check", -2}, {"Failsafe check", -3},
    {"Audio check", -4}, {"Save profile", -5}};
bool done(const ChecklistInput& in, const Item& it) {
    switch (it.bit) {
    case -1: return in.baseline_filled;
    case -2: return in.radio_passed;
    case -3: return in.failsafe_passed;
    case -4: return in.audio_passed;
    case -5: return in.status_fresh && !in.unsaved;
    default: return (in.saved_acceptance >> it.bit) & 1u;
    }
}
}

size_t formatChecklist(const ChecklistInput& in, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0; out[0] = '\0';
    for (const Item& it : kItems) {
        const int w = snprintf(out + n, cap - n, "[%c] %s\n", done(in, it) ? 'x' : ' ', it.name);
        if (w < 0 || size_t(w) >= cap - n) { out[cap - 1] = '\0'; return cap - 1; }
        n += size_t(w);
    }
    return n;
}

const char* nextChecklistStep(const ChecklistInput& in) {
    for (const Item& it : kItems) if (!done(in, it)) return it.name;
    return "All done";
}
```

The `formatChecklist(in, buf, 8)` assertion: on truncation the function returns `cap - 1` (7) with `buf` NUL-terminated by `snprintf`.

- [ ] **Step 4: Run** — Expected: PASS.

- [ ] **Step 5: Sketch wiring** in `ASTROPIXELS_PLUS_UNIFIED.ino` (after `static DomeBehaviour g_dome_behaviour(...)`):

```cpp
#include "ProfileMirror.h"
#include "CommissionWizard.h"
#include "RadioCheck.h"
#include "AudioCheck.h"
#include "CommissionChecklist.h"
static ProfileMirror g_profile_mirror(g_body_client);
static CommissionWizard g_wizard(g_body_client);
static RadioCheck g_radio_check;
static AudioCheck g_audio_check;
static bool g_wheels_raised = false;
```

In `dispatchBodyCompletions()` inside the loop add `g_wizard.onCompletion(comp); g_audio_check.onCompletion(comp);`. In `dispatchBodyEvents()` add `g_audio_check.onEvent(ev);`. Add `void processCommissioningTools(uint32_t now)`:

```cpp
void processCommissioningTools(uint32_t now) {
    const auto diag = g_body_client.diagnostics(now);
    g_profile_mirror.tick(now, g_body_client.linkUp(now), diag.value, diag.rx_ms);
    const auto cs = g_body_client.commissionStatus(now);
    g_wizard.tick(now, cs.value, cs.fresh, g_body_client.bodyStatus(now).value.control_epoch);
    g_radio_check.tick(g_body_client.rcSnapshot(now), now);
    g_audio_check.tick(now);
}
void startAudioCheck() {
    const RequestHandle h = g_remote_audio.play(255, r2link::AudioPriority::Foreground, millis());
    g_audio_check.begin(h.sequence, h.queued, millis());
}
ChecklistInput checklistInput() {
    const uint32_t now = millis();
    const auto cs = g_body_client.commissionStatus(now);
    ChecklistInput in{};
    in.status_fresh = cs.fresh;
    in.saved_acceptance = cs.value.saved_acceptance;
    in.unsaved = cs.value.unsaved != 0;
    int32_t v = 0;
    in.baseline_filled = g_profile_mirror.value(12, 0, v) && g_profile_mirror.value(12, 1, v) &&
                         g_profile_mirror.value(0, 0, v);
    in.radio_passed = g_radio_check.step() >= 8 || g_radio_check.state() == RadioCheck::State::Passed;
    in.failsafe_passed = g_radio_check.state() == RadioCheck::State::Passed;
    in.audio_passed = g_audio_check.state() == AudioCheck::State::Passed;
    return in;
}
```

Call `processCommissioningTools(now);` in `loop()` after `processCommissioningKeepalive(now);`. Replace `processCommissioningKeepalive()`'s condition so it also covers wheel tests (it already keys on `cs.value.state == 1`; no change needed).

- [ ] **Step 6: Web pages** in `WebPages.h`. Replace `commissioningContents[]` sections between the existing status readouts and the "Back" button with:

```cpp
    WTextField("Checklist:", "c_list", []()->String {
        char buf[512]; formatChecklist(checklistInput(), buf, sizeof buf); return String(buf); }, [](String) {}),
    WTextField("Next step:", "c_next", []()->String { return String(nextChecklistStep(checklistInput())); }, [](String) {}),
    WButton("Apply baseline", "c_base", []() { g_wizard.applyBaseline(millis()); }),
    WVerticalAlign(),
    W1("Dome"),
    WButton("Neutral hold", "c_neutral", []() { g_wizard.startNeutral(millis()); }),
    WHorizontalAlign(),
    WButton("Nudge -5us", "c_nm", []() { int32_t v = 1500; g_profile_mirror.value(0, 0, v);
        if (g_wizard.nudgeNeutral(-5, v, millis())) g_profile_mirror.invalidate(0, 0); }),
    WHorizontalAlign(),
    WButton("Nudge +5us", "c_np", []() { int32_t v = 1500; g_profile_mirror.value(0, 0, v);
        if (g_wizard.nudgeNeutral(5, v, millis())) g_profile_mirror.invalidate(0, 0); }),
    WHorizontalAlign(),
    WButton("Accept neutral & Save", "c_acc_neu", []() { const uint8_t b[] = {0}; g_wizard.acceptAndSave(b, 1, millis()); }),
    WVerticalAlign(),
    WButton("Calibrate dome (refs + timing)", "c_cal", []() { g_wizard.startDomeCalibration(millis()); }),
    WHorizontalAlign(),
    WButton("Accept dome & Save", "c_acc_dome", []() { const uint8_t b[] = {1, 2, 3}; g_wizard.acceptAndSave(b, 3, millis()); }),
    WHorizontalAlign(),
    WButton("Cancel", "c_cancel", []() { g_wizard.cancel(millis()); }),
    WVerticalAlign(),
    WTextField("Wizard:", "c_wiz", []()->String { return formatWizard(); }, [](String) {}),
    WVerticalAlign(),
    W1("Radio & audio"),
    WButton("Start radio check", "c_radio", []() { g_radio_check.start(millis()); }),
    WHorizontalAlign(),
    WButton("Audio check", "c_audio", []() { startAudioCheck(); }),
    WVerticalAlign(),
    WTextField("Radio:", "c_rprompt", []()->String { return formatRadioCheck(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Audio:", "c_aresult", []()->String { return formatAudioCheck(); }, [](String) {}),
    WVerticalAlign(),
    WButton("Drive commissioning", "c_drive", "/drive"),
    WVerticalAlign(),
```

Add formatters above the array:

```cpp
inline String formatWizard() {
    static const char* const states[] = {"Idle", "Running", "Done", "Failed"};
    char buf[96];
    snprintf(buf, sizeof buf, "%s test=%u error=%u result=%u", states[uint8_t(g_wizard.state())],
             g_wizard.currentTest(), g_wizard.lastError(), g_wizard.lastResult());
    return String(buf);
}
inline String formatRadioCheck() {
    static const char* const states[] = {"Not started", "", "PASSED", "FAILED"};
    static const char* const fs[] = {"", " (failsafe frames OK)", " (receiver stops output; drive disarms on staleness)",
                                     " (failsafe values wrong: set CH1/2/4 centre, CH6/8/9 low)"};
    if (g_radio_check.state() == RadioCheck::State::Prompting) return String(g_radio_check.prompt());
    return String(states[uint8_t(g_radio_check.state())]) + fs[uint8_t(g_radio_check.failsafe())];
}
inline String formatAudioCheck() {
    static const char* const states[] = {"Not started", "Waiting for playback...", "PASSED", "FAILED (no playback)"};
    return String(states[uint8_t(g_audio_check.state())]);
}
inline String formatFieldRow(uint8_t field, uint8_t wheel) {
    int32_t v = 0;
    if (!g_profile_mirror.known(field, wheel)) return String("...");
    return g_profile_mirror.value(field, wheel, v) ? String(v) : String("(unset)");
}
inline String formatVesc(uint8_t wheel) {
    const auto v = g_body_client.vescStatus(wheel, millis());
    if (!v.valid && v.fw_major == 0 && v.fw_minor == 0) return String("not detected");
    char buf[80];
    snprintf(buf, sizeof buf, "FW %u.%u, %u.%02u V, %ld erpm, fault %u", v.fw_major, v.fw_minor,
             v.pack_cV / 100, v.pack_cV % 100, (long)v.erpm, v.fault);
    return String(buf);
}
inline String formatWheelResult() {
    const auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh || cs.value.test < 6) return String("-");
    char buf[96];
    snprintf(buf, sizeof buf, "%s wheel %c: stop %u ms, peak %d.%02d A, peak %ld erpm, fault %u",
             cs.value.state == 2 ? "PASSED" : cs.value.state == 1 ? "running" : "FAILED",
             cs.value.wheel ? 'R' : 'L', cs.value.stop_ms, cs.value.peak_current_cA / 100,
             abs(cs.value.peak_current_cA % 100), (long)cs.value.peak_erpm, cs.value.vesc_fault);
    return String(buf);
}
```

(`BodyClient::vescStatus` returns `valid=false` and zeros when stale; the firmware fields come through because the body now publishes before commissioning.)

Add the `/drive` page:

```cpp
#define FIELD_ROW(label, id, field, wheel) \
    WTextFieldInteger(label, id, []()->String { return formatFieldRow(field, wheel); }, \
        [](String val) { setCommissionField(field, wheel, val.toInt()); g_profile_mirror.invalidate(field, wheel); })

WElement driveContents[] = {
    W1("Drive Commissioning (CH6 OFF, sticks centred)"),
    WTextField("Left VESC:", "d_vl", []()->String { return formatVesc(0); }, [](String) {}),
    WTextField("Right VESC:", "d_vr", []()->String { return formatVesc(1); }, [](String) {}),
    WLabel("Compare these with VESC Tool for each controller, then accept VESC config.", "d_hint"),
    FIELD_ROW("Slew permille/s:", "f4", 4, 0),
    FIELD_ROW("L motor mA:", "f9l", 9, 0), FIELD_ROW("R motor mA:", "f9r", 9, 1),
    FIELD_ROW("L battery mA:", "f10l", 10, 0), FIELD_ROW("R battery mA:", "f10r", 10, 1),
    FIELD_ROW("L regen mA:", "f11l", 11, 0), FIELD_ROW("R regen mA:", "f11r", 11, 1),
    FIELD_ROW("L brake mA:", "f12l", 12, 0), FIELD_ROW("R brake mA:", "f12r", 12, 1),
    FIELD_ROW("L undervolt cV:", "f13l", 13, 0), FIELD_ROW("R undervolt cV:", "f13r", 13, 1),
    FIELD_ROW("L overvolt cV:", "f14l", 14, 0), FIELD_ROW("R overvolt cV:", "f14r", 14, 1),
    FIELD_ROW("L timeout brake mA:", "f16l", 16, 0), FIELD_ROW("R timeout brake mA:", "f16r", 16, 1),
    FIELD_ROW("L reversal erpm:", "f17l", 17, 0), FIELD_ROW("R reversal erpm:", "f17r", 17, 1),
    FIELD_ROW("L reversal dwell ms:", "f18l", 18, 0), FIELD_ROW("R reversal dwell ms:", "f18r", 18, 1),
    FIELD_ROW("L direction:", "f5l", 5, 0), FIELD_ROW("R direction:", "f5r", 5, 1),
    WButton("Accept VESC config & Save", "d_acc_cfg", []() { const uint8_t b[] = {4, 5}; g_wizard.acceptAndSave(b, 2, millis()); }),
    WVerticalAlign(),
    WCheckbox("Wheels are raised", "d_raised", []() { return g_wheels_raised; }, [](bool v) { g_wheels_raised = v; }),
    WVerticalAlign(),
    WButton("Timeout test L", "d_tl", []() { g_wizard.startWheelTest(6, 0, g_wheels_raised, millis()); }),
    WHorizontalAlign(),
    WButton("Timeout test R", "d_tr", []() { g_wizard.startWheelTest(6, 1, g_wheels_raised, millis()); }),
    WVerticalAlign(),
    WButton("Direction test L", "d_dl", []() { g_wizard.startWheelTest(7, 0, g_wheels_raised, millis()); }),
    WHorizontalAlign(),
    WButton("Direction test R", "d_dr", []() { g_wizard.startWheelTest(7, 1, g_wheels_raised, millis()); }),
    WVerticalAlign(),
    WButton("L rolled forward", "d_lf", []() { setCommissionField(5, 0, 1); g_profile_mirror.invalidate(5, 0); }),
    WHorizontalAlign(),
    WButton("L rolled backward", "d_lb", []() { setCommissionField(5, 0, -1); g_profile_mirror.invalidate(5, 0); }),
    WHorizontalAlign(),
    WButton("R rolled forward", "d_rf", []() { setCommissionField(5, 1, 1); g_profile_mirror.invalidate(5, 1); }),
    WHorizontalAlign(),
    WButton("R rolled backward", "d_rb", []() { setCommissionField(5, 1, -1); g_profile_mirror.invalidate(5, 1); }),
    WVerticalAlign(),
    WButton("Reversal test L", "d_rvl", []() { g_wizard.startWheelTest(8, 0, g_wheels_raised, millis()); }),
    WHorizontalAlign(),
    WButton("Reversal test R", "d_rvr", []() { g_wizard.startWheelTest(8, 1, g_wheels_raised, millis()); }),
    WVerticalAlign(),
    WTextField("Last wheel test:", "d_res", []()->String { return formatWheelResult(); }, [](String) {}),
    WVerticalAlign(),
    WButton("Accept wheel tests & Save", "d_acc_wt", []() {
        const uint8_t b[] = {6, 7, 8, 9, 10, 11}; g_wizard.acceptAndSave(b, 6, millis()); }),
    WVerticalAlign(),
    WButton("Back", "back", "/commissioning"),
    WVerticalAlign(),
    rseriesSVG
};
```

Register `WPage("/drive", driveContents, SizeOfArray(driveContents)),` in `pages[]` next to `/commissioning`. Remove the now-replaced "Field ID/Wheel/Value" and "Accept bit" controls and the separate test/accept buttons from the old commissioning page (the wizard covers them).

Note: "Accept wheel tests & Save" sends six Accepts; any bit without evidence is refused and the wizard reports `Failed` with that result — the Save is still sent last but the body applies earlier accepted bits. Acceptable: the checklist shows which bits landed.

- [ ] **Step 7: Build** `cd ASTROPIXELS_PLUS_UNIFIED && PLATFORMIO_BUILD_DIR=/tmp/esp_guided pio run`. Expected: SUCCESS. Fix any ReelTwo element signature mismatch by checking `.pio/libdeps/astropixelsplus/Reeltwo/src/wifi/WifiWebServer.h` for the exact `WCheckbox`/`WTextFieldInteger` constructor signatures used elsewhere in `WebPages.h`.

- [ ] **Step 8: Run** the full suite. Expected: PASS.

- [ ] **Step 9: Commit** `git add ASTROPIXELS_PLUS_UNIFIED tests && git commit -m "feat(dome): guided commissioning pages, checklist and tool wiring"`

---

### Task 12: Two-board end-to-end commissioning test and docs

**Files:**
- Test: `tests/test_body_integration.py`
- Modify: `BODY_CONTROLLER_COMMISSIONING.md`, `VESC_DRIVE_INTEGRATION.md`, `CONTROLLER_OPERATOR_GUIDE.md` (radio check mention), `TEENSY_BODY_CONTROLLER/README.md` (`profile baseline`)

**Interfaces:**
- Consumes: everything above via `HostRig` (BodyController + BodyClient over a pipe) and `CommissionWizard`.

- [ ] **Step 1: Write the end-to-end test** — add to `BodyIntegrationTests`. The scripted VESC must spin: extend `ScriptedVescPort` with a simple wheel model:

```cpp
// In ScriptedVescPort: physical state driven by the commands written to it.
    double wheel_erpm{0}; uint32_t last_cmd_ms{0}; uint32_t clock_ms{0};
    void advance(uint32_t now) {            // call once per rig step
        const uint32_t dt = now - clock_ms; clock_ms = now;
        if (now - last_cmd_ms > 150) for (uint32_t i = 0; i < dt && std::abs(wheel_erpm) > 5; ++i) wheel_erpm *= 0.98;
        erpm = int32_t(wheel_erpm);
    }
// In write(), when a SET_DUTY (b[i+2] == 5) frame is seen:
//     const int32_t v = int32_t(uint32_t(b[i+3]) << 24 | uint32_t(b[i+4]) << 16 | uint32_t(b[i+5]) << 8 | b[i+6]);
//     wheel_erpm = v / 100 * 8.0; last_cmd_ms = clock_ms;
// and when SET_CURRENT_BRAKE (b[i+2] == 7): wheel_erpm *= 0.5; last_cmd_ms = clock_ms;
```

(Match `b[i+1] == 5` for 5-byte payload frames when parsing duty/brake; the existing scan matches 1-byte query frames with `b[i+1] == 1`.) `HostRig::step` calls `left_vesc_port.advance(now); right_vesc_port.advance(now);` before ticking. The rig's constructor must **not** pre-save `makeTestProfile()` for this test: add a `HostRig(bool commissioned)` overload that skips the save.

```python
    def test_blank_profile_to_fully_commissioned_from_the_dome_only(self):
        self.check(r'''
    HostRig rig(false);                                   // blank EEPROM
    rig.handshake();
    CommissionWizard wiz(rig.client);
    uint32_t hall_counter = 0; uint8_t hall_mask = 0;
    auto pump = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            rig.controller.updateRc(makeRcSnapshot(rig.now, 1000, 1000), rig.now);
            rig.client.publishHall(0x03, hall_mask, ++hall_counter, rig.now);
            rig.step(20);
            r2link::Completion c; while (rig.client.takeCompletion(c)) wiz.onCompletion(c);
            const auto cs = rig.client.commissionStatus(rig.now);
            wiz.tick(rig.now, cs.value, cs.fresh, rig.client.bodyStatus(rig.now).value.control_epoch);
            if (cs.fresh && cs.value.state == 1) {      // keepalive, as the sketch does
                r2link::CommissionRequest k{}; k.operation = 2; k.run_id = cs.value.run_id;
                k.control_epoch = rig.client.bodyStatus(rig.now).value.control_epoch;
                rig.client.requestCommission(k, rig.now);
            }
        }
    };
    auto settle = [&]() { for (int i = 0; i < 400 && wiz.state() == CommissionWizard::State::Running; ++i) pump(1); };
    pump(50);
    assert(wiz.applyBaseline(rig.now)); settle(); assert(wiz.state() == CommissionWizard::State::Done);
    const uint8_t cfg[] = {4, 5};
    assert(wiz.acceptAndSave(cfg, 2, rig.now)); settle(); assert(wiz.state() == CommissionWizard::State::Done);
    pump(50);
    for (uint8_t w = 0; w < 2; ++w) {
        for (uint8_t test : {6, 7, 8}) {
            assert(wiz.startWheelTest(test, w, true, rig.now)); settle();
            assert(wiz.state() == CommissionWizard::State::Done);
            pump(30);
        }
        r2link::CommissionRequest dir{}; dir.operation = 4; dir.field = 5; dir.wheel = w; dir.value = w ? -1 : 1;
        dir.control_epoch = rig.client.bodyStatus(rig.now).value.control_epoch;
        assert(rig.client.requestCommission(dir, rig.now).queued); pump(10);
    }
    const uint8_t wheels[] = {6, 7, 8, 9, 10, 11};
    assert(wiz.acceptAndSave(wheels, 6, rig.now)); settle(); assert(wiz.state() == CommissionWizard::State::Done);
    // Dome: neutral, then refs + timing with a scripted magnet passing the sensors.
    assert(wiz.startNeutral(rig.now)); settle(); assert(wiz.state() == CommissionWizard::State::Done);
    const uint8_t neu[] = {0}; assert(wiz.acceptAndSave(neu, 1, rig.now)); settle();
    assert(wiz.startDomeCalibration(rig.now));
    for (int i = 0; i < 6000 && wiz.state() == CommissionWizard::State::Running; ++i) {
        const uint32_t phase = (rig.now / 20) % 200;              // one revolution every 4 s
        hall_mask = phase < 5 ? 0x01 : (phase >= 100 && phase < 105) ? 0x02 : 0x00;
        pump(1);
    }
    hall_mask = 0;
    assert(wiz.state() == CommissionWizard::State::Done);
    const uint8_t dome[] = {1, 2, 3}; assert(wiz.acceptAndSave(dome, 3, rig.now)); settle();
    assert(wiz.state() == CommissionWizard::State::Done);
    pump(20);
    assert(rig.client.bodyStatus(rig.now).value.profile_ready == 7);
''')
```

Add `ASTRO / "CommissionWizard.cpp"`, `ASTRO / "ProfileMirror.cpp"` and `BODY / "body/WheelTest.cpp"` to `SOURCES`, and `#include "CommissionWizard.h"` to the PRELUDE.

- [ ] **Step 2: Run** — `cd tests && python3 -m unittest test_body_integration.BodyIntegrationTests.test_blank_profile_to_fully_commissioned_from_the_dome_only`. If it fails, debug with the first assertion that fails: it names the stage. Expected after fixes: PASS. Do not loosen any firmware gate to make it pass; adjust the scripted VESC/Hall models if they are unrealistic.

- [ ] **Step 3: Docs.** In `BODY_CONTROLLER_COMMISSIONING.md` section 5.1, replace steps 0–5 and the "two switch positions" note with the guided flow: Apply baseline → compare VESC Tool on `/drive` → Accept VESC config & Save → tick "Wheels are raised" → Timeout/Direction(+ answer)/Reversal tests per wheel → Accept wheel tests & Save → Neutral hold (+ nudges) → Accept neutral & Save → Calibrate dome → Accept dome & Save → radio check → audio check; and state that CH9 is not used during commissioning, that a Save which enables auto dome waits for a CH9 OFF→ON, and the wheel-test safety gates. In `VESC_DRIVE_INTEGRATION.md` "Signing off the VESC records", say bits 6–11 now require the automated tests. In `TEENSY_BODY_CONTROLLER/README.md` add `profile baseline`. Keep the strings pinned by `tests/test_body_documentation.py`.

- [ ] **Step 4: Full verification** — from the repo root: `python3 -m unittest discover -s tests -p "test_*.py"`; `cd TEENSY_BODY_CONTROLLER && PLATFORMIO_BUILD_DIR=/tmp/t_final pio run`; `cd ../ASTROPIXELS_PLUS_UNIFIED && PLATFORMIO_BUILD_DIR=/tmp/e_final pio run`. Expected: all PASS / SUCCESS.

- [ ] **Step 5: Commit** `git add -A && git commit -m "test+docs: end-to-end guided commissioning and updated guides"`
