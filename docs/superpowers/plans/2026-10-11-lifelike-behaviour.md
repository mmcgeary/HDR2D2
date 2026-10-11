# Lifelike Dome and Holo Behaviour Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the dome's random wander and the holos' single-servo twitch with a mood-driven attention behaviour: eased dome looks tracked by a ring of body magnets, a leading front holo, independent top/rear holos, and linked sounds, all switched by SwC levels.

**Architecture:** Six header-only, hardware-free modules in `AstroPixelsPlus/` (`R2Mood.h`, `R2Eyes.h`, `R2DomeTracker.h`, `R2DomeMotion.h`, `R2Attention.h`, plus the existing `R2Logic.h`), each unit-tested on the PC by compiling small C++ programs from Python. `R2Remote.h` stays the only file that touches ESP32 pins, the DFPlayer, servos and preferences.

**Tech Stack:** C++11 (ESP32 Arduino core 2.0.5 via PlatformIO, `pio run -d AstroPixelsPlus`), ReelTwo 23.5.3 (`servoDispatch`, `Easing`), Python 3 `unittest` driving the host C++ compiler (`c++ -std=c++11`).

**Spec:** `docs/superpowers/specs/2026-10-11-lifelike-behaviour-design.md`

**Build order note:** the spec's §12 integrated the dome in step 2 with interim random looks. This plan builds the holos end to end first (Task 1), then the dome and planner logic (Tasks 2–4), and integrates them together (Task 5). That avoids writing interim look code that Task 4 would throw away. Every commit still builds and passes the tests.

## Global Constraints

