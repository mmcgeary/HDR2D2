# Troubleshooting

Find the symptom, then work down its checks in order.

## Driving

| Symptom | Check |
| --- | --- |
| Neither wheel moves | Master cutoff on, F1 intact. Transmitter on and bound. Right stick centred once after power-on (safe start). |
| One wheel never moves | That half's receiver lead is plugged into receiver CH1 or CH2. That half's App to Use is PPM. Repeat [Drive Setup](DRIVE_SETUP.md) §1 on it. |
| Wheels move without stick input | Re-run PPM mapping on both halves. Raise the deadband to 15%. |
| Stick forward spins the droid in place | One wheel is reversed. Invert Motor Direction on that half in VESC Tool. |
| Stick forward drives backwards | Invert Motor Direction on both halves |
| Stick right turns left | Swap the CH1 and CH2 leads at the receiver |
| Wheels keep turning with the transmitter off | Failsafe not set. Set it ([Transmitter Setup](TRANSMITTER_SETUP.md) §7) and retest. |
| Droid slows or stops on a long drive | Battery below 11.5V: the VESC voltage cutoff is acting. Charge it. |
| F1 blows | Battery Current Max set too high. Set 5.0–5.5A per half. |
| Jerky at low speed | Re-run the FOC wizard in Hall mode. Check the five Hall wires and their pin order ([Drive Setup](DRIVE_SETUP.md) §4). |

## Dome

| Symptom | Check |
| --- | --- |
| Dome doesn't turn | B-SERVO fuse. Slip ring CH6. Shifter LV2 → HV2. Transmitter on: the servo gets no pulses until the receiver is heard. |
| Dome creeps with the stick centred | Set CH4 subtrim ([Transmitter Setup](TRANSMITTER_SETUP.md) §6) |
| Random dome never starts, SwA down and sticks centred | CH4 subtrim is more than 80 µs from centre, so the firmware reads the stick as pushed. Reduce the subtrim; adjust the servo's own neutral instead. |
| Random dome never starts | SwA down. Both sticks centred for 3 seconds. |
| Random dome seeks for 20 s, then stops | Sensor doesn't see the magnet. Check the sensor LED lights at front, the gap is 5 mm or less, and the magnet's pole faces the sensor. Check shifter HV3 → LV3 → GPIO5. Flip SwA up and down to retry. |
| Dome stops past front | Return speed too high: lower `seekSpeedUs` in `AstroPixelsPlus/R2Logic.h` |

## Sound

| Symptom | Check |
| --- | --- |
| No sound | SD card FAT32, `mp3` folder present, files numbered ([Sound Setup](SOUND_SETUP.md) §1). B-LOGIC and F5 fuses. Slip ring CH4 and the 1k resistor. VrB not at zero. |
| Start-up sound plays, nothing else | Radio link down: see "Holos and radio" below |
| Wrong sound for the mood | File numbers don't match the bank table |
| Hum or buzz | Ground-loop isolator bypassed or missing |

## Holos and radio

| Symptom | Check |
| --- | --- |
| SwC has no effect | The ESP32 isn't reading the receiver. Slip ring CH3. Shifter HV1 → LV1 → GPIO16. Receiver serial set to i-BUS. Common ground through CH2. |
| One holo servo doesn't move | Its plug on the right PCA9685 channel ([Wiring](WIRING.md) §3). D-SERVO fuse. |
| No holo servo moves | PCA9685 SDA/SCL/VCC/GND. Address jumpers open (0x40). |
| Holo servo buzzes or binds | Mechanical stop inside the holo travel. Free the linkage. |

## Lights

| Symptom | Check |
| --- | --- |
| No lights | D-LOGIC fuse. Dome buck output 4.9–5.1V. F4 and slip ring CH1/CH2. |
| One display dark | Its cable and connector on the AstroPixels board |
| Lights flicker when servos move | The holo servos must be powered from D-SERVO through the PCA9685 V+ terminal, never from D-LOGIC |

## Resetting the dome firmware

1. Power off. Remove the ESP32 module from the AstroPixels board.
2. Connect it by USB. Open the serial monitor (`pio device monitor -d AstroPixelsPlus`).
3. Type `#APZERO` and press Enter.
4. Refit the module with power off.
