# FlySky FS-i6X Controller Guide

![FlySky FS-i6X layout](docs/images/flysky_fs_i6x_layout.png)

## What each control does

| Control | Channel | Action |
| --- | --- | --- |
| Right stick | CH1 + CH2 (Elevon mix) | Drive: forward/back and turn |
| Left stick, left/right | CH4 | Turn the dome. Always overrides random dome. |
| Left stick, up/down | CH3 | Not used |
| VrA dial | CH5 | Mood: mixed → general → chat → happy → sad → whistle |
| VrB dial | CH6 | Volume. Saved for next power-up. |
| SwA | CH7 | Down = random dome on |
| SwB | CH8 | Flip down = play one sound from the current mood |
| SwC (3-position) | CH9 | Up = quiet. Middle or down = random chatter and holo movement. |
| SwD | CH10 | Spare |

**Driving goes straight to the VESCs.** If the dome board is off or crashed, the droid still drives.

### Random dome

- With SwA down and the left stick centred for 3 seconds, the dome:
  1. Turns a random amount left or right.
  2. Pauses.
  3. Turns back until the front Hall sensor sees the magnet.
  4. Waits 4–12 seconds and repeats.
- If it doesn't know where front is (at power-up, or after you've turned it with the stick), it first turns slowly until it finds front.
- Driving stops new moves and brings the dome back to front.
- Switching SwA up during a move still finishes the return to front.
- If front isn't found within about 20 seconds, the dome stops and stays stopped until you flip SwA up. Check the sensor and magnet.

## Transmitter setup

Menu names are from the stock FS-i6X firmware. Hold **OK** to open the menu.

### 1. Model and channels

1. **System setup → Model select:** pick an unused model and name it R2.
2. **System setup → Model type:** Airplane/glider (needed for the Elevon menu).
3. **Functions setup → Aux. channels:**

   | Channel | Source |
   | --- | --- |
   | CH5 | VrA |
   | CH6 | VrB |
   | CH7 | SwA |
   | CH8 | SwB |
   | CH9 | SwC |
   | CH10 | SwD |

### 2. Receiver output mode

**System setup → RX setup → Output mode:** set **PWM** (not PPM) and serial **i-BUS**.

### 3. Drive mix

1. **Functions setup → Elevon:** On. Start with ELE (forward/back) 100% and AIL (turn) 50%.
2. Test with the wheels up (see [Drive Setup](DRIVE_SETUP.md) §5). If directions are wrong:
   - forward/back swapped with turning: swap the two values,
   - one wheel backwards: **Functions setup → Reverse** on that channel, or invert that wheel in VESC Tool.
3. Optional slower driving: **Functions setup → Dual rate/exp** can reduce the AIL/ELE rates. Check which switch your transmitter uses for it and don't pick one already used above. The VESC current limits stay the hard cap either way.

### 4. Dome neutral

If the dome creeps with the left stick centred, adjust **Functions setup → Subtrim → CH4** until it stops. The firmware treats the centred stick value as neutral, so this trim also applies to random dome moves.

Keep subtrim within about ±60. Beyond that the firmware reads the stick as deflected.

### 5. Failsafe

What the receiver sends when it loses the transmitter.

**System setup → RX setup → Failsafe.** For each channel: put the stick or switch in the position shown, select the channel, turn failsafe **On**, and save.

| Channel | Failsafe position | Why |
| --- | --- | --- |
| CH1, CH2 | Right stick centred | Wheels stop |
| CH4 | Left stick centred | Dome stops |
| CH7 (SwA) | Up | Random dome off |

Check it with the wheels up: drive a wheel, turn the transmitter off. Both wheels and the dome must stop.

## Power-on checklist

1. Transmitter **on first**: sticks centred, SwA up, SwC as wanted.
2. Droid master cutoff on. Wait for the start-up sound.
3. Move a little first to confirm both wheels respond the right way.
4. Power off: droid master cutoff off first, then the transmitter.