- Logic headers are C++11, header-only, in namespace `r2`. They include only `<stdint.h>`, `<math.h>` and other `R2*.h` files. No Arduino calls.
- Aggregate-initialised structs (`Range`, `MoodProfile`, `EyePos`, `EyeMove`, `DomeRequest`) have no default member initialisers (C++11 aggregate rule).
- Holo servo ranges come only from the servo table in `AstroPixelsPlus.ino` (1000–2000 µs, inside stock AstroPixelsPlus's 800–2200 µs). Eye moves use ReelTwo's `servoDispatch.moveTo(num, startDelay, moveTime, float pos)` with pos 0..1 across that range, as ReelTwo's own holo code does. No calibration step.
- Random numbers come only through `r2::RandomFn` (`uint32_t (*)(uint32_t lo, uint32_t hi)`, returns `[lo, hi)`).
- Angle convention: 0° = front, positive = clockwise seen from above, range −180..+180.
- Magnets: 12 at 30° spacing from 0°, plus one at +5°.
- Channels: CH1/CH2 driving detection (±40 µs), CH4 dome, **CH5 SwC level** (up Still / middle Aware / down Alive), CH6 SwB, CH7 VrA mood, CH8 VrB volume.
- Stick deadband 80 µs. Dome command changes at most 10 µs per 20 ms, except stick takeover and radio loss. Dome looks clamped to ±135°.
- Before the first iBUS frame: Aware, General mood, dome sends no pulses.
- Test command: `python3 -m unittest discover tests`. Firmware build: `pio run -d AstroPixelsPlus`.
- C++ test compile flags: `-std=c++11 -Wall -Wextra -Werror -Wno-unused-function`.
- Docs: plain, declarative language. No hedging words.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **A magnet blip while the dome is commanded to stop** (coasting): it counts in the last direction of travel, rather than being ignored or crashing. Test: Task 2 `test_blip_while_stopped_counts_last_direction`.
2. **`micros()` wrapping past 0xFFFFFFFF** mid-gap: the gap timing and front detection still work. Test: Task 2 `test_micros_wraparound`.
3. **Mood dial moved mid-look:** the look finishes with the numbers it started with, and the next sound uses the new mood's bank. Test: Task 4 `test_mood_change_mid_look`.
4. **SwC to Still in the middle of an SwB gesture:** the dome returns to front, the eyes rest and no more sounds play. Test: Task 4 `test_still_mid_gesture_returns_and_silences`.
5. **Corrupt learned-speed table from preferences** (NaN, negative, huge): it's ignored, and speeds stay finite. Test: Task 2 `test_import_sanitises_corrupt_speeds`.

---

## File Structure

| File | Responsibility | Task |
| --- | --- | --- |
| `tests/r2_cpp.py` (create) | Shared helper: compile and run a C++ test body against the firmware headers | 1 |
| `AstroPixelsPlus/R2Mood.h` (create) | Mood profile table, `pick`/`chance`, Mixed mood selector | 1 |
| `AstroPixelsPlus/R2Eyes.h` (create) | Eye positions, mapping to ReelTwo servo positions, `ChameleonEye`, `LeadingEye` | 1 |
| `AstroPixelsPlus/R2Remote.h` (modify) | Task 1: holos via eyes. Task 5: full rewrite for tracker, motion, attention, levels | 1, 5 |
| `tests/test_r2_mood.py`, `tests/test_r2_eyes.py` (create) | Mood and eye tests | 1 |
| `AstroPixelsPlus/R2DomeTracker.h` (create) | Magnet blips → dome angle, front detection, speed learning | 2 |
| `tests/sim_dome.h` (create) | Test-only physical dome that emits blips | 2 |
| `tests/test_r2_dome_tracker.py` (create) | Tracker tests | 2 |
| `AstroPixelsPlus/R2DomeMotion.h` (create) | Dome request → eased servo pulse | 3 |
| `tests/test_r2_dome_motion.py` (create) | Motion tests | 3 |
| `AstroPixelsPlus/R2Attention.h` (create) | Planner: looks, sounds, gestures, priorities | 4 |
| `AstroPixelsPlus/R2Logic.h` (modify) | Task 4: add `switchLevel`. Task 5: remove `DomeBrain`/`DomeInputs` | 4, 5 |
| `tests/test_r2_attention.py` (create) | Attention tests | 4 |
| `tests/test_r2_logic.py` (modify) | Task 4: `switchLevel` test. Task 5: remove `DomeTest` | 4, 5 |
| `tests/test_r2_virtual_droid.py` (create) | 30-minute simulation per mood, checking invariants | 6 |
| `tools/magnet_template.py`, `tests/test_magnet_template.py` (create) | Printable magnet ring template | 7 |
| Docs, BOM, `wiring_reference.html` (modify) | Reflect levels, magnet ring, moods | 7 |

---

### Task 1: Mood profiles, holo eyes, and smooth holos on the droid

**Files:**
- Create: `tests/r2_cpp.py`
- Create: `AstroPixelsPlus/R2Mood.h`
- Create: `AstroPixelsPlus/R2Eyes.h`
- Create: `tests/test_r2_mood.py`
- Create: `tests/test_r2_eyes.py`
- Modify: `AstroPixelsPlus/R2Remote.h` (holo code only)

**Interfaces:**
- Consumes (from `R2Logic.h`): `r2::RandomFn`.
- Produces:
  - `struct r2::Range { float lo; float hi; }`
  - `float r2::pick(const Range&, RandomFn)`
  - `bool r2::chance(uint8_t percent, RandomFn)`
  - `enum r2::Gesture : uint8_t { kGlance, kHalfTurnFront, kWiggle, kSlowTurnAway, kFaceFront }`
  - `enum r2::Mood : uint8_t { kMixed=0, kGeneral=1, kChat=2, kHappy=3, kSad=4, kWhistle=5 }`
  - `struct r2::MoodProfile` (fields below)
  - `const MoodProfile& r2::moodProfile(uint8_t mood)`
  - `class r2::MoodSelector { uint8_t update(uint8_t dialMood, uint32_t nowMs, RandomFn); uint8_t active() const; }`
  - `struct r2::EyePos { float h; float v; }`
  - `struct r2::EyeMove { bool pending; uint32_t durationMs; EyePos to; }`
  - `enum r2::EyeCue : uint8_t { kCueNone, kCueLook, kCueReturn, kCueFlick }`
  - `enum r2::Eye : uint8_t { kFrontEye, kRearEye, kTopEye }`
  - `r2::kEyeServoH[3]`, `r2::kEyeServoV[3]`, `r2::kEyeFlipH[3]`, `r2::kEyeFlipV[3]`
  - `float r2::eyeServoPos(float pos, bool flip)`: eye position −1..+1 to a ReelTwo servo position 0..1
  - `uint32_t r2::eyeMoveDuration(EyePos, EyePos, float speed)`
  - `class r2::ChameleonEye { EyeMove update(uint32_t nowMs, bool active, const MoodProfile&, float rangeScale, float holdScale, RandomFn); EyeMove moveTo(uint32_t nowMs, EyePos to, float speed, uint32_t holdMs); EyePos position() const; bool due(uint32_t nowMs) const; }`
  - `class r2::LeadingEye { EyeMove update(uint32_t nowMs, bool active, const MoodProfile&, EyeCue cue, int8_t side, bool calm, RandomFn); bool looking() const; EyePos position() const; }`

- [ ] **Step 1: Create the shared C++ test helper**

Create `tests/r2_cpp.py`:

```python
"""Compiles and runs a C++ test body against the dome firmware headers.

Test bodies use CHECK(cond) and CHECK_NEAR(a, b, tol). Random sources:
lowest (always lo), highest (always hi - 1), seeded (repeatable LCG).
"""

import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r"""
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)
#define CHECK_NEAR(a, b, tol) do { double _a = (a), _b = (b); if (std::fabs(_a - _b) > (tol)) { \
    std::printf("FAIL line %d: %s = %f, expected %f +/- %f\n", __LINE__, #a, _a, _b, (double)(tol)); failures++; } } while (0)
static uint32_t lowest(uint32_t lo, uint32_t) { return lo; }
static uint32_t highest(uint32_t lo, uint32_t hi) { return hi > lo ? hi - 1 : lo; }
static uint32_t sLcg = 12345;
static uint32_t seeded(uint32_t lo, uint32_t hi)
{
    sLcg = sLcg * 1103515245u + 12345u;
    return hi > lo ? lo + (sLcg >> 8) % (hi - lo) : lo;
}
"""


def run_cpp(includes, body, helpers="", optimize=False):
    """Builds PRELUDE + includes + helpers + main(){body} and runs it. Raises on failure."""
    source = PRELUDE
    source += "".join(f'#include "{name}"\n' for name in includes)
    source += "using namespace r2;\n" + helpers
    source += "\nint main() {\n" + body + "\n    return failures ? 1 : 0;\n}\n"
    with tempfile.TemporaryDirectory() as directory:
        src = Path(directory) / "test.cpp"
        exe = Path(directory) / "test"
        src.write_text(source)
        command = ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                   "-I", str(ROOT / "AstroPixelsPlus"), "-I", str(ROOT / "tests")]
        if optimize:
            command.append("-O2")
        compiled = subprocess.run(command + [str(src), "-o", str(exe)], capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)
        result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
        if result.returncode:
            raise AssertionError(result.stdout[-4000:])
        return result.stdout
```

- [ ] **Step 2: Write the failing mood tests**

Create `tests/test_r2_mood.py`:

```python
"""Tests for AstroPixelsPlus/R2Mood.h."""

import unittest

from r2_cpp import run_cpp


def run(body):
    run_cpp(["R2Mood.h"], body)


class MoodTableTest(unittest.TestCase):
    def test_rows_are_complete_and_ordered(self):
        run(r"""
            for (uint8_t m = 1; m <= 5; m++) {
                const MoodProfile& p = moodProfile(m);
                CHECK(p.soundBank == m);
                CHECK(p.lookIntervalS.lo > 0 && p.lookIntervalS.lo <= p.lookIntervalS.hi);
                CHECK(p.lookAngleDeg.lo > 0 && p.lookAngleDeg.lo <= p.lookAngleDeg.hi && p.lookAngleDeg.hi <= 135);
                CHECK(p.holdS.lo > 0 && p.holdS.lo <= p.holdS.hi);
                CHECK(p.remarkIntervalS.lo > 0 && p.remarkIntervalS.lo <= p.remarkIntervalS.hi);
                CHECK(p.holoHoldS.lo > 0 && p.holoHoldS.lo <= p.holoHoldS.hi);
                CHECK(p.domeCruiseDps > 0 && p.domeEaseS > 0);
                CHECK(p.holoRange > 0 && p.holoRange <= 1 && p.holoSpeed > 0);
                CHECK(p.readjustPct <= 100 && p.doubleTakePct <= 100 && p.chainPct <= 100);
                CHECK(p.glanceFrontPct <= 100 && p.noticePct <= 100 && p.commentPct <= 100);
            }
        """)

    def test_lookup_defaults_to_general(self):
        run(r"""
            CHECK(std::strcmp(moodProfile(kHappy).name, "happy") == 0);
            CHECK(std::strcmp(moodProfile(kMixed).name, "general") == 0);
            CHECK(std::strcmp(moodProfile(9).name, "general") == 0);
            CHECK(moodProfile(kSad).gesture == kSlowTurnAway);
            CHECK(moodProfile(kHappy).gesture == kWiggle);
        """)

    def test_pick_and_chance(self):
        run(r"""
            Range r = {2.0f, 6.0f};
            CHECK_NEAR(pick(r, lowest), 2.0, 0.001);
            CHECK_NEAR(pick(r, highest), 6.0, 0.001);
            Range flat = {3.0f, 3.0f};
            CHECK_NEAR(pick(flat, highest), 3.0, 0.001);
            CHECK(chance(1, lowest));
            CHECK(!chance(0, lowest));
            CHECK(!chance(99, highest));
            CHECK(chance(100, highest));
        """)


class MoodSelectorTest(unittest.TestCase):
    def test_fixed_moods_pass_through(self):
        run(r"""
            MoodSelector s;
            CHECK(s.update(kHappy, 0, lowest) == kHappy);
            CHECK(s.update(kSad, 10, lowest) == kSad);
            CHECK(s.update(7, 20, lowest) == kGeneral);
        """)

    def test_mixed_picks_and_repicks(self):
        run(r"""
            MoodSelector s;
            CHECK(s.update(kMixed, 1000, highest) == kWhistle);      // rnd(1,6) highest = 5
            CHECK(s.update(kMixed, 1000 + 179999, lowest) == kWhistle);  // next change at +180000
            CHECK(s.update(kMixed, 1000 + 180000, lowest) == kGeneral);  // rnd(1,6) lowest = 1
            CHECK(s.update(kHappy, 200000, lowest) == kHappy);
            CHECK(s.update(kMixed, 200020, highest) == kWhistle);    // back to mixed: picks at once
        """)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the mood tests to verify they fail**

Run: `python3 -m unittest discover tests -p "test_r2_mood.py" -v`
Expected: errors containing `R2Mood.h: No such file or directory`.

- [ ] **Step 4: Implement `R2Mood.h`**

Create `AstroPixelsPlus/R2Mood.h`:

```cpp
#pragma once

// Mood profiles (spec §7): the numbers that give each mood its personality.
// Starting values; tune them on the droid.

#include <stdint.h>
#include "R2Logic.h"

namespace r2 {

struct Range
{
    float lo;
    float hi;
};

inline float pick(const Range& r, RandomFn rnd)
{
    if (r.hi <= r.lo)
        return r.lo;
    return r.lo + (r.hi - r.lo) * float(rnd(0, 10001)) / 10000.0f;
}

inline bool chance(uint8_t percent, RandomFn rnd)
{
    return rnd(0, 100) < percent;
}

enum Gesture : uint8_t
{
    kGlance,
    kHalfTurnFront,
    kWiggle,
    kSlowTurnAway,
    kFaceFront
};

// Mood dial positions. 1-5 are also the Marcduino sound banks.
enum Mood : uint8_t
{
    kMixed = 0,
    kGeneral = 1,
    kChat = 2,
    kHappy = 3,
    kSad = 4,
    kWhistle = 5
};

struct MoodProfile
{
    const char* name;
    Range lookIntervalS;
    Range lookAngleDeg;
    float domeCruiseDps;
    float domeEaseS;
    Range holdS;
    uint8_t readjustPct;
    uint8_t doubleTakePct;
    uint8_t chainPct;
    uint8_t glanceFrontPct;
    uint8_t noticePct;
    uint8_t commentPct;
    Range remarkIntervalS;
    float holoRange;
    Range holoHoldS;
    float holoSpeed;
    float holoVBias;
    uint8_t soundBank;
    Gesture gesture;
};

static const MoodProfile kMoodProfiles[5] = {
    // name      look every   look angle   cruise ease   hold         adj dbl chn gfr ntc cmt  remark every  range  holo hold    speed vbias  bank gesture
    {"general", {12, 35}, {25, 100}, 45, 0.6f, {2, 6}, 30, 15, 15, 0, 25, 20, {40, 90}, 0.5f, {3, 8}, 1.0f, 0.0f, 1, kGlance},
    {"chat", {10, 28}, {20, 80}, 50, 0.5f, {2, 5}, 25, 10, 20, 0, 40, 60, {20, 50}, 0.55f, {3, 7}, 1.0f, 0.0f, 2, kHalfTurnFront},
    {"happy", {6, 18}, {20, 120}, 75, 0.35f, {1, 3}, 35, 20, 25, 0, 40, 40, {25, 60}, 0.8f, {1.5f, 4}, 1.4f, 0.1f, 3, kWiggle},
    {"sad", {30, 75}, {15, 60}, 25, 1.0f, {3, 8}, 10, 5, 5, 0, 15, 15, {60, 150}, 0.4f, {5, 12}, 0.6f, -0.4f, 4, kSlowTurnAway},
    {"whistle", {10, 30}, {25, 90}, 50, 0.5f, {1.5f, 4}, 20, 10, 10, 50, 30, 30, {30, 70}, 0.6f, {2, 6}, 1.0f, 0.0f, 5, kFaceFront},
};

// mood 1-5; anything else gives General.
inline const MoodProfile& moodProfile(uint8_t mood)
{
    return kMoodProfiles[(mood >= kGeneral && mood <= kWhistle) ? mood - 1 : 0];
}

// Turns the dial position into the active mood (1-5). Mixed picks one of the
// five at random and picks again every 1-3 minutes.
class MoodSelector
{
public:
    uint8_t update(uint8_t dialMood, uint32_t nowMs, RandomFn rnd)
    {
        if (dialMood != kMixed)
        {
            fWasMixed = false;
            fActive = (dialMood >= kGeneral && dialMood <= kWhistle) ? dialMood : uint8_t(kGeneral);
            return fActive;
        }
        if (!fWasMixed || int32_t(nowMs - fNextChangeMs) >= 0)
        {
            fWasMixed = true;
            fActive = uint8_t(rnd(1, 6));
            fNextChangeMs = nowMs + rnd(60000, 180001);
        }
        return fActive;
    }

    uint8_t active() const { return fActive; }

private:
    uint8_t fActive = kGeneral;
    bool fWasMixed = false;
    uint32_t fNextChangeMs = 0;
};

} // namespace r2
```

- [ ] **Step 5: Run the mood tests to verify they pass**

Run: `python3 -m unittest discover tests -p "test_r2_mood.py" -v`
Expected: 5 tests, OK.

- [ ] **Step 6: Write the failing eye tests**

Create `tests/test_r2_eyes.py`:

```python
"""Tests for AstroPixelsPlus/R2Eyes.h."""

import unittest

from r2_cpp import run_cpp


def run(body):
    run_cpp(["R2Eyes.h"], body)


class PulseAndDurationTest(unittest.TestCase):
    def test_eye_servo_position_follows_reeltwo(self):
        run(r"""
            // ReelTwo HoloLights: 0 = right / up, 1 = left / down. Eyes: +h = right, +v = up.
            CHECK_NEAR(eyeServoPos(1, false), 0.0, 0.0001);
            CHECK_NEAR(eyeServoPos(-1, false), 1.0, 0.0001);
            CHECK_NEAR(eyeServoPos(0, false), 0.5, 0.0001);
            CHECK_NEAR(eyeServoPos(0.5f, false), 0.25, 0.0001);
            CHECK_NEAR(eyeServoPos(0.5f, true), 0.75, 0.0001);
            CHECK_NEAR(eyeServoPos(2, false), 0.0, 0.0001);
            CHECK_NEAR(eyeServoPos(-3, true), 0.0, 0.0001);
        """)

    def test_move_duration_grows_with_distance_and_mood_speed(self):
        run(r"""
            EyePos a = {0, 0}, b = {1, 0}, c = {-1, 0}, d = {1, 0.2f};
            CHECK(eyeMoveDuration(a, b, 1.0f) == 1050);
            CHECK(eyeMoveDuration(c, b, 1.0f) == 1500);
            CHECK(eyeMoveDuration(c, b, 2.0f) == 750);
            CHECK(eyeMoveDuration(a, d, 1.0f) == 1050);   // larger axis decides
            CHECK(eyeMoveDuration(a, a, 1.0f) == 600);
        """)


class ChameleonEyeTest(unittest.TestCase):
    def test_holds_first_then_moves_both_axes_together(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kGeneral);       // holo hold 3-8 s, range 0.5
            EyeMove first = e.update(0, true, m, 1.0f, 1.0f, highest);
            CHECK(!first.pending);                              // starts with a hold
            CHECK(!e.update(7999, true, m, 1.0f, 1.0f, highest).pending);
            EyeMove mv = e.update(8000, true, m, 1.0f, 1.0f, highest);
            CHECK(mv.pending);                                  // highest roll 99: new point
            CHECK_NEAR(mv.to.h, 0.5, 0.001);
            CHECK_NEAR(mv.to.v, 0.5, 0.001);
            CHECK(mv.durationMs == 825);
            CHECK(!e.update(8000 + 825 + 7999, true, m, 1.0f, 1.0f, highest).pending);
            CHECK(e.update(8000 + 825 + 8000, true, m, 1.0f, 1.0f, highest).pending);
        """)

    def test_micro_adjust_and_rest(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, 1.0f, 1.0f, lowest);
            EyeMove adj = e.update(3000, true, m, 1.0f, 1.0f, lowest);   // lowest roll 0: micro-adjust
            CHECK(adj.pending);
            CHECK_NEAR(adj.to.h, -0.1, 0.001);
            CHECK_NEAR(adj.to.v, -0.1, 0.001);
        """)

    def test_inactive_eases_to_centre_once(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kHappy);
            e.update(0, true, m, 1.0f, 1.0f, highest);
            CHECK(e.update(4000, true, m, 1.0f, 1.0f, highest).pending);  // moved away
            EyeMove home = e.update(4100, false, m, 1.0f, 1.0f, highest);
            CHECK(home.pending);
            CHECK_NEAR(home.to.h, 0, 0.001);
            CHECK_NEAR(home.to.v, 0, 0.001);
            CHECK(!e.update(4200, false, m, 1.0f, 1.0f, highest).pending);
            CHECK(!e.update(90000, false, m, 1.0f, 1.0f, highest).pending);
        """)

    def test_hold_scale_lengthens_holds(self):
        run(r"""
            ChameleonEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, 1.0f, 1.5f, highest);                     // 8 s x 1.5
            CHECK(!e.update(11999, true, m, 1.0f, 1.5f, highest).pending);
            CHECK(e.update(12000, true, m, 1.0f, 1.5f, highest).pending);
        """)

    def test_eyes_wander_independently_and_stay_in_range(self):
        run(r"""
            ChameleonEye top, rear;
            const MoodProfile& m = moodProfile(kSad);
            int same = 0, moves = 0; double vSum = 0;
            for (uint32_t t = 0; t < 600000; t += 20) {
                EyeMove a = top.update(t, true, m, 1.0f, 1.0f, seeded);
                EyeMove b = rear.update(t, true, m, 1.0f, 1.0f, seeded);
                if (a.pending) { moves++; vSum += a.to.v; CHECK(a.to.h >= -1 && a.to.h <= 1 && a.to.v >= -1 && a.to.v <= 1); }
                if (a.pending && b.pending) same++;
            }
            CHECK(moves > 20);
            CHECK(same < moves / 2);          // not moving in lockstep
            CHECK(vSum / moves < -0.1);       // sad: biased downwards
        """)


class LeadingEyeTest(unittest.TestCase):
    def test_look_return_and_adjust(self):
        run(r"""
            LeadingEye e;
            const MoodProfile& m = moodProfile(kHappy);          // range 0.8
            e.update(0, true, m, kCueNone, 0, false, lowest);
            EyeMove look = e.update(100, true, m, kCueLook, 1, false, lowest);
            CHECK(look.pending);
            CHECK(e.looking());
            CHECK_NEAR(look.to.h, 0.48, 0.001);                  // 0.6 x 0.8 toward the right
            CHECK(!e.update(1599, true, m, kCueNone, 0, false, lowest).pending);
            CHECK(e.update(1600, true, m, kCueNone, 0, false, lowest).pending);   // adjust after 1.5 s
            EyeMove back = e.update(2000, true, m, kCueReturn, 0, false, lowest);
            CHECK(back.pending);
            CHECK(!e.looking());
            CHECK_NEAR(back.to.h, 0, 0.001);
            CHECK_NEAR(back.to.v, 0, 0.001);
            EyeMove left = e.update(2100, true, m, kCueLook, -1, false, lowest);
            CHECK_NEAR(left.to.h, -0.48, 0.001);
        """)

    def test_flick_and_resume(self):
        run(r"""
            LeadingEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, kCueNone, 0, false, lowest);
            e.update(10, true, m, kCueLook, 1, false, lowest);    // to (0.3, -0.2)
            EyeMove flick = e.update(20, true, m, kCueFlick, 0, false, lowest);
            CHECK(flick.pending);
            CHECK_NEAR(flick.to.h, 0, 0.001);
            CHECK_NEAR(flick.to.v, 0.2, 0.001);
            uint32_t back = 20 + flick.durationMs + 600;
            CHECK(!e.update(back - 1, true, m, kCueNone, 0, false, lowest).pending);
            EyeMove resume = e.update(back, true, m, kCueNone, 0, false, lowest);
            CHECK(resume.pending);
            CHECK_NEAR(resume.to.h, 0.3, 0.001);
            CHECK_NEAR(resume.to.v, -0.2, 0.001);
            CHECK(e.looking());
        """)

    def test_inactive_centres(self):
        run(r"""
            LeadingEye e;
            const MoodProfile& m = moodProfile(kGeneral);
            e.update(0, true, m, kCueLook, 1, false, lowest);
            EyeMove rest = e.update(50, false, m, kCueNone, 0, false, lowest);
            CHECK(rest.pending);
            CHECK_NEAR(rest.to.h, 0, 0.001);
            CHECK(!e.looking());
        """)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 7: Run the eye tests to verify they fail**

Run: `python3 -m unittest discover tests -p "test_r2_eyes.py" -v`
Expected: errors containing `R2Eyes.h: No such file or directory`.

- [ ] **Step 8: Implement `R2Eyes.h`**

Create `AstroPixelsPlus/R2Eyes.h`:

```cpp
#pragma once

// Holoprojector "eyes" (spec §6). Every move is a smooth two-axis move: both
// servos of an eye start together and take the same time, eased by ReelTwo.
// The front eye leads the dome; the top and rear eyes wander on their own,
// like a chameleon's.

#include <stdint.h>
#include <math.h>
#include "R2Mood.h"

namespace r2 {

struct EyePos
{
    float h;
    float v;
};

// One move for one eye. pending = false means no new move this update.
struct EyeMove
{
    bool pending;
    uint32_t durationMs;
    EyePos to;
};

enum EyeCue : uint8_t
{
    kCueNone,
    kCueLook,
    kCueReturn,
    kCueFlick
};

// Eye order and their PCA9685 channels (WIRING.md): front 0/1, rear 2/3, top 4/5.
enum Eye : uint8_t
{
    kFrontEye = 0,
    kRearEye = 1,
    kTopEye = 2
};
static const uint8_t kEyeServoH[3] = {0, 2, 4};
static const uint8_t kEyeServoV[3] = {1, 3, 5};

// Set an axis to true if that holo's mechanism is mounted the other way round
// (it looks left when it should look right, or up when it should look down).
static const bool kEyeFlipH[3] = {false, false, false};
static const bool kEyeFlipV[3] = {false, false, false};

inline float clampUnit(float x)
{
    return x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
}

// Eye position (-1..+1; +h = R2's right, +v = up) to a ReelTwo servo position
// (0..1 across the servo table's start..end pulses). ReelTwo's HoloLights uses
// 0 = right / up and 1 = left / down, and treats that whole range as safe.
inline float eyeServoPos(float pos, bool flip)
{
    float p = (1.0f - clampUnit(pos)) / 2.0f;
    return flip ? 1.0f - p : p;
}

// Bigger moves take longer; the mood's holo speed scales the time.
inline uint32_t eyeMoveDuration(EyePos from, EyePos to, float speed)
{
    float dh = fabsf(to.h - from.h);
    float dv = fabsf(to.v - from.v);
    float d = (dh > dv ? dh : dv) / 2.0f;
    if (speed < 0.1f)
        speed = 0.1f;
    return uint32_t((600.0f + 900.0f * d) / speed);
}

// -1..+1
inline float signedFraction(RandomFn rnd)
{
    return float(rnd(0, 20001)) / 10000.0f - 1.0f;
}

inline uint32_t pickMs(const Range& r, float scale, RandomFn rnd)
{
    return uint32_t(pick(r, rnd) * scale * 1000.0f);
}

// A wandering eye: move to a point, hold, then adjust, rest or move on.
class ChameleonEye
{
public:
    // active = false: ease to centre once and stay (Still). rangeScale and
    // holdScale shrink the range and lengthen the holds (front eye between
    // looks, driving).
    EyeMove update(uint32_t nowMs, bool active, const MoodProfile& mood, float rangeScale, float holdScale,
                   RandomFn rnd)
    {
        EyeMove none = {false, 0, fPos};
        if (!active)
        {
            fStarted = false;
            if (fPos.h != 0.0f || fPos.v != 0.0f)
            {
                EyePos centre = {0.0f, 0.0f};
                return moveTo(nowMs, centre, mood.holoSpeed, 0);
            }
            return none;
        }
        if (!fStarted)
        {
            fStarted = true;
            fNextMs = nowMs + pickMs(mood.holoHoldS, holdScale, rnd);
            return none;
        }
        if (!due(nowMs))
            return none;
        uint32_t hold = pickMs(mood.holoHoldS, holdScale, rnd);
        uint32_t roll = rnd(0, 100);
        EyePos to;
        if (roll < 25)
        {
            to.h = clampUnit(fPos.h + 0.1f * signedFraction(rnd));
            to.v = clampUnit(fPos.v + 0.1f * signedFraction(rnd));
        }
        else if (roll < 45)
        {
            to.h = 0.0f;
            to.v = 0.0f;
        }
        else
        {
            float range = mood.holoRange * rangeScale;
            to.h = clampUnit(range * signedFraction(rnd));
            to.v = clampUnit(range * signedFraction(rnd) + mood.holoVBias);
        }
        return moveTo(nowMs, to, mood.holoSpeed, hold);
    }

    // Starts a move now; the next decision comes after the move plus holdMs.
    EyeMove moveTo(uint32_t nowMs, EyePos to, float speed, uint32_t holdMs)
    {
        EyeMove move = {true, eyeMoveDuration(fPos, to, speed), to};
        fPos = to;
        fNextMs = nowMs + move.durationMs + holdMs;
        return move;
    }

    EyePos position() const { return fPos; }

    bool due(uint32_t nowMs) const { return int32_t(nowMs - fNextMs) >= 0; }

private:
    EyePos fPos = {0.0f, 0.0f};
    uint32_t fNextMs = 0;
    bool fStarted = false;
};

// The front eye: wanders gently between looks, leads the dome into a look,
// adjusts while the dome holds, and flicks toward front on SwB.
class LeadingEye
{
public:
    EyeMove update(uint32_t nowMs, bool active, const MoodProfile& mood, EyeCue cue, int8_t side, bool calm,
                   RandomFn rnd)
    {
        if (!active)
        {
            fMode = kRoaming;
            return fEye.update(nowMs, false, mood, 1.0f, 1.0f, rnd);
        }
        if (cue == kCueLook)
        {
            fMode = kLooking;
            fNextAdjustMs = nowMs + rnd(1500, 3001);
            EyePos to = {clampUnit(0.6f * mood.holoRange * float(side)), clampUnit(0.2f * signedFraction(rnd))};
            return fEye.moveTo(nowMs, to, mood.holoSpeed, 0);
        }
        if (cue == kCueReturn)
        {
            fMode = kRoaming;
            EyePos centre = {0.0f, 0.0f};
            return fEye.moveTo(nowMs, centre, mood.holoSpeed, pickMs(mood.holoHoldS, 1.5f, rnd));
        }
        if (cue == kCueFlick)
        {
            if (fMode != kFlicking)
            {
                fResumeMode = fMode;
                fFlickBack = fEye.position();
            }
            fMode = kFlicking;
            EyePos up = {0.0f, 0.2f};
            return fEye.moveTo(nowMs, up, 4.0f, 600);
        }
        EyeMove none = {false, 0, fEye.position()};
        switch (fMode)
        {
            case kFlicking:
                if (fEye.due(nowMs))
                {
                    fMode = fResumeMode;
                    fNextAdjustMs = nowMs + rnd(1500, 3001);
                    return fEye.moveTo(nowMs, fFlickBack, mood.holoSpeed,
                                       fMode == kRoaming ? pickMs(mood.holoHoldS, 1.5f, rnd) : 0);
                }
                return none;
            case kLooking:
                if (int32_t(nowMs - fNextAdjustMs) >= 0)
                {
                    fNextAdjustMs = nowMs + rnd(1500, 3001);
                    EyePos p = fEye.position();
                    EyePos to = {clampUnit(p.h + 0.1f * signedFraction(rnd)), clampUnit(p.v + 0.1f * signedFraction(rnd))};
                    return fEye.moveTo(nowMs, to, mood.holoSpeed, 0);
                }
                return none;
            case kRoaming:
                return fEye.update(nowMs, true, mood, calm ? 0.35f : 0.5f, calm ? 2.25f : 1.5f, rnd);
        }
        return none;
    }

    bool looking() const { return fMode == kLooking; }

    EyePos position() const { return fEye.position(); }

private:
    enum Mode : uint8_t
    {
        kRoaming,
        kLooking,
        kFlicking
    };

    ChameleonEye fEye;
    Mode fMode = kRoaming;
    Mode fResumeMode = kRoaming;
    EyePos fFlickBack = {0.0f, 0.0f};
    uint32_t fNextAdjustMs = 0;
};

} // namespace r2
```

- [ ] **Step 9: Run the eye tests to verify they pass**

Run: `python3 -m unittest discover tests -p "test_r2_eyes.py" -v`
Expected: 10 tests, OK.

- [ ] **Step 10: Drive the holos from the eyes in `R2Remote.h`**

Make these edits to `AstroPixelsPlus/R2Remote.h`:

1. After `#include "R2Logic.h"`, add:

```cpp
#include "R2Eyes.h"
```

2. In the header comment, replace the line `//   - random holoprojector servo movement` with:

```cpp
//   - holoprojector eyes: smooth moves, front eye roams gently, top and rear wander independently
```

3. In `setup()`, replace the line `        centreHolos();` with:

```cpp
        for (uint8_t i = 0; i < 6; i++)
            servoDispatch.setServoEasingMethod(i, Easing::SineEaseInOut);
```

4. In the member list, delete these two lines:

```cpp
    r2::RandomTimer fHoloTwitch{1500, 4500};
```

```cpp
    bool fHolosMoving = true;
```

and add after `r2::VolumeDial fVolumeDial;`:

```cpp
    r2::LeadingEye fFrontEye;
    r2::ChameleonEye fRearEye;
    r2::ChameleonEye fTopEye;
```

5. Replace the whole `updateHolos` function and the `centreHolos` function with:

```cpp
    void updateHolos(uint32_t now)
    {
        const r2::MoodProfile& mood = r2::moodProfile(fMood);
        float range = fDriving ? 0.7f : 1.0f;
        float hold = fDriving ? 1.5f : 1.0f;
        applyEyeMove(r2::kFrontEye, fFrontEye.update(now, fRandomOn, mood, r2::kCueNone, 0, fDriving, r2Random));
        applyEyeMove(r2::kRearEye, fRearEye.update(now, fRandomOn, mood, range, hold, r2Random));
        applyEyeMove(r2::kTopEye, fTopEye.update(now, fRandomOn, mood, range, hold, r2Random));
    }

    // Both servos of an eye start together and take the same time.
    static void applyEyeMove(uint8_t eye, const r2::EyeMove& move)
    {
        if (!move.pending)
            return;
        servoDispatch.moveTo(r2::kEyeServoH[eye], 0, move.durationMs, r2::eyeServoPos(move.to.h, r2::kEyeFlipH[eye]));
        servoDispatch.moveTo(r2::kEyeServoV[eye], 0, move.durationMs, r2::eyeServoPos(move.to.v, r2::kEyeFlipV[eye]));
    }
```

- [ ] **Step 11: Build the firmware and run all tests**

Run: `pio run -d AstroPixelsPlus 2>&1 | tail -3`
Expected: `[SUCCESS]`.

Run: `python3 -m unittest discover tests`
Expected: all tests OK.

- [ ] **Step 12: Commit**

```bash
git add tests/r2_cpp.py tests/test_r2_mood.py tests/test_r2_eyes.py AstroPixelsPlus/R2Mood.h AstroPixelsPlus/R2Eyes.h AstroPixelsPlus/R2Remote.h
git commit -m "feat(dome): smooth holo eyes with mood profiles

Holos move as eyes: both servos of a holo start together and ease to a
point, hold, then adjust, rest or move on. Top and rear wander on their
own; the front eye roams gently. Mood profiles hold every behaviour
number. Shared C++ test helper for the new modules.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Dome tracker (magnet ring)

**Files:**
- Create: `AstroPixelsPlus/R2DomeTracker.h`
- Create: `tests/sim_dome.h`
- Create: `tests/test_r2_dome_tracker.py`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `r2::kFrontPairDeg` (5.0f), `r2::kMagnetCount` (13), `r2::kMagnetDeg[13]`, `r2::kSpeedBands` (29)
  - `float r2::wrapDeg(float)`
  - `float r2::nextMagnet(float deg, int8_t dir)`
  - `class r2::DomeTracker`:
    - `void setCommand(uint32_t nowUs, int8_t direction, uint16_t offsetUs)`
    - `void onBlip(uint32_t tUs)`
    - `void update(uint32_t nowUs)`
    - `bool known() const`
    - `float angle() const`
    - `void markUnknown()`
    - `float speedDps(int8_t dir, uint16_t offsetUs) const`
    - `uint16_t offsetForSpeed(int8_t dir, float dps) const`
    - `void exportSpeeds(float out[2][kSpeedBands]) const`
    - `void importSpeeds(const float in[2][kSpeedBands])`
    - `bool dirty() const`
    - `void markSaved()`
  - Test-only `r2sim::SimDome { float angle; float dpsPerUs; uint16_t deadbandUs; bool magnetPresent[13]; SimDome(float startDeg, float dpsPerUs, uint16_t deadband); float speed(uint16_t) const; void step(r2::DomeTracker&, uint32_t& nowUs, uint32_t stepUs, int8_t dir, uint16_t offsetUs); }`

- [ ] **Step 1: Write the simulated dome used by the tests**

Create `tests/sim_dome.h`:

```cpp
#pragma once

// Test-only physical dome: a real angle driven by the servo command, and a
// blip to the tracker whenever the dome sensor passes one of the body magnets.

#include "R2DomeTracker.h"

namespace r2sim {

struct SimDome
{
    float angle;          // real angle, -180..180
    float dpsPerUs;       // real degrees per second per microsecond of servo offset above the deadband
    uint16_t deadbandUs;  // offsets at or below this don't move the dome
    bool magnetPresent[r2::kMagnetCount];

    SimDome(float startDeg, float dpsPerUsReal, uint16_t deadband)
        : angle(startDeg), dpsPerUs(dpsPerUsReal), deadbandUs(deadband)
    {
        for (uint8_t i = 0; i < r2::kMagnetCount; i++)
            magnetPresent[i] = true;
    }

    float speed(uint16_t offsetUs) const
    {
        return offsetUs > deadbandUs ? dpsPerUs * float(offsetUs - deadbandUs) : 0.0f;
    }

    // Moves the dome for stepUs under one command in 1 ms slices, reporting each
    // magnet pass to the tracker at the slice it happens in. Ends with update().
    void step(r2::DomeTracker& tracker, uint32_t& nowUs, uint32_t stepUs, int8_t dir, uint16_t offsetUs)
    {
        tracker.setCommand(nowUs, dir, offsetUs);
        float v = speed(offsetUs);
        for (uint32_t s = 0; s < stepUs; s += 1000)
        {
            float before = angle;
            angle = r2::wrapDeg(angle + float(dir) * v * 0.001f);
            if (dir == 0 || v == 0.0f)
                continue;
            float moved = r2::wrapDeg(angle - before);
            for (uint8_t i = 0; i < r2::kMagnetCount; i++)
            {
                if (!magnetPresent[i])
                    continue;
                float d = r2::wrapDeg(r2::kMagnetDeg[i] - before);
                bool crossed = dir > 0 ? (d > 0.0f && d <= moved) : (d < 0.0f && d >= moved);
                if (crossed)
                    tracker.onBlip(nowUs + s + 1000);
            }
        }
        nowUs += stepUs;
        tracker.update(nowUs);
    }
};

} // namespace r2sim
```

- [ ] **Step 2: Write the failing tracker tests**

Create `tests/test_r2_dome_tracker.py`:

```python
"""Tests for AstroPixelsPlus/R2DomeTracker.h."""

import unittest

from r2_cpp import run_cpp


def run(body):
    run_cpp(["R2DomeTracker.h", "sim_dome.h"], body, optimize=True)


class GeometryTest(unittest.TestCase):
    def test_next_magnet(self):
        run(r"""
            CHECK(nextMagnet(0, 1) == 5);
            CHECK(nextMagnet(5, 1) == 30);
            CHECK(nextMagnet(5, -1) == 0);
            CHECK(nextMagnet(0, -1) == -30);
            CHECK(nextMagnet(150, 1) == 180);
            CHECK(nextMagnet(180, 1) == -150);
            CHECK(nextMagnet(-150, -1) == 180);
            CHECK_NEAR(wrapDeg(190), -170, 0.001);
            CHECK_NEAR(wrapDeg(-180), 180, 0.001);
        """)


class SpeedModelTest(unittest.TestCase):
    def test_default_speed_and_inverse(self):
        run(r"""
            DomeTracker t;
            CHECK_NEAR(t.speedDps(1, 100), 50, 0.01);
            CHECK_NEAR(t.speedDps(-1, 100), 50, 0.01);
            CHECK(t.speedDps(1, 10) == 0);
            CHECK(t.speedDps(0, 100) == 0);
            CHECK(t.offsetForSpeed(1, 20) == 40);
            CHECK(t.offsetForSpeed(1, 15) == 30);
            CHECK(t.offsetForSpeed(1, 0) == 0);
            uint16_t prev = 0;
            for (int dps = 5; dps < 140; dps += 5) { uint16_t o = t.offsetForSpeed(1, float(dps)); CHECK(o >= prev); prev = o; }
        """)

    def test_import_sanitises_corrupt_speeds(self):
        run(r"""
            DomeTracker t;
            float table[2][kSpeedBands];
            for (int d = 0; d < 2; d++) for (int b = 0; b < kSpeedBands; b++) table[d][b] = NAN;
            table[0][3] = -5.0f;
            table[0][5] = 1e9f;
            table[0][8] = 30.0f;            // offsets 100-109
            t.importSpeeds(table);
            CHECK_NEAR(t.speedDps(1, 105), 30, 0.01);
            float far = t.speedDps(1, 200);
            CHECK(far == far);              // not NaN
            CHECK_NEAR(far, 30.0 * 200 / 105, 0.5);
            CHECK_NEAR(t.speedDps(1, 50), 30.0 * 50 / 105, 0.5);
            CHECK_NEAR(t.speedDps(-1, 100), 50, 0.01);   // anticlockwise row all invalid: default
        """)


class TrackingTest(unittest.TestCase):
    def test_unknown_until_front_pair_clockwise(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            CHECK(!t.known());
            bool sawKnown = false;
            while (dome.angle < 100.0f) {
                dome.step(t, now, 20000, 1, 100);
                if (t.known() && !sawKnown) { sawKnown = true; CHECK_NEAR(dome.angle, 5.0, 1.0); CHECK_NEAR(t.angle(), dome.angle, 2.0); }
                if (t.known()) CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f);
            }
            CHECK(sawKnown);
        """)

    def test_front_pair_anticlockwise(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(100, 0.35f, 15); uint32_t now = 1000;
            bool sawKnown = false;
            while (dome.angle > -100.0f) {
                dome.step(t, now, 20000, -1, 100);
                if (t.known() && !sawKnown) { sawKnown = true; CHECK_NEAR(dome.angle, 0.0, 1.0); CHECK_NEAR(t.angle(), dome.angle, 2.0); }
            }
            CHECK(sawKnown);
        """)

    def test_learns_speed_and_tracks_closely(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);   // about two turns
            CHECK(t.known());
            CHECK_NEAR(t.speedDps(1, 100), dome.speed(100), 1.5);
            CHECK(t.dirty());
            double worst = 0;
            for (int i = 0; i < 700; i++) { dome.step(t, now, 20000, 1, 100); double e = std::fabs(wrapDeg(t.angle() - dome.angle)); if (e > worst) worst = e; }
            CHECK(worst < 3.0);
            float table[2][kSpeedBands]; t.exportSpeeds(table);
            CHECK(table[0][8] > 0);
            t.markSaved();
            CHECK(!t.dirty());
        """)

    def test_missing_magnet_resyncs_at_front(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            dome.magnetPresent[4] = false;                                   // 90 degrees gone
            double worst = 0;
            for (int i = 0; i < 700; i++) { dome.step(t, now, 20000, 1, 100); if (t.known()) { double e = std::fabs(wrapDeg(t.angle() - dome.angle)); if (e > worst) worst = e; } }
            CHECK(worst < 62.0);                                             // never worse than one miscount
            // carry on past the front pair: back in step
            bool passedFront = false;
            for (int i = 0; i < 1400 && !passedFront; i++) { float before = dome.angle; dome.step(t, now, 20000, 1, 100); passedFront = before < 10.0f && dome.angle >= 10.0f; }
            CHECK(passedFront);
            CHECK(t.known());
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 3.0f);
        """)

    def test_stalled_dome_becomes_unknown(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            for (int i = 0; i < 1300; i++) dome.step(t, now, 20000, 1, 100);
            CHECK(t.known());
            dome.dpsPerUs = 0.0f;                                            // jammed
            for (int i = 0; i < 250; i++) dome.step(t, now, 20000, 1, 100);  // 5 s commanded
            CHECK(!t.known());
        """)

    def test_reversal_counts_back(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            while (dome.angle < 45.0f) dome.step(t, now, 20000, 1, 100);     // past the pair to 45
            CHECK(t.known());
            while (dome.angle > -40.0f) { dome.step(t, now, 20000, -1, 100); if (t.known()) CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f); }
            CHECK(t.known());
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f);
        """)

    def test_long_stop_does_not_fake_front(self):
        run(r"""
            DomeTracker t; r2sim::SimDome dome(-100, 0.35f, 15); uint32_t now = 1000;
            while (dome.angle < 45.0f) dome.step(t, now, 20000, 1, 100);
            for (int i = 0; i < 500; i++) dome.step(t, now, 20000, 0, 0);    // 10 s still
            float before = t.angle();
            while (dome.angle < 75.0f) dome.step(t, now, 20000, 1, 100);     // passes 60
            CHECK(t.known());
            CHECK(t.angle() > before);
            CHECK(std::fabs(wrapDeg(t.angle() - dome.angle)) < 31.0f);
        """)


class BlipEdgeCaseTest(unittest.TestCase):
    def test_bounce_is_dropped(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);                 // 50 deg/s by default
            t.onBlip(1000000);
            t.onBlip(1002000);                       // 2 ms later: bounce
            CHECK(!t.known());
            t.onBlip(2000000);
            t.onBlip(2100000);                       // 5 deg after a 50 deg gap: front pair
            CHECK(t.known());
            CHECK_NEAR(t.angle(), 5.0, 0.01);
        """)

    def test_blip_while_stopped_counts_last_direction(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);
            CHECK(t.known());
            t.setCommand(2500000, 0, 0);
            t.onBlip(3000000);                       // coasting onto the next magnet
            CHECK(t.known());
            CHECK_NEAR(t.angle(), 30.0, 0.01);
        """)

    def test_micros_wraparound(self):
        run(r"""
            DomeTracker t;
            uint32_t base = 0xFFFFFFFFu - 1500000u;
            t.setCommand(base, 1, 100);
            t.onBlip(base + 1000000u);
            t.onBlip(base + 2000000u);               // wraps past zero
            t.onBlip(base + 2100000u);
            CHECK(t.known());
            CHECK_NEAR(t.angle(), 5.0, 0.01);
            t.update(base + 2300000u);               // 0.2 s at 50 deg/s
            CHECK_NEAR(t.angle(), 15.0, 0.5);
        """)

    def test_missing_short_gap_at_pair_marks_unknown(self):
        run(r"""
            DomeTracker t;
            t.setCommand(0, 1, 100);
            t.onBlip(1000000); t.onBlip(2000000); t.onBlip(2100000);   // known at 5
            t.onBlip(2600000);                                          // 30
            t.onBlip(3200000);                                          // 60
            CHECK_NEAR(t.angle(), 60.0, 0.01);
            for (int i = 0; i < 10; i++) t.onBlip(3800000 + 600000 * i); // 90, 120 ... -30, 0 (0 is a normal gap)
            CHECK_NEAR(t.angle(), 0.0, 0.01);
            t.onBlip(3800000 + 600000 * 10);                             // should have been short: count is wrong
            CHECK(!t.known());
        """)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the tracker tests to verify they fail**

Run: `python3 -m unittest discover tests -p "test_r2_dome_tracker.py" -v`
Expected: errors containing `R2DomeTracker.h: No such file or directory`.

- [ ] **Step 4: Implement `R2DomeTracker.h`**

Create `AstroPixelsPlus/R2DomeTracker.h`:

```cpp
#pragma once

// Dome angle from the ring of body magnets (spec §4). 0 = front, positive =
// clockwise seen from above, -180..+180. Magnets sit every 30 degrees, plus
// one at +5 degrees that pairs with the front magnet. The sensor turns with
// the dome: each blip means the dome reached the next magnet in its direction
// of travel. A blip that comes after much less travel than the one before
// marks the front pair. Between blips the angle is estimated from speeds the
// tracker learns as the dome turns.

#include <stdint.h>
#include <math.h>

namespace r2 {

static const float kFrontPairDeg = 5.0f;
static const uint8_t kMagnetCount = 13;
// Magnet positions in clockwise order from front, 0-360 degrees.
static const float kMagnetDeg[kMagnetCount] = {0, 5, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330};
// Learned speeds are kept for servo offsets 20-309 us in 10 us bands.
static const uint8_t kSpeedBands = 29;

inline float wrapDeg(float a)
{
    while (a > 180.0f)
        a -= 360.0f;
    while (a <= -180.0f)
        a += 360.0f;
    return a;
}

inline uint8_t nearestMagnetIndex(float deg)
{
    uint8_t best = 0;
    float bestDistance = 1000.0f;
    for (uint8_t i = 0; i < kMagnetCount; i++)
    {
        float d = fabsf(wrapDeg(deg - kMagnetDeg[i]));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

// The magnet after the one at `deg`, travelling in dir (+1 clockwise).
inline float nextMagnet(float deg, int8_t dir)
{
    uint8_t i = nearestMagnetIndex(deg);
    uint8_t n = dir > 0 ? uint8_t((i + 1) % kMagnetCount) : uint8_t((i + kMagnetCount - 1) % kMagnetCount);
    return wrapDeg(kMagnetDeg[n]);
}

class DomeTracker
{
public:
    struct Config
    {
        uint32_t bounceUs;     // blips closer than this to the last one are dropped
        float frontGapRatio;   // travel below this fraction of the previous gap = front pair
        float overdueFactor;   // learned travel beyond this many gaps without a blip = lost
        float defaultDpsPerUs; // speed estimate before anything is learned
        float alpha;           // learning rate
    };

    static Config defaults()
    {
        Config c = {8000, 0.4f, 2.5f, 0.5f, 0.3f};
        return c;
    }

    DomeTracker() : fCfg(defaults())
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSpeed[d][b] = fSaved[d][b] = 0.0f;
    }

    // The servo command now in force. direction: +1 clockwise, -1 anticlockwise,
    // 0 stopped. offsetUs = |pulse - neutral|.
    void setCommand(uint32_t nowUs, int8_t direction, uint16_t offsetUs)
    {
        advance(nowUs);
        if (direction != 0 && fLastDir != 0 && direction != fLastDir)
            reverse();
        if (direction != fDir || band(offsetUs) != band(fOffset))
            fSteady = false;
        fDir = direction;
        fOffset = offsetUs;
        if (direction != 0)
            fLastDir = direction;
    }

    // A magnet blip at tUs. Blips arrive in time order.
    void onBlip(uint32_t tUs)
    {
        if (fHaveBlip && tUs - fLastBlipUs < fCfg.bounceUs)
            return;
        advance(tUs);
        float travel = fTravelDeg;
        float prevTravel = fPrevTravelDeg;
        bool steady = fSteady && fHaveBlip && fDir != 0;
        uint32_t gapUs = tUs - fLastBlipUs;
        fHaveBlip = true;
        fLastBlipUs = tUs;
        fTravelDeg = 0.0f;
        fPrevTravelDeg = travel;
        fSteady = true;
        if (fLastDir == 0)
            return;
        if (prevTravel > 0.0f && travel < fCfg.frontGapRatio * prevTravel)
        {
            fKnown = true;
            fLearnable = false;
            fLastMagnet = fLastDir > 0 ? kFrontPairDeg : 0.0f;
            return;
        }
        if (!fKnown)
            return;
        float next = nextMagnet(fLastMagnet, fLastDir);
        bool pairSecond = (fLastDir > 0 && next == kFrontPairDeg) || (fLastDir < 0 && next == 0.0f);
        if (pairSecond)
        {
            // The pair's second magnet always follows a short gap: the count is wrong.
            markUnknown();
            return;
        }
        if (steady && fLearnable)
            learn(fDir, fOffset, fabsf(wrapDeg(next - fLastMagnet)) / (float(gapUs) * 1e-6f));
        fLastMagnet = next;
        fLearnable = true;
    }

    // Advances the estimate and checks for a magnet that is overdue.
    void update(uint32_t nowUs)
    {
        advance(nowUs);
        if (fKnown && fDir != 0 && learned(fDir, fOffset) && fTravelDeg > fCfg.overdueFactor * gapAhead())
            markUnknown();
    }

    bool known() const { return fKnown; }

    float angle() const
    {
        if (!fKnown)
            return 0.0f;
        float limit = gapAhead() - 1.0f;
        float t = fTravelDeg < limit ? fTravelDeg : limit;
        if (t < 0.0f)
            t = 0.0f;
        return wrapDeg(fLastMagnet + float(fLastDir) * t);
    }

    void markUnknown()
    {
        fKnown = false;
        fLearnable = false;
    }

    float speedDps(int8_t dir, uint16_t offsetUs) const
    {
        int b = band(offsetUs);
        if (b < 0 || dir == 0)
            return 0.0f;
        const float* row = fSpeed[dir > 0 ? 0 : 1];
        if (row[b] > 0.0f)
            return row[b];
        for (int d = 1; d < kSpeedBands; d++)
        {
            if (b - d >= 0 && row[b - d] > 0.0f)
                return row[b - d] * float(offsetUs) / float(bandCentre(b - d));
            if (b + d < kSpeedBands && row[b + d] > 0.0f)
                return row[b + d] * float(offsetUs) / float(bandCentre(b + d));
        }
        return fCfg.defaultDpsPerUs * float(offsetUs);
    }

    // Smallest offset expected to give at least dps (0 for no movement).
    uint16_t offsetForSpeed(int8_t dir, float dps) const
    {
        if (dps <= 0.0f || dir == 0)
            return 0;
        for (uint16_t off = 20; off < 310; off += 2)
            if (speedDps(dir, off) >= dps)
                return off;
        return 308;
    }

    void exportSpeeds(float out[2][kSpeedBands]) const
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                out[d][b] = fSpeed[d][b];
    }

    // Values that aren't a plausible speed (NaN, <= 0, >= 1000 deg/s) are ignored.
    void importSpeeds(const float in[2][kSpeedBands])
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSpeed[d][b] = (in[d][b] > 0.0f && in[d][b] < 1000.0f) ? in[d][b] : 0.0f;
    }

    bool dirty() const { return fDirty; }

    void markSaved()
    {
        for (uint8_t d = 0; d < 2; d++)
            for (uint8_t b = 0; b < kSpeedBands; b++)
                fSaved[d][b] = fSpeed[d][b];
        fDirty = false;
    }

private:
    Config fCfg;
    float fSpeed[2][kSpeedBands];
    float fSaved[2][kSpeedBands];
    bool fDirty = false;

    bool fKnown = false;
    bool fLearnable = false;
    float fLastMagnet = 0.0f;
    float fTravelDeg = 0.0f;
    float fPrevTravelDeg = 0.0f;

    int8_t fDir = 0;
    int8_t fLastDir = 0;
    uint16_t fOffset = 0;
    bool fSteady = false;

    bool fHaveBlip = false;
    uint32_t fLastBlipUs = 0;
    bool fHaveTime = false;
    uint32_t fLastUs = 0;

    static int band(uint16_t offsetUs)
    {
        if (offsetUs < 20)
            return -1;
        int b = (offsetUs - 20) / 10;
        return b >= kSpeedBands ? kSpeedBands - 1 : b;
    }

    static uint16_t bandCentre(int b) { return uint16_t(25 + 10 * b); }

    bool learned(int8_t dir, uint16_t offsetUs) const
    {
        int b = band(offsetUs);
        return b >= 0 && dir != 0 && fSpeed[dir > 0 ? 0 : 1][b] > 0.0f;
    }

    void learn(int8_t dir, uint16_t offsetUs, float dps)
    {
        int b = band(offsetUs);
        if (b < 0 || !(dps > 0.0f && dps < 1000.0f))
            return;
        uint8_t d = dir > 0 ? 0 : 1;
        float old = fSpeed[d][b];
        fSpeed[d][b] = old > 0.0f ? old + fCfg.alpha * (dps - old) : dps;
        float saved = fSaved[d][b];
        if (saved <= 0.0f || fabsf(fSpeed[d][b] - saved) > 0.1f * saved)
            fDirty = true;
    }

    float gapAhead() const
    {
        return fabsf(wrapDeg(nextMagnet(fLastMagnet, fLastDir) - fLastMagnet));
    }

    void advance(uint32_t nowUs)
    {
        if (fHaveTime && fDir != 0)
            fTravelDeg += speedDps(fDir, fOffset) * float(nowUs - fLastUs) * 1e-6f;
        fLastUs = nowUs;
        fHaveTime = true;
    }

    // Direction flipped between magnets: the magnet ahead becomes the one behind.
    void reverse()
    {
        fPrevTravelDeg = 0.0f;
        fSteady = false;
        fLearnable = false;
        if (!fKnown)
        {
            fTravelDeg = 0.0f;
            return;
        }
        float gap = gapAhead();
        float t = fTravelDeg < gap ? fTravelDeg : gap;
        fLastMagnet = nextMagnet(fLastMagnet, fLastDir);
        fTravelDeg = gap - t;
    }
};

} // namespace r2
```

- [ ] **Step 5: Run the tracker tests to verify they pass**

Run: `python3 -m unittest discover tests -p "test_r2_dome_tracker.py" -v`
Expected: 14 tests, OK.

If `test_learns_speed_and_tracks_closely` fails on `worst < 3.0`, print `worst` and the learned band values. The model must use the learned band for offset 100 (band 8). Don't loosen the test past 3°.

- [ ] **Step 6: Run all tests**

Run: `python3 -m unittest discover tests`
Expected: all OK.

- [ ] **Step 7: Commit**

```bash
git add AstroPixelsPlus/R2DomeTracker.h tests/sim_dome.h tests/test_r2_dome_tracker.py
git commit -m "feat(dome): track the dome angle from a ring of body magnets

Each blip is the next magnet in the direction of travel; a short gap
marks the front pair. Speeds are learned per direction and servo offset
band and used to estimate the angle between magnets. Missed or extra
magnets are caught at the front pair or by overdue travel.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Dome motion (eased moves)

**Files:**
- Create: `AstroPixelsPlus/R2DomeMotion.h`
- Create: `tests/test_r2_dome_motion.py`

**Interfaces:**
- Consumes:
  - `r2::DomeTracker` (`speedDps`, `offsetForSpeed`)
  - `r2::deflected(uint16_t us, uint16_t band)` from `R2Logic.h`
- Produces:
  - `enum r2::DomeRequestKind : uint8_t { kDomeHold, kDomeGoTo, kDomeSeek }`
  - `struct r2::DomeRequest { DomeRequestKind kind; float targetDeg; float cruiseDps; float easeS; bool slowFinish; }`
  - `DomeRequest r2::domeHold()`
  - `DomeRequest r2::domeGoTo(float target, float cruise, float ease, bool slowFinish = false)`
  - `DomeRequest r2::domeSeek()`
  - `float r2::clampDeg(float)` (±135)
  - `float r2::sineRamp(float)`, `float r2::rampDistance(float)`
  - `class r2::DomeMotion`:
    - `uint16_t update(uint32_t nowMs, bool radio, uint16_t stickUs, const DomeRequest& req, bool known, float angleDeg, const DomeTracker& speeds)`
    - `bool arrived() const`
    - `bool manual() const`
    - `int8_t direction() const`
    - `uint16_t offsetUs() const`
    - `uint16_t neutral() const`

- [ ] **Step 1: Write the failing motion tests**

Create `tests/test_r2_dome_motion.py`:

```python
"""Tests for AstroPixelsPlus/R2DomeMotion.h."""

import unittest

from r2_cpp import run_cpp

HELPERS = r"""
// An ideal dome that moves exactly at the tracker's model speed.
static uint16_t tick(DomeMotion& m, const DomeTracker& model, uint32_t& nowMs, float& angle,
                     const DomeRequest& req, uint16_t stick = 1500, bool radio = true, bool known = true)
{
    nowMs += 20;
    uint16_t pulse = m.update(nowMs, radio, stick, req, known, angle, model);
    angle += float(m.direction()) * model.speedDps(m.direction(), m.offsetUs()) * 0.02f;
    return pulse;
}
"""


def run(body):
    run_cpp(["R2Logic.h", "R2DomeTracker.h", "R2DomeMotion.h"], body, HELPERS)


class BasicsTest(unittest.TestCase):
    def test_no_pulses_before_radio_then_neutral(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            CHECK(tick(m, model, now, a, domeHold(), 1500, false) == 0);
            CHECK(tick(m, model, now, a, domeHold(), 1500, true) == 1500);
            for (int i = 0; i < 5; i++) tick(m, model, now, a, domeHold(), 1530, true);
            CHECK(tick(m, model, now, a, domeHold(), 1530, true) == 1530);   // subtrim sets neutral (eased in)
            CHECK(m.neutral() == 1530);
            CHECK(tick(m, model, now, a, domeHold(), 1530, false) == 1530);  // radio lost: last neutral
        """)

    def test_stick_passes_through_and_releases_smoothly(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            CHECK(tick(m, model, now, a, domeHold(), 1800) == 1800);
            CHECK(m.manual());
            uint16_t prev = 1800;
            for (int i = 0; i < 40; i++) { uint16_t p = tick(m, model, now, a, domeHold()); CHECK(prev - p <= 10); prev = p; }
            CHECK(prev == 1500);
        """)

    def test_clamp_and_ramp_helpers(self):
        run(r"""
            CHECK_NEAR(clampDeg(170), 135, 0.001);
            CHECK_NEAR(clampDeg(-170), -135, 0.001);
            CHECK_NEAR(clampDeg(40), 40, 0.001);
            CHECK_NEAR(sineRamp(0), 0, 0.001);
            CHECK_NEAR(sineRamp(1), 1, 0.001);
            CHECK_NEAR(sineRamp(0.5f), 0.5, 0.001);
            CHECK_NEAR(rampDistance(1), 0.5, 0.001);
            CHECK_NEAR(rampDistance(0), 0, 0.001);
        """)


class GoToTest(unittest.TestCase):
    def test_eased_move_arrives(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            DomeRequest req = domeGoTo(90, 45, 0.6f);
            uint16_t prev = 1500, peak = 0; bool falling = false; int ticks = 0;
            while (!m.arrived() && ticks < 2000) {
                uint16_t p = tick(m, model, now, a, req); ticks++;
                CHECK(std::abs(int(p) - int(prev)) <= 10);
                if (p > peak) { CHECK(!falling); peak = p; } else if (p < peak) falling = true;
                prev = p;
            }
            CHECK(m.arrived());
            CHECK_NEAR(a, 90, 4);
            CHECK(peak >= 1500 + model.offsetForSpeed(1, 40));
            for (int i = 0; i < 30; i++) prev = tick(m, model, now, a, req);
            CHECK(prev == 1500);
        """)

    def test_target_clamped_to_135(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            for (int i = 0; i < 3000 && !m.arrived(); i++) tick(m, model, now, a, domeGoTo(170, 60, 0.5f));
            CHECK(m.arrived());
            CHECK_NEAR(a, 135, 4);
        """)

    def test_crosses_front_not_the_back(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 100;
            tick(m, model, now, a, domeHold());
            float last = a;
            for (int i = 0; i < 3000 && !m.arrived(); i++) { tick(m, model, now, a, domeGoTo(-100, 60, 0.5f)); CHECK(a <= last + 0.001f); last = a; }
            CHECK(m.arrived());
            CHECK_NEAR(a, -100, 4);
        """)

    def test_unknown_position_holds(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            for (int i = 0; i < 50; i++) CHECK(tick(m, model, now, a, domeGoTo(60, 45, 0.6f), 1500, true, false) == 1500);
            CHECK(!m.arrived());
        """)

    def test_slow_finish(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 60;
            tick(m, model, now, a, domeHold());
            DomeRequest req = domeGoTo(0, 45, 0.6f, true);
            for (int i = 0; i < 3000 && !m.arrived(); i++) {
                tick(m, model, now, a, req);
                if (a < 12.0f && m.direction() != 0) CHECK(m.offsetUs() <= 40);
            }
            CHECK(m.arrived());
            CHECK_NEAR(a, 0, 4);
        """)

    def test_radio_loss_stops_at_once(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            for (int i = 0; i < 40; i++) tick(m, model, now, a, domeGoTo(120, 60, 0.5f));
            CHECK(m.offsetUs() > 40);
            CHECK(tick(m, model, now, a, domeGoTo(120, 60, 0.5f), 1500, false) == 1500);
        """)

    def test_seek_turns_clockwise_until_known(self):
        run(r"""
            DomeMotion m; DomeTracker model; uint32_t now = 0; float a = 0;
            tick(m, model, now, a, domeHold());
            uint16_t p = 0;
            for (int i = 0; i < 60; i++) p = tick(m, model, now, a, domeSeek(), 1500, true, false);
            CHECK(p == 1500 + model.offsetForSpeed(1, 20));
            CHECK(m.direction() == 1);
            for (int i = 0; i < 60; i++) p = tick(m, model, now, a, domeSeek(), 1500, true, true);
            CHECK(p == 1500);
        """)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the motion tests to verify they fail**

Run: `python3 -m unittest discover tests -p "test_r2_dome_motion.py" -v`
Expected: errors containing `R2DomeMotion.h: No such file or directory`.

- [ ] **Step 3: Implement `R2DomeMotion.h`**

Create `AstroPixelsPlus/R2DomeMotion.h`:

```cpp
#pragma once

// Turns dome requests into smooth servo pulses (spec §5). Moves ease up to a
// cruise speed and ease down onto the target. The stick always wins.

#include <stdint.h>
#include <math.h>
#include "R2Logic.h"
#include "R2DomeTracker.h"

namespace r2 {

enum DomeRequestKind : uint8_t
{
    kDomeHold,
    kDomeGoTo,
    kDomeSeek
};

struct DomeRequest
{
    DomeRequestKind kind;
    float targetDeg;
    float cruiseDps;
    float easeS;
    bool slowFinish; // last 15 degrees at the final speed (returns to front)
};

inline DomeRequest domeHold()
{
    DomeRequest r = {kDomeHold, 0.0f, 0.0f, 0.5f, false};
    return r;
}

inline DomeRequest domeGoTo(float target, float cruise, float ease, bool slowFinish = false)
{
    DomeRequest r = {kDomeGoTo, target, cruise, ease, slowFinish};
    return r;
}

inline DomeRequest domeSeek()
{
    DomeRequest r = {kDomeSeek, 0.0f, 20.0f, 0.5f, false};
    return r;
}

static const float kPi = 3.14159265f;
static const float kDomeLimitDeg = 135.0f;

inline float clampDeg(float a)
{
    return a > kDomeLimitDeg ? kDomeLimitDeg : (a < -kDomeLimitDeg ? -kDomeLimitDeg : a);
}

// Speed fraction along a sine-shaped ramp, s from 0 to 1.
inline float sineRamp(float s)
{
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    return (1.0f - cosf(kPi * s)) / 2.0f;
}

// Distance covered ramping from 0 to s, as a fraction of cruise x ease time.
inline float rampDistance(float s)
{
    s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
    return s / 2.0f - sinf(kPi * s) / (2.0f * kPi);
}

class DomeMotion
{
public:
    struct Config
    {
        uint16_t stickDeadbandUs;
        uint16_t minMoveOffsetUs; // servo deadband: smallest offset that moves the dome
        float finalDps;           // speed for the final approach
        float arriveDeg;
        float slowZoneDeg;
        float maxStepUs;          // per 20 ms
    };

    static Config defaults()
    {
        Config c = {80, 40, 15.0f, 3.0f, 15.0f, 10.0f};
        return c;
    }

    DomeMotion() : fCfg(defaults()) {}

    // Returns the servo pulse in us, or 0 for no pulses (before any radio).
    uint16_t update(uint32_t nowMs, bool radio, uint16_t stickUs, const DomeRequest& req, bool known, float angleDeg,
                    const DomeTracker& speeds)
    {
        float dt = fHaveTime ? float(nowMs - fLastMs) / 1000.0f : 0.0f;
        if (dt > 0.1f)
            dt = 0.1f;
        fLastMs = nowMs;
        fHaveTime = true;

        if (!radio)
        {
            fFrac = 0.0f;
            fMoveDir = 0;
            fManual = false;
            fPulse = fNeutral;
            return fNeutral;
        }
        if (deflected(stickUs, fCfg.stickDeadbandUs))
        {
            fManual = true;
            fFrac = 0.0f;
            fMoveDir = 0;
            fArrived = false;
            fPulse = stickUs;
            return stickUs;
        }
        fManual = false;
        fNeutral = stickUs;
        if (fPulse == 0)
            fPulse = stickUs;
        if (req.kind != fReqKind || req.targetDeg != fReqTarget)
        {
            fReqKind = req.kind;
            fReqTarget = req.targetDeg;
            fArrived = false;
            fBraking = false;
            fGoDir = 0;
        }

        int8_t dir = 0;
        bool accelerate = false;
        bool approach = false;
        float remaining = 0.0f;
        if (req.kind == kDomeSeek && !known)
        {
            dir = 1;
            accelerate = true;
            fCruise = req.cruiseDps;
            fEase = req.easeS;
        }
        else if (req.kind == kDomeGoTo && known && !fArrived)
        {
            remaining = clampDeg(req.targetDeg) - angleDeg;
            // Passed: moving under this request, and the target is now behind.
            bool passed = fGoDir != 0 && remaining * float(fGoDir) < 0.0f;
            if (fabsf(remaining) <= fCfg.arriveDeg || passed)
            {
                fArrived = true;
                fBraking = false;
                fFrac = 0.0f;
            }
            else
            {
                dir = remaining > 0.0f ? 1 : -1;
                if (fMoveDir != 0 && dir != fMoveDir)
                {
                    fFrac = 0.0f;
                    fBraking = false;
                }
                fGoDir = dir;
                fCruise = req.cruiseDps;
                fEase = req.easeS;
                // Once braking starts it continues to the target (no re-acceleration).
                // A slow finish brakes to reach the final speed where the slow zone starts.
                float brakeAt = fabsf(remaining) - (req.slowFinish ? fCfg.slowZoneDeg : 0.0f);
                if (!fBraking && brakeAt <= fCruise * fEase * rampDistance(fFrac) + 0.5f)
                    fBraking = true;
                accelerate = !fBraking;
                approach = true;
            }
        }

        float ease = fEase > 0.05f ? fEase : 0.05f;
        fFrac += (accelerate ? dt : -dt) / ease;
        fFrac = fFrac < 0.0f ? 0.0f : (fFrac > 1.0f ? 1.0f : fFrac);
        float speed = fCruise * sineRamp(fFrac);
        if (approach)
        {
            if (!accelerate && speed < fCfg.finalDps)
                speed = fCfg.finalDps;
            if (req.slowFinish && fabsf(remaining) < fCfg.slowZoneDeg && speed > fCfg.finalDps)
                speed = fCfg.finalDps;
        }
        if (dir == 0)
            dir = fFrac > 0.0f ? fMoveDir : 0;
        if (dir == 0)
            speed = 0.0f;
        fMoveDir = speed > 0.0f ? dir : 0;

        uint16_t offset = 0;
        if (speed > 0.0f)
        {
            offset = speeds.offsetForSpeed(dir, speed);
            if (offset < fCfg.minMoveOffsetUs)
                offset = fCfg.minMoveOffsetUs;
        }
        int32_t target = int32_t(fNeutral) + int32_t(dir) * int32_t(offset);
        int32_t limit = int32_t(fCfg.maxStepUs * dt / 0.02f + 0.001f);
        if (limit < 1 && dt > 0.0f)
            limit = 1;
        int32_t step = target - int32_t(fPulse);
        if (step > limit)
            step = limit;
        if (step < -limit)
            step = -limit;
        fPulse = uint16_t(int32_t(fPulse) + step);
        return fPulse;
    }

    bool arrived() const { return fArrived; }
    bool manual() const { return fManual; }
    uint16_t neutral() const { return fNeutral; }

    // The command actually sent, as the tracker needs it.
    int8_t direction() const
    {
        int32_t d = int32_t(fPulse) - int32_t(fNeutral);
        return (fPulse == 0 || (d < 20 && d > -20)) ? 0 : (d > 0 ? 1 : -1);
    }

    uint16_t offsetUs() const
    {
        int32_t d = int32_t(fPulse) - int32_t(fNeutral);
        return direction() == 0 ? 0 : uint16_t(d > 0 ? d : -d);
    }

private:
    Config fCfg;
    bool fHaveTime = false;
    uint32_t fLastMs = 0;
    uint16_t fNeutral = 0;
    uint16_t fPulse = 0;
    bool fManual = false;
    bool fArrived = false;
    DomeRequestKind fReqKind = kDomeHold;
    float fReqTarget = 0.0f;
    float fFrac = 0.0f;
    float fCruise = 0.0f;
    float fEase = 0.5f;
    int8_t fMoveDir = 0;
    int8_t fGoDir = 0;
    bool fBraking = false;
};

} // namespace r2
```

- [ ] **Step 4: Run the motion tests to verify they pass**

Run: `python3 -m unittest discover tests -p "test_r2_dome_motion.py" -v`
Expected: 10 tests, OK.

If `test_eased_move_arrives` reports the peak falling then rising, the braking latch (`fBraking`) isn't holding. Check it resets only on a new request, arrival or reversal. Don't remove the monotonic check.

- [ ] **Step 5: Run all tests and commit**

Run: `python3 -m unittest discover tests`
Expected: all OK.

```bash
git add AstroPixelsPlus/R2DomeMotion.h tests/test_r2_dome_motion.py
git commit -m "feat(dome): eased dome moves from requests

Go-to moves ramp up and down on a sine curve, brake onto the target,
creep the last degrees and never pass behind R2. The stick passes
straight through; command changes are limited to 10us per 20ms.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Attention planner

**Files:**
- Create: `AstroPixelsPlus/R2Attention.h`
- Modify: `AstroPixelsPlus/R2Logic.h` (add `switchLevel` after `switchNotUp`)
- Modify: `tests/test_r2_logic.py` (add a `switchLevel` test to `ControlsTest`)
- Create: `tests/test_r2_attention.py`

**Interfaces:**
- Consumes:
  - `r2::MoodProfile`, `moodProfile`, `pick`, `chance`, `Range` (Task 1)
  - `r2::EyeCue` (Task 1)
  - `r2::DomeRequest`, `domeHold`, `domeGoTo`, `domeSeek`, `clampDeg` (Task 3)
  - `r2::RandomTimer` (existing, `R2Logic.h`)
- Produces:
  - `uint8_t r2::switchLevel(uint16_t us)` (0 below 1250 µs, 2 above 1750 µs, else 1)
  - `enum r2::Level : uint8_t { kStill = 0, kAware = 1, kAlive = 2 }`
  - `struct r2::AttentionInputs { uint32_t nowMs; bool radio; uint8_t level; uint8_t mood; bool stick; bool driving; bool swb; bool domeKnown; float domeAngle; bool domeArrived; }`
  - `struct r2::AttentionOutputs { DomeRequest dome; EyeCue eyeCue; int8_t eyeSide; bool eyesActive; bool calm; int8_t soundBank; bool soundForced; }`
  - `class r2::Attention`:
    - `explicit Attention(RandomFn)`
    - `AttentionOutputs update(const AttentionInputs&)`
    - `bool domeDisabled() const`
    - `bool busy() const`
    - `const char* phase() const`, one of `"rest"`, `"looking"`, `"returning"`, `"gesture"`, `"manual"`

- [ ] **Step 1: Write the failing `switchLevel` test**

In `tests/test_r2_logic.py`, inside `class ControlsTest`, after `test_switches`, add:

```python
    def test_switch_level(self):
        run(r"""
            CHECK(switchLevel(1000) == 0);
            CHECK(switchLevel(1249) == 0);
            CHECK(switchLevel(1500) == 1);
            CHECK(switchLevel(1750) == 1);
            CHECK(switchLevel(1751) == 2);
            CHECK(switchLevel(2000) == 2);
        """)
```

Run: `python3 -m unittest discover tests -p "test_r2_logic.py" -v`
Expected: `test_switch_level` fails to compile with `'switchLevel' was not declared`.

- [ ] **Step 2: Add `switchLevel` to `R2Logic.h`**

In `AstroPixelsPlus/R2Logic.h`, after the `switchNotUp` function, add:

```cpp
// Three-position switch: 0 up (about 1000us), 1 middle, 2 down (about 2000us).
inline uint8_t switchLevel(uint16_t us)
{
    return us < 1250 ? 0 : (us > 1750 ? 2 : 1);
}
```

Run: `python3 -m unittest discover tests -p "test_r2_logic.py" -v`
Expected: all OK.

- [ ] **Step 3: Write the failing attention tests**

Create `tests/test_r2_attention.py`:

```python
"""Tests for AstroPixelsPlus/R2Attention.h.

Each test drives the planner every 20 ms with a fake dome that reaches any
target one update after it is requested, and records a token per event:
S<bank> sound (S<bank>! forced), EL+/EL- look cue, ER return cue, EF flick,
D<deg> new dome target, K seek.
"""

import unittest

from r2_cpp import run_cpp

HELPERS = r"""
struct FakeDome
{
    float angle;
    bool known;
    bool arrived;
    DomeRequestKind kind;
    float target;
    FakeDome() : angle(0), known(true), arrived(false), kind(kDomeHold), target(0) {}
    void apply(const DomeRequest& r)
    {
        bool fresh = r.kind != kind || r.targetDeg != target;
        kind = r.kind; target = r.targetDeg;
        if (r.kind == kDomeGoTo && known) { if (fresh) arrived = false; else { angle = clampDeg(r.targetDeg); arrived = true; } }
        else arrived = false;
    }
};

struct Rig
{
    Attention att;
    AttentionInputs in;
    FakeDome dome;
    std::string log;
    AttentionOutputs last;
    float lastTokenTarget;
    bool lastWasGoTo;
    explicit Rig(RandomFn rnd, uint8_t level, uint8_t mood) : att(rnd), lastTokenTarget(1e9f), lastWasGoTo(false)
    {
        in.nowMs = 0; in.radio = true; in.level = level; in.mood = mood; in.stick = false; in.driving = false;
        in.swb = false; in.domeKnown = true; in.domeAngle = 0; in.domeArrived = false;
    }
    void tick()
    {
        in.nowMs += 20;
        in.domeKnown = dome.known; in.domeAngle = dome.angle; in.domeArrived = dome.arrived;
        last = att.update(in);
        in.swb = false;
        char buf[32];
        if (last.soundBank >= 0) { std::snprintf(buf, sizeof(buf), "S%d%s ", last.soundBank, last.soundForced ? "!" : ""); log += buf; }
        if (last.eyeCue == kCueLook) log += last.eyeSide > 0 ? "EL+ " : "EL- ";
        if (last.eyeCue == kCueReturn) log += "ER ";
        if (last.eyeCue == kCueFlick) log += "EF ";
        if (last.dome.kind == kDomeGoTo && (!lastWasGoTo || last.dome.targetDeg != lastTokenTarget)) {
            std::snprintf(buf, sizeof(buf), "D%d ", int(std::lround(last.dome.targetDeg))); log += buf;
            lastTokenTarget = last.dome.targetDeg;
        }
        lastWasGoTo = last.dome.kind == kDomeGoTo;
        if (last.dome.kind == kDomeSeek && log.find("K ") == std::string::npos) log += "K ";
        dome.apply(last.dome);
    }
    void run(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 20) tick(); }
    bool has(const char* s) const { return log.find(s) != std::string::npos; }
};
"""


def run(body):
    run_cpp(["R2Logic.h", "R2Mood.h", "R2Eyes.h", "R2DomeTracker.h", "R2DomeMotion.h", "R2Attention.h"], body, HELPERS)


class LookTest(unittest.TestCase):
    def test_plain_look_with_no_extras(self):
        run(r"""
            Rig r(highest, kAlive, kGeneral);       // highest: no notice, no event, no chain; side -1
            r.run(34900);
            CHECK(r.log.empty());                    // first look after 35 s
            r.run(10000);
            CHECK(r.log == "EL- D-100 ER D0 ");
            CHECK(std::fabs(r.dome.angle) < 0.01f);
            CHECK(!r.att.busy());
            CHECK(std::strcmp(r.att.phase(), "rest") == 0);
        """)

    def test_full_look_sequence(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);         // lowest: every chance true, side +1, smallest values
            r.run(28000);                            // the next look starts at 29.5 s
            CHECK(r.log == "S1 EL+ D25 D35 EL+ D25 D35 ER D0 ");
        """)

    def test_aware_moves_no_dome(self):
        run(r"""
            Rig r(lowest, kAware, kGeneral);
            r.run(28000);
            CHECK(r.log == "S1 EL+ EL+ ER ");
            CHECK(r.last.eyesActive);
        """)

    def test_lone_remark(self):
        run(r"""
            Rig r(highest, kAware, kChat);          // remark every 50 s (highest), looks every 28 s
            r.run(51000);
            CHECK(r.has("S2 "));
        """)


