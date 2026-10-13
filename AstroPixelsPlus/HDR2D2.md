# Dome Firmware

This folder is stock [reeltwo/AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus) at commit `09ec5eb` (2026-01-11), plus these files of our own.

| File | Purpose |
| --- | --- |
| `R2Logic.h` | iBUS decoding, switches, dials, volume |
| `R2Mood.h` | Mood profiles: every behaviour number, one row per mood |
| `R2Eyes.h` | Holo eyes: leading front eye, independent top and rear eyes, per-axis direction flips; holo lamps |
| `R2DomeTracker.h` | Dome angle from the body magnet ring, learned dome speeds, reading the magnet blips in order |
| `R2DomeMotion.h` | Eased dome moves, stick pass-through |
| `R2Attention.h` | When R2 looks, where, what it says, SwB gestures, levels and interruptions |
| `R2Remote.h` | Connects the modules to the ESP32 pins, the DFPlayer, the holo servos and saved settings |

## Changes from stock

Every change in a stock file is marked with a `HDR2D2` comment. `git diff a242c79 -- AstroPixelsPlus` lists them all.

| File | Change |
| --- | --- |
| `AstroPixelsPlus.ino` | Servo table: six holo servos on PCA9685 0x40 channels 0–5, 1000–2000 µs. Dome panel servos removed. |
| `AstroPixelsPlus.ino` | Sound module fixed to DFPlayer, ignoring saved settings |
| `AstroPixelsPlus.ino` | Marcduino serial input and pass-through off; Serial2 carries iBUS |
| `AstroPixelsPlus.ino` | Holo servos not assigned to the holo lights; `R2Remote` moves them |
| `AstroPixelsPlus.ino` | Stock random sound off; `R2Remote` plays mood chatter |
| `AstroPixelsPlus.ino` | Wi-Fi and the ESP-NOW droid remote off by default |
| `AstroPixelsPlus.ino` | Dome servo pin held low from the first line of `setup()` |
| `AstroPixelsPlus.ino` | Calls `r2Remote.setup()` and `r2Remote.loop()` |
| `AstroPixelsPlus.ino` | `updateAliveLighting()`: logic pauses and PSI wipe speed follow the mood; holo lamps take over a holo while lit |
| `MarcduinoSound.h` | `isVocalizing()`: true for 2 seconds after a sound starts (the DFPlayer's BUSY pin isn't wired) |
| `MarcduinoSound.h` | DFPlayer plays `/mp3/NNNN.mp3` by number (`playMp3Folder`), not by copy order |
| `MarcduinoSound.h` | DFPlayer ACKs off after start-up: each command takes 10 ms and never waits for a reply |
| `platformio.ini` | Library versions pinned |

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

## Pins

| GPIO | Use |
| --- | --- |
| 16 | iBUS in (Serial2, 115200) |
| 4 | Dome servo out (LEDC channel 15, 50 Hz) |
| 5 | Dome Hall sensor over the body magnet ring: active low, internal pull-up. A falling-edge interrupt timestamps each magnet; the loop also reads the level, so a dome that reverses over a magnet keeps its count. |
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
| `[R2] level still` / `aware` / `alive` | SwC moved |
| `[R2] mood <name>` | Active mood changed (`(mixed)` when the dial is on Mixed) |
| `[R2] behaviour rest` / `looking` / `returning` / `gesture` / `manual` | What the planner is doing |
| `[R2] dome position found` / `lost` | The magnet count is in step, or was lost |
| `[R2] dome looks off: ...` | Two seeks failed. SwC to Still resets. |
| `COMMAND: HPF098` (or `HPR`, `HPT`) | A holo lamp takes over that holo; its stock flicker pauses |
| `COMMAND: HPF099` (or `HPR`, `HPT`) | The lamp has faded out; stock flicker resumes |

Stock serial commands still work. Examples:

