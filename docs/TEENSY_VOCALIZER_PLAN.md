# Teensy 4.1 HCR Vocalizer & Mood-Reactive Lighting Implementation Plan

**Target Repository:** `HDR2D2`  
**Base Branch:** `origin/simplified-droid`  
**Working Branch:** `feature/teensy-vocalizer-alive`  
**Status:** Architecture approved, ready for implementation  

---

## 1. Executive Summary & Objectives

This plan transitions HDR2D2 from static DFPlayer Mini MP3 sound playback to a **Teensy 4.1 running Human-Cyborg Relations (HCR) Vocalizer** firmware, while tying the vocalizer's real-time state and mood into the droid's lighting subsystems (PSIs, FLD, RLD, and Holoprojectors) to bring R2 to life.

### Core Architectural Decisions
1. **Sound Engine:** Teensy 4.1 + PJRC Audio Adapter (SGTL5000) running stock HCR Vocalizer firmware connected via `SOUND_SERIAL` (`Serial1` on ESP32, 9600 baud, pins `PIN_AUX4` / `PIN_AUX5`).
2. **Autonomous Musing vs. Stimuli:**
   - In **General (Mood 1)**: Native autonomous musing is enabled via `<OR,M1,MN12,MX45>`.
   - In **Active Moods (Chat, Happy, Sad, Whistle)**: Autonomous musing is turned off (`<M0>`), and mood-appropriate procedural stimuli commands (`<SH#>`, `<SS#>`, `<SC#>`, etc.) are fired on intervals or events.
3. **Bi-Directional Telemetry:**
   - Query `<QPV>` ("Is Vocalizing?") polled periodically (~100ms during active states).
   - Query `<QT>` ("Get Duration") upon triggering procedural speech.
   - Parse responses `<QPV,0>`, `<QPV,1>`, `<QT,duration>` to drive synchronized lighting effects.
4. **PSIs (Approach 2 - Canon Wipe Modulation):**
   - Retain the iconic sliding color wipe as baseline:
     - **Front PSI:** Sliding Red $\leftrightarrow$ Blue.
     - **Rear PSI:** Sliding Yellow $\leftrightarrow$ Green.
   - Modulate sweep speed by current mood (faster sweeps during Happy/Chat, slow crawl during Sad/Sleep).
   - Temporary emotional overrides during speech/events (e.g., rapid flashing red during angry/overload stimuli, immediately resuming sliding wipe when vocalization completes).
5. **Logics (FLD & RLD):**
   - **Movie Canon Colors Preserved 100%:**
     - **Front Logic (FLD):** Blue and White only.
     - **Rear Logic (RLD):** Red, Yellow, and Green only.
   - Modulate flicker delay (`fDelay`): Rapid energetic flicker when vocalizing or excited; lazy, slow-paced cycling when sad, resting, or quiet.
6. **Holoprojectors (Front, Rear, Top):**
   - All three eyes (`frontHolo`, `rearHolo`, `topHolo` on pins 25, 26, 27) share the `R2Eyes.h` look/hold lifecycle.
   - When **any** eye enters a deliberate inspection hold ($\ge 1.5$s), roll a mood-scaled chance (e.g., Chat: 35%, General: 15%, Sad: 0%).
   - If triggered: gently fade in a pale inspection beam (~25% brightness) over 250ms, sustain through the hold, and smoothly fade out over 350ms.

---

## 2. Pinout & Hardware Architecture

| Component | ESP32 Pin / Interface | Protocol / Notes |
| :--- | :--- | :--- |
| **Teensy 4.1 RX** | `PIN_AUX4` (ESP32 TX) | 9600 baud serial (HCR ASCII protocol `<...>`) |
| **Teensy 4.1 TX** | `PIN_AUX5` (ESP32 RX) | 9600 baud serial telemetry query responses |
| **Front Holo LED** | Pin 25 | PWM fade / pulse |
| **Rear Holo LED** | Pin 26 | PWM fade / pulse |
| **Top Holo LED** | Pin 27 | PWM fade / pulse |
| **Front PSI** | Marcduino / Neopixel bus | Standard sliding wipe Red/Blue |
| **Rear PSI** | Marcduino / Neopixel bus | Standard sliding wipe Yellow/Green |
| **FLD / RLD** | Marcduino / Logic bus | Canon colors, variable flicker delay |

---

## 3. Detailed Component Specifications

### 3.1. HCR Command & Telemetry Specification (`AstroPixelsPlus/R2Logic.h`)

#### Outgoing Commands
- **Mood Configuration (`hcrMoodSetup`):**
  - General (1): `OR,M1,MN12,MX45` (Override reset, Musing ON, min 12s, max 45s)
  - Chat (2): `M0,OR,OH30,OC20` (Musing OFF, 30% Happy, 20% Conflict)
  - Happy (3): `M0,OR,OH80` (Musing OFF, 80% Happy)
  - Sad (4): `M0,OR,OS85` (Musing OFF, 85% Sad)
  - Whistle (5): `M0,OR,OH95` (Musing OFF, 95% Happy)
- **Procedural Stimuli (`hcrStimulus`):**
  - General: `variation ? "SH0" : "MM"` (Short happy or manual muse trigger)
  - Chat: `variation ? "SH0" : "SC0"` (Short happy or conversational blip)
  - Happy: `variation ? "SH1" : "SH0"` (Happy phrase 1 or 0)
  - Sad: `variation ? "SS1" : "SS0"` (Sad whimper 1 or 0)
  - Whistle: `variation ? "CA0901C1020" : "SH1"`