class LevelTest(unittest.TestCase):
    def test_still_does_nothing_but_swb(self):
        run(r"""
            Rig r(lowest, kStill, kHappy);
            r.run(60000);
            CHECK(r.log.empty());
            CHECK(!r.last.eyesActive);
            r.in.swb = true; r.tick(); r.run(5000);
            CHECK(r.log == "S3! ");
        """)

    def test_radio_loss_behaves_as_still(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.in.radio = false;
            r.run(60000);
            CHECK(r.log.empty());
            CHECK(!r.last.eyesActive);
        """)

    def test_still_mid_look_returns_dome(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(13000);                            // look under way, dome at 25
            CHECK(r.has("D25 "));
            r.in.level = kStill; r.log.clear();
            r.run(5000);
            CHECK(r.log == "ER D0 ");
            CHECK(std::fabs(r.dome.angle) < 0.01f);
            r.log.clear(); r.run(60000);
            CHECK(r.log.empty());
        """)

    def test_still_mid_gesture_returns_and_silences(self):
        run(r"""
            Rig r(lowest, kAlive, kHappy);
            r.run(1000);
            r.in.swb = true; r.tick(); r.tick();     // wiggle starts
            CHECK(r.has("S3! EF D10 "));
            r.in.level = kStill; r.log.clear();
            r.run(60000);
            CHECK(r.log == "ER D0 ");
            CHECK(!r.last.eyesActive);
        """)


