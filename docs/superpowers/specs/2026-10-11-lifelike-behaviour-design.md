# Lifelike Dome and Holo Behaviour: Design

Branch: `simplified-droid`. Firmware: `AstroPixelsPlus/` (stock AstroPixelsPlus + R2Remote).

## 1. Goal

R2 behaves like a movie droid that is aware of its surroundings. It looks around from time to time, as if something caught its attention, and comments on what it sees. Movement is never random twitching.

Success means:

- Every dome and holo move eases in and out. Nothing snaps.
- Each dome look has a purpose: turn toward a point, hold, sometimes readjust or double-take, return.
- The front holo leads the dome like an eye leading a head. The top and rear holos roam independently, like a chameleon's eyes.
- The mood dial sets the personality of both movement and sound.
- R2 is mostly calm.
- The left stick and driving always take priority.

## 2. Controls

| Control | Channel | Function |
| --- | --- | --- |
| Right stick | CH1, CH2 | Drive (Elevon mix to the VESCs). Read by the ESP32 only to detect driving. |
| Left stick X | CH4 | Dome. Always overrides behaviour. Stick right turns the dome clockwise seen from above. |
| **SwC** (3-position) | **CH5** | Behaviour level: **Up = Still**, **Middle = Aware**, **Down = Alive** |
| SwB | CH6 | Flip down: play one sound from the current mood, at any level. In Aware and Alive, a gesture goes with it. |
| VrA | CH7 | Mood: mixed, general, chat, happy, sad, whistle |
| VrB | CH8 | Volume |
| SwA | CH9 | Spare |
| SwD | CH10 | Spare |

Behaviour levels:

| Level | Dome | Holos | Autonomous sound |
| --- | --- | --- | --- |
| Still | Stick only | Centred, not moving | None |
| Aware | Stick only, stays front | Roaming; front holo makes "looks" | Yes |
| Alive | Looks around | Roaming; front holo leads the dome | Yes |

Receiver failsafe: CH1, CH2, CH4 centred; **CH5 (SwC) Up**. Signal loss therefore puts R2 in Still. The current failsafe on CH5 (SwA) moves to SwC on the same channel.

Before the first iBUS frame arrives, R2 runs at Aware with the mood set to General.

## 3. Hardware change: magnet ring

- **13 disc magnets** (6×2mm or 10×2mm) glued flat on the body's top surface, on one circle, all with the same face up:
  - 12 at 30° spacing, the first at front (0°),
  - one extra 5° clockwise of the front magnet.
- **The existing front Hall sensor** (KY-003, GPIO5 through level shifter channel 3) moves to the dome's underside, over that circle, with a gap of 5mm or less.
- No new wiring, boards or slip ring contacts.
- A printable template is generated from the circle's diameter (§10).

Setup check: push the left stick right; the dome turns clockwise seen from above (R2 turns its head to its right). If it turns the other way, reverse CH4 on the transmitter.

Angle convention used throughout: 0° = front, positive = clockwise seen from above, range −180° to +180°.

## 4. Dome position: `R2DomeTracker.h`

**Input:**
- blip timestamps from the Hall sensor,
- the current servo command (direction and offset from neutral),
- the time.

**Output:**
- the dome angle and whether it is known,
- the learned speed for any servo offset.

Blip capture:
- A falling-edge interrupt writes `micros()` into an 8-entry ring buffer. The main loop drains it.
- Blips closer than 8ms to the previous one are bounce and are dropped.

Front detection:
- A gap shorter than 0.4 × the previous gap marks the front pair.
- At that blip the angle is set to +5° when turning clockwise, and 0° when turning anticlockwise.

Normal blips:
- The predicted angle snaps to the nearest magnet position (multiples of 30°, plus +5°).
- If the prediction is more than 15° from every magnet position, the position becomes unknown.

Between blips: angle += direction × learned speed × elapsed time.

