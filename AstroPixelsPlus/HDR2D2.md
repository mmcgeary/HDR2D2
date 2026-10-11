# Dome Firmware

This folder is stock [reeltwo/AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus) at commit `09ec5eb` (2026-01-11), plus two files of our own.

| File | Purpose |
| --- | --- |
| `R2Logic.h` | Hardware-free logic: iBUS decoding, dome state machine, switches, dials, mood sound selection. Unit-tested on a PC. |
| `R2Remote.h` | Connects `R2Logic.h` to the ESP32 pins, the DFPlayer and the holo servos |

## Changes from stock

Every change in a stock file is marked with a `HDR2D2` comment. `git diff a242c79 -- AstroPixelsPlus` lists them all.

| File | Change |
| --- | --- |
| `AstroPixelsPlus.ino` | Servo table: six holo servos on PCA9685 0x40 channels 0–5, 1000–2000 µs. Dome panel servos removed. |
| `AstroPixelsPlus.ino` | Sound module fixed to DFPlayer, ignoring saved settings |
| `AstroPixelsPlus.ino` | Marcduino serial input and pass-through off; Serial2 carries iBUS |
| `AstroPixelsPlus.ino` | Holo servos not assigned to the holo lights; `R2Remote` moves them |
| `AstroPixelsPlus.ino` | Stock random sound off; `R2Remote` plays mood chatter |
| `AstroPixelsPlus.ino` | Calls `r2Remote.setup()` and `r2Remote.loop()` |
| `MarcduinoSound.h` | DFPlayer plays `/mp3/NNNN.mp3` by number (`playMp3Folder`), not by copy order |
| `platformio.ini` | Library versions pinned |

## Behaviour

| Input | Channel | Behaviour |
| --- | --- | --- |
| Left stick X | CH4 | Dome servo follows the stick. Always overrides random dome. |
| SwA | CH5 | Down: random dome on |
| SwB | CH6 | Flip down: play one sound from the current mood |
| VrA | CH7 | Mood: mixed, general, chat, happy, sad, whistle (six equal dial zones) |
| VrB | CH8 | Volume 0–30. Saved 3 seconds after the last change. |
| SwC | CH9 | Up: chatter and holo twitches off, holos centred. Middle or down: on. |
| Right stick | CH1, CH2 | Read only to detect driving (±100 µs from centre) |

The channel numbers are the `R2_CH_*` defines at the top of `R2Remote.h`.

### Dome state machine (`DomeBrain`)

| State | Output | Leaves when |
| --- | --- | --- |
| Idle | Neutral | Random on, sticks quiet 3 s: to At front (sensor active) or Seeking |
| Seeking | Neutral + 80 µs | Sensor active: At front. 20 s: Fault. Random off: Idle. |
| At front | Neutral | 4–12 s pause, not driving: Turning away. Random off: Idle. |
| Turning away | Neutral ± 120 µs, random direction | 0.4–1.5 s: Looking. Random off or driving: Returning. |
| Looking | Neutral | 1–4 s, random off or driving: Returning |
| Returning | Neutral ∓ 80 µs (opposite direction) | Sensor active: At front. Timeout (3 × turn time + 5 s): Fault. |
| Fault | Neutral | Random off: Idle |
| Manual | Stick value | Stick centred: Idle |

- **Neutral** is the CH4 value whenever the stick is within ±80 µs of centre, so CH4 subtrim sets it.
- **Radio lost** (no iBUS frame for 300 ms): the firmware keeps sending the last neutral.
- **Before the first frame:** the firmware sends no pulses.

### Before the radio is heard

Random chatter and holo twitches run, as in stock. The dome servo receives no pulses.

## Pins

| GPIO | Use |
| --- | --- |
| 16 | iBUS in (Serial2, 115200) |
| 4 | Dome servo out (LEDC channel 15, 50 Hz) |
| 5 | Front Hall sensor in, active low, internal pull-up |
| 18 / 19 | DFPlayer serial (Serial1, 9600), stock |

## Build and flash

```bash
pio run -d AstroPixelsPlus                 # build
pio run -d AstroPixelsPlus -t upload       # flash (module out of the AstroPixels board)
pio device monitor -d AstroPixelsPlus      # serial monitor, 115200
```

After the first flash, type `#APZERO` in the serial monitor to clear settings saved by earlier firmware.

## Serial log

The module prints these messages at 115200 baud. Read them only with the module on USB and out of a powered AstroPixels board.

| Message | Meaning |
| --- | --- |
| `[R2] radio OK` / `[R2] radio lost` | iBUS frames started or stopped |
| `[R2] mood <name>` | VrA moved into a new mood zone |
| `[R2] dome <state>` | Dome state change |

Stock serial commands still work. Examples:

| Command | Effect |
| --- | --- |
| `$31` | Play happy sound 1 |
| `$s` | Stop sound |
| `#APWIFI0` | Turn Wi-Fi off |
| `#APZERO` | Clear saved settings |

## Tuning

| Setting | Location | Default |
| --- | --- | --- |
| Dome turn speed | `DomeBrain::Config::turnSpeedUs` in `R2Logic.h` | 120 µs |
| Dome seek and return speed | `seekSpeedUs` | 80 µs |
| Stick deadband | `deadbandUs` | 80 µs |
| Quiet time before random dome | `idleBeforeRandomMs` | 3000 ms |
| Pause at front | `pauseMinMs` / `pauseMaxMs` | 4000–12000 ms |
| Turn duration | `turnMinMs` / `turnMaxMs` | 400–1500 ms |
| Look duration | `lookMinMs` / `lookMaxMs` | 1000–4000 ms |
| Seek timeout | `seekTimeoutMs` | 20000 ms |
| Chatter interval | `MARC_SOUND_RANDOM_MIN` / `MAX` in `AstroPixelsPlus.ino` | 5–30 s |
| Holo twitch interval | `fHoloTwitch` in `R2Remote.h` | 1.5–4.5 s |
| Holo twitch travel | `updateHolos()` in `R2Remote.h` | 1350–1650 µs |
| Mood bank sizes | `MP3_BANK1_SOUNDS` … `MP3_BANK5_SOUNDS` in `MarcduinoSound.h` | 19, 18, 7, 4, 3 |

## Tests

```bash
python3 -m unittest discover tests
```

The tests compile `R2Logic.h` with the PC's C++ compiler. They cover iBUS decoding and resync, switches and dials, volume hysteresis, random timers, mood selection and every dome state transition.

## Stock features not used

- Marcduino serial input (Serial2 carries iBUS)
- Dome panel servos
- Holo servo commands such as `*RD` (`R2Remote` owns the holo servos)

Wi-Fi, the web page and the droid remote are stock and still present. `#APWIFI0` turns Wi-Fi off.