class InterruptionTest(unittest.TestCase):
    def test_stick_pauses_and_returns(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(13000);
            CHECK(r.has("D25 "));
            r.dome.angle = 25;
            r.in.stick = true; r.log.clear();
            r.run(500);
            CHECK(std::strcmp(r.att.phase(), "manual") == 0);
            r.in.stick = false;
            r.run(2900);
            CHECK(r.log.empty());                    // waits 3 s after release
            r.run(400);
            CHECK(r.log == "ER D0 ");
        """)

    def test_driving_returns_and_blocks_looks(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(13000);
            r.in.driving = true; r.log.clear();
            r.tick(); r.tick();
            CHECK(r.log == "ER D0 ");
            CHECK(r.last.calm);
            r.run(20000);                            // keep driving
            CHECK(r.log == "ER D0 ");
            r.in.driving = false;
            r.run(2900);
            CHECK(r.last.calm);
            r.run(200);
            CHECK(!r.last.calm);
        """)


class GestureTest(unittest.TestCase):
    def test_happy_wiggle(self):
        run(r"""
            Rig r(lowest, kAlive, kHappy);
            r.run(1000);
            r.in.swb = true; r.run(3000);
            CHECK(r.log.find("S3! EF D10 D-10 D10 D-10 D0 ") == 0);
        """)

    def test_aware_gesture_is_flick_only(self):
        run(r"""
            Rig r(lowest, kAware, kHappy);
            r.run(1000);
            r.in.swb = true; r.run(3000);
            CHECK(r.log == "S3! EF ");
        """)

    def test_sad_slow_turn_away(self):
        run(r"""
            Rig r(lowest, kAlive, kSad);
            r.run(1000);
            r.dome.angle = 30;
            r.in.swb = true; r.run(5000);
            CHECK(r.log.find("S4! EF D75 D30 D0 ") == 0);
        """)

    def test_sound_gap_skips_remark_after_swb(self):
        run(r"""
            Rig r(lowest, kAware, kChat);            // look at 10 s, remark due at 20 s
            r.run(19000);
            CHECK(r.log == "S2 EL+ EL+ ER ");
            r.in.swb = true; r.run(4000);            // SwB at 19 s: the 20 s remark falls inside the 4 s gap
            CHECK(r.log == "S2 EL+ EL+ ER S2! EF ");
        """)

    def test_mood_change_mid_look(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.run(12100);                            // notice played (S1), look under way
            CHECK(r.has("S1 "));
            r.in.mood = kHappy;
            r.run(20000);
            CHECK(r.has("D25 D35 "));                // the look keeps General's numbers
            r.log.clear();
            r.in.swb = true; r.tick();
            CHECK(r.log.find("S3!") == 0);           // new sounds use the new mood
        """)


class SeekTest(unittest.TestCase):
    def test_seek_before_look_when_lost(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.dome.known = false;
            r.run(13000);
            CHECK(r.has("K "));
            r.dome.known = true; r.dome.angle = 5;
            r.run(20000);
            CHECK(r.has("D25 "));
        """)

    def test_two_failed_seeks_disable_dome_until_still(self):
        run(r"""
            Rig r(lowest, kAlive, kGeneral);
            r.dome.known = false;
            r.run(12000 + 21600 + 12000 + 21600 + 200);
            CHECK(r.att.domeDisabled());
            r.in.level = kStill; r.tick();
            CHECK(!r.att.domeDisabled());
        """)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Run the attention tests to verify they fail**