Speed learning:
- Each 30° gap crossed while the servo command stayed constant is one measurement: 30° / gap time.
- Measurements are kept per direction and per 10µs band of offset from neutral (bands 20–300µs), as an exponential average (α = 0.3).
- Bands with no measurement interpolate between learned neighbours. With none learned, the default is 0.5°/s per µs.
- The table is saved to preferences when it has changed by more than 10%, at most once every 5 minutes.

The position becomes unknown when:
- at power-up,
- a magnet is overdue: moving, and no blip within 2.5 × the expected gap,
- a snap mismatch (above).

While unknown, the next automatic move is a seek: a slow turn (20°/s) until the front pair is seen.
- A seek gives up after 1.2 full turns' worth of time.
- Two failed seeks in a row disable automatic dome movement until SwC passes through Still.

The stick still works while the position is unknown.

## 5. Dome motion: `R2DomeMotion.h`

**Input:**
- a request: hold neutral, go to a target angle, seek, or manual stick value,
- the tracker state,
- the cruise speed and ease time from the mood.

**Output:** a servo pulse in µs, or 0 for no pulses.

Behaviour:
- **Manual:**
  - The stick is more than 80µs from 1500: the stick value passes straight through.
  - Within the deadband: neutral, which is the centred stick value, so CH4 subtrim sets it.
- **Go to target:**
  - Target velocity ramps from 0 to cruise over the ease time, using a sine-shaped curve.
  - Deceleration starts when the remaining angle is no more than the braking distance (cruise × ease time / 2).
  - The final approach is held at 15°/s or more.
  - The move ends within 3° of the target, or when the target is passed.
  - Velocity converts to a servo offset through the tracker's learned speed table. Offsets below 40µs are raised to 40µs while moving (servo deadband).
- **Limits:**
  - Targets are clamped to ±135°.
  - Moves always take the short way.
  - The command changes by at most 10µs per 20ms update, except when the stick takes over or the radio is lost.
- **Return to front:** a go-to-target of 0° whose last 15° runs at 15°/s. The front pair confirms arrival.
- **Radio lost** (no iBUS frame for 300ms): the last neutral, immediately.
- **No pulses** before the first iBUS frame.

## 6. Holos: `R2Eyes.h`

Each holo has a horizontal and a vertical servo. A holo position is (h, v), each from −1 to +1.

Servo ranges:
- They come from the servo table in `AstroPixelsPlus.ino`: 1000–2000 µs, inside stock AstroPixelsPlus's 800–2200 µs.
- Eye positions map onto that range with ReelTwo's `moveTo(num, startDelay, moveTime, pos)`, pos 0..1, the same way ReelTwo's HoloLights does: 0 = right / up, 1 = left / down.
- `kEyeFlipH` / `kEyeFlipV` in `R2Eyes.h` flip an axis for a mechanism mounted the other way round.
- No calibration step.

Every holo move:
- Both servos of a holo move together, with the same start delay and duration.
- Easing: SineEaseInOut, set once per servo with `servoDispatch.setServoEasingMethod`.
- Duration = (600ms + 900ms × d) / mood holo speed, where d = the larger of the two axis changes ÷ 2, giving 0 to 1.

**Front holo: leading eye**

| Situation | Behaviour |
| --- | --- |
| A look starts (Alive) | 400–600ms before the dome moves, the eye moves to h = ±0.6 × mood range toward the target side, v = random ±0.2 |
| During the hold | Every 1.5–3s, a micro-adjust of ±0.1 on each axis |
| The return starts | The eye eases to centre, ahead of the dome |
| A look in Aware | The same eye sequence, with no dome |
| Between looks | Roams like the chameleon eyes at half range, with holds ×1.5 |
| SwB pressed | A quick flick to (0, +0.2) for 600ms, then resume |

**Top and rear holos: chameleon eyes**
- Each has its own random timer and state, independent of everything else.
- The cycle:
  1. Pick a point within ±(mood range) on each axis, plus the mood's vertical bias.
  2. Move there.
  3. Hold for a time from the mood's range.
  4. Then do one of:
     - 25%: a follow-up micro-adjust (±0.1), then hold again,
     - 20%: return to centre and rest for one hold time,
     - otherwise: pick a new point.

