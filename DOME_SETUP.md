# Dome Setup

The dome's AstroPixels ESP32 runs:

- the lights
- the holo servos
- the sounds
- the dome rotation

It runs stock AstroPixelsPlus plus our remote add-on. Firmware details are in [AstroPixelsPlus/HDR2D2.md](AstroPixelsPlus/HDR2D2.md).

Work through the sections in order. Each one is tested before the next is added.

## What you need

- A computer with [PlatformIO](https://platformio.org/) (`pio`) and this repository
- A USB data cable for the ESP32
- The dome wired per [Wiring](WIRING.md) §3, and powered per [Power](POWER.md) §4

## 1. Flash the firmware

1. Power off. Lift the ESP32 module out of the AstroPixels board.
2. Connect the module to the computer by USB.
3. Build and flash:

   ```bash
   pio run -d AstroPixelsPlus -t upload
   ```

4. Open the serial monitor:

   ```bash
   pio device monitor -d AstroPixelsPlus
   ```

5. Type `#APZERO` and press Enter. This clears settings left by earlier firmware. The module restarts.
6. Disconnect USB. Refit the module to the AstroPixels board with all power off.

Do the first flash over USB. Later updates also go over USB, with the module removed.

## 2. Lights

1. Fit D-LOGIC (3A). Leave D-SERVO out.
2. Power on.

**Pass:** the front logic scrolls "R2D2", the rear logic scrolls "AstroPixels", then both run their normal patterns. PSIs and holo LEDs light.

## 3. Holo servos

1. Power off. Plug one holo servo into its PCA9685 channel ([Wiring](WIRING.md) §3). Fit D-SERVO (5A).
2. Power on. With no radio connected, the holo servos twitch at random every 1.5–4.5 seconds.
3. Check the servo moves without binding or buzzing at its limits.
4. Add the other five servos one at a time and repeat.

The firmware limits holo servos to 1000–2000 µs, and random twitches stay within 1350–1650 µs.

## 4. Radio link (iBUS)

1. Connect the receiver's i-BUS SERVO port through slip ring CH3 to the level shifter ([Wiring](WIRING.md)).
2. Turn the transmitter on, then the droid.
3. Flip SwC up: the holo servos stop twitching and centre.
4. Flip SwC to the middle: the twitching resumes.

**Pass:** SwC controls the holo servos. That proves the ESP32 is reading the receiver.

If SwC does nothing:

- Check CH3 continuity through the slip ring.
- Check the shifter path HV1 → LV1 → GPIO16.
- Check the receiver's serial output is set to i-BUS.

Never plug USB into the ESP32 while it sits in a powered AstroPixels board.

## 5. Front Hall sensor

1. Mount the KY-003 sensor on the dome's inner rim, facing down.
2. Mount a small neodymium magnet on the body's top ring, so it sits directly under the sensor when the dome faces front.
3. Set the gap to 5 mm or less.
4. The sensor responds to one pole of the magnet. If it doesn't switch, turn the magnet over.

**Check:** the sensor module has an LED. Turn the dome by hand: the LED lights only when the dome faces front.

## 6. Dome servo

1. Connect the servo signal through slip ring CH6 ([Wiring](WIRING.md)). Fit B-SERVO (5A).
2. Set random dome off (SwA up).
3. Power on with the transmitter on.
4. Move the left stick left and right: the dome follows.
5. Centre the stick. If the dome creeps, set CH4 subtrim ([Transmitter Setup](TRANSMITTER_SETUP.md) §6).

Before the receiver is heard, the firmware sends the dome servo no pulses, so the dome stays still at power-up.

## 7. Random dome

1. Point the dome away from front.
2. Centre both sticks. Flip SwA down.
3. After 3 seconds the dome turns slowly until the sensor finds front, then stops.
4. After 4–12 seconds it turns away, pauses for 1–4 seconds, and turns back until it reaches front.
5. Push the left stick: the dome follows the stick immediately.
6. Push the right stick (driving): any random move ends and the dome returns to front.
7. Flip SwA up: random moves stop once the dome is back at front.

| Problem | Fix |
| --- | --- |
| The dome seeks for 20 seconds, then stops and stays stopped | The sensor never saw the magnet. Check §5, then flip SwA up and down to retry. |
| Dome overshoots front | Lower the return speed, `seekSpeedUs` in `AstroPixelsPlus/R2Logic.h`, and reflash |
| Random turns too large or too fast | Lower `turnSpeedUs` or `turnMaxMs` in the same file |

## 8. Done

The dome is ready. Continue with [Sound Setup](SOUND_SETUP.md).