| Command | Effect |
| --- | --- |
| `$31` | Play happy sound 1 |
| `$s` | Stop sound |
| `#APWIFI0` | Turn Wi-Fi off |
| `#APZERO` | Clear saved settings |

## Tuning

| Setting | Location |
| --- | --- |
| Every behaviour number per mood (look interval, angle, speed, holds, chances, holo range and speed, logic pauses, PSI speed, holo lamp chance) | `kMoodProfiles` in `R2Mood.h` |
| Logic speed-up while speaking | `speakingDelay()` in `R2Mood.h`; its 2-second window is `kVocalizeMs` in `MarcduinoSound.h` |
| Holo lamp brightness and fades | `HoloLampFader` constants in `R2Eyes.h` |
| Longest front holo lamp per look | `R2_FRONT_LAMP_MAX_MS` in `R2Remote.h` |
| Holo servo ranges | Servo table (`servoSettings`) in `AstroPixelsPlus.ino` |
| Holo direction | `kEyeFlipH` / `kEyeFlipV` in `R2Eyes.h` |
| Stick deadband, final approach speed, step limit | `DomeMotion::defaults()` in `R2DomeMotion.h` |
| Front-pair gap ratio, gap growth limit, overdue factors, default speed and zero-speed offset, learning rate | `DomeTracker::defaults()` in `R2DomeTracker.h` |
| Gap between sounds, pause after the stick or driving, seek timeout | `kSoundGapMs`, `kResumeMs`, `kSeekTimeoutMs` in `R2Attention.h` |
| Mood bank sizes | `MP3_BANK1_SOUNDS` … `MP3_BANK5_SOUNDS` in `MarcduinoSound.h` |

## Tests

```bash
python3 -m unittest discover tests
```

The tests compile the `R2*.h` modules with the PC's C++ compiler. They cover iBUS decoding, switches and dials, moods, the holo eyes, the magnet tracker against a simulated dome, eased dome motion, and the attention planner's sequences and priorities.

The virtual droid test runs R2Remote's loop for 30 simulated minutes per level and mood, Mixed included. The simulated dome has a Hall arc around each magnet, a speed lag and a servo deadband, and the run includes stick turns, driving, SwB, a Still flip and radio loss. It checks:
- tracking within 5° once speeds are learned,
- every return resting within 3° of front,
- no lost position, the 10 µs step limit and the 4-second sound gap,
- Still and Aware never moving the dome on their own.

`R2Remote.h` itself (pins, interrupt, servo output, saved settings) runs only on the droid.

## Wi-Fi

Wi-Fi, the web page, OTA updates and the ESP-NOW droid remote are off by default. Nothing in the droid needs them.

| Command (serial monitor) | Effect |
| --- | --- |
| `#APWIFI1` | Wi-Fi on (access point `AstroPixels`, password `Astromech`). The board restarts. |
| `#APWIFI0` | Wi-Fi off. The board restarts. |
| `#APREMOTE1` / `#APREMOTE0` | ESP-NOW droid remote on / off |

Change the Wi-Fi password on the web page's **WiFi** tab the first time you turn Wi-Fi on.

## Stock settings

Settings saved through the web page or Marcduino commands:

| Setting | Status |
| --- | --- |
| Sound random min / max | Ignored: each mood sets its own timing |
| Sound volume | Used at power-on. The VrB dial replaces it once the radio is heard. |
| Sound startup track | Used |
| Learned dome speeds | Saved by R2Remote under the key r2speeds (a speed and the servo offset it was measured at, per band), at most every 5 minutes |
| Sound player, sound serial | Ignored: always DFPlayer on Serial1 |
| Random sound on/off | Ignored: SwC controls chatter |
| Marcduino serial baud, pass-through, JawaLite pass-through | Ignored: Serial2 carries iBUS |

## Stock features not used

- Marcduino serial input (Serial2 carries iBUS)
- Dome panel servos
- Holo servo commands such as `*RD` (`R2Remote` owns the holo servos)