Run: `python3 -m unittest discover tests -p "test_r2_attention.py" -v`
Expected: errors containing `R2Attention.h: No such file or directory`.

- [ ] **Step 5: Implement `R2Attention.h`**

Create `AstroPixelsPlus/R2Attention.h`:

```cpp
#pragma once

// The attention planner (spec §8): when R2 looks around, where, what it says,
// and how SwB gestures play out. Each update returns a dome request, a cue for
// the front eye and a sound request. Work is queued as small steps (wait,
// sound, eye cue, dome move, seek); interruptions clear the queue.

#include <stdint.h>
#include <math.h>
#include "R2Logic.h"
#include "R2Mood.h"
#include "R2Eyes.h"
#include "R2DomeMotion.h"

namespace r2 {

enum Level : uint8_t
{
    kStill = 0,
    kAware = 1,
    kAlive = 2
};

struct AttentionInputs
{
    uint32_t nowMs;
    bool radio;        // receiver heard (false = behave as Still)
    uint8_t level;     // Level
    uint8_t mood;      // active mood 1-5
    bool stick;        // left stick out of its deadband
    bool driving;
    bool swb;          // SwB flipped down since the last update
    bool domeKnown;
    float domeAngle;
    bool domeArrived;  // DomeMotion::arrived() from the last update
};

struct AttentionOutputs
{
    DomeRequest dome;
    EyeCue eyeCue;
    int8_t eyeSide;
    bool eyesActive;
    bool calm;
    int8_t soundBank;  // -1 = none
    bool soundForced;  // SwB: plays regardless of the gap between sounds
};

class Attention
{
public:
    explicit Attention(RandomFn rnd) : fRnd(rnd), fRemark(40000, 90000) {}

    AttentionOutputs update(const AttentionInputs& in)
    {
        const MoodProfile& mood = moodProfile(in.mood);
        AttentionOutputs out;
        out.dome = domeHold();
        out.eyeCue = kCueNone;
        out.eyeSide = 0;
        out.soundBank = -1;
        out.soundForced = false;
        uint8_t level = in.radio ? in.level : uint8_t(kStill);
        out.eyesActive = level != kStill;
        out.calm = false;

        if (!fStarted)
        {
            fStarted = true;
            fLevel = level;
            fLastSoundMs = in.nowMs - kSoundGapMs;
            scheduleLook(in.nowMs, mood);
        }
        if (in.swb)
        {
            out.soundBank = int8_t(mood.soundBank);
            out.soundForced = true;
            fLastSoundMs = in.nowMs;
        }
        if (level != fLevel)
        {
            if (fLevel == kAlive)
            {
                clear();
                if (domeAway(in))
                    pushReturn(mood);
            }
            if (level == kStill)
            {
                fDomeDisabled = false;
                fSeekFailures = 0;
            }
            if (fLevel == kStill)
                scheduleLook(in.nowMs, mood);
            fLevel = level;
        }
        if (level == kStill)
        {
            runQueue(in, out, mood, level);
            finishUpdate(in, mood);
            return out;
        }
        if (in.stick)
        {
            if (hasDomeSteps())
                fReturnAfterStick = true;
            clear();
            fResumeMs = in.nowMs + kResumeMs;
            fPhase = "manual";
            return out;
        }
        if (int32_t(in.nowMs - fResumeMs) < 0)
        {
            if (int32_t(fLookDueMs - fResumeMs) < 0)
                fLookDueMs = fResumeMs;
            return out;
        }
        if (fReturnAfterStick)
        {
            fReturnAfterStick = false;
            if (level == kAlive && domeAway(in))
                pushReturn(mood);
        }
        if (in.driving)
        {
            fCalmUntilMs = in.nowMs + kResumeMs;
            if (hasDomeSteps() && !onlyReturn())
            {
                clear();
                if (domeAway(in))
                    pushReturn(mood);
            }
        }
        if (int32_t(fLookDueMs - fCalmUntilMs) < 0 && int32_t(in.nowMs - fCalmUntilMs) < 0)
            fLookDueMs = fCalmUntilMs;
        out.calm = int32_t(in.nowMs - fCalmUntilMs) < 0;
        if (in.swb)
        {
            clear();
            pushGesture(in, mood, level);
            fPhase = "gesture";
        }
        if (fCount == 0 && !out.calm && int32_t(in.nowMs - fLookDueMs) >= 0)
        {
            buildLook(in, mood, level);
            fPhase = "looking";
        }
        fRemark.setRange(uint32_t(mood.remarkIntervalS.lo * 1000.0f), uint32_t(mood.remarkIntervalS.hi * 1000.0f));
        if (fRemark.update(in.nowMs, true, fRnd))
            requestSound(in, out, mood);
        runQueue(in, out, mood, level);
        finishUpdate(in, mood);
        return out;
    }

    bool domeDisabled() const { return fDomeDisabled; }
    bool busy() const { return fCount > 0; }
    const char* phase() const { return fPhase; }

private:
    enum StepKind : uint8_t
    {
        kStepWait,
        kStepSound,
        kStepEye,
        kStepDome,
        kStepSeek
    };

    struct Step
    {
        StepKind kind;
        uint32_t ms;
        EyeCue cue;
        int8_t side;
        float target;
        float cruise;
        float ease;
        bool slowFinish;
        bool isReturn;
        bool started;
        uint32_t untilMs;
    };

    static const uint8_t kCapacity = 32;
    static const uint32_t kSoundGapMs = 4000;
    static const uint32_t kResumeMs = 3000;
    static const uint32_t kSeekTimeoutMs = 21600; // 1.2 turns at 20 deg/s

    RandomFn fRnd;
    RandomTimer fRemark;
    Step fQueue[kCapacity];
    uint8_t fHead = 0;
    uint8_t fCount = 0;
    bool fStarted = false;
    bool fWasBusy = false;
    uint8_t fLevel = kStill;
    uint32_t fLookDueMs = 0;
    uint32_t fLastSoundMs = 0;
    uint32_t fResumeMs = 0;
    uint32_t fCalmUntilMs = 0;
    bool fReturnAfterStick = false;
    uint8_t fSeekFailures = 0;
    bool fDomeDisabled = false;
    const char* fPhase = "rest";

    // ---- queue ----

    static Step makeStep(StepKind kind)
    {
        Step s;
        s.kind = kind;
        s.ms = 0;
        s.cue = kCueNone;
        s.side = 0;
        s.target = 0.0f;
        s.cruise = 0.0f;
        s.ease = 0.0f;
        s.slowFinish = false;
        s.isReturn = false;
        s.started = false;
        s.untilMs = 0;
        return s;
    }

    void push(const Step& s)
    {
        if (fCount < kCapacity)
            fQueue[(fHead + fCount++) % kCapacity] = s;
    }

    void pushWait(uint32_t ms)
    {
        Step s = makeStep(kStepWait);
        s.ms = ms;
        push(s);
    }

    void pushSound() { push(makeStep(kStepSound)); }

    void pushEye(EyeCue cue, int8_t side)
    {
        Step s = makeStep(kStepEye);
        s.cue = cue;
        s.side = side;
        push(s);
    }

    void pushDome(float target, float cruise, float ease, bool slowFinish = false, bool isReturn = false)
    {
        Step s = makeStep(kStepDome);
        s.target = clampDeg(target);
        s.cruise = cruise;
        s.ease = ease;
        s.slowFinish = slowFinish;
        s.isReturn = isReturn;
        push(s);
    }

    void pushSeek() { push(makeStep(kStepSeek)); }

    void pushReturn(const MoodProfile& mood, bool withDome = true)
    {
        pushEye(kCueReturn, 0);
        if (withDome)
            pushDome(0.0f, mood.domeCruiseDps, mood.domeEaseS, true, true);
        fPhase = "returning";
    }

    void clear()
    {
        fHead = 0;
        fCount = 0;
    }

    Step& front() { return fQueue[fHead]; }

    void pop()
    {
        fHead = uint8_t((fHead + 1) % kCapacity);
        fCount--;
    }

    bool hasDomeSteps() const
    {
        for (uint8_t i = 0; i < fCount; i++)
        {
            StepKind k = fQueue[(fHead + i) % kCapacity].kind;
            if (k == kStepDome || k == kStepSeek)
                return true;
        }
        return false;
    }

    bool onlyReturn() const
    {
        for (uint8_t i = 0; i < fCount; i++)
        {
            const Step& s = fQueue[(fHead + i) % kCapacity];
            if ((s.kind == kStepDome && !s.isReturn) || s.kind == kStepSeek)
                return false;
        }
        return true;
    }

    // ---- building looks and gestures ----

    static bool domeAway(const AttentionInputs& in) { return in.domeKnown && fabsf(in.domeAngle) > 3.0f; }

    int8_t randomSide() { return chance(50, fRnd) ? 1 : -1; }

    float pickTarget(const MoodProfile& mood) { return clampDeg(float(randomSide()) * pick(mood.lookAngleDeg, fRnd)); }

    void scheduleLook(uint32_t nowMs, const MoodProfile& mood)
    {
        fLookDueMs = nowMs + uint32_t(pick(mood.lookIntervalS, fRnd) * 1000.0f);
    }

    void buildLook(const AttentionInputs& in, const MoodProfile& mood, uint8_t level)
    {
        if (level == kAlive && !fDomeDisabled && !in.domeKnown)
            pushSeek();
        if (chance(mood.noticePct, fRnd))
        {
            pushSound();
            pushWait(500);
        }
        float target = pickTarget(mood);
        pushLookBody(target, 0.0f, mood);
        if (chance(mood.chainPct, fRnd))
            pushLookBody(pickTarget(mood), target, mood);
        pushReturn(mood, level == kAlive);
        fPhase = "looking";
    }

    void pushLookBody(float target, float previous, const MoodProfile& m)
    {
        static const Range kReadjust = {10.0f, 15.0f};
        pushEye(kCueLook, target >= 0.0f ? 1 : -1);
        pushWait(fRnd(400, 601));
        pushDome(target, m.domeCruiseDps, m.domeEaseS);
        uint32_t hold = uint32_t(pick(m.holdS, fRnd) * 1000.0f);
        pushWait(hold / 2);
        uint32_t roll = fRnd(0, 100);
        uint32_t edge = m.readjustPct;
        if (roll < edge)
        {
            pushDome(target + float(randomSide()) * pick(kReadjust, fRnd), m.domeCruiseDps, m.domeEaseS);
        }
        else if (roll < (edge += m.doubleTakePct))
        {
            pushDome(target - 0.4f * (target - previous), m.domeCruiseDps * 1.5f, 0.25f);
            pushWait(300);
            pushDome(target, m.domeCruiseDps * 1.5f, 0.25f);
        }
        else if (roll < (edge += m.glanceFrontPct))
        {
            pushDome(target - (target >= 0.0f ? 30.0f : -30.0f), m.domeCruiseDps, m.domeEaseS);
            pushWait(400);
            pushDome(target, m.domeCruiseDps, m.domeEaseS);
        }
        else if (roll < (edge += m.commentPct))
        {
            pushSound();
        }
        pushWait(hold - hold / 2);
    }

    void pushGlance(float a, float cruise)
    {
        pushDome(a + 20.0f * float(randomSide()), cruise, 0.3f);
        pushWait(300);
        pushDome(a, cruise, 0.3f);
    }

    void pushGesture(const AttentionInputs& in, const MoodProfile& mood, uint8_t level)
    {
        pushEye(kCueFlick, 0);
        if (level != kAlive || fDomeDisabled || !in.domeKnown)
            return;
        float a = in.domeAngle;
        float fast = mood.domeCruiseDps * 1.5f;
        switch (mood.gesture)
        {
            case kGlance:
                pushGlance(a, fast);
                break;
            case kHalfTurnFront:
                if (fabsf(a) < 20.0f)
                    pushGlance(a, fast);
                else
                {
                    pushDome(a / 2.0f, mood.domeCruiseDps, mood.domeEaseS);
                    pushWait(800);
                }
                break;
            case kWiggle:
                for (uint8_t i = 0; i < 2; i++)
                {
                    pushDome(a + 10.0f, 80.0f, 0.2f);
                    pushDome(a - 10.0f, 80.0f, 0.2f);
                }
                pushDome(a, 80.0f, 0.2f);
                break;
            case kSlowTurnAway:
            {
                int8_t side = a > 3.0f ? 1 : (a < -3.0f ? -1 : randomSide());
                pushDome(a + 45.0f * float(side), 20.0f, 1.0f);
                pushWait(1000);
                pushDome(a, 20.0f, 1.0f);
                break;
            }
            case kFaceFront:
                break;
        }
        pushDome(0.0f, mood.domeCruiseDps, mood.domeEaseS, true, true);
    }

    // ---- running ----

    void requestSound(const AttentionInputs& in, AttentionOutputs& out, const MoodProfile& mood)
    {
        if (out.soundBank >= 0 || int32_t(in.nowMs - fLastSoundMs) < int32_t(kSoundGapMs))
            return;
        out.soundBank = int8_t(mood.soundBank);
        fLastSoundMs = in.nowMs;
    }

    void runQueue(const AttentionInputs& in, AttentionOutputs& out, const MoodProfile& mood, uint8_t level)
    {
        while (fCount > 0)
        {
            Step& s = front();
            if (s.kind == kStepWait)
            {
                if (!s.started)
                {
                    s.started = true;
                    s.untilMs = in.nowMs + s.ms;
                }
                if (int32_t(in.nowMs - s.untilMs) < 0)
                    return;
                pop();
                continue;
            }
            if (s.kind == kStepSound)
            {
                if (level != kStill)
                    requestSound(in, out, mood);
                pop();
                continue;
            }
            if (s.kind == kStepEye)
            {
                out.eyeCue = s.cue;
                out.eyeSide = s.side;
                pop();
                continue;
            }
            bool allowed = !fDomeDisabled && (level == kAlive || s.isReturn);
            if (s.kind == kStepSeek)
            {
                if (!allowed || in.domeKnown)
                {
                    if (in.domeKnown && s.started)
                        fSeekFailures = 0;
                    pop();
                    continue;
                }
                if (!s.started)
                {
                    s.started = true;
                    s.untilMs = in.nowMs + kSeekTimeoutMs;
                }
                if (int32_t(in.nowMs - s.untilMs) >= 0)
                {
                    if (++fSeekFailures >= 2)
                        fDomeDisabled = true;
                    clear();
                    return;
                }
                out.dome = domeSeek();
                return;
            }
            if (!allowed || !in.domeKnown)
            {
                pop();
                continue;
            }
            out.dome = domeGoTo(s.target, s.cruise, s.ease, s.slowFinish);
            if (!s.started)
            {
                s.started = true;
                return;
            }
            if (in.domeArrived)
            {
                pop();
                continue;
            }
            return;
        }
    }

    void finishUpdate(const AttentionInputs& in, const MoodProfile& mood)
    {
        if (fCount > 0)
        {
            fWasBusy = true;
            return;
        }
        if (fWasBusy)
        {
            fWasBusy = false;
            scheduleLook(in.nowMs, mood);
        }
        fPhase = "rest";
    }
};

} // namespace r2
```