- **Telemetry Query:**
  - `QPV` -> Queries vocalization state. Response: `<QPV,0>` or `<QPV,1>`.
  - `QT` -> Queries sound duration in seconds. Response: `<QT,1.42>`.

#### Telemetry Parser (`HcrTelemetryParser`)
- Stream parser reading characters between `<` and `>`.
- Parses token key (`QPV`, `QT`, `QE`).
- Updates internal state:
  ```cpp
  bool isVocalizing() const;
  float currentDuration() const;
  ```

---

### 3.2. Mood Profiles (`AstroPixelsPlus/R2Mood.h`)

Extend `MoodProfile` to include lighting speeds and chances:

```cpp
struct MoodProfile {
    uint8_t id;
    const char* name;
    // Eye movement & sound timing
    uint16_t soundIntervalMinMs;
    uint16_t soundIntervalMaxMs;
    uint16_t eyeIntervalMinMs;
    uint16_t eyeIntervalMaxMs;
    // Alive lighting parameters
    uint16_t psiSpeedMs;       // Speed per step of sliding wipe
    uint8_t  fldDelay;         // Front Logic flicker delay (lower = faster)
    uint8_t  rldDelay;         // Rear Logic flicker delay
    uint8_t  holoHoldChancePct;// Probability (0-100) of illuminating during inspection hold
};
```

Values per mood:
- **General (1):** `psiSpeedMs = 120`, `fldDelay = 5`, `rldDelay = 5`, `holoHoldChancePct = 15`
- **Chat (2):** `psiSpeedMs = 80`, `fldDelay = 3`, `rldDelay = 3`, `holoHoldChancePct = 35`
- **Happy (3):** `psiSpeedMs = 60`, `fldDelay = 2`, `rldDelay = 2`, `holoHoldChancePct = 25`
- **Sad (4):** `psiSpeedMs = 250`, `fldDelay = 12`, `rldDelay = 12`, `holoHoldChancePct = 0`
- **Whistle (5):** `psiSpeedMs = 50`, `fldDelay = 2`, `rldDelay = 2`, `holoHoldChancePct = 40`

---

### 3.3. Holoprojector Inspection Lamp (`AstroPixelsPlus/R2Eyes.h`)

Add `HoloLampFader`:
- Symmetrically monitors look/hold state on Front, Rear, and Top holoprojectors.
- State machine per lamp: `IDLE` -> `FADE_IN` (250ms) -> `HOLD` -> `FADE_OUT` (350ms).
- Max brightness: soft pale beam (~64 out of 255 PWM, or ~25%).
- Trigger condition: Target eye entered a deliberate inspection hold with `durationMs >= 1500`, and `random(100) < mood.holoHoldChancePct`.

---

### 3.4. Sound Manager & Integration (`AstroPixelsPlus/MarcduinoSound.h` & `.ino`)

1. Default sound hardware type set to `MarcSound::kHCR`.
2. Telemetry polling inside `sMarcSound.idle()`:
   - Every 100ms when an utterance was recently triggered or awaiting completion, send `<QPV>`.
   - Update `sMarcSound.isVocalizing()`.
3. In main loop (`AstroPixelsPlus.ino`):
   - Modulate FLD/RLD delay dynamically: when `sMarcSound.isVocalizing()` is true, temporarily reduce `fDelay` by 50% for high-speed chatter, restoring mood baseline when quiet.
   - Modulate PSI sweep speed or trigger temporary angry/alarm override if an error or alarm stimulus occurs.

---

## 4. Implementation Steps & Verification

### Step 1: Verify Environment & Branch
Run in repository root:
```bash
git checkout feature/teensy-vocalizer-alive
python3 -m unittest discover tests
```
Ensure all existing tests pass before making any changes.

### Step 2: Implement C++ Packet Generators & Parsers (`R2Logic.h`)
- Implement `HcrCommand::stimulus()`
- Implement `HcrCommand::moodSetup()`
- Implement `HcrTelemetryParser::feed(char c)`

### Step 3: Add Unit Tests (`tests/test_r2_logic.py`)
- Test string output for all moods and stimuli.
- Test stream parsing of `<QPV,0>`, `<QPV,1>`, `<QT,2.35>` with edge cases (malformed input, split packets, noise).
- Run `python3 -m unittest discover tests`.

### Step 4: Extend Mood Profiles (`R2Mood.h`)
- Add PSI and Logic flicker parameters to `MoodProfile`.
- Add test in `tests/test_r2_mood.py` verifying each mood has valid timing parameters.

### Step 5: Implement `HoloLampFader` (`R2Eyes.h`)
- Implement linear or smooth fade curves for holo lamps.
- Add unit test verifying fade timing and hold triggering across front, rear, and top eyes.

### Step 6: Update `MarcduinoSound.h` & Wire into `AstroPixelsPlus.ino`
- Implement robust Teensy HCR command dispatch.
- Wire `<QPV>` polling loop.
- Connect telemetry state to FLD, RLD, and PSI controllers.

### Step 7: Final Regression Testing
- Run complete test suite: `python3 -m unittest discover tests`.
- Verify clean compilation with zero warnings (`-Wall -Wextra -Werror`).
