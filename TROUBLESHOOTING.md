# Troubleshooting

Find the symptom, then work down its checks in order.

## Driving

| Symptom | Check |
| --- | --- |
| Neither wheel moves | Master cutoff on, F1 intact. Transmitter on and bound. Right stick centred once after power-on (safe start). |
| One wheel never moves | That half's receiver lead is plugged into receiver CH1 or CH2. That half's App to Use is PPM. Repeat [Drive Setup](DRIVE_SETUP.md) §1 on it. |
| Wheels move without stick input | Re-run PPM mapping on both halves. Raise the deadband to 15%. |
| A wheel rolls backwards at a positive VESC Tool duty | Invert Motor Direction on that half ([Drive Setup](DRIVE_SETUP.md) §4) |
| Stick forward spins the droid in place | One channel is reversed by the mix. Reverse CH1 (left) or CH2 (right) on the transmitter. |
| Stick forward drives backwards | Reverse both CH1 and CH2 on the transmitter |
| Stick right turns left | Set AIL to −50% in the Elevon menu |
| Wheels coast after the stick is released | Set Control Type to PID Speed ([Drive Setup](DRIVE_SETUP.md) §6) |
| Wheels keep turning with the transmitter off | Failsafe not set. Set it ([Transmitter Setup](TRANSMITTER_SETUP.md) §7) and retest. |
| Droid slows or stops on a long drive | Battery below 11.5V: the VESC voltage cutoff is acting. Charge it. |
| F1 blows | Battery Current Max set too high. Set 5.0–5.5A per half. |
| Jerky at low speed | Re-run the FOC wizard in Hall mode. Check the five Hall wires and their pin order ([Drive Setup](DRIVE_SETUP.md) §4). |

## Dome

| Symptom | Check |
| --- | --- |
| Dome doesn't turn | B-SERVO fuse. Slip ring CH6. Shifter LV2 → HV2. Transmitter on: the servo gets no pulses until the receiver is heard. |
| Dome creeps with the stick centred | Set CH4 subtrim ([Transmitter Setup](TRANSMITTER_SETUP.md) §6) |
| R2 never looks around | SwC down (Alive). Both sticks centred and no driving for 3 seconds. Looks come every 6–75 seconds, depending on mood. |
| R2 never looks around although SwC is down and the sticks are centred | CH4 subtrim is more than 80 µs from centre, so the firmware reads the stick as pushed. Reduce the subtrim; adjust the servo's own neutral instead. |
| The dome turns slowly for about 22 seconds, does it again at the next look, then stops moving on its own | The front magnets aren't being found. Check the sensor LED blinks at every magnet, the gap is 5 mm or less, all magnets face the same way up, and the extra magnet is 5° clockwise of front. Check shifter HV3 → LV3 → GPIO5. Flip SwC up and back down to retry. |
| The dome turns a slow circle between looks now and then | It lost count of the magnets and is finding front again. Check every magnet is glued firmly and faces the same way up, the gap is 5 mm or less, and the sensor LED gives exactly one blink per magnet when you turn the dome by hand. |
| The dome turns the wrong way for its looks | Stick-right must turn the dome clockwise. Reverse CH4 ([Transmitter Setup](TRANSMITTER_SETUP.md) §6). |
| Looks stop short of front or past it | A magnet is loose or missing. Check all 13 against the template. |

## Sound

| Symptom | Check |
| --- | --- |
| No sound | SD card FAT32, `mp3` folder present, files numbered ([Sound Setup](SOUND_SETUP.md) §1). B-LOGIC and F5 fuses. Slip ring CH4 and the 1k resistor. VrB not at zero. |
| Start-up sound plays, nothing else | Radio link down: see "Holos and radio" below |
| Wrong sound for the mood | File numbers don't match the bank table |
| Most mood sounds are silent | The phrase files are missing, or the bank counts don't match the card. Copy them, set the counts, or set `MOOD_SOUND_SOURCE` to `MOOD_SOUNDS_CLIPS_ONLY` ([Sound Setup](SOUND_SETUP.md) §1). |
| R2 is quiet for a minute or more | Normal in General and Sad: see how often each mood talks in the [Operating Guide](OPERATING_GUIDE.md#sounds). Still is silent apart from SwB. |
| Hum or buzz | Ground-loop isolator bypassed or missing |

## Holos and radio

| Symptom | Check |
| --- | --- |
| SwC has no effect | The ESP32 isn't reading the receiver. Slip ring CH3. Shifter HV1 → LV1 → GPIO16. Receiver serial set to i-BUS. Common ground through CH2. |
| One holo servo doesn't move | Its plug on the right PCA9685 channel ([Wiring](WIRING.md) §3). D-SERVO fuse. |
| No holo servo moves | PCA9685 SDA/SCL/VCC/GND. Address jumpers open (0x40). |
| Holo servo buzzes or binds | Its range in the servo table in `AstroPixelsPlus/AstroPixelsPlus.ino` reaches a mechanical stop. Narrow that row ([Dome Setup](DOME_SETUP.md) §3). |

## Lights

| Symptom | Check |
| --- | --- |
| No lights | D-LOGIC fuse. Dome buck output 4.9–5.1V. F4 and slip ring CH1/CH2. |
| One display dark | Its cable and connector on the AstroPixels board |
| Lights flicker when servos move | The holo servos must be powered from D-SERVO through the PCA9685 V+ terminal, never from D-LOGIC |
| Holo LEDs dark most of the time | Normal: as stock, each holo flickers blue for 5–25 s every 45–180 s |
| A holo glows pale white | Normal: the holo lamp while R2 studies something ([Operating Guide](OPERATING_GUIDE.md#lights)). Its brightness is `kLampLevel` in `AstroPixelsPlus/R2Eyes.h`. |

## Resetting the dome firmware

1. Power off. Remove the ESP32 module from the AstroPixels board.
2. Connect it by USB. Open the serial monitor (`pio device monitor -d AstroPixelsPlus`).
3. Type `#APZERO` and press Enter.
4. Refit the module with power off.