- [ ] **Step 6: Run the attention tests to verify they pass**

Run: `python3 -m unittest discover tests -p "test_r2_attention.py" -v`
Expected: 17 tests, OK.

If a sequence test fails, print `r.log` in the failing test (`std::printf("%s\n", r.log.c_str());`) and compare it with the token order in the test docstring. For example, `test_full_look_sequence` expects:
- notice `S1`,
- look `EL+` and dome to 25 (`D25`),
- readjust to 35,
- the chained look's `EL+` and `D25`,
- readjust `D35`,
- return `ER D0`.

Fix the planner, not the expectation, unless the expectation contradicts the spec.

- [ ] **Step 7: Run all tests and commit**

Run: `python3 -m unittest discover tests`
Expected: all OK.

```bash
git add AstroPixelsPlus/R2Attention.h AstroPixelsPlus/R2Logic.h tests/test_r2_attention.py tests/test_r2_logic.py
git commit -m "feat(dome): attention planner for looks, sounds and gestures

Looks are queued as steps: notice sound, front-eye lead, dome turn,
hold with a readjust, double-take, glance or comment, an optional
chained look, then return. Stick, driving, SwB and the Still/Aware/
Alive levels interrupt with the spec's priorities. Two failed seeks
disable the dome until Still.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Integrate on the ESP32

**Files:**
- Modify (rewrite): `AstroPixelsPlus/R2Remote.h`
- Modify: `AstroPixelsPlus/R2Logic.h` (delete `DomeInputs` and `DomeBrain`)
- Modify: `tests/test_r2_logic.py` (delete `class DomeTest` and the now-unused `dome()` helper in `PRELUDE`)

**Interfaces:**
- Consumes: every module from Tasks 1–4. ReelTwo `servoDispatch.moveToPulse(uint16_t num, uint32_t startDelay, uint32_t moveTime, uint16_t pos)`, `servoDispatch.setServoEasingMethod(uint16_t, float (*)(float))`, `Easing::SineEaseInOut`, `sMarcSound.playSound(uint8_t bank, uint8_t sound)`, `sMarcSound.setVolume(float)`, `preferences`.
- Produces: `R2Remote r2Remote` with `static void holdDomeServoLow()`, `void setup()`, `void loop()` (unchanged call sites in `AstroPixelsPlus.ino`).

- [ ] **Step 1: Remove `DomeBrain` and its tests**

In `AstroPixelsPlus/R2Logic.h`, delete from the comment block that starts `// Dome: the stick always wins.` through the end of `class DomeBrain` (the closing `};` before `} // namespace r2`). Keep everything above it.

In `tests/test_r2_logic.py`:
- delete the whole `class DomeTest(unittest.TestCase):` block,
- delete the `static DomeInputs dome(...)` helper function from `PRELUDE`.

Run: `python3 -m unittest discover tests -p "test_r2_logic.py" -v`
Expected: all remaining tests OK.

Run: `pio run -d AstroPixelsPlus 2>&1 | grep -E "error|SUCCESS|FAILED"`
Expected: errors in `R2Remote.h` mentioning `DomeBrain`. Step 2 fixes them.

- [ ] **Step 2: Rewrite `R2Remote.h`**

Replace the entire contents of `AstroPixelsPlus/R2Remote.h` with:

```cpp
#pragma once

// FlySky remote features for the simplified droid
// (docs/superpowers/specs/2026-10-11-lifelike-behaviour-design.md).
// The receiver's iBUS line comes up the slip ring to Serial2. From it this file
// runs the behaviour level, mood, volume, sounds, the dome and the holo eyes.
// Driving never passes through here: the VESCs read the receiver directly.
//
// Include after MarcduinoSound.h, servoDispatch and preferences are declared.

#include "R2Logic.h"
#include "R2Mood.h"
#include "R2Eyes.h"
#include "R2DomeTracker.h"
#include "R2DomeMotion.h"
#include "R2Attention.h"

#define R2_IBUS_SERIAL Serial2
#define R2_IBUS_RX_PIN 16
#define R2_DOME_SERVO_PIN PIN_AUX2 // GPIO4, through the dome level shifter to ring CH6
#define R2_DOME_HALL_PIN PIN_AUX3  // GPIO5, dome Hall sensor: low at each body magnet
#define R2_DOME_LEDC_CHANNEL 15

// Transmitter channels as numbered on the FS-i6X
#define R2_CH_DRIVE_LEFT 1
#define R2_CH_DRIVE_RIGHT 2
#define R2_CH_DOME 4
#define R2_CH_LEVEL 5      // SwC: up = Still, middle = Aware, down = Alive. Failsafe: up.
#define R2_CH_PLAY_SOUND 6 // SwB: flip down = play a mood sound
#define R2_CH_MOOD 7       // VrA
#define R2_CH_VOLUME 8     // VrB

#define R2_RADIO_TIMEOUT_MS 300
// Below the VESC's 10% deadband, so the wheels never move while this reads "not driving".
#define R2_DRIVE_BAND_US 40
// Same as DomeMotion's stick deadband.
#define R2_STICK_BAND_US 80
#define R2_VOLUME_SAVE_DELAY_MS 3000
#define R2_SPEED_SAVE_INTERVAL_MS 300000
#define PREFERENCE_R2_DOME_SPEEDS "r2speeds"

static uint32_t r2Random(uint32_t lo, uint32_t hi)
{
    return hi > lo ? lo + esp_random() % (hi - lo) : lo;
}

// Magnet blips: the interrupt stores the time of each, the loop reads them in order.
static volatile uint32_t sBlipUs[8];
static volatile uint8_t sBlipHead = 0;

static void IRAM_ATTR r2HallIsr()
{
    uint8_t head = sBlipHead;
    sBlipUs[head & 7] = micros();
    sBlipHead = uint8_t(head + 1);
}

class R2Remote
{
public:
    // Called first in setup(): the level shifter pulls a floating pin high,
    // which some servos read as a command.
    static void holdDomeServoLow()
    {
        pinMode(R2_DOME_SERVO_PIN, OUTPUT);
        digitalWrite(R2_DOME_SERVO_PIN, LOW);
    }

    void setup()
    {
        R2_IBUS_SERIAL.begin(115200, SERIAL_8N1, R2_IBUS_RX_PIN, -1);
        pinMode(R2_DOME_HALL_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(R2_DOME_HALL_PIN), r2HallIsr, FALLING);
        ledcSetup(R2_DOME_LEDC_CHANNEL, 50, 16);
        ledcAttachPin(R2_DOME_SERVO_PIN, R2_DOME_LEDC_CHANNEL);
        ledcWrite(R2_DOME_LEDC_CHANNEL, 0);
        fSavedVolume = preferences.getInt(PREFERENCE_MARCSOUND_VOLUME, MARC_SOUND_VOLUME);
        float speeds[2][r2::kSpeedBands];
        if (preferences.getBytes(PREFERENCE_R2_DOME_SPEEDS, speeds, sizeof(speeds)) == sizeof(speeds))
        {
            fTracker.importSpeeds(speeds);
            fTracker.markSaved();
        }
        for (uint8_t i = 0; i < 6; i++)
            servoDispatch.setServoEasingMethod(i, Easing::SineEaseInOut);
    }

    void loop()
    {
        uint32_t now = millis();
        readRadio(now);
        if (fRadio)
            readControls(now);
        uint8_t mood = fMoods.update(fDialMood, now, r2Random);
        const r2::MoodProfile& profile = r2::moodProfile(mood);

        while (fBlipTail != sBlipHead)
        {
            fTracker.onBlip(sBlipUs[fBlipTail & 7]);
            fBlipTail = uint8_t(fBlipTail + 1);
        }
        fTracker.update(micros());

        r2::AttentionInputs in;
        in.nowMs = now;
        in.radio = fRadio || !fRadioSeen; // before any radio: run at the starting level
        in.level = fLevel;
        in.mood = mood;
        in.stick = fStick;
        in.driving = fDriving;
        in.swb = fSwbPressed;
        in.domeKnown = fTracker.known();
        in.domeAngle = fTracker.angle();
        in.domeArrived = fMotion.arrived();
        fSwbPressed = false;
        r2::AttentionOutputs out = fAttention.update(in);

        uint16_t pulse = fMotion.update(now, fRadio, fIbus.channel(R2_CH_DOME), out.dome, fTracker.known(),
                                        fTracker.angle(), fTracker);
        fTracker.setCommand(micros(), fMotion.direction(), fMotion.offsetUs());
        if (pulse != fDomePulse)
        {
            fDomePulse = pulse;
            ledcWrite(R2_DOME_LEDC_CHANNEL, uint32_t(pulse) * 65536 / 20000);
        }

        updateEyes(now, out, profile);
        if (out.soundBank >= 0)
            playSound(uint8_t(out.soundBank));
        saveSettings(now);
        report(mood);
    }

private:
    r2::IbusParser fIbus;
    r2::EdgeTrigger fPlaySwitch;
    r2::VolumeDial fVolumeDial;
    r2::MoodSelector fMoods;
    r2::DomeTracker fTracker;
    r2::DomeMotion fMotion;
    r2::Attention fAttention{r2Random};
    r2::LeadingEye fFrontEye;
    r2::ChameleonEye fRearEye;
    r2::ChameleonEye fTopEye;
    uint8_t fBlipTail = 0;

    bool fRadioSeen = false;
    bool fRadio = false;
    uint32_t fLastFrameMs = 0;

    // Before the receiver is heard: Aware, General mood, dome silent.
    uint8_t fLevel = r2::kAware;
    uint8_t fDialMood = r2::kGeneral;
    bool fStick = false;
    bool fDriving = false;
    bool fSwbPressed = false;

    int fVolumeLevel = -1;
    uint32_t fVolumeChangedMs = 0;
    int fSavedVolume = 0;
    uint32_t fSpeedsSavedMs = 0;
    uint16_t fDomePulse = 0;

    uint8_t fReportedLevel = 255;
    uint8_t fReportedMood = 255;
    const char* fReportedPhase = "";
    bool fReportedKnown = false;
    bool fReportedDisabled = false;

    void readRadio(uint32_t now)
    {
        while (R2_IBUS_SERIAL.available())
        {
            if (fIbus.feed(R2_IBUS_SERIAL.read()))
            {
                fLastFrameMs = now;
                fRadioSeen = true;
            }
        }
        bool radio = fRadioSeen && now - fLastFrameMs < R2_RADIO_TIMEOUT_MS;
        if (radio != fRadio)
        {
            fRadio = radio;
            Serial.println(radio ? "[R2] radio OK" : "[R2] radio lost");
        }
    }

    void readControls(uint32_t now)
    {
        fDriving = r2::deflected(fIbus.channel(R2_CH_DRIVE_LEFT), R2_DRIVE_BAND_US) ||
                   r2::deflected(fIbus.channel(R2_CH_DRIVE_RIGHT), R2_DRIVE_BAND_US);
        fStick = r2::deflected(fIbus.channel(R2_CH_DOME), R2_STICK_BAND_US);
        fLevel = r2::switchLevel(fIbus.channel(R2_CH_LEVEL));
        if (fPlaySwitch.update(r2::switchOn(fIbus.channel(R2_CH_PLAY_SOUND))))
            fSwbPressed = true;
        fDialMood = r2::dialPosition(fIbus.channel(R2_CH_MOOD), r2::kMoodCount);

        int level = fVolumeDial.update(fIbus.channel(R2_CH_VOLUME));
        if (level >= 0)
        {
            sMarcSound.setVolume(float(level) / r2::VolumeDial::kMaxLevel);
            fVolumeLevel = level;
            fVolumeChangedMs = now;
        }
    }

    void updateEyes(uint32_t now, const r2::AttentionOutputs& out, const r2::MoodProfile& profile)
    {
        float range = out.calm ? 0.7f : 1.0f;
        float hold = out.calm ? 1.5f : 1.0f;
        applyEyeMove(r2::kFrontEye,
                     fFrontEye.update(now, out.eyesActive, profile, out.eyeCue, out.eyeSide, out.calm, r2Random));
        applyEyeMove(r2::kRearEye, fRearEye.update(now, out.eyesActive, profile, range, hold, r2Random));
        applyEyeMove(r2::kTopEye, fTopEye.update(now, out.eyesActive, profile, range, hold, r2Random));
    }

    // Both servos of an eye start together and take the same time.
    static void applyEyeMove(uint8_t eye, const r2::EyeMove& move)
    {
        if (!move.pending)
            return;
        servoDispatch.moveTo(r2::kEyeServoH[eye], 0, move.durationMs, r2::eyeServoPos(move.to.h, r2::kEyeFlipH[eye]));
        servoDispatch.moveTo(r2::kEyeServoV[eye], 0, move.durationMs, r2::eyeServoPos(move.to.v, r2::kEyeFlipV[eye]));
    }

    void playSound(uint8_t bank)
    {
        static const uint8_t kBankSizes[5] = {
            MP3_BANK1_SOUNDS, MP3_BANK2_SOUNDS, MP3_BANK3_SOUNDS, MP3_BANK4_SOUNDS, MP3_BANK5_SOUNDS};
        uint8_t b, s;
        r2::pickMoodSound(bank, kBankSizes, r2Random, b, s);
        sMarcSound.playSound(b, s);
    }

    // Saves the dial's volume once it has settled, and the learned dome
    // speeds at most every 5 minutes.
    void saveSettings(uint32_t now)
    {
        if (fVolumeLevel >= 0 && now - fVolumeChangedMs >= R2_VOLUME_SAVE_DELAY_MS)
        {
            int value = fVolumeLevel * 1000 / r2::VolumeDial::kMaxLevel;
            if (value != fSavedVolume)
            {
                preferences.putInt(PREFERENCE_MARCSOUND_VOLUME, value);
                fSavedVolume = value;
            }
        }
        if (fTracker.dirty() && now - fSpeedsSavedMs >= R2_SPEED_SAVE_INTERVAL_MS)
        {
            float speeds[2][r2::kSpeedBands];
            fTracker.exportSpeeds(speeds);
            preferences.putBytes(PREFERENCE_R2_DOME_SPEEDS, speeds, sizeof(speeds));
            fTracker.markSaved();
            fSpeedsSavedMs = now;
        }
    }

    void report(uint8_t mood)
    {
        static const char* const kLevels[] = {"still", "aware", "alive"};
        if (fLevel != fReportedLevel)
        {
            fReportedLevel = fLevel;
            Serial.printf("[R2] level %s\n", kLevels[fLevel < 3 ? fLevel : 0]);
        }
        if (mood != fReportedMood)
        {
            fReportedMood = mood;
            Serial.printf("[R2] mood %s%s\n", r2::moodProfile(mood).name, fDialMood == r2::kMixed ? " (mixed)" : "");
        }
        if (fAttention.phase() != fReportedPhase)
        {
            fReportedPhase = fAttention.phase();
            Serial.printf("[R2] behaviour %s\n", fReportedPhase);
        }
        if (fTracker.known() != fReportedKnown)
        {
            fReportedKnown = fTracker.known();
            Serial.println(fReportedKnown ? "[R2] dome position found" : "[R2] dome position lost");
        }
        if (fAttention.domeDisabled() != fReportedDisabled)
        {
            fReportedDisabled = fAttention.domeDisabled();
            if (fReportedDisabled)
                Serial.println("[R2] dome looks off: front magnets not found twice (SwC to Still resets)");
        }
    }
};

R2Remote r2Remote;
```

