# Teensy Body Controller Wiring

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Flash both the Teensy 4.1 body controller and the AstroPixels Plus ESP32 with the current firmware before combined testing; nothing counts as bench-verified until the commissioning record is filled in.

The body controller is a **Teensy 4.1 in the Treedix socketed screw-terminal carrier [B09NXYWYK7](https://www.amazon.ca/dp/B09NXYWYK7)**. The AstroPixels ESP32 controller resides in the dome.

Use [Power Harness Guide](POWER_HARNESS_GUIDE.md) for power wiring, [Dome Wiring](DOME_WIRING_DIAGRAM.md) for PCA9685 and lighting, and [Commissioning](BODY_CONTROLLER_COMMISSIONING.md) for checks before motion.

## 1. Mounting and connector rules

Mount the carrier on standoffs in the body beside the receiver and body shifter. Leave room to reach the screw terminals, USB socket and Teensy's Program button. Keep motor phase cables and the amplifier away from receiver antennas and signal wiring.

Teensy plugs into the carrier sockets. If its male headers are not installed, solder the matching headers before inserting it. No perfboard or separate female sockets are needed. With power disconnected, confirm carrier labels by continuity to the corresponding Teensy pins; do not count terminal positions from a photograph.

Use short 22AWG copper signal/ground leads, properly sized ferrules at screw terminals, and labelled detachable device connectors. Use keyed connectors where available. Do not leave loose Dupont jumpers as the permanent harness. Confirm device pin labels before inserting each connector: a reversed three-pin plug can put 5V on a signal input.

Servo and motor power go through distribution wiring, not the Teensy carrier. Never solder-tin stranded ends that enter screw terminals. Clamp cables beside the carrier so pulling a harness does not pull its terminals.

## 2. Approved 5V power branches

Use the owned screw-terminal distribution and four inline holders [B0FDJYRGB7](https://www.amazon.ca/dp/B0FDJYRGB7). No separate 5V fuse boxes.

| Circuit | Load | Fuse | Feed / return |
| --- | --- | --- | --- |
| B-SERVO | Body dome-rotation servo | 5A | 16AWG to factory servo cable |
| B-LOGIC | Teensy, receiver, DFPlayer, body shifter | 2A | 18AWG shared feed; device leads below |
| D-SERVO | PCA9685 V+ / six holo servos | 5A | 16AWG to screw terminals |
| D-LOGIC | AstroPixels, Hall sensor, dome shifter | 3A | 18AWG motherboard feed; 22AWG sensor/reference leads |

Each fuse goes in the positive lead at the distribution takeoff. Grounds remain unfused. The electronics fuse output feeds an **insulated fan-out separate from the unfused positive bus**. Do not return its output to the same bus: that bypasses the fuse. Use unused isolated terminal positions or an insulated multiwire connector to make the fused fan-out.

Body device feeds: Teensy VIN/GND 22AWG, receiver 5V/GND 22AWG, DFPlayer VCC/GND 20AWG, shifter HV/GND 22AWG. Keep these short. Use the manufacturer's installed device leads rather than replacing them with thinner extensions.

Body and dome 5V positive rails never connect together. All grounds connect. Both bucks' negative outputs and inputs share the common ground; the converters are non-isolated.

## 3. Teensy carrier terminal map

UARTs cross: **TX at one device goes to RX at the other**. Both VESCs and DFPlayer have separate ground connections to body ground.

| Teensy terminal | Interface | Connect to | Signal |
| --- | --- | --- | --- |
| 1 | Serial1 TX | Left VESC COMM RX | 115200, 3.3V |
| 0 | Serial1 RX | Left VESC COMM TX | 115200, 3.3V |
| 8 | Serial2 TX | Right VESC COMM RX | 115200, 3.3V |
| 7 | Serial2 RX | Right VESC COMM TX | 115200, 3.3V |
| 14 | Serial3 TX | 1k ohm resistor, then DFPlayer RX | 9600, 3.3V |
| 15 | Serial3 RX | DFPlayer TX | 9600, 3.3V |
| 17 | Serial4 TX | Slip ring BODY CH3 | 115200, 3.3V |
| 16 | Serial4 RX | Slip ring BODY CH6 | 115200, 3.3V |
| 21 | Serial5 RX | Body shifter LV1 | 115200 iBUS input |
| 24 | Serial6 single-wire TX/RX | Body shifter LV2 | 115200 iBUS telemetry |
| 2 | Dome servo pulses | Body shifter LV3 | 50Hz servo pulses |
| VIN | Power input | B-LOGIC fused 5V | Regulated 5V |
| GND | Ground | Body ground distribution | Common reference |
| 3.3V | Reference supply | Body shifter LV | 3.3V |

**14 Teensy terminal connections:** 11 signal terminals plus VIN, GND and 3.3V. Serial5 TX20 and Serial6 RX25 are unused. Serial7/8 remain spare. USB diagnostics use no UART pins.

Only shifter LV is powered from Teensy 3.3V. Do not power receiver, servo or DFPlayer from Teensy output pins. Teensy signal pins are **not 5V tolerant**.

## 4. Selected logic shifters

Use **Lonely Binary [B0FFMLDYNY](https://www.amazon.ca/dp/B0FFMLDYNY)**: one four-channel module in the body and another in the dome. The old converters are being returned. No Pololu or TXS0108E substitution is part of this design.

The diagrams call low-side channels LV1-LV4 and high-side channels HV1-HV4. On boards labelled A/B, these mean **A1-A4 / B1-B4** respectively. Follow actual PCB labels, not left/right orientation.

These modules contain 10k ohm pull-ups; LV and HV are voltage **inputs**, not regulator outputs. They do not generate 3.3V. The [manufacturer documents 115200-baud serial with short jumpers](https://learn.lonelybinary.com/manuals/llc/the-slow-rising-edge).

| Body shifter | Connection |
| --- | --- |
| LV | Teensy 3.3V |
| HV | B-LOGIC fused 5V |
| GND | Body ground |
| HV1 / B1 | Receiver iBUS SERVO signal |
| LV1 / A1 | Teensy RX21 |
| HV2 / B2 | Receiver iBUS SENSOR signal |
| LV2 / A2 | Teensy pin 24 |
| LV3 / A3 | Teensy pin 2 |
| HV3 / B3 | Dome-rotation servo signal |
| Channel 4 | Unconnected |

The dome shifter uses dome fused D-LOGIC 5V at HV, ESP32 3.3V at LV and dome ground at GND. Front Hall signal goes to HV2/B2 (LV2/A2 to GPIO19); Rear Hall signal goes to HV3/B3 (LV3/A3 to GPIO18). Channels 1 and 4 are unused.

### Receiver power and input

Mount the FS-iA6B in the body. Feed its labelled positive/negative supply pins from B-LOGIC 5V and body ground. Its SERVO and SENSOR connectors share supply pins internally: one supply connection is enough; additional cables need signal and ground, not another power source.

SERVO is the receiver's **channel-data output**, not our dome servo connection. Teensy listens on RX21. Do not connect TX20 to that output.

### Single-wire telemetry

```text
Receiver SENSOR data <--> Body HV2/B2 | LV2/A2 <--> Teensy pin 24
Receiver ground -------- Body ground ----------- Teensy GND
```

The receiver polls; Teensy replies and releases the same wire. Teensy uses native half-duplex plus open drain:

```cpp
Serial6.begin(115200, SERIAL_8N1_HALF_DUPLEX);
Serial6.setTX(24, true);
```

RX25 is unused. Do not join separate push-pull TX/RX pins. Put the shifter beside Teensy/receiver and use short leads. No oscilloscope or logic analyzer is required; commissioning uses channel counters, telemetry replies and the handheld display.

Teensy supplies external pack voltage from the lower of two fresh VESC readings and the hottest VESC MOSFET temperature. FS-CVT01 is unused; F6 is empty. Neither eRPM nor voltage is a road-speed or battery-percentage measurement.

## 5. Direct VESC and audio links

Connect TX/RX/GND on **each** COMM connector. VESC power pins 5V/3.3V and ADC/ADC2 stay unconnected. Leave RECEIVER/SIN, CAN and SWD unused. Set the internal CAN switch OFF. There is one shared VESC battery power pair, not one per wheel.

Both VESC UART links are 3.3V and bypass shifters. Configure each controller separately in [VESC Drive Setup](VESC_DRIVE_INTEGRATION.md).

DFPlayer VCC uses body fused 5V, but its UART is **3.3V**. Connect Teensy TX14 through the 1k ohm resistor to DFPlayer RX; connect DFPlayer TX directly to Teensy RX15. Do not put a shifter in this UART. BUSY is unconnected in version 1.

## 6. Slip ring and dome link

Use end-to-end continuity to identify the six contacts; wire colours alone do not establish channel numbers.

| Contact | BODY end | DOME end | Function |
| --- | --- | --- | --- |
| CH1 | F4 7.5A fused battery positive | Dome buck IN+ | Dome 12V feed |
| CH2 | Body ground bus | Dome ground / buck IN- | Common return |
| CH3 | Teensy TX17 | ESP32 GPIO16 | Body to dome serial |
| CH4 | Unconnected | Unconnected | Spare |
| CH5 | Unconnected | Unconnected | Spare |
| CH6 | Teensy RX16 | ESP32 GPIO17 | Dome to body serial |

Serial is 115200, 8N1, two-way 3.3V, no shifter. Use AstroPixels' dedicated serial RX16/TX17 connections; do not connect a serial-header 5V pin. Ring CH2 provides common ground. Insulate both ends of CH4/CH5 separately and label SPARE.

GPIO5/AUX3 and GPIO4/AUX2 are spare. GPIO19 is Front Hall input, GPIO18 is Rear Hall input. GPIO21/22 remain I2C. The radio receiver, VESC motor controllers, audio player, and dome rotation servo connect directly to the Teensy in the body.

## 7. First-time harness assembly

Work with battery, USB and charger disconnected. Label both ends before terminating.

| Harness | Assemble | Unpowered check |
| --- | --- | --- |
| TEENSY-POWER | B-LOGIC 5V -> VIN; GND -> ground | Correct carrier terminals; no VUSB/VIN re-bridge |
| RADIO-IBUS | SERVO -> shifter channel 1 -> RX21 | No TX20 connection |
| RADIO-SENS | SENSOR -> shifter channel 2 -> pin 24 | One signal wire; RX25 unused |
| LEFT-VESC | TX1 -> RX; TX -> RX0; ground | VESC supply pins unconnected |
| RIGHT-VESC | TX8 -> RX; TX -> RX7; ground | Separate from left UART |
| AUDIO-UART | TX14 -> 1k resistor -> RX; TX -> RX15 | Resistor only in command line |
| DOME-SERVO | B-SERVO 5V/GND; shifter HV3 signal | Power off carrier; gear initially disengaged |
| DOME-LINK | CH3 / CH6 UART, CH1 / CH2 supply | Contact continuity; CH4/5 insulated |

Perform [VUSB/VIN isolation and commissioning](BODY_CONTROLLER_COMMISSIONING.md) before combining USB and externally powered VIN. Cutting the link is a one-time modification; Teensy stays installed for subsequent USB programming.
