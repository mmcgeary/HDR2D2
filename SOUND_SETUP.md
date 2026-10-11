# Sound Setup

The DFPlayer, amplifier and speaker are in the body. The dome ESP32 controls the DFPlayer through slip ring CH4/CH5.

## 1. Prepare the SD card

1. Use a microSD card of 32GB or less, formatted **FAT32**.
2. Make a folder named `mp3` at the top level of the card.
3. Name every sound with a four-digit number. Text after the number is allowed: `0051 happy beep.mp3` works.

Each sound's number comes from its bank and its position in the bank:

**number = (bank − 1) × 25 + position**

| Bank | Mood | Files | Count |
| --- | --- | --- | --- |
| 1 | General | `0001`–`0019` | 19 |
| 2 | Chat | `0026`–`0043` | 18 |
| 3 | Happy | `0051`–`0057` | 7 |
| 4 | Sad | `0076`–`0079` | 4 |
| 5 | Whistle | `0101`–`0103` | 3 |
| 6 | Scream | `0126` scream, `0128` short circuit | |
| 7 | Leia | `0151` | |
| 8 | Music | `0176` beep Cantina, `0177` Star Wars, `0178` Imperial March, `0180` Cantina, `0181` Disco | |
| — | Start-up | `0255` | Plays at power-on |

The firmware picks mood sounds only from the counts in the table. A bank with a different number of sounds needs its count changed: `MP3_BANK1_SOUNDS` … `MP3_BANK5_SOUNDS` in `AstroPixelsPlus/MarcduinoSound.h`. Then rebuild and reflash.

## 2. Wiring

| From | Through | To |
| --- | --- | --- |
| ESP32 GPIO19 | Slip ring CH4, then 1k resistor | DFPlayer RX |
| DFPlayer TX | Slip ring CH5 | ESP32 GPIO18 |
| DFPlayer DAC_L / GND | Ground-loop isolator | Amplifier line input |
| Amplifier L+ / L− | — | Speaker |

Full details: [Wiring](WIRING.md) §1–2 and [Power](POWER.md) §8.

## 3. Test

1. Insert the SD card. Fit B-LOGIC (2A) and F5 (5A).
2. Turn the transmitter on. Set VrB (volume) to one-quarter.
3. Power the droid on.

| Check | Pass |
| --- | --- |
| Power-on | The start-up sound (`0255`) plays |
| Flip SwB down | One sound from the current mood plays |
| Turn VrA, flip SwB up then down | The sound matches the new mood |
| Turn VrB | Volume follows the dial |
| SwC middle, wait 30 s | Random chatter plays every 5–30 seconds |
| SwC up | Chatter stops |

## 4. Problems

| Symptom | Cause and fix |
| --- | --- |
| No sound at all | SD card not FAT32, or the `mp3` folder is missing. Check B-LOGIC and F5. Check CH4 continuity and the 1k resistor. |
| Wrong sounds play | Files are misnumbered. Check against the table in §1. |
| Some moods are silent | Their files are missing. Each mood needs every number up to its count. |
| Hum or buzz | The ground-loop isolator is bypassed or missing |
| Too quiet at full VrB | The amplifier has no volume control. Normalise the source files to a louder level. |
