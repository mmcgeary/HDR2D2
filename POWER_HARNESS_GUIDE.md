# R2-D2 Power Harness: Assembly and Fuse Schedule

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Flash both the Teensy 4.1 body controller and the AstroPixels Plus ESP32 with the current firmware before combined testing; nothing counts as bench-verified until the commissioning record is filled in.

This is the approved first-assembly schedule. Use [Body Controller Wiring](BODY_CONTROLLER_WIRING.md) for the exact Teensy terminals and [Commissioning](BODY_CONTROLLER_COMMISSIONING.md) before applying motion. The [interactive inspector](wiring_visualizer.html) supplies terminal-to-terminal wiring.

## 1. Main supply

| Part | Selection / connection |
| --- | --- |
| Battery | Renogy RBT1220LFP-TM, nominal12.8V20Ah LiFePO4;20A continuous limit |
| Main positive | Battery ->25A main fuse -> accessible master cutoff ->12V fuse block;10AWG |
| Main fuse location | Within 150mm of battery positive;10AWG holder rated >=30A /32V DC |
| Cutoff | DC load-break rated >=30A at >=16V; reachable outside droid |
| Main negative | Battery -> fuse block negative bus;10AWG, unfused |
| Fuse block | One six-way automotive blade block with integrated negative bus |
| Bucks | Two fixed5V10A [B09T954ZV1](https://www.amazon.ca/dp/B09T954ZV1), one body/one dome; listed input 10-35V |
| Slip ring | Six contacts,10A/contact,17AWG installed leads;16AWG power extensions |

Fuses across the power harness are sized as best estimates based on planned draw for each subsystem. Total continuous battery draw should be kept within the 20A battery rating by configuring motor limits in VESC Tool and accounting for lighting, audio, and servo loads.

## 2. Approved fuse schedule

### Main and 12V fuse block

| Circuit | Load | Fuse | Copper wire / return |
| --- | --- | --- | --- |
| MAIN | Fuse block through cutoff | 25A | 10AWG |
| F1 | Dual VESC shared battery input | 15A | 12AWG |
| F2 | Empty spare | None | No connection |
| F3 | Body buck IN+ | 7.5A | 16AWG to buck; ground to negative bus |
| F4 | Dome buck IN+ through ring CH1 | 7.5A | 16AWG extensions; CH2 ground return |
| F5 | HF82 amplifier power | 5A | 18AWG |
| F6 | Empty spare | None | FS-CVT01 unused |

F1 powers **one shared VESC input pair**; do not add a second supply to a COMM or RECEIVER connector. Do not parallel F1/F2 or ring contacts.

### Four local 5V inline fuses

| Circuit | Load | Fuse | Feed / return |
| --- | --- | --- | --- |
| B-SERVO | Body dome-rotation servo | 5A | 16AWG to servo factory leads |
| B-LOGIC | Teensy, receiver, DFPlayer, body shifter | 2A | 18AWG fan-out feed;20/22AWG short device leads |
| D-SERVO | PCA9685 V+ / six holo servos | 5A | 16AWG to PCA screw terminals |
| D-LOGIC | AstroPixels, Hall sensor, dome shifter | 3A | 18AWG motherboard;22AWG short sensor/reference leads |

Use the four owned standard blade inline holders [B0FDJYRGB7](https://www.amazon.ca/dp/B0FDJYRGB7), with 12AWG holder leads. No separate 5V fuse boxes. The indexed included assortment starts at 5A: obtain matching **standard ATO/ATC2A and3A**, not mini-blade fuses. Fit two 5A, one 2A and one 3A; keep matching spares.

Put every fuse in the positive branch **at its takeoff, before the smaller cable**. Use a correctly sized reducing crimp to join holder leads to the branch. Do not force a12AWG holder lead into an undersized terminal. Ground branches remain unfused.

### Make the owned terminal blocks into the required groups

The uxcell dual-row strip may connect each opposite pair of screws, not every terminal in a row. With all power disconnected:

1. Measure continuity between opposite screws and adjacent positions. Identify the actual connections.
2. Reserve isolated positions for positive, ground and fused-electronics fan-out. Label them.
3. Join the intended positive positions with 12AWG copper jumpers; separately join intended ground positions. Confirm no positive-to-ground continuity.
4. Buck OUT+ -> unfused local positive group; buck OUT- -> ground group. Use12AWG trunks.
5. Positive takeoff -> servo inline fuse -> servo supply. Positive takeoff -> electronics inline fuse -> **a separate insulated fused-electronics fan-out**.
6. Connect electronics only to the fused fan-out. **Do not jumper its output back to the unfused positive group**: that defeats the fuse.

Use available isolated positions or insulated multiwire connectors for the small fused fan-outs. Existing blocks are retained; no new fuse box is needed.

```text
BODY buck OUT+ -> BODY unfused positive -> B-SERVO 5A -> body dome servo
                                      -> B-LOGIC 2A -> insulated logic fan-out
                                                       -> Teensy VIN
                                                       -> receiver5V
                                                       -> DFPlayer VCC
                                                       -> body shifter HV

DOME buck OUT+ -> DOME unfused positive -> D-SERVO 5A -> PCA9685 V+ terminal
                                      -> D-LOGIC3A -> insulated logic fan-out
                                                       -> AstroPixels 5V
                                                       -> Two Hall sensors 5V / dome shifter HV
```

The two 5V positive rails never join. Common ground connects throughout via ring CH2. These bucks are non-isolated; their input/output negatives are common.

### PCA9685 supply and servo checks

Run 16AWG from D-SERVO 5A to PCA9685 **V+ screw terminal** and 16AWG return to its ground screw. VCC is a separate 3.3V logic supply from ESP32. Do not feed servo power through I2C/header wiring.

Each servo plugs directly into its designated 3-pin channel header (Channels 0–5) using its factory 3-pin servo plug. The green screw terminal is exclusively for the main 16AWG 5V feed and ground return; the board's internal copper plane powers all 16 channel headers. Connect/test one servo at a time, then all six together through their calibrated travel. Check for mechanical binding, excessive heating, or travel limits. If a fuse blows, investigate and resolve mechanical binding or short circuits rather than increasing the fuse rating.

## 3. Slip ring

| Contact | BODY end | DOME end | Function |
| --- | --- | --- | --- |
| CH1 | F4 7.5A fused battery positive | Dome buck IN+ | Dome 12V feed |
| CH2 | Body ground bus | Dome ground / buck IN- | Common return |
| CH3 | Teensy TX17 | ESP32 GPIO16 | Body to dome serial |
| CH4 | Unconnected | Unconnected | Spare |
| CH5 | Unconnected | Unconnected | Spare |
| CH6 | Teensy RX16 | ESP32 GPIO17 | Dome to body serial |

Identify contacts by continuity, not colours. UART uses3.3V115200-baud8N1. No shifter or5V is attached to either signal contact. CH4/5 are insulated at **both** ends. Keep ring signal wiring separate from high-current motor cables.

## 4. Wire quantities and assembly

Measure actual paths before cutting; these are shopping allowances, not required run lengths.

| Copper gauge | Allowance | Use |
| --- | --- | --- |
| 10AWG | 4m red,2m black | Battery/main supply and return |
| 12AWG | 15m assorted | Six approximately2m phase runs, VESC supply and buck5V trunks |
| 16AWG | 6m red,6m black | Buck inputs, ring power extensions and servo trunks |
| 18AWG | 3m red,3m black | Amp supply, AstroPixels and electronics fan-outs |
| 20AWG | 2m red,2m black | DFPlayer power and speaker |
| 22AWG five-core | 5m | Two approximately2m Hall extensions |
| 22AWG singles | Measured short runs | UART, receiver supply, shifters, Hall and I2C |

Use stranded copper, not CCA. Secure battery, converters, fuse block and carriers before wiring. Protect shell holes with grommets; leave service loops at leg/shoulder joints. Keep phase wiring away from signal wiring and antennas. Crimp with the correctly sized tool, pull-test each crimp and insulate joints with heatshrink. Use ferrules at screw terminals; do not solder-tin those ends.

Converters and terminals need airflow and strain relief. Label both ends of each harness with circuit and connector pin names.

## 5. Audio power and signal path

HF82 TPA3110 amplifier [B09F2XR9MN](https://www.amazon.ca/dp/B09F2XR9MN) uses F5 battery power and common ground. DFPlayer uses B-LOGIC fused 5V.

```text
DFPlayer DAC_L -> input TRS tip -> BESIGN isolator -> output TRS tip -> amp LINE L
DFPlayer GND   -> input sleeve                    -> output sleeve  -> amp LINE GND
Amp L+ / L-   -> speaker + / -
```

Use the [BESIGN B06XQYN77L](https://www.amazon.ca/dp/B06XQYN77L) line isolator. Do not add an external sleeve jumper around it. TRS ring/right channel is unused for this mono connection.

Neither amplifier speaker output connects to ground; it is a bridge output. DFPlayer SPK1/SPK2 never go into an amplifier line input. Default digital volume 10/30 and Wi-Fi volume control replace a separate potentiometer.

The1k ohm resistor in Teensy TX14 -> DFPlayer RX is a serial command-line resistor, not a3.3V/5V converter.

## 6. Power-up sequence

Follow the detailed [commissioning procedure](BODY_CONTROLLER_COMMISSIONING.md).

First test continuity with all sources disconnected. Energize the main bus with loads disconnected, then F3/F4 with all four5V output fuses removed. Measure each buck output 4.9-5.1V. Turn off before adding loads.

Power electronics via B-LOGIC 2A/D-LOGIC3A before servos; do not plug computer USB into mounted ESP32. Teensy needs the approved VUSB/VIN cut and carrier isolation check before ordinary USB is used with external 5V.

Only after compatible firmware and individual subsystem checks: fit B-SERVO 5A/D-SERVO 5A, then F1 for unloaded VESC tests. Test transmitter-off, each lost UART and physical stop behaviour before floor driving. A capacitor is optional troubleshooting for observed supply transients, not a required purchase.

## 7. Charging

Use [ECO-WORTHY B09SYNQNC1](https://www.amazon.ca/dp/B09SYNQNC1),14.4V9A, **LiFePO4 mode**, through the supplied SAE ring-terminal harness with 10A inline fuse close to battery positive. Confirm SAE polarity with a meter before connection.

This charging harness attaches directly to the battery: master cutoff does not isolate it. Cutoff OFF while charging; unplug charger before driving or servicing. Never use lead-acid repair/desulfation modes or charge below0C. Protect the live SAE socket from shorts.