**Still:** all six servos ease to centre and stop.

**Driving:** holo hold times ×1.5 and range ×0.7 (calmer).

## 7. Mood profiles: `R2Mood.h`

One row per mood, plain numbers. These are starting values, tuned on the droid.

| Field | General | Chat | Happy | Sad | Whistle |
| --- | --- | --- | --- | --- | --- |
| Look interval (s) | 12–35 | 10–28 | 6–18 | 30–75 | 10–30 |
| Look angle (°) | 25–100 | 20–80 | 20–120 | 15–60 | 25–90 |
| Dome cruise (°/s) | 45 | 50 | 75 | 25 | 50 |
| Dome ease time (s) | 0.6 | 0.5 | 0.35 | 1.0 | 0.5 |
| Hold (s) | 2–6 | 2–5 | 1–3 | 3–8 | 1.5–4 |
| Readjust chance | 30% | 25% | 35% | 10% | 20% |
| Double-take chance | 15% | 10% | 20% | 5% | 10% |
| Chain to a second look | 15% | 20% | 25% | 5% | 10% |
| Glance-to-front during hold | 0% | 0% | 0% | 0% | 50% |
| Notice sound chance | 25% | 40% | 40% | 15% | 30% |
| Comment sound chance | 20% | 60% | 40% | 15% | 30% |
| Lone remark interval (s) | 40–90 | 20–50 | 25–60 | 60–150 | 30–70 |
| Holo range | 0.5 | 0.55 | 0.8 | 0.4 | 0.6 |
| Holo hold (s) | 3–8 | 3–7 | 1.5–4 | 5–12 | 2–6 |
| Holo speed | 1.0 | 1.0 | 1.4 | 0.6 | 1.0 |
| Holo vertical bias | 0 | 0 | +0.1 | −0.4 | 0 |
| Sound bank | 1 | 2 | 3 | 4 | 5 |
| SwB gesture | Glance | Half-turn toward front | Wiggle | Slow turn away | Face front |

**Mixed** picks one of the five rows at random, and picks again every 1–3 minutes. Sounds then come from that row's bank.

Gesture definitions (Alive only; Aware gets the front-holo flick only). With the dome position unknown, every gesture is the front-holo flick only:
- **Glance:** the dome turns ±20° (random side) and comes back.
- **Half-turn toward front:** the dome turns half the way toward 0°. Within 20° of front, it does a Glance instead.
- **Wiggle:** the dome makes ±10° twice, quickly (ease 0.2s).
- **Slow turn away:** the dome turns 45° further from front (random side when at front) at 20°/s, holds 1s and returns.
- **Face front:** the dome returns to 0°.

## 8. Attention planner: `R2Attention.h`

**Input:**
- behaviour level and mood row,
- the stick state and driving,
- the tracker state,
- SwB press events,
- the time.

**Output, each update:**
- a dome request,
- front-eye cues,
- a sound request (bank, or none),
- the chameleon eyes' calm flag.

A look:
1. **Notice** (chance from the mood): a sound request, then wait 0.5s.
2. **Lead:** the front-eye cue fires.
3. **Turn:** after 400–600ms, the dome goes to the target. The target is ± (look angle range), with a random side.
4. **Hold:** for the hold time. During the hold, at most one of:
   - readjust: ±10–15°,
   - double-take: back 40% toward the previous angle, pause 0.3s, then back to the target,
   - glance-to-front: turn 30° toward front and return,
   - comment sound.
5. **Then:** chain to a second look (new target, no return), or return to front.
6. **Rest:** wait for the next look interval.

In Aware, steps 3 and 5 do nothing to the dome. The front eye and sounds still run.

Lone remarks run on their own timer at Aware and Alive.

Sounds:
- Every sound request keeps a gap of at least 4s from the previous sound.
- SwB plays straight away, at any level, and restarts the gap.

