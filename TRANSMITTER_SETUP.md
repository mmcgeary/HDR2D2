# Transmitter Setup (FlySky FS-i6X + FS-iA6B)

You set this up once. Day-to-day use is in the [Operating Guide](OPERATING_GUIDE.md).

Menu names are from the stock FS-i6X firmware. Hold **OK** to open the menu, **Cancel** to go back, and hold **Cancel** to save a page.

## Channel map

| Channel | Control | Used by | Function |
| --- | --- | --- | --- |
| CH1 | Right stick (mixed) | VESC half A | Left wheel |
| CH2 | Right stick (mixed) | VESC half B | Right wheel |
| CH3 | Left stick up/down | — | Unused |
| CH4 | Left stick left/right | ESP32 | Dome |
| CH5 | SwA | ESP32 | Random dome (down = on) |
| CH6 | SwB | ESP32 | Play a mood sound (flip down) |
| CH7 | VrA | ESP32 | Mood |
| CH8 | VrB | ESP32 | Volume |
| CH9 | SwC | ESP32 | Random sounds and holos (up = off) |
| CH10 | SwD | — | Spare |

## 1. Model

1. **System setup → Model select:** choose an empty model.
2. **System setup → Model name:** R2.
3. **System setup → Type select:** Airplane/glider. The Elevon mix is only offered for this type.
4. **System setup → Stick mode:** Mode 2 (throttle on the left stick).

## 2. Bind the receiver

1. Plug the bind plug into the receiver's **B/VCC** port and power the receiver. Its LED flashes.
2. On the transmitter: **System setup → RX bind**.
3. When the receiver LED stays on, remove the bind plug and power-cycle the receiver.

## 3. Auxiliary channels

**Functions setup → Aux. channels:**

| Channel | Source |
| --- | --- |
| CH5 | SwA |
| CH6 | SwB |
| CH7 | VrA |
| CH8 | VrB |
| CH9 | SwC |
| CH10 | SwD |

## 4. Receiver output

**System setup → RX setup → Output mode:**

- Output: **PWM**
- Serial: **i-BUS**

In PPM mode, CH1 carries every channel at once and the VESC can't read it.

## 5. Drive mix (Elevon)

1. **Functions setup → Elevon:** On.
2. Set **ELE** (forward/back) to **100%** and **AIL** (turning) to **50%**.
3. With the wheels off the ground ([Drive Setup](DRIVE_SETUP.md) §5), check:
   - Stick forward: both wheels roll forward.
   - Stick right: the left wheel runs faster than the right.
4. Fix directions:
   - A wheel rolls backwards on forward stick: invert that half in VESC Tool.
   - Turning is reversed: swap the CH1 and CH2 leads at the receiver.
5. Raise AIL for sharper turns, lower it for gentler ones.

## 6. Dome neutral (CH4 subtrim)

1. Power the droid with the dome servo connected and random dome off (SwA up).
2. Centre the left stick.
3. If the dome creeps, open **Functions setup → Subtrim → CH4** and adjust until it stops.

The firmware uses the centred CH4 value as the dome's stop point, for both the stick and random moves. It treats any CH4 value more than 80 µs from 1500 µs as a pushed stick. If random dome never starts after trimming, the trim is too large: reduce it and adjust the servo's own neutral instead.

## 7. Failsafe

The failsafe sets what the receiver outputs when it loses the transmitter.

**System setup → RX setup → Failsafe.** For each channel below:

1. Put the stick or switch in the listed position.
2. Select the channel.
3. Set it **On**.
4. Hold **Cancel** to save.

| Channel | Position | Result on signal loss |
| --- | --- | --- |
| CH1 | Right stick centred | Left wheel stops |
| CH2 | Right stick centred | Right wheel stops |
| CH4 | Left stick centred | Dome stops |
| CH5 | SwA up | Random dome off; the dome returns to front and stops |

Test it with the wheels off the ground. Drive both wheels and the dome, then switch the transmitter off. Wheels and dome must stop.

## 8. Battery voltage on screen (optional)

Plug an FS-CVT01 voltage sensor into the receiver's **SENS** port. Connect its sense lead to fuse block position F6 with a 1A fuse, and its negative to the negative bus. The transmitter then shows pack voltage.
