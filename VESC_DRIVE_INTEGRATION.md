# Dual FSESC4.20: Two Independent UARTs

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Flash both the Teensy 4.1 body controller and the AstroPixels Plus ESP32 with the current firmware before combined testing; nothing counts as bench-verified until the commissioning record is filled in.

The Flipsky Dual FSESC4.20 drives two Razor Tekno Pop12V hub/wheel motors, one per foot. Exact motor power is unconfirmed (roughly80-100W discussed). Do not derive safe winding current or braking limits from that estimate.

**Teensy owns two direct full-duplex UARTs.** There is no master/slave forwarding. Internal CAN switch **OFF**, multiple-controller mode disabled. Neither VESC UART passes through the slip ring.

## 1. Inventory and supply

This controller has two COMM connectors, two CAN connectors, two USB ports, two Hall connectors, two SWD connectors, two three-phase wire groups, **one shared battery input pair**, one RECEIVER connector labelled GND/5V/SIN, and an internal CAN switch.

Label halves LEFT and RIGHT according to their motor wiring. Supply the shared battery+ via F1 **15A**,12AWG; battery- goes to fuse-box negative bus. Leave RECEIVER/SIN, CAN and SWD unconnected.

## 2. COMM connections

Use the connector's actual pin markings/orientation. The following numbered assignment is the existing COMM map, **not a left-to-right view of an unidentified photograph**.

| COMM pin / label | Left controller | Right controller |
| --- | --- | --- |
| 1 / 5V | Unconnected | Unconnected |
| 2 / 3.3V | Unconnected | Unconnected |
| 3 / GND | Body common ground | Body common ground |
| 4 / ADC | Unconnected | Unconnected |
| 5 / TX | Teensy RX0 / Serial1 | Teensy RX7 / Serial2 |
| 6 / RX | Teensy TX1 / Serial1 | Teensy TX8 / Serial2 |
| 7 / ADC2 | Unconnected | Unconnected |

Both UARTs are native3.3V1152008N1. No shifters. Do not power Teensy/receiver from VESC 5V/3.3V outputs. Route signal/ground harnesses away from phase wires.

### Motors and Hall connectors

Use12AWG phase extensions, approximately2m per lead, and22AWG five-core Hall cable, approximately2m per motor. Keep each motor's phases and Hall connector with its corresponding controller.

| Marked Hall pin | Existing motor wire colour |
| --- | --- |
| GND | Black |
| H3 | Green |
| H2 | Blue |
| H1 | Yellow |
| TMP | Unused, insulated |
| 5V | Red |

Verify marked connector orientation before inserting the harness. These Hall sensors are powered by their own VESC, not the body 5V distribution. The detection wizard maps Hall timing; arbitrary pin reversal is not a substitute.

## 3. VESC Tool Configuration & Current Limits

Elevate both wheels securely before configuring the drive system. Keep the Teensy UART harnesses disconnected during initial VESC Tool setup. Connect your PC via USB to **one controller port at a time**; configure each side independently.

### Power Budget & Current Allocation
The droid is powered by a Renogy 12.8V 20Ah LiFePO4 battery with a maximum continuous discharge rating of **20A**:
- **Auxiliary Systems Budget (~5A – 6A peak):** Non-drive electronics—including the dome ESP32 brain, ReelTwo logic displays, 6 MG90S holoprojector servos, 35kg dome continuous rotation servo, HF82 50W audio amplifier, Teensy 4.1 body controller, and radio receiver—consume approximately 5A to 6A under peak operating conditions.
- **Drive System Budget (~10A – 11A continuous draw):** Subtracting auxiliary loads and maintaining a safe 3A–5A continuous headroom below the 20A BMS threshold leaves approximately 10A to 11A of continuous current for the drive system. This also ensures continuous drive current stays comfortably below the 15A rating of supply fuse F1.
- **Per-Motor Allocation (5.0A – 5.5A Battery Current Max):** To guarantee that both motors driving simultaneously never overload the battery, blow fuse F1, or trip the battery management system (BMS), set each motor controller's **Battery Current Max** to **5.0A – 5.5A** (10.0A – 11.0A total combined drive draw). This ensures reliable operation with safe headroom for all sound, lighting, and dome motion.