Priorities, highest first:
1. **Radio lost:** dome to neutral, behaviour as Still.
2. **Still:** the dome returns to front if it was mid-look, then holds. All eyes rest. No autonomous sound.
3. **Left stick:** manual, immediately. The planner pauses. It resumes 3s after the stick is centred, first returning to front if it was mid-look.
4. **Driving** (CH1 or CH2 more than 40µs from centre): any look becomes a return to front. New looks wait until 3s after driving stops. Eyes go calm.
5. **SwB gesture.**
6. **Planned looks.**

## 9. Glue: `R2Remote.h`

- Reads iBUS, SwC level, mood and volume (unchanged), and SwB.
- The Hall interrupt fills the blip ring buffer.
- Runs the tracker, attention, motion and eyes each loop.
- Writes the dome LEDC pulse.
- Issues `servoDispatch.moveToPulse(num, startDelay, moveTime, pulse)` for eye moves.
- Plays sounds through `sMarcSound.playSound(bank, sound)`, choosing the sound within the bank as now.
- Persists the learned dome speeds and the volume.
- Prints state changes on serial.

Removed:
- `DomeBrain`, `RandomTimer`'s holo use and the single-servo holo twitch.
- The SwA random-dome switch and the SwC chatter switch, replaced by the SwC level.

## 10. Documentation and parts

| Document | Change |
| --- | --- |
| `OPERATING_GUIDE.md` | Controls (SwC levels, SwA spare), behaviour description, moods |
| `TRANSMITTER_SETUP.md` | Channel map (CH5 = SwC, CH9 = SwA), failsafe (CH5 Up), CH4 direction check |
| `DOME_SETUP.md` | Magnet ring and sensor mounting, holo range check, behaviour tests per level |
| `WIRING.md`, `wiring_reference.html` | Hall sensor location note (no wiring change) |
| `AstroPixelsPlus/HDR2D2.md` | New modules, state descriptions, tuning tables |
| `Master_R2D2_BOM.xls` | 13 magnets |
| `BRING_UP.md`, `TROUBLESHOOTING.md` | Updated checks |

New file `tools/magnet_template.py` writes a printable SVG for a given circle diameter:
- 12 marks at 30°,
- the extra mark at +5°,
- an arrow for front,
- a 100mm scale bar to check the print scaling.

## 11. Testing

Unit tests, run on the PC (`python3 -m unittest discover tests`):

| Module | Tests |
| --- | --- |
| Tracker | A simulated dome passes simulated magnets in both directions at changing speeds. The angle error stays below 5° after the first front pass. Front is detected both ways. A missed magnet makes the position unknown. Bounce is dropped. Speed learning converges. |
| Motion | The velocity profile is monotonic up and down. It stops within 3° of target. Command steps stay ≤ 10µs per update. The ±135° clamp and the short-way rule hold. The stick passes through. No pulses before radio. |
| Eyes | Both axes always share a duration. Positions stay within −1..+1. Still centres everything. Chameleon timers are independent. The front-eye lead precedes the dome start. |
| Mood | Every row is complete. Ranges are ordered min ≤ max. Mixed switches within 1–3 minutes. |
| Attention | Deterministic random gives the expected look sequence. Each interruption gets the right priority. The 4s sound gap holds. SwB works at every level. |
| Virtual droid | 30 simulated minutes per mood and level, checking every invariant. |

Hardware tuning on the droid:
- mood numbers,
- the 0.4 front-gap ratio,
- the 40µs servo deadband.

## 12. Build order

Each step is committed with its tests and works on its own.

1. **Holos:** `R2Eyes.h`, eased moves, chameleon eyes. Mood is read from the dial with fixed defaults.
2. **Dome:** `R2DomeTracker.h` and `R2DomeMotion.h`, replacing `DomeBrain`, with the existing simple random looks running on the new motion.
3. **Behaviour:** `R2Mood.h`, `R2Attention.h`, the sound link, SwB gestures, SwC levels on CH5.
4. **Docs, parts list and magnet template.**

## 13. Out of scope

- Holo LED behaviour (stays stock).
- Choreographed routines (Scream, Leia and others).
- An absolute dome encoder.
- Changes to driving.