- [ ] **Step 3: Build the firmware**

Run: `pio run -d AstroPixelsPlus 2>&1 | grep -E "error|warning: .*R2|SUCCESS|FAILED"`
Expected: `[SUCCESS]`, no warnings from `R2*.h`.

If the build reports `'Easing' has not been declared`, add `#include "ServoEasing.h"` at the top of `R2Remote.h`. ReelTwo's `ServoDispatch.h` normally includes it already.

- [ ] **Step 4: Run all tests**

Run: `python3 -m unittest discover tests`
Expected: all OK, including `tests/test_wiring_reference.py`. Its firmware-pin test reads `R2_DOME_SERVO_PIN` and `R2_FRONT_HALL_PIN`; this task renames the latter to `R2_DOME_HALL_PIN`. Update the regex in `tests/test_wiring_reference.py`, in `test_esp_pins_match_firmware`, from:

```python
            "HALL": aux[re.search(r"#define R2_FRONT_HALL_PIN (PIN_AUX\d)", remote).group(1)],
```

to:

```python
            "HALL": aux[re.search(r"#define R2_DOME_HALL_PIN (PIN_AUX\d)", remote).group(1)],
```

Run `python3 -m unittest discover tests` again. Expected: all OK.

- [ ] **Step 5: Commit**

```bash
git add AstroPixelsPlus/R2Remote.h AstroPixelsPlus/R2Logic.h tests/test_r2_logic.py tests/test_wiring_reference.py
git commit -m "feat(dome): run the attention behaviour on the ESP32

SwC on CH5 sets Still, Aware or Alive. Magnet blips are timestamped in
the interrupt and fed to the tracker; attention drives the dome motion,
eye cues and sounds; mood comes from the dial with Mixed rotating.
Learned dome speeds persist. DomeBrain is retired.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Virtual droid test

**Files:**
- Create: `tests/test_r2_virtual_droid.py`

**Interfaces:**
- Consumes: `DomeTracker`, `DomeMotion`, `Attention`, `moodProfile`, `r2sim::SimDome`, `seeded`.
- Produces: none (test only).

- [ ] **Step 1: Write the simulation test**

Create `tests/test_r2_virtual_droid.py`:

```python
"""Thirty simulated minutes of Alive behaviour per mood: tracker, motion and
attention together on a simulated dome. Checks the spec's invariants."""

import unittest

from r2_cpp import run_cpp


class VirtualDroidTest(unittest.TestCase):
    def test_thirty_minutes_per_mood(self):
        run_cpp(["R2Logic.h", "R2Mood.h", "R2Eyes.h", "R2DomeTracker.h", "R2DomeMotion.h", "R2Attention.h",
                 "sim_dome.h"], r"""
            for (uint8_t mood = kGeneral; mood <= kWhistle; mood++)
            {
                DomeTracker tracker; DomeMotion motion; Attention attention(seeded);
                r2sim::SimDome dome(70, 0.35f, 15);
                uint32_t nowUs = 1000, nowMs = 0; uint16_t prevPulse = 0;
                int stepJumps = 0, trackErrors = 0, rangeErrors = 0, restErrors = 0, looks = 0;
                bool everKnown = false; const char* lastPhase = "";
                uint32_t restSince = 0;
                AttentionInputs in;
                in.radio = true; in.level = kAlive; in.mood = mood; in.stick = false;
                for (uint32_t i = 0; i < 90000; i++)
                {
                    nowMs += 20;
                    in.nowMs = nowMs;
                    in.swb = (i % 15000) == 7500;
                    in.driving = (i % 30000) >= 20000 && (i % 30000) < 20500;
                    in.domeKnown = tracker.known(); in.domeAngle = tracker.angle(); in.domeArrived = motion.arrived();
                    AttentionOutputs out = attention.update(in);
                    uint16_t pulse = motion.update(nowMs, true, 1500, out.dome, tracker.known(), tracker.angle(), tracker);
                    if (prevPulse != 0 && std::abs(int(pulse) - int(prevPulse)) > 10) stepJumps++;
                    prevPulse = pulse;
                    dome.step(tracker, nowUs, 20000, motion.direction(), motion.offsetUs());
                    if (tracker.known())
                    {
                        everKnown = true;
                        if (std::fabs(wrapDeg(tracker.angle() - dome.angle)) >= 31.0f) trackErrors++;
                        if (std::fabs(dome.angle) >= 150.0f) rangeErrors++;
                    }
                    if (std::strcmp(attention.phase(), "looking") == 0 && std::strcmp(lastPhase, "looking") != 0) looks++;
                    lastPhase = attention.phase();
                    if (!attention.busy() && motion.direction() == 0 && tracker.known())
                    {
                        if (restSince == 0) restSince = nowMs;
                        if (nowMs - restSince > 1000 && std::fabs(dome.angle) >= 12.0f) restErrors++;
                    }
                    else restSince = 0;
                }
                if (stepJumps || trackErrors || rangeErrors || restErrors || !everKnown || looks <= 10)
                    std::printf("mood %d: jumps %d track %d range %d rest %d known %d looks %d\n",
                                mood, stepJumps, trackErrors, rangeErrors, restErrors, everKnown, looks);
                CHECK(stepJumps == 0);
                CHECK(trackErrors == 0);
                CHECK(rangeErrors == 0);
                CHECK(restErrors == 0);
                CHECK(everKnown);
                CHECK(looks > 10);
            }
        """, optimize=True)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it**

Run: `python3 -m unittest discover tests -p "test_r2_virtual_droid.py" -v`
Expected: OK, within about a minute.

If it fails, the printed line names the invariant and the mood:
- **jumps:** the motion's step limit.
- **track:** the tracker's error between magnets.
- **range:** a look or gesture passed ±150°.
- **rest:** the dome wasn't at front while idle.
- **known:** the seek never found front.
- **looks:** the planner stalled.

Fix the module concerned and add a focused unit test in that module's test file reproducing the case. Don't relax the invariant.

- [ ] **Step 3: Commit**

```bash
git add tests/test_r2_virtual_droid.py
git commit -m "test(dome): thirty-minute virtual droid run per mood

Runs tracker, motion and attention on a simulated dome with SwB and
driving interruptions; checks smooth commands, tracking accuracy, the
135-degree range, rest at front, and that looks keep happening.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Magnet template, docs and parts list

**Files:**
- Create: `tools/magnet_template.py`
- Create: `tests/test_magnet_template.py`
- Modify:
  - `OPERATING_GUIDE.md`
  - `TRANSMITTER_SETUP.md`
  - `DOME_SETUP.md`
  - `WIRING.md`
  - `TROUBLESHOOTING.md`
  - `BRING_UP.md`
  - `README.md`
  - `AstroPixelsPlus/HDR2D2.md`
  - `wiring_reference.html`
  - `Master_R2D2_BOM.xls`

**Interfaces:**
- Produces: `tools/magnet_template.py` with `MAGNETS` (list of degrees), `magnet_positions(diameter_mm) -> [(deg, x, y)]`, `template_svg(diameter_mm) -> str`, and the CLI `python3 tools/magnet_template.py DIAMETER_MM [OUTPUT.svg]`.

- [ ] **Step 1: Write the failing template test**

Create `tests/test_magnet_template.py`:

```python
"""Tests for tools/magnet_template.py."""

import math
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import magnet_template  # noqa: E402


