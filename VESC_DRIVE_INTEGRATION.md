# Dual FSESC4.20: Two Independent UARTs

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Both the Teensy 4.1 body controller and AstroPixels Plus ESP32 firmware are fully implemented; flash both boards before combined testing.

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

## 3. Configure each half over its own USB

Elevate both wheels securely. Keep Teensy UART harnesses disconnected during initial VESC Tool setup. Connect to **one USB port at a time**; never infer right-side settings from left-side results.

1. Record hardware/firmware version and export original motor/app configuration. Do not update firmware until the installed hardware version is identified and supported.
2. Identify which motor this USB controls; label it LEFT or RIGHT.
3. Set **App to use: UART**, baud **115200**, timeout **150ms**. Both halves use UART; right is not "No App."
4. Disable multiple-ESC/CAN forwarding and CAN status broadcasting. Internal switch remains OFF. IDs1/2 are labels, not a forwarding route.
5. Set timeout brake current explicitly, then test command-loss braking. A timeout with zero brake current may coast.
6. Set and record motor/battery/regen/brake/voltage limits for this hardware and pack before the motor detection wizard. Start with conservative low-energy detection settings supported by VESC Tool; do not accept a high-current wizard default blindly.
7. Run individual FOC detection with Hall sensors, save results, then verify wheel direction unloaded. Export final settings.
8. Repeat through the other USB port. Reconnect direct UARTs only with power off.

### Current and voltage settings are commissioning records

Battery current and motor-phase current are different. A low battery limit can still allow high phase current at low duty. Regeneration charges the battery; at full charge it can cause overvoltage or BMS disconnection.

The earlier suggested5A battery maximum per side is a **starting allocation** of10A combined, not a proven motor current limit or guarantee that all other loads fit the20A pack budget. Record the final limits after individual low-speed and thermal checks. The earlier12A phase/-2.5A regen/-5A brake suggestions are not established safe ratings and are not automatic defaults.

Do not run a floor test while this table is blank. Use motor/controller identification, low-speed tests and full-pack braking to fill it; if detection cannot be configured without an unsupported guess, stop before running the wizard.

| Setting | Left | Right |
| --- | --- | --- |
| Hardware / firmware version | | |
| Supported values-layout/profile ID | | |
| Motor detection current and results | | |
| Battery current maximum | | |
| Battery regenerative current limit | | |
| Motor phase-current maximum | | |
| Motor brake-current limit | | |
| Voltage cutoffs / overvoltage behaviour | | |
| Normal neutral brake command | | |
| App timeout / timeout brake current | 150ms / record current | 150ms / record current |
| FOC Hall detection / wheel direction | | |

Teensy firmware must ship **motion disabled** until this installed-controller profile is commissioned. Unknown/truncated telemetry layouts inhibit motion; they must not be parsed using guessed field offsets.

## 4. Receiver and drive behaviour

For the complete illustrated switch layout, plain-language operator guide, and safe startup checklist, see [FlySky Controller Operator Guide](CONTROLLER_OPERATOR_GUIDE.md).

Receiver stays in body: SERVO ->Lonely Binary channel 1->TeensyRX21, SENSOR <->channel 2<->pin 24. No receiver PPM output connects to either VESC.

| Channel | Assignment |
| --- | --- |
| CH1 | Steering |
| CH2 | Throttle |
| CH3 | Unused |
| CH4 | Manual dome rotation |
| CH5 / SwB | Duty rates35/70/100% |
| CH6 / SwA | Drive enable |
| CH7 / VrA | Mood/macro selection |
| CH8 / SwC | Trigger |
| CH9 / SwD | Auto Dome enable |
| CH10 | Unused |

Disable transmitter-side tank mixing: Teensy mixes throttle/steer. Confirm radio channel mapping in diagnostics.

At boot/fault, CH6 OFF <=1250us -> ON >=1750us -> CH1/2 neutral1460-1540us for 500ms is required. Mid-position disarms. Validate all 14 iBUS channel fields900-2100us; stale frames>250ms inhibit drive.

The50Hz mixer calculates L=throttle+steer, R=throttle-steer, divides both by max(1,abs(L),abs(R)), then applies selected rate and95% duty cap. This preserves turning proportions without independently clipping each side.

Both feedback records are polled every 100ms, must be <=500ms old, supported and fault-free. A fault/stale record on either controller stops **both** feet and requires rearming. Neutral/stop sends **positive `COMM_SET_CURRENT_BRAKE` magnitude**, not `COMM_SET_DUTY(0)`. Normal motion uses duty commands.

## 5. Acceptance before floor driving

Complete [Commissioning](BODY_CONTROLLER_COMMISSIONING.md), including transmitter OFF and receiver data cable unplugged as **separate** tests. Configure receiver failsafe: CH1/2/3/4 centered, CH6 OFF, CH8 released, CH9 OFF. Valid failsafe frames must still disarm through CH6.

Test each VESC UART feedback disconnection separately; either failure must inhibit both feet. Stop UART commands and verify each controller's 150ms timeout action independent of Teensy. Reconnection does not rearm automatically.

Start at slow 35% duty on level open ground with the cutoff reachable. Record full-pack braking voltage/faults, actual stopping distance and ten-minute temperatures. Do not deliberately stall a wheel. Healthy manual feet may continue after ordinary dome-link loss, but explicit STOP/Faint/maintenance locks inhibit them and remain latched across a dome reboot.