### Step-by-Step VESC Tool Setup Guide

Connect each side to VESC Tool over USB and apply the following recommended settings:

1. **Motor Current Limits (`Motor Settings -> General -> Current`):**
   - **Motor Current Max:** Set to **12.0A – 15.0A**.  
     *Why:* This limits the AC phase current delivered to the motor windings. Phase current generates torque at low speeds and can safely exceed battery current at lower duty cycles. 12A–15A delivers responsive acceleration for the Razor Tekno Pop hub motors without overheating the stator coils.
   - **Motor Current Max Brake:** Set to **-6.0A to -8.0A**.  
     *Why:* Sets the maximum phase braking force. Provides firm, smooth deceleration when sticks return to neutral without skidding or throwing the droid off balance.
   - **Battery Current Max:** Set to **5.0A – 5.5A** (10.0A – 11.0A total combined drive draw).  
     *Why:* Directly restricts the DC current drawn from the LiFePO4 battery pack per side. Sized to fit comfortably within the 15A F1 fuse rating and the 20A pack limit with full auxiliary loads running and genuine headroom.
   - **Battery Current Max Regen:** Set to **-2.5A to -3.0A** per side (-5.0A to -6.0A total pack regen).  
     *Why:* Prevents regenerative braking from pushing excessive charging current into the LiFePO4 cells or triggering BMS overvoltage protection when the battery is near 100% state of charge.

2. **Voltage Cutoffs (`Motor Settings -> General -> Voltage`):**
   - **Battery Voltage Cutoff Start:** Set to **11.5V**.  
     *Why:* Throttles motor output gradually as the battery discharges, alerting the operator that the pack needs charging before sudden cutoffs occur.
   - **Battery Voltage Cutoff End:** Set to **10.5V**.  
     *Why:* Hard cutoff preventing cell over-discharge and keeping the battery above the BMS low-voltage disconnect threshold (10.0V).

3. **App Communication & Failsafe (`App Settings -> General`):**
   - **App to use:** Set to **UART**. (Both halves must be set to UART; do not leave the secondary side on "No App").
   - **Baudrate:** Set to **115200 bps**.
   - **Timeout:** Set to **150ms**.
   - **Timeout Brake Current:** Set to **3.0A**.  
     *Why:* If Teensy UART communication packets are interrupted or lost for more than 150ms, the VESC automatically applies 3.0A of active braking to stop the droid rather than freewheeling.

4. **CAN Bus Settings:**
   - Leave the physical **internal CAN switch OFF**.
   - Disable multiple-ESC over CAN and CAN status broadcasting. Each half communicates directly with the Teensy via its own independent UART port (Serial1 on Left, Serial2 on Right).

5. **FOC & Hall Sensor Detection Wizard:**
   - With wheels elevated and clear of the floor, run the **FOC Motor Detection Wizard** for each side.
   - Select **Sensored / Hall Sensor** mode. The wizard automatically measures stator resistance (R), inductance (L), flux linkage, and Hall sensor timing offsets.
   - Verify motor rotation direction. If a wheel spins backwards relative to throttle commands, either toggle **Invert Motor Direction** in VESC Tool, or set that wheel's `direction` field (field 5) to `-1`. Set it on the `/commissioning` page (Field ID 5, Wheel 0 or 1), or over USB with `profile set direction -1 WHEEL`.
   - Write and save configuration to the controller.

### Commissioning Record Table

Record the final tuned parameters from VESC Tool in this commissioning log for documentation and reproducibility:

| Parameter | Left Controller | Right Controller | Recommended Baseline |
| :--- | :--- | :--- | :--- |
| **Hardware / Firmware Version** | | | FW 5.x / HW 4.20 |
| **App Configuration** | UART (115200 baud) | UART (115200 baud) | UART @ 115200 |
| **App Timeout / Brake Current** | 150ms / 3.0A | 150ms / 3.0A | 150ms / 3.0A |
| **Battery Current Max** | | | **5.0A – 5.5A** (10–11A combined) |
| **Battery Current Max Regen** | | | **-2.5A to -3.0A** (-5 to -6A combined) |
| **Motor Current Max** | | | **12.0A – 15.0A** phase |
| **Motor Current Max Brake** | | | **-6.0A to -8.0A** phase |
| **Voltage Cutoff Start / End** | 11.5V / 10.5V | 11.5V / 10.5V | 11.5V / 10.5V |
| **FOC Hall Sensor Table** | Saved | Saved | Validated via Wizard |
| **Wheel Direction Normal/Invert** | | | Verified unloaded |