class MagnetTemplateTest(unittest.TestCase):
    def test_positions_match_the_firmware_ring(self):
        firmware = (ROOT / "AstroPixelsPlus/R2DomeTracker.h").read_text()
        ring = re.search(r"kMagnetDeg\[kMagnetCount\] = \{([^}]*)\}", firmware).group(1)
        self.assertEqual([int(v) for v in ring.split(",")], magnet_template.MAGNETS)

    def test_positions_are_on_the_circle_clockwise_from_front(self):
        points = magnet_template.magnet_positions(250)
        self.assertEqual(len(points), 13)
        for deg, x, y in points:
            self.assertAlmostEqual(math.hypot(x, y), 125, places=6)
        deg, x, y = points[0]
        self.assertEqual((deg, round(x, 6), round(y, 6)), (0, 0, -125))      # front is up the page
        deg, x, y = points[1]
        self.assertEqual(deg, 5)
        self.assertGreater(x, 0)                                             # clockwise = to the right

    def test_svg_has_marks_scale_bar_and_true_size(self):
        svg = magnet_template.template_svg(250)
        self.assertEqual(svg.count('class="magnet"'), 13)
        self.assertIn('width="300mm"', svg)
        self.assertIn('class="scale"', svg)
        bar = re.search(r'<line class="scale" x1="([\d.]+)" y1="[\d.]+" x2="([\d.]+)"', svg)
        self.assertAlmostEqual(float(bar.group(2)) - float(bar.group(1)), 100.0)
        self.assertIn("FRONT", svg)

    def test_command_line_writes_a_file(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "ring.svg"
            subprocess.run([sys.executable, str(ROOT / "tools/magnet_template.py"), "260", str(out)], check=True)
            self.assertIn("<svg", out.read_text())


if __name__ == "__main__":
    unittest.main()
```

Run: `python3 -m unittest discover tests -p "test_magnet_template.py" -v`
Expected: `ModuleNotFoundError: No module named 'magnet_template'`.

- [ ] **Step 2: Implement the template tool**

Create `tools/magnet_template.py`:

```python
"""Writes a printable SVG template for the dome magnet ring.

Usage: python3 tools/magnet_template.py DIAMETER_MM [OUTPUT.svg]

DIAMETER_MM is the diameter of the circle the magnet centres sit on, on the
body's top surface. Print at 100% scale and check the 100 mm bar with a ruler.
For circles larger than the paper, use the print dialog's poster or tile option.
"""

import math
import sys
from pathlib import Path

# Degrees clockwise from front, seen from above. Must match kMagnetDeg in
# AstroPixelsPlus/R2DomeTracker.h.
MAGNETS = [0, 5, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330]
MARGIN_MM = 25
MAGNET_MARK_MM = 10


def magnet_positions(diameter_mm):
    """(degrees, x, y) for each magnet, relative to the centre, front up the page (-y)."""
    r = diameter_mm / 2
    return [(deg, r * math.sin(math.radians(deg)), -r * math.cos(math.radians(deg))) for deg in MAGNETS]


def template_svg(diameter_mm):
    size = diameter_mm + 2 * MARGIN_MM
    height = size + 30
    c = size / 2
    r = diameter_mm / 2
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{size:g}mm" height="{height:g}mm" '
        f'viewBox="0 0 {size:g} {height:g}" font-family="sans-serif">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<circle cx="{c:.2f}" cy="{c:.2f}" r="{r:.2f}" fill="none" stroke="#999" stroke-width="0.3"/>',
        f'<line x1="{c - 5:.2f}" y1="{c:.2f}" x2="{c + 5:.2f}" y2="{c:.2f}" stroke="black" stroke-width="0.3"/>',
        f'<line x1="{c:.2f}" y1="{c - 5:.2f}" x2="{c:.2f}" y2="{c + 5:.2f}" stroke="black" stroke-width="0.3"/>',
        f'<line x1="{c:.2f}" y1="{c - 10:.2f}" x2="{c:.2f}" y2="{c - r + 12:.2f}" stroke="black" stroke-width="0.5"/>',
        f'<text x="{c:.2f}" y="{c - r - 9:.2f}" font-size="6" text-anchor="middle">FRONT</text>',
    ]
    for deg, x, y in magnet_positions(diameter_mm):
        px, py = c + x, c + y
        out.append(f'<circle class="magnet" cx="{px:.2f}" cy="{py:.2f}" r="{MAGNET_MARK_MM / 2:g}" '
                   'fill="none" stroke="black" stroke-width="0.4"/>')
        out.append(f'<line x1="{px - 2:.2f}" y1="{py:.2f}" x2="{px + 2:.2f}" y2="{py:.2f}" stroke="black" stroke-width="0.2"/>')
        out.append(f'<line x1="{px:.2f}" y1="{py - 2:.2f}" x2="{px:.2f}" y2="{py + 2:.2f}" stroke="black" stroke-width="0.2"/>')
        lx = c + x * (r - 12) / r
        ly = c + y * (r - 12) / r
        out.append(f'<text x="{lx:.2f}" y="{ly + 1.5:.2f}" font-size="3.5" text-anchor="middle">{deg}°</text>')
    out += [
        f'<text x="{MARGIN_MM}" y="{size + 6:.2f}" font-size="3.5">Seen from above. One magnet on each mark, all the same face up. '
        f'Circle {diameter_mm:g} mm. The double mark at FRONT is 0° and 5° clockwise.</text>',
        f'<line class="scale" x1="{MARGIN_MM:.2f}" y1="{size + 15:.2f}" x2="{MARGIN_MM + 100:.2f}" y2="{size + 15:.2f}" '
        'stroke="black" stroke-width="0.6"/>',
        f'<text x="{MARGIN_MM}" y="{size + 22:.2f}" font-size="3.5">100 mm: check with a ruler before use</text>',
        "</svg>",
    ]
    return "\n".join(out) + "\n"


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    diameter = float(argv[1])
    output = Path(argv[2]) if len(argv) > 2 else Path(f"magnet_ring_{diameter:g}mm.svg")
    output.write_text(template_svg(diameter))
    print(f"Wrote {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

Run: `python3 -m unittest discover tests -p "test_magnet_template.py" -v`
Expected: 4 tests, OK.

- [ ] **Step 3: Update `OPERATING_GUIDE.md`**

Replace everything from `## Controls` up to (not including) `## Before each session` with:

```markdown
## Controls

| Control | What it does |
| --- | --- |
| **Right stick** | Drive. Forward/back moves, left/right turns. |
| **Left stick, left/right** | Turns the dome. Always overrides R2's own behaviour. |
| **Left stick, up/down** | Nothing |
| **SwC** (3-position) | Behaviour level. Up: Still. Middle: Aware. Down: Alive. |
| **SwB** | Flip down: play one sound from the current mood, with a matching gesture |
| **VrA** | Mood, fully anticlockwise to fully clockwise: mixed, general, chat, happy, sad, whistle |
| **VrB** | Volume. Fully anticlockwise is silent, fully clockwise is loudest. |
| **SwA, SwD** | Nothing |

## Behaviour levels (SwC)

| Level | What R2 does |
| --- | --- |
| **Still** (up) | Nothing on its own. Holos centred, no random sounds, the dome moves only with the stick. For tight spaces and people up close. |
| **Aware** (middle) | Looks around with its holos and talks. The dome stays facing front. |
| **Alive** (down) | Looks around with its dome too. The front holo glances toward something just before the dome turns to it. |

Losing the transmitter signal puts R2 in Still.

## Moods (VrA)

The mood sets what R2 says and how it moves.

| Mood | Character |
| --- | --- |
| General | Calm and curious. Measured looks, few sounds. |
| Chat | Sociable. Looks often end in a comment. |
| Happy | Lively. Frequent, brisk looks; holos roam widely. |
| Sad | Withdrawn. Rare, slow looks; holos low; quiet. |
| Whistle | Attentive. Looks often end with a glance back toward front. |
| Mixed | Changes to a different mood every 1–3 minutes. |

## SwB

- Flip SwB down to play one sound from the current mood, at any level. Flip it up, then down again, for another.
- In **Aware**, the front holo flicks toward front as if addressing whoever's there.
- In **Alive**, the dome adds a gesture:

| Mood | Gesture |
| --- | --- |
| General | A glance to one side and back |
| Chat | A turn partway toward front |
| Happy | A quick left-right wiggle |
| Sad | A slow half-turn away, then back |
| Whistle | Turns to face front |

## Power on

1. Transmitter **on**. Right stick centred, SwC up (Still).
2. Droid master cutoff **on**.
3. The start-up sound plays and the logics scroll their start-up text.
4. Nudge the right stick to confirm both wheels respond.
5. Set SwC to the level you want.

- The wheels stay still until the right stick has been centred after power-on.
- At power-on the dome doesn't know where it's pointing. In Alive, its first move is a slow turn until it passes the front magnets.

## Power off

1. Droid master cutoff **off**.
2. Transmitter **off**.

## Driving

- The droid drives the moment the right stick moves. There is no arming switch.
- Releasing the stick brakes the wheels.
- Driving ends any look and brings the dome back to front. R2 starts looking around again 3 seconds after you stop.
- If the transmitter switches off or goes out of range, the wheels stop and R2 goes Still.
- The master cutoff stops everything.

## Dome

- The left stick turns the dome. Speed follows how far you push. R2 waits 3 seconds after you let go before it looks around again.
- In Alive, R2 never turns more than 135° from front and comes back to front after every look.
- **The dome stops looking around and won't restart:** it failed to find the front magnets twice. Flip SwC up to Still and back down to retry. If it happens again, see [Troubleshooting](TROUBLESHOOTING.md).

```

- [ ] **Step 4: Update `TRANSMITTER_SETUP.md`**

1. In the **Channel map** table, replace the CH5–CH10 rows with:

```markdown
| CH5 | SwC | ESP32 | Behaviour level: up Still, middle Aware, down Alive |
| CH6 | SwB | ESP32 | Play a mood sound (flip down) |
| CH7 | VrA | ESP32 | Mood |
| CH8 | VrB | ESP32 | Volume |
| CH9 | SwA | — | Spare |
| CH10 | SwD | — | Spare |
```

2. In **§3 Auxiliary channels**, replace the table rows with:

```markdown
| CH5 | SwC |
| CH6 | SwB |
| CH7 | VrA |
| CH8 | VrB |
| CH9 | SwA |
| CH10 | SwD |
```

3. Replace the whole **§6** section (heading and body) with:

```markdown
## 6. Dome direction and neutral (CH4)

1. Power the droid with the dome servo connected and SwC up (Still).
2. Push the left stick right. The dome turns clockwise seen from above: R2 turns its head to its right. If it turns the other way, set **Functions setup → Reverse → CH4**.
3. Centre the left stick. If the dome creeps, open **Functions setup → Subtrim → CH4** and adjust until it stops.

The firmware counts the body magnets assuming stick-right is clockwise, so step 2 matters.

The firmware uses the centred CH4 value as the dome's stop point, for both the stick and R2's own looks. It treats any CH4 value more than 80 µs from 1500 µs as a pushed stick. If R2 never looks around after trimming, the trim is too large: reduce it and adjust the servo's own neutral instead.
```

4. In **§7 Failsafe**, replace the CH5 row with:

```markdown
| CH5 | SwC up | R2 goes Still; the dome returns to front and stops |
```

- [ ] **Step 5: Update `DOME_SETUP.md`**

1. Replace the body of **§3 Holo servos** (everything after its heading up to `## 4.`) with:

```markdown
1. Power off. Plug one holo servo into its PCA9685 channel ([Wiring](WIRING.md) §3). Fit D-SERVO (5A).
2. Power on. With no radio connected, R2 is Aware: the holo moves to a point, holds, and moves on.
3. Add the other five servos one at a time.
4. Set the mood dial to Happy (the widest holo movement) and SwC to the middle. Watch each holo for a minute.

**Pass:** no holo touches a mechanical stop, buzzes or binds. For each holo:

- the top and rear holos wander on their own,
- a sad mood aims them low,
- and a look toward R2's right turns the front holo to the right.

**Ranges.** Each holo servo's range is its row in the servo table in `AstroPixelsPlus/AstroPixelsPlus.ino`: 1000–2000 µs. That's inside stock AstroPixelsPlus's 800–2200 µs, which ReelTwo's own holo code drives to both ends. R2's moods use at most the middle 80% of it.

| Problem | Fix |
| --- | --- |
| A holo touches its stop | Narrow that servo's row in the servo table, for example `{2, 1100, 1900, HOLO_HSERVO}`, rebuild and reflash |
| A holo looks the wrong way (left instead of right, or up when sad) | Set that eye's axis to `true` in `kEyeFlipH` / `kEyeFlipV` in `AstroPixelsPlus/R2Eyes.h` (order: front, rear, top), rebuild and reflash |
```

2. Replace the body of **§4 Radio link (iBUS)** steps 3–4 and the pass line with:

```markdown
3. Flip SwC up (Still): the holos ease to centre and stop.
4. Flip SwC to the middle (Aware): the holos start looking around again.

**Pass:** SwC controls the holos. That proves the ESP32 is reading the receiver.
```

3. Replace the whole **§5** section with:

```markdown
## 5. Magnet ring and dome sensor

The dome finds its position from 13 magnets on the body and the Hall sensor under the dome.

1. Measure the diameter of the circle the magnets will sit on, on the body's top surface under the dome edge.
2. Print the template: `python3 tools/magnet_template.py <diameter in mm>`. Print the SVG at 100% and check the 100 mm bar with a ruler.
3. Tape the template on the body with FRONT pointing to R2's front.
4. Glue a magnet (6×2 mm or 10×2 mm) on each of the 13 marks, all with the same face up. The two marks at FRONT are 0° and 5° clockwise.
5. Mount the KY-003 sensor on the underside of the dome, facing down, at the same distance from the centre as the magnets. Set the gap to 5 mm or less.

**Check:** turn the dome slowly by hand. The sensor's LED blinks at every magnet: 13 blinks per turn, with two close together at front. If it never lights, turn all the magnets over.
```

4. In **§6 Dome servo**, replace steps 2–5 with:

```markdown
2. Set SwC up (Still).
3. Power on with the transmitter on.
4. Push the left stick right: the dome turns clockwise seen from above. If not, reverse CH4 ([Transmitter Setup](TRANSMITTER_SETUP.md) §6).
5. Centre the stick. If the dome creeps, set CH4 subtrim.
```

5. Replace the whole **§7** section with:

```markdown
## 7. Behaviour

1. Set the mood dial (VrA) to General. Flip SwC down (Alive), both sticks centred.
2. The dome turns slowly clockwise until it passes the front magnets, then stops facing front.
3. Within 35 seconds R2 looks around:
   - the front holo glances one way,
   - the dome follows smoothly,
   - it holds for a few seconds,
   - and comes back to front.
4. Push the left stick: the dome follows the stick immediately. Let go: 3 seconds later R2 carries on.
5. Push the right stick (driving): the dome returns to front.
6. Flip SwB down: R2 makes a sound and a glance.
7. Turn VrA to Happy: looks come more often and faster. Turn it to Sad: slow and rare.
8. Flip SwC to the middle (Aware): the dome stops at front; the holos keep looking around.
9. Flip SwC up (Still): everything rests.

| Problem | Fix |
| --- | --- |
| The dome turns for about 20 seconds, then stops looking around | The front magnets aren't being found. Check §5, then flip SwC up and down to retry. |
| The dome stops short of front or past it | A magnet is loose or missing. Check all 13 against the template. |
| Looks are too fast or too big | Edit that mood's row in `AstroPixelsPlus/R2Mood.h` (`domeCruiseDps`, `lookAngleDeg`), rebuild and reflash. |
```

- [ ] **Step 6: Update `WIRING.md`, `wiring_reference.html`, `TROUBLESHOOTING.md`, `BRING_UP.md`, `README.md`**

1. **`WIRING.md`.** Replace the heading `### Front Hall sensor (KY-003)` with `### Dome Hall sensor (KY-003)`. Replace the paragraph starting `The output goes low when the magnet is under it.` (through the end of that bullet list) with:

```markdown
The output goes low each time the sensor passes a magnet in the body's magnet ring (13 magnets: 12 every 30°, plus one 5° clockwise of front). The firmware counts magnets to know where the dome points. Mounting is in [Dome Setup](DOME_SETUP.md) §5.
```

2. **`wiring_reference.html`.** In the JSON component `"hall"`:
   - set `"name"` to `"Dome Hall sensor (KY-003)"`,
   - set `"area"` to `"Dome underside, over the body magnet ring"`,
   - set `"note"` to `"Output low at each magnet. Body magnet ring: 12 magnets every 30 degrees plus one 5 degrees clockwise of front. Gap 5mm or less."`
   - In the wire `"hall-esp"`, set `"note"` to `"AUX3. Low at each magnet."`

3. **`TROUBLESHOOTING.md`.** Replace the whole **Dome** table with:

```markdown
| Symptom | Check |
| --- | --- |
| Dome doesn't turn | B-SERVO fuse. Slip ring CH6. Shifter LV2 → HV2. Transmitter on: the servo gets no pulses until the receiver is heard. |
| Dome creeps with the stick centred | Set CH4 subtrim ([Transmitter Setup](TRANSMITTER_SETUP.md) §6) |
| R2 never looks around | SwC down (Alive). Both sticks centred and no driving for 3 seconds. Looks come every 6–75 seconds, depending on mood. |
| R2 never looks around although SwC is down and the sticks are centred | CH4 subtrim is more than 80 µs from centre, so the firmware reads the stick as pushed. Reduce the subtrim; adjust the servo's own neutral instead. |
| The dome turns for about 20 seconds, then stops looking around | The front magnets aren't being found. Check the sensor LED blinks at every magnet, the gap is 5 mm or less, all magnets face the same way up, and the extra magnet is 5° clockwise of front. Check shifter HV3 → LV3 → GPIO5. Flip SwC up and down to retry. |
| The dome turns the wrong way for its looks | Stick-right must turn the dome clockwise. Reverse CH4 ([Transmitter Setup](TRANSMITTER_SETUP.md) §6). |
| Looks stop short of front or past it | A magnet is loose or missing. Check all 13 against the template. |
```

   In the **Holos and radio** table, replace the row starting `| Holo servo buzzes or binds |` with:

```markdown
| Holo servo buzzes or binds | Its range in the servo table in `AstroPixelsPlus/AstroPixelsPlus.ino` reaches a mechanical stop. Narrow that row ([Dome Setup](DOME_SETUP.md) §3). |
```

4. **`BRING_UP.md`.** In Stage 4, replace the iBUS line with:

```markdown
- [ ] iBUS through slip ring CH3: SwC up centres the holos, SwC middle sets them moving
```

   Replace the whole Stage 6 checklist (the lines under `Guide: [Dome Setup](DOME_SETUP.md)` in Stage 6) with:

```markdown
- [ ] Magnet ring glued from the printed template: 13 magnets, same face up
- [ ] Dome sensor mounted: its LED blinks 13 times per turn, twice close together at front
- [ ] Dome servo signal through slip ring CH6; B-SERVO (5A) fitted
- [ ] Stick right turns the dome clockwise seen from above
- [ ] CH4 subtrim set: no creep with the stick centred
- [ ] Alive: the dome finds front, then looks around and returns to front
- [ ] Stick overrides the dome; driving brings the dome to front
- [ ] SwB: sound and gesture in Alive, front-holo flick in Aware
- [ ] Transmitter off: wheels stop, R2 goes Still
```

5. **`README.md`.** In the **Features** table, replace the rows for random dome (SwA), mood sound, mood and random chatter (SwC) with:

```markdown
| Behaviour level: Still, Aware, Alive | SwC |
| R2 looks around: dome, holos and sounds together | SwC down |
| Play a mood sound, with a gesture | SwB |
| Mood: sets what R2 says and how it moves | VrA |
```

   Keep the Drive, Dome rotation, Volume and Lights rows.

- [ ] **Step 7: Update `AstroPixelsPlus/HDR2D2.md`**

1. In the opening table of our files, replace it with:

```markdown
| File | Purpose |
| --- | --- |
| `R2Logic.h` | iBUS decoding, switches, dials, volume |
| `R2Mood.h` | Mood profiles: every behaviour number, one row per mood |
| `R2Eyes.h` | Holo eyes: leading front eye, independent top and rear eyes, per-axis direction flips |
| `R2DomeTracker.h` | Dome angle from the body magnet ring, learned dome speeds |
| `R2DomeMotion.h` | Eased dome moves, stick pass-through |
| `R2Attention.h` | When R2 looks, where, what it says, SwB gestures, levels and interruptions |
| `R2Remote.h` | Connects the modules to the ESP32 pins, the DFPlayer, the holo servos and saved settings |
```

2. Replace the **Behaviour** section, including its dome state machine subsection, with:

```markdown
## Behaviour

| Input | Channel | Behaviour |
| --- | --- | --- |
| Left stick X | CH4 | Dome follows the stick. Always overrides behaviour. |
| SwC | CH5 | Up Still, middle Aware, down Alive |
| SwB | CH6 | Flip down: a mood sound at any level; a gesture in Aware and Alive |
| VrA | CH7 | Mood: mixed, general, chat, happy, sad, whistle |
| VrB | CH8 | Volume 0–30, saved 3 seconds after the last change |
| Right stick | CH1, CH2 | Read only to detect driving: more than 40 µs from centre |

The full design is in `docs/superpowers/specs/2026-10-11-lifelike-behaviour-design.md`.

Before the first iBUS frame, R2 runs at Aware in the General mood and the dome receives no pulses.
```

3. In the **Pins** table, replace the GPIO5 row with:

```markdown
| 5 | Dome Hall sensor over the body magnet ring: active low, internal pull-up, falling-edge interrupt timestamps each magnet |
```

   Delete the paragraph about the front Hall sensor being latched.

4. Replace the **Tuning** table with:

```markdown
| Setting | Location |
| --- | --- |
| Every behaviour number per mood (look interval, angle, speed, holds, chances, holo range and speed) | `kMoodProfiles` in `R2Mood.h` |
| Holo servo ranges | Servo table (`servoSettings`) in `AstroPixelsPlus.ino` |
| Holo direction | `kEyeFlipH` / `kEyeFlipV` in `R2Eyes.h` |
| Stick deadband, final approach speed, step limit | `DomeMotion::defaults()` in `R2DomeMotion.h` |
| Front-pair gap ratio, overdue factor, default speed | `DomeTracker::defaults()` in `R2DomeTracker.h` |
| Gap between sounds, pause after the stick or driving, seek timeout | `kSoundGapMs`, `kResumeMs`, `kSeekTimeoutMs` in `R2Attention.h` |
| Mood bank sizes | `MP3_BANK1_SOUNDS` … `MP3_BANK5_SOUNDS` in `MarcduinoSound.h` |
```

5. In **Stock settings**:
   - Change the "Sound random min / max" row to `| Sound random min / max | Ignored: each mood sets its own timing |`.
   - Change the "Sound volume" row's status to `Used at power-on. The VrB dial replaces it once the radio is heard.`
   - Add `| Learned dome speeds | Saved by R2Remote under the key r2speeds, at most every 5 minutes |`.

6. In **Serial log**, replace the message table with:

```markdown
| Message | Meaning |
| --- | --- |
| `[R2] radio OK` / `[R2] radio lost` | iBUS frames started or stopped |
| `[R2] level still` / `aware` / `alive` | SwC moved |
| `[R2] mood <name>` | Active mood changed (`(mixed)` when the dial is on Mixed) |
| `[R2] behaviour rest` / `looking` / `returning` / `gesture` / `manual` | What the planner is doing |
| `[R2] dome position found` / `lost` | The magnet count is in step, or was lost |
| `[R2] dome looks off: ...` | Two seeks failed. SwC to Still resets. |
```

- [ ] **Step 8: Update the parts list**

Run this from the repository root. It edits items 9 and 10 in `Master_R2D2_BOM.xls`:

```bash
python3 - <<'EOF'
import re, html
p = 'Master_R2D2_BOM.xls'
s = open(p, encoding='utf-8').read()
U = {
    '9': {3: "Ordered / Acquired (1 used)",
          6: "One sensor on the dome underside over the body magnet ring: dome D-LOGIC 5V, output via dome shifter ch3 to ESP32 GPIO5 (AUX3). Counts magnets to track the dome. Second sensor not used."},
    '10': {2: "Dome Magnet Ring", 4: "13",
           5: "Neodymium disc magnets, 6x2mm or 10x2mm",
           6: "Glued on the body top surface on one circle, same face up: 12 every 30 degrees plus one 5 degrees clockwise of front. Print the template with tools/magnet_template.py. See DOME_SETUP.md section 5."},
}
def fix(row):
    m = re.search(r'<Data[^>]*>(.*?)</Data>', row, re.S)
    if not m: return row
    item = html.unescape(m.group(1)).strip()
    if item not in U: return row
    datas = list(re.finditer(r'(<Data[^>]*>)(.*?)(</Data>)', row, re.S))
    out = row
    for col, text in sorted(U.pop(item).items(), reverse=True):
        d = datas[col]
        out = out[:d.start(2)] + html.escape(text, quote=False) + out[d.end(2):]
    return out
s = re.sub(r'<Row.*?</Row>', lambda m: fix(m.group(0)), s, flags=re.S)
assert not U, U
open(p, 'w', encoding='utf-8').write(s)
EOF
```

Expected: no output, no assertion error.

- [ ] **Step 9: Check docs and run everything**

Run: `grep -n -E "SwA.*random|random dome|R2_FRONT_HALL|seekSpeedUs|DomeBrain|twitch" *.md AstroPixelsPlus/HDR2D2.md`
Expected: no matches. Fix any that appear.

Run: `grep -n -i -w -E "about|approximately|probably|likely|should|may|might|try|roughly|consider|perhaps|typically|usually|just" OPERATING_GUIDE.md TRANSMITTER_SETUP.md DOME_SETUP.md TROUBLESHOOTING.md AstroPixelsPlus/HDR2D2.md`
Expected: no matches. "About" in "Turns for about 20 seconds" is a match: change it to "for 20 seconds".

Run: `python3 -m unittest discover tests && pio run -d AstroPixelsPlus 2>&1 | tail -1`
Expected: all tests OK; `[SUCCESS]`.

- [ ] **Step 10: Commit**

```bash
git add tools/magnet_template.py tests/test_magnet_template.py OPERATING_GUIDE.md TRANSMITTER_SETUP.md DOME_SETUP.md WIRING.md wiring_reference.html TROUBLESHOOTING.md BRING_UP.md README.md AstroPixelsPlus/HDR2D2.md Master_R2D2_BOM.xls
git commit -m "docs: behaviour levels, moods and the dome magnet ring

Operating guide, transmitter setup (CH5 SwC, dome direction check),
dome setup (magnet ring, holo ranges, behaviour tests), wiring,
troubleshooting, bring-up and firmware notes updated. Printable magnet
ring template generator. Parts list: 13 magnets.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
