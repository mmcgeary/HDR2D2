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

Updates go the same way: module out, USB, flash.

Wi-Fi is off. The droid needs no Wi-Fi to run. To use the stock web page or Wi-Fi updates, type `#APWIFI1` in the serial monitor; `#APWIFI0` turns it off again. The access point is `AstroPixels` with the published password `Astromech`: change it on the web page's **WiFi** tab the first time you turn Wi-Fi on.

## 2. Lights

1. Fit D-LOGIC (3A). Leave D-SERVO out.
2. Power on.

**Pass:** the front logic scrolls "R2D2", the rear logic scrolls "AstroPixels", then both run their normal patterns. PSIs and holo LEDs light.

## 3. Holo servos

1. Power off. Plug one holo servo into its PCA9685 channel ([Wiring](WIRING.md) §3). Fit D-SERVO (5A).
2. Power on. With no radio connected, R2 is Aware: the holo moves to a point, holds, and moves on.
3. Add the other five servos one at a time.
4. Set the mood dial to Happy (the widest holo movement) and SwC to the middle. Watch each holo for a minute.

**Pass:** no holo touches a mechanical stop, buzzes or binds. For each holo:

- the top and rear holos wander on their own,
- a sad mood aims them low,
- and a look toward R2's right turns the front holo to the right.

**Ranges.** Each holo servo's range is its row in the servo table in `AstroPixelsPlus/AstroPixelsPlus.ino`: 1000–2000 µs. That's inside stock AstroPixelsPlus's 800–2200 µs, which ReelTwo's own holo code drives to both ends. R2's looks can reach both ends of it.

| Problem | Fix |
| --- | --- |
| A holo touches its stop | Narrow that servo's row in the servo table, for example `{2, 1100, 1900, HOLO_HSERVO}`, rebuild and reflash |
| A holo looks the wrong way (left instead of right, or up when sad) | Set that eye's axis to `true` in `kEyeFlipH` / `kEyeFlipV` in `AstroPixelsPlus/R2Eyes.h` (order: front, rear, top), rebuild and reflash |

## 4. Radio link (iBUS)

1. Connect the receiver's i-BUS SERVO port through slip ring CH3 to the level shifter ([Wiring](WIRING.md)).
2. Turn the transmitter on, then the droid.
3. Flip SwC up (Still): the holos ease to centre and stop.
4. Flip SwC to the middle (Aware): the holos start looking around again.

**Pass:** SwC controls the holos. That proves the ESP32 is reading the receiver.

If SwC does nothing:

- Check CH3 continuity through the slip ring.
- Check the shifter path HV1 → LV1 → GPIO16.
- Check the receiver's serial output is set to i-BUS.

Never plug USB into the ESP32 while it sits in a powered AstroPixels board.

## 5. Magnet ring and dome sensor

The dome finds its position from 13 magnets on the body and the Hall sensor under the dome.

1. Measure the diameter of the circle the magnets will sit on, on the body's top surface under the dome edge.
2. Print the template: `python3 tools/magnet_template.py <diameter in mm>`. Print the SVG at 100% and check the 100 mm bar with a ruler.
3. Tape the template on the body with FRONT pointing to R2's front.
4. Glue a magnet (6×2 mm or 10×2 mm) on each of the 13 marks, all with the same face up. The two marks at FRONT are 0° and 5° clockwise.
5. Mount the KY-003 sensor on the underside of the dome, facing down, at the same distance from the centre as the magnets. Set the gap to 5 mm or less.

**Check:** turn the dome slowly by hand. The sensor's LED blinks at every magnet: 13 blinks per turn, with two close together at front. If it never lights, turn all the magnets over.

## 6. Dome servo

1. Connect the servo signal through slip ring CH6 ([Wiring](WIRING.md)). Fit B-SERVO (5A).
2. Set SwC up (Still).
3. Power on with the transmitter on.
4. Push the left stick right: the dome turns clockwise seen from above. If not, reverse CH4 ([Transmitter Setup](TRANSMITTER_SETUP.md) §6).
5. Centre the stick. If the dome creeps, set CH4 subtrim.

Before the receiver is heard, the firmware sends the dome servo no pulses, so the dome stays still at power-up.

## 7. Behaviour

1. Set the mood dial (VrA) to General. Flip SwC down (Alive), both sticks centred.
2. Within 35 seconds R2 starts its first look. The dome doesn't know where it is yet, so it first turns slowly clockwise until it passes the front magnets. Then:
   - the front holo glances one way,
   - the dome follows smoothly,
   - it holds for a few seconds,
   - and comes back to front.
3. Push the left stick: the dome follows the stick immediately. Let go: 3 seconds later R2 carries on.
4. Push the right stick (driving) during a look: the dome returns to front.
5. Flip SwB down: R2 makes a sound and a glance, and ends facing front.
6. Turn VrA to Happy: looks come more often and faster. Turn it to Sad: slow and rare.
7. Flip SwC to the middle (Aware): a look in progress finishes facing front, then the dome stays put; the holos keep looking around.
8. Flip SwC up (Still): everything rests.

| Problem | Fix |
| --- | --- |
| The dome turns slowly for about 22 seconds, does it again at the next look, then stops moving on its own | The front magnets aren't being found. Check §5, then flip SwC up and back down to retry. |
| The dome turns a slow circle between looks now and then | It lost count of the magnets. Check every magnet is glued firmly, faces the same way up, and sits 5 mm or less from the sensor, and that §5's check gives exactly one blink per magnet. |
| The dome stops short of front or past it | A magnet is loose or missing. Check all 13 against the template. |
| Looks are too fast or too big | Edit that mood's row in `AstroPixelsPlus/R2Mood.h` (`domeCruiseDps`, `lookAngleDeg`), rebuild and reflash. |

## 8. Done

The dome is ready. Continue with [Sound Setup](SOUND_SETUP.md).
