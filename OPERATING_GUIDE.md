# Operating Guide

How to drive R2 and use the dome, sounds and lights. One-time setup is in [Transmitter Setup](TRANSMITTER_SETUP.md).

![FlySky FS-i6X layout](docs/images/flysky_fs_i6x_layout.png)

## Controls

| Control | What it does |
| --- | --- |
| **Right stick** | Drive. Forward/back moves, left/right turns. |
| **Left stick, left/right** | Turns the dome. Always overrides random dome. |
| **Left stick, up/down** | Nothing |
| **SwA** | Down: random dome on. Up: off. |
| **SwB** | Flip down: play one sound from the current mood |
| **VrA** | Mood, fully anticlockwise to fully clockwise: mixed, general, chat, happy, sad, whistle |
| **VrB** | Volume. Fully anticlockwise is silent, fully clockwise is loudest. |
| **SwC** | Up: quiet (no random chatter, holo servos still). Middle or down: random chatter and holo movement. |
| **SwD** | Nothing |

## Power on

1. Transmitter **on**. Right stick centred, SwA up.
2. Droid master cutoff **on**.
3. The start-up sound plays and the logics scroll their start-up text.
4. Nudge the right stick to confirm both wheels respond.

The wheels stay still until the right stick has been centred after power-on.

## Power off

1. Droid master cutoff **off**.
2. Transmitter **off**.

## Driving

- The droid drives the moment the right stick moves. There is no arming switch.
- Releasing the stick brakes the wheels.
- If the transmitter switches off or goes out of range, the wheels stop and random dome turns off.
- The master cutoff stops everything.

## Dome

- **Manual:** the left stick turns the dome. Speed follows how far you push.
- **Random dome (SwA down):** with the left stick centred for 3 seconds, the dome:
  1. turns a random distance left or right,
  2. pauses,
  3. returns to front, then waits 4–12 seconds and repeats.
- **Driving** ends any random move and brings the dome back to front. New random moves wait until you have stopped driving for 3 seconds.
- **Finding front:** at power-on, and after you turn the dome with the stick, random dome first turns slowly to find front. Turn the dome only with the stick: the firmware can't detect a dome turned by hand.
- **Dome stops and won't restart:** it couldn't find front within 20 seconds. Flip SwA up, then down, to retry. If it happens again, see [Troubleshooting](TROUBLESHOOTING.md).

## Sounds

- **Mood (VrA):** chooses which group of sounds plays. "Mixed" draws from all five moods.
- **SwB:** flip down to play one sound now. Flip up, then down again, for the next one.
- **Random chatter (SwC middle or down):** a sound from the current mood every 5–30 seconds.
- **Volume (VrB):** takes effect immediately and is remembered for the next power-on.

## Lights and holos

- Logics, PSIs and holo lights run their AstroPixels patterns all the time.
- With SwC middle or down, the holo projectors twitch to random positions every 1.5–4.5 seconds.
- With SwC up, they return to centre and stay there.

## Before each session

| Check | How |
| --- | --- |
| Battery charged | Charger shows full, or the transmitter's voltage reading (optional FS-CVT01 sensor) is 13.2V or more |
| Charger unplugged | SAE lead disconnected and covered |
| Transmitter batteries | Transmitter battery indicator above half |
| Failsafe | Wheels up, drive, switch transmitter off: wheels stop |
| Clear area | No people or pets within the droid's path |

## Charging

1. Master cutoff **off**.
2. Connect the charger's SAE lead. Charger in **LiFePO4** mode.
3. Unplug the charger before switching the droid on.

Never charge below 0°C.
