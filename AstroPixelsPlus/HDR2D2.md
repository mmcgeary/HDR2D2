# Dome Firmware (AstroPixelsPlus + R2Remote)

This folder is stock [reeltwo/AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus) at commit `09ec5eb` (2026-01-11), plus two files of our own:

| File | What it does |
| --- | --- |
| `R2Logic.h` | Hardware-free logic: iBUS decoding, dome behaviour, dials, mood sound picking. Unit-tested on a PC. |
| `R2Remote.h` | Connects that logic to the ESP32 pins, DFPlayer and holo servos |

Every change to the stock files is marked `HDR2D2`:

- `AstroPixelsPlus.ino`:
  - holo servos on PCA9685 0x40 channels 0–5, range 1000–2000us
  - DFPlayer always on
  - no Marcduino serial input (Serial2 now carries iBUS)
  - random chatter comes from R2Remote
  - calls into R2Remote from `setup()` and the main loop
- `MarcduinoSound.h`: the DFPlayer plays `/mp3/NNNN.mp3` by filename instead of by copy order.
- `platformio.ini`: library versions pinned.

`git diff a242c79 -- AstroPixelsPlus` shows every change against stock.

## Build and flash

Needs [PlatformIO](https://platformio.org/).

```bash
pio run -d AstroPixelsPlus
```

Flash with the ESP32 module **removed from the AstroPixels board**, then refit it with all power off:

```bash
pio run -d AstroPixelsPlus -t upload
```

The first time, clear settings saved by earlier firmware. Open the serial monitor:

```bash
pio device monitor -d AstroPixelsPlus
```

Type `#APZERO` and press Enter. The board reboots with stock defaults.

## SD card

Format the card FAT32. Put the sounds in a folder named `mp3` with four-digit names. Text after the number is allowed, for example `0051 happy beep.mp3`.

A sound's number is `(bank − 1) × 25 + position in the bank`:

| Bank | Mood | Files | Sounds expected |
| --- | --- | --- | --- |
| 1 | General | `0001`–`0019` | 19 |
| 2 | Chat | `0026`–`0043` | 18 |
| 3 | Happy | `0051`–`0057` | 7 |
| 4 | Sad | `0076`–`0079` | 4 |
| 5 | Whistle | `0101`–`0103` | 3 |
| 6 | Scream | `0126`… | `0126` scream, `0128` short circuit |
| 7 | Leia | `0151` | |
| 8 | Music | `0176`… | `0177` Star Wars, `0178` Imperial March, `0180` Cantina |
| — | Start-up | `0255` | Plays at power-up |

Moods pick only from the counts in `MP3_BANK1_SOUNDS` … `MP3_BANK5_SOUNDS` in `MarcduinoSound.h`. If you have a different number of sounds in a bank, change that count.

## Transmitter mapping

See the [Controller Guide](../CONTROLLER_GUIDE.md). The channel numbers are the `R2_CH_*` defines at the top of `R2Remote.h`.

Before the receiver is heard, the dome behaves like stock: random chatter and holo movement on, dome servo silent (no pulses).

## Serial log

At 115200 baud the board reports:

| Message | Meaning |
| --- | --- |
| `[R2] radio OK` / `[R2] radio lost` | iBUS frames started or stopped (more than 300ms gap) |
| `[R2] mood happy` | VrA dial moved to a new mood |
| `[R2] dome seeking front` / `at front` / `turning away` / `looking` / `returning` | Random dome steps |
| `[R2] dome manual` | Left stick moved |
| `[R2] dome FAULT ...` | Front sensor not found in time. Flip SwA up to reset. |

Stock commands still work from the serial monitor. For example, `$31` plays happy sound 1 and `$s` stops sound.

## Adjusting behaviour

| What | Where |
| --- | --- |
| Dome speeds, pauses, timeouts, stick deadband | `DomeBrain::Config` in `R2Logic.h` |
| Chatter interval | `MARC_SOUND_RANDOM_MIN` / `MAX` in `AstroPixelsPlus.ino` (5–30s) |
| Holo twitch interval and travel | `fHoloTwitch` and `updateHolos()` in `R2Remote.h` |
| Pins and channels | Defines at the top of `R2Remote.h` |

## Tests

```bash
python3 -m unittest discover tests
```

These compile `R2Logic.h` on the PC and check iBUS decoding, switches and dials, mood picking and the dome behaviour.

## Not used from stock

- **Marcduino serial input:** Serial2 carries iBUS instead.
- **Dome panel servos:** there are no panels.
- **Holo servo commands such as `*RD`:** R2Remote moves the holo servos.

Wi-Fi (access point `AstroPixels`, password `Astromech`) and the web page are still stock. They aren't needed; `#APWIFI0` in the serial monitor turns Wi-Fi off.
