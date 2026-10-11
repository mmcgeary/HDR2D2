# Drive Setup

The feet run straight from the FlySky receiver:

- The transmitter's Elevon mix turns the right stick into a left-wheel signal (CH1) and a right-wheel signal (CH2).
- Each half of the Flipsky Dual FSESC4.20 reads one of them on its own receiver input. VESC Tool calls this the **PPM** app.

The Dual FSESC4.20 is two complete VESC controllers on one board, joined only by CAN. With the CAN switch **OFF**, each half runs on its own.

**Keep the wheels off the ground for every step until §6.**

## What you need

- VESC Tool on a computer, and a Micro-USB data cable
- The receiver bound and set up ([Transmitter Setup](TRANSMITTER_SETUP.md) §1–4; leave the Elevon mix off for now)
- A multimeter
- A soldering iron, a female servo lead (26AWG) and hot glue

## 1. Prove the factory receiver input

This confirms the VESC reads your receiver before you modify anything.

1. Disconnect the motor from half A (the half with the factory receiver lead), or lift that wheel.
2. Set the CAN switch to **OFF (single)**.
3. Connect the battery through F1. Plug USB into half A.
4. VESC Tool: **App Settings → General → App to Use: PPM**. Write the settings.
5. Open **App Settings → PPM → Mapping**. It shows the incoming pulse width live.
6. Disconnect the receiver from B-LOGIC. Plug receiver CH1 into the factory lead with its 5V pin in place: for this test the VESC powers the receiver.
7. Move the right stick left and right.

| Result | Meaning |
| --- | --- |
| The value follows the stick, 1.0–2.0 ms, 1.5 ms at centre | Input works. Continue. |
| The value jumps around | Receiver is in PPM output mode. Set PWM ([Transmitter Setup](TRANSMITTER_SETUP.md) §4). |
| No value | Plug reversed, or receiver not bound |

## 2. Add a receiver lead to half B

Half B has unpopulated receiver pads in the same position as half A's soldered lead. This modification voids the board's warranty.

1. Disconnect the battery. Remove the heat sink. Keep its thermal pad.
2. Multimeter on continuity:
   - Half B **signal** pad to half A's SIN wire: **no beep**. If it beeps, the inputs are shared; stop and buy two single Flipsky Mini FSESC4.20 controllers.
   - Half B **ground** pad to battery negative: **beep**.
3. Solder the servo lead's signal and ground wires to the matching pads. Leave the 5V pad empty.
4. Glue the lead down next to the pads so a pull can't lift them.
5. Refit the heat sink with its thermal pad. Tighten every screw.
6. Repeat §1 on half B's USB port with receiver CH2.

## 3. Final lead wiring

1. Remove the 5V (red) pin from both receiver leads and insulate it. The receiver runs from the body 5V supply ([Wiring](WIRING.md) §1).
2. Half A lead → receiver CH1. Half B lead → receiver CH2.

## 4. Motor wiring, detection and wheel direction

Half A drives the left wheel. Half B drives the right wheel.

Each motor's three phase wires go to its half's phase outputs. Its Hall lead goes to that half's SENSE connector:

| SENSE pin | Motor wire |
| --- | --- |
| GND | Black |
| H3 | Green |
| H2 | Blue |
| H1 | Yellow |
| TMP | — (insulated) |
| 5V | Red |

For each half, over its own USB port:

1. Run the **FOC setup wizard**. Select **sensored (Hall)**.
2. Let it detect resistance, inductance, flux linkage and the Hall table. Write the configuration.
3. **Set the wheel direction:**
   1. In VESC Tool's realtime control panel, set **Duty** to 0.05 and press its run button. Watch the wheel.
   2. If it rolls backwards (relative to the droid's front), press stop and turn on **Motor Settings → General → Invert Motor Direction**. Write, and run it again to confirm.
   3. Press stop.

After this step, a positive command rolls each wheel forward. All wheel direction is set here, in VESC Tool.

## 5. VESC Tool settings

Set these on **each half**, over its own USB port.

### Motor and battery limits

| Setting | Value |
| --- | --- |
| Motor Current Max | 12–15A |
| Motor Current Max Brake | −6 to −8A |
| Battery Current Max | 5.0–5.5A |
| Battery Current Max Regen | −2.5 to −3.0A |
| Battery Voltage Cutoff Start | 11.5V |
| Battery Voltage Cutoff End | 10.5V |

These limits keep both wheels together under 11A from the battery, inside its 20A rating with the dome running ([Power](POWER.md) §6).

### PPM app

| Setting | Value |
| --- | --- |
| App to Use | PPM |
| Control Type | Duty Cycle |
| Deadband | 10% |
| Safe Start | On |
| Timeout | 300 ms |
| Timeout Brake Current | 3A |
| Multiple VESCs over CAN | Off |

### Mapping

1. Turn on the Elevon mix ([Transmitter Setup](TRANSMITTER_SETUP.md) §5).
2. On each half, run **PPM → Mapping**: move the right stick around its full range (every edge and corner), then centre it. Write.
3. Check with the stick:
   - Forward: both wheels roll forward. The wheel directions are already set (§4), so a wheel rolling backwards here means its channel is reversed by the mix. Reverse that channel on the transmitter (**Functions setup → Reverse**: CH1 for the left wheel, CH2 for the right) and re-run its mapping.
   - Right: the left wheel runs faster than the right. If it's the other way round, set **AIL** to **−50%** in the Elevon menu ([Transmitter Setup](TRANSMITTER_SETUP.md) §5).

### Settings record

| | Half A (left) | Half B (right) |
| --- | --- | --- |
| Firmware version | | |
| Invert Motor Direction | | |
| Motor Current Max / Brake | | |
| Battery Current Max / Regen | | |
| Control type / deadband | | |
| Timeout / brake current | | |

## 6. Safety tests

Wheels off the ground for tests 1–7.

| # | Test | Pass |
| --- | --- | --- |
| 1 | Stick forward, back, left, right | Wheels follow the stick in the right directions |
| 2 | Run both wheels at half stick, then let the stick spring back to centre | Both wheels stop within 1 s. They don't coast. |
| 3 | Hold the stick forward and power on | Neither wheel moves until the stick returns to centre |
| 4 | Drive both wheels, switch the transmitter off | Both wheels stop |
| 5 | Drive both wheels, unplug half A's receiver lead | Wheel A brakes within 0.3 s |
| 6 | Repeat test 5 for half B | Wheel B brakes within 0.3 s |
| 7 | Master cutoff off with both wheels turning | Everything stops |
| 8 | First floor drive: open space, slow, a helper at the master cutoff | Drives and stops under control. Releasing the stick stops the droid. |

All eight must pass before normal driving.

If test 2 fails (the wheels coast):

1. Set **Control Type** to **PID Speed** on both halves.
2. Set **PID Max ERPM** to the ERPM VESC Tool shows at full stick.
3. Re-run the mapping and repeat all tests.