Teensy firmware maintains motion disarmed at boot until the operator arms the system via radio switch SwA with sticks centered.

## 4. Receiver and drive behaviour

For the complete illustrated switch layout, detailed operator guide, and safe startup checklist, see [FlySky Controller Operator Guide](CONTROLLER_OPERATOR_GUIDE.md).

Receiver stays in body: SERVO ->Lonely Binary channel 1->TeensyRX21, SENSOR <->channel 2<->pin 24. No receiver PPM output connects to either VESC.

| Channel | Assignment |
| --- | --- |
| CH1 | Steering |
| CH2 | Throttle |
| CH3 | Unused |
| CH4 | Manual dome rotation |
| CH5 / SwC (3-pos) | Duty rates35/70/100% |
| CH6 / SwA | Drive enable |
| CH7 / VrA | Mood/macro selection |
| CH8 / SwB | Trigger |
| CH9 / SwD | Auto Dome enable |
| CH10 | Unused |

Disable transmitter-side tank mixing: Teensy mixes throttle/steer. Confirm radio channel mapping in diagnostics.

At boot/fault, CH6 OFF <=1250us -> ON >=1750us -> CH1/2 neutral1460-1540us for 500ms is required. Mid-position disarms. All 14 iBUS channel fields (low 12 bits; newer receivers pack channels 15-18 in the top nibbles) must be 900-2100us; stale frames>250ms inhibit drive.

The50Hz mixer calculates L=throttle+steer, R=throttle-steer, divides both by max(1,abs(L),abs(R)), then applies selected rate and95% duty cap. This preserves turning proportions without independently clipping each side. Duty changes in both directions are limited to the commissioned slew (`slew`, permille/s): a partial stick reduction, or a lower rate on SwC, ramps down rather than stepping into hard regenerative braking. A centred stick, CH6 OFF, a fault or STOP still brakes at the commissioned brake current straight away.

Both feedback records are polled every 100ms, must be <=500ms old, supported and fault-free. A fault/stale record on either controller stops **both** feet and requires rearming. Neutral/stop sends **positive `COMM_SET_CURRENT_BRAKE` magnitude**, not `COMM_SET_DUTY(0)`, renewed every 20ms (well inside the 150ms VESC timeout, and leaving UART time for the telemetry polls). Normal motion uses duty commands.

### Signing off the VESC records

Drive stays disabled until both wheels have all four sign-offs (acceptance bits): `vesc_config` 4/5, `timeout_brake` 6/7, `direction` 8/9 and `reversal` 10/11 (left/right). Each one records that **you** checked that item in VESC Tool or on the stand against the values staged in the profile. Set the fields first (`/commissioning` Field ID/Wheel/Value, or `profile set FIELD VALUE WHEEL` over USB). Then, with CH6 OFF, CH9 OFF and sticks centred, accept each bit with the `/commissioning` **Accept bit** box or with `profile accept vesc_config_left` (and the other bit names) over USB. Finally press **Save Profile** (or run `profile save`). The saved profile takes effect at once; no reboot is needed. Changing a field later clears the sign-offs that depend on it.

## 5. Acceptance before floor driving

Complete [Commissioning](BODY_CONTROLLER_COMMISSIONING.md), including transmitter OFF and receiver data cable unplugged as **separate** tests. Configure receiver failsafe: CH1/2/3/4 centered, CH6 OFF, CH8 released, CH9 OFF. Valid failsafe frames must still disarm through CH6.

Test each VESC UART feedback disconnection separately; either failure must inhibit both feet. Stop UART commands and verify each controller's 150ms timeout action independent of Teensy. Reconnection does not rearm automatically.

Start at slow 35% duty on level open ground with the cutoff reachable. Record full-pack braking voltage/faults, actual stopping distance and ten-minute temperatures. Do not deliberately stall a wheel. Healthy manual feet may continue after ordinary dome-link loss, but explicit STOP/Faint/maintenance locks inhibit them and remain latched across a dome reboot.
