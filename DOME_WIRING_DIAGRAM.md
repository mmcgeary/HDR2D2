# Body/Dome Wiring and AstroPixels Connections

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Both the Teensy 4.1 body controller and AstroPixels Plus ESP32 firmware are fully implemented; flash both boards before combined testing.

Use [Body Controller Wiring](BODY_CONTROLLER_WIRING.md) for Teensy terminals and [Power Harness](POWER_HARNESS_GUIDE.md) for exact fuse/distribution wiring. The [interactive inspector](wiring_visualizer.html) is the terminal graph.

## 1. Body components

Mount Teensy 4.1/Treedix, FS-iA6B receiver, body Lonely Binary four-channel shifter, DFPlayer and dual VESC in the body. The dome-rotation servo also stays in the body.

Body buck OUT+ feeds two branches: B-SERVO 5A for dome servo and B-LOGIC 2A for Teensy VIN, receiver, DFPlayer and body shifter HV. Teensy 3.3V supplies shifter LV. Grounds common. Body shifter channels: receiver SERVO ->HV1/LV1->RX21; receiver SENSOR <->HV2/LV2<->pin 24; pin 2->LV3/HV3->dome servo signal. Fourth channel unused.

Each VESC gets direct3.3V TX/RX/GND: left TeensyTX1/RX0, right TX8/RX7. No CAN master/slave forwarding; internal CAN switch OFF. TeensyTX14->1k resistor->DFPlayerRX; DFPlayerTX->RX15.

## 2. Slip ring

| Contact | BODY end | DOME end | Function |
| --- | --- | --- | --- |
| CH1 | F4 7.5A fused battery positive | Dome buck IN+ | Dome 12V feed |
| CH2 | Body ground bus | Dome ground / buck IN- | Common return |
| CH3 | Teensy TX17 | ESP32 GPIO16 | Body to dome serial |
| CH4 | Unconnected | Unconnected | Spare |
| CH5 | Unconnected | Unconnected | Spare |
| CH6 | Teensy RX16 | ESP32 GPIO17 | Dome to body serial |

Verify numbering end-to-end with a multimeter. Insulate CH4/5 separately at both ends. CH3/6 use3.3V1152008N1; no translator. GPIO16/17 are the dedicated AstroPixels serial pins. Leave the header voltage pin disconnected;5V must never enter a UART pin.

## 3. Dome power and selected shifter

CH1/2 -> dome buck IN+/IN-. Buck OUT+/OUT- -> owned dome 5V/GND terminal groups using12AWG trunks. D-LOGIC3A feeds AstroPixels 5V screw terminal, Hall supply and shifter HV; D-SERVO 5A separately feeds PCA V+.

Use a four-channel **Lonely Binary B0FFMLDYNY** module; the old modules are being returned. HV/LV are externally supplied references, not regulator outputs.

| Dome shifter terminal | Wire |
| --- | --- |
| HV | D-LOGIC fused 5V |
| LV | ESP32 3.3V |
| GND | Dome ground |
| HV2 / B2 | KY-003 Front Hall signal (0°) |
| LV2 / A2 | GPIO19 / AUX5 |
| HV3 / B3 | KY-003 Rear Hall signal (180°) |
| LV3 / A3 | GPIO18 / AUX4 |
| Channels 1/4 | Unconnected |

Two KY-003 Hall sensors (Front 0° and Rear 180°) detect the single dome ring magnet. Hall VCC is D-LOGIC 5V and ground is dome ground. Set/record detected polarity during commissioning. Hall state is sent to Teensy every 20ms / on change for body-controlled homing and calibration.

## 4. PCA9685: separate logic and servo power

The AstroPixels I2C header letters are **G** ground, **V** supply, **C** clock, **D** data. Its voltage header pin is not the PCA logic supply.

| AstroPixels / source | PCA9685 terminal |
| --- | --- |
| I2C D / GPIO21 | SDA |
| I2C C / GPIO22 | SCL |
| I2C G | Logic GND |
| ESP32 3.3V | VCC |
| D-SERVO 5A fused 5V | V+ screw terminal |
| Dome ground,16AWG | Ground screw terminal |

**Leave I2C V disconnected.** No level shifter on I2C. Logic ground and servo ground are common on PCA9685; the heavy return still goes directly to its ground screw terminal, not through the small I2C ground lead.

Use16AWG positive/negative power trunks and MG90S factory plugs. Align each plug to the labelled GND/V+/signal rows; wire colours do not override PCB labels. The PCA9685 is address **0x40**,50Hz servo output.

| PCA channel | Servo |
| --- | --- |
| 0 | Front pan |
| 1 | Front tilt |
| 2 | Rear pan |
| 3 | Rear tilt |
| 4 | Top pan |
| 5 | Top tilt |
| 6-15 | Unused |

Test one servo at a time with horns removed, calibrate travel before mounting, then test all six without hitting mechanical stops. Stop for heat at the board/terminal/lead or any jam. Do not increase D-SERVO 5A after a fault. The shared fuse does not individually limit every servo overload.

## 5. ESP32 signal map

| Pin | Target function / AstroPixels connection |
| --- | --- |
| GPIO16 | Serial2 RX; ringCH3 from TeensyTX17 |
| GPIO17 | Serial2 TX; ringCH6 to TeensyRX16 |
| GPIO18 | Rear Hall through dome shifter channel 3 / AUX4 |
| GPIO19 | Front Hall through dome shifter channel 2 / AUX5 |
| GPIO21 | I2C SDA / D |
| GPIO22 | I2C SCL / C |
| GPIO15 | Front logic displays / FLD |
| GPIO33 | Rear logic display / RLD |
| GPIO32 | Front PSI / FPSI |
| GPIO23 | Rear PSI / RPSI |
| GPIO25 | Front holo LED / FHP |
| GPIO26 | Rear holo LED / RHP |
| GPIO27 | Top holo LED / THP |
| GPIO2/4/5 | Unused / spare |

The dedicated body protocol is the sole Serial2 reader at 115200. The firmware migration has disabled the old MarcDuino serial reader on that UART; web/internal command dispatch remains. GPIO5 is no longer a UART input, avoiding use of a boot-strapping pin.

## 6. Programming and test order

Flash ESP32 **removed from AstroPixels**, then unplug USB before reinstalling with all power off. Never let a computer USB port power assembled lights/servos. Installed ESP32 updates use the acknowledged maintenance/OTA path.

Teensy uses external VIN with the factory VUSB/VIN link cut once and carrier checked for re-bridging; ordinary USB then supplies data/debug only. See the official cut illustration and exact continuity checks in [Commissioning](BODY_CONTROLLER_COMMISSIONING.md).

First power logic only; validate UART/Hall/I2C before adding servo power. Rotate dome through full turns while watching communication counts/errors. Complete the failure matrix and unloaded motion checks before engaging drive gears or floor testing.
