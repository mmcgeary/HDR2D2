# Drive Setup: Receiver → VESC

The feet are driven straight from the FlySky receiver. No board sits in between.

- The transmitter's **Elevon** mix turns the right stick into a left-wheel command (CH1) and a right-wheel command (CH2).
- Each half of the Flipsky Dual FSESC4.20 reads one of them on its own receiver input (VESC "PPM" app).

The Dual FSESC4.20 is two separate VESC controllers on one board, joined only by CAN. With the CAN switch **OFF**, each half runs alone.

> **Wheels off the ground** for every step on this page until the final floor test.

## 1. Prove the factory receiver input first

Do this before soldering anything. It confirms the VESC reads your receiver.

1. Disconnect that half's motor, or lift the wheels.
2. Set the CAN switch to **OFF (single)**.
3. Power the board from the battery. Plug USB into the half with the factory receiver lead.
4. In VESC Tool: **App Settings → General → App to Use = PPM**. Write the settings.
5. Open **App Settings → PPM → Mapping**. It shows the pulse width the VESC is receiving.
6. Plug receiver **CH1** into the factory lead.
   - For this test only, the lead's 5V can power the receiver.
   - Use a transmitter model with no mixes yet.
7. Move the right stick left and right.

| You see | Meaning |
| --- | --- |
| Smooth change, about 1.0 / 1.5 / 2.0 ms | ✅ Works |
| Value jumps around | Receiver is in PPM output mode. Set it to PWM (see [Controller Guide](CONTROLLER_GUIDE.md)). |
| Nothing | Check plug orientation and that the receiver is bound |

## 2. Add a receiver lead to the second half

The second half has unpopulated receiver pads in the same position as the first half's soldered lead.

1. Disconnect the battery. Remove the heat sink and keep its thermal pad.
2. **Beep check** with a multimeter on continuity:
   - New **signal** pad to the factory lead's SIN wire: must **not** beep. If it beeps, the pads are shared; stop and use two single controllers instead.
   - New **ground** pad to battery negative: must beep.
3. Solder a servo lead (about 26AWG, female servo plug) to the **signal and ground** pads only. Leave the 5V pad empty.
4. Strain-relieve the lead (hot glue or a tie-down) so a pull can't lift the pads.
5. Refit the heat sink with its thermal pad and tighten every screw.
6. Repeat the test from step 1 on this half's USB port, using receiver **CH2**.

Expect this to void the board's warranty.

## 3. Final receiver lead wiring

- On **both** leads, remove the red (5V) pin and insulate it. The receiver is powered by the body 5V supply.
- Factory lead → receiver CH1. New lead → receiver CH2.

## 4. VESC Tool settings, each half separately

Connect USB to one half at a time. CAN switch OFF.

### Motor detection

Run the **FOC wizard** in **sensored (Hall)** mode with the wheels clear.

Motor Hall wiring, motor to VESC SENSE connector:

| VESC pin | Motor wire |
| --- | --- |
| GND | Black |
| H3 | Green |
| H2 | Blue |
| H1 | Yellow |
| TMP | Not used, insulated |
| 5V | Red |

### Current and voltage limits

The 12.8V 20Ah pack's continuous limit is 20A. The lights, sound and servos use about 5–6A, which leaves about 10–11A for both wheels together.

| Setting | Value |
| --- | --- |
| Motor Current Max | 12–15A |
| Motor Current Max Brake | −6 to −8A |
| Battery Current Max | 5.0–5.5A |
| Battery Current Max Regen | −2.5 to −3.0A |
| Battery Voltage Cutoff Start / End | 11.5V / 10.5V |

### PPM app

| Setting | Value |
| --- | --- |
| App to Use | PPM |
| Control Type | Duty Cycle. PID Speed holds better on slopes; try it later if needed. |
| Mapping | Run the mapping with the transmitter's final mix active: full forward, full back, centre |
| Deadband | About 10% |
| Safe Start | On |
| Timeout | About 300ms, with a brake current of about 3A |
| Multiple VESCs over CAN | **Off** |

If a wheel turns the wrong way, use **Invert Motor Direction** for that half.

### Settings record

| | Half A (left) | Half B (right) |
| --- | --- | --- |
| Firmware version | | |
| Control type / deadband | | |
| Timeout / brake current | | |
| Battery current max / regen | | |
| Motor current max / brake | | |
| Direction inverted? | | |

## 5. Tests before the floor

All with the wheels in the air:

1. **Steering:**
   - Stick forward: both wheels forward.
   - Stick right: left wheel forward, right wheel back (or slower).
   - Fix with transmitter channel Reverse or VESC Invert.
2. **Power-up with stick pushed:** hold the stick forward and power on. Neither wheel may move until the stick has been centred (safe start).
3. **Transmitter off** with a wheel turning: both wheels must stop. This is the receiver failsafe at work.
4. **Pull each receiver lead** in turn with a wheel turning: that wheel must brake within about 0.3s. This is the VESC timeout.
5. **Master cutoff:** everything stops.

Then do a slow floor test in an open space with a helper beside the master cutoff.
