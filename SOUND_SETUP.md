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

### Phrase banks

Each mood also has a phrase bank of up to 150 sounds, for droid phrases beyond the film clips:

**number = 300 + (bank − 1) × 150 + n**, n = 1 … 150

| Bank | Mood | Files (120 each) | Room up to |
| --- | --- | --- | --- |
| 1 | General | `0301`–`0420` | `0450` |
| 2 | Chat | `0451`–`0570` | `0600` |
| 3 | Happy | `0601`–`0720` | `0750` |
| 4 | Sad | `0751`–`0870` | `0900` |
| 5 | Whistle | `0901`–`1020` | `1050` |

The firmware expects 120 phrases in each bank. Every number from the first in the bank up to its count must be on the card. A missing number plays nothing.

### Add your own sounds

1. Pick the mood the sound belongs to.
2. Give it the next free number in that mood's phrase bank. For a 121st Happy sound: 300 + (3 − 1) × 150 + 121 = `0721`.
3. Convert it to MP3 and name it with the number first: `0721 happy chirp.mp3`.
4. Copy it into the card's `mp3/` folder.
5. Set that bank's count in `AstroPixelsPlus/MarcduinoSound.h` to the new total, e.g. `#define MP3_BANK3_PHRASES 121`. The most a bank holds is 150.
6. Rebuild and reflash.

A bank with fewer than 120 sounds needs its count lowered to match. A mood with no phrases at all has its count set to `0`.

Film clips follow the same steps in their own banks: the next free number from the first table, and `MP3_BANKn_SOUNDS` set to the new total. A film bank holds at most 25.

### Choose which sounds moods play

`MOOD_SOUND_SOURCE` in `AstroPixelsPlus/MarcduinoSound.h` sets which banks the moods draw from:

```cpp
#define MOOD_SOUND_SOURCE MOOD_SOUNDS_EXTENDED
```

| Setting | Moods play |
| --- | --- |
| `MOOD_SOUNDS_CLIPS_ONLY` | Film clips only (`0001`–`0125`) |
| `MOOD_SOUNDS_EXTENDED` | Film clips and phrases. The default. Every file in the mood's two banks is equally likely, so the bigger bank plays more often: with 120 phrases, a film clip is about 1 sound in 7 in General and 1 in 40 in Whistle. |
| `MOOD_SOUNDS_GENERATED_ONLY` | Phrases only (`0301`–`1050`) |

Both sets of files stay on the card. Change the setting, then rebuild and reflash. A card with no phrase files uses `MOOD_SOUNDS_CLIPS_ONLY`.

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
| VrA on General, SwC middle, wait 2 minutes | R2 makes a sound on its own at least once. How often each mood talks is in the [Operating Guide](OPERATING_GUIDE.md#sounds). |
| SwC up | R2 stops talking on its own. SwB still plays a sound. |

## 4. Problems

| Symptom | Cause and fix |
| --- | --- |
| No sound at all | SD card not FAT32, or the `mp3` folder is missing. Check B-LOGIC and F5. Check CH4 continuity and the 1k resistor. |
| Wrong sounds play | Files are misnumbered. Check against the table in §1. |
| Some moods are silent | Their files are missing. Each mood needs every number up to its count. |
| Most mood sounds are silent | The phrase files are missing. Copy them to the card, or set `MOOD_SOUND_SOURCE` to `MOOD_SOUNDS_CLIPS_ONLY`. |
| Hum or buzz | The ground-loop isolator is bypassed or missing |
| Too quiet at full VrB | The amplifier has no volume control. Normalise the source files to a louder level. |
