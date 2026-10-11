# Power

This guide covers the battery, fuses, 5V supplies, wire sizes, audio power and charging. Signal connections are in [Wiring](WIRING.md).

## 1. Parts

| Part | Selection |
| --- | --- |
| Battery | Renogy RBT1220LFP-TM, 12.8V 20Ah LiFePO4, 20A continuous |
| Main fuse | 25A, in a holder rated 30A / 32V DC or higher, within 150mm of battery positive |
| Master cutoff | DC load-break switch rated 30A at 16V or higher, reachable from outside the droid |
| 12V fuse block | Six-way automotive blade block with an integrated negative bus |
| 5V supplies | Two fixed 5V 10A bucks ([B09T954ZV1](https://www.amazon.ca/dp/B09T954ZV1)), one in the body, one in the dome; 10–35V input |
| 5V fuses | Four inline blade holders ([B0FDJYRGB7](https://www.amazon.ca/dp/B0FDJYRGB7)) with standard ATO/ATC fuses |
| Slip ring | Senring H3086, six contacts, 10A per contact, 17AWG leads |

## 2. Main supply

```text
Battery + -> 25A main fuse -> master cutoff -> fuse block + (10AWG)
Battery - -> fuse block negative bus (10AWG, unfused)
```

## 3. 12V fuse block

| Fuse | Load | Rating | Wire |
| --- | --- | --- | --- |
| F1 | Dual FSESC4.20 battery input | 15A | 12AWG |
| F2 | Spare | — | — |
| F3 | Body 5V buck input | 7.5A | 16AWG |
| F4 | Dome 5V buck input, through slip ring CH1 | 7.5A | 16AWG; return through CH2 |
| F5 | Audio amplifier | 5A | 18AWG |
| F6 | Optional FS-CVT01 voltage sensor lead | 1A | Sensor's own lead |

F1 feeds the controller's single battery input pair. Nothing else powers the VESC. The VESC receiver leads carry signal and ground only.

## 4. 5V supplies

Each buck feeds its own distribution block. Every 5V branch is fused at the point it leaves the block.

| Branch | Loads | Fuse | Wire |
| --- | --- | --- | --- |
| B-SERVO | Dome servo | 5A | 16AWG |
| B-LOGIC | Receiver, DFPlayer | 2A | 18AWG feed, 20/22AWG to each device |
| D-SERVO | PCA9685 V+ (six holo servos) | 5A | 16AWG |
| D-LOGIC | AstroPixels board, dome Hall sensor, level shifter HV | 3A | 18AWG feed, 22AWG to sensor and shifter |

```text
BODY buck OUT+ -> body positive group -> B-SERVO 5A -> dome servo +
                                      -> B-LOGIC 2A -> fused fan-out -> receiver 5V
                                                                     -> DFPlayer VCC

DOME buck OUT+ -> dome positive group -> D-SERVO 5A -> PCA9685 V+ terminal
                                      -> D-LOGIC 3A -> fused fan-out -> AstroPixels 5V
                                                                     -> Hall sensor 5V
                                                                     -> level shifter HV
```

Rules:

- **The body and dome 5V positives never connect.** Grounds are common everywhere, through slip ring CH2.
- Fuse every positive branch at its takeoff, before the smaller wire.
- Grounds are never fused.
- Connect devices only to the fused fan-out. Never jumper a fan-out back to the unfused positive group.
- Fuses: two 5A, one 2A, one 3A, plus spares.

### Making the terminal blocks

The dual-row terminal strips connect opposite screws in pairs. With power disconnected:

1. Find the connected pairs with a continuity meter. Label them.
2. Pick positions for the positive group, the ground group and the fused fan-out.
3. Join positive positions with 12AWG jumpers. Join ground positions separately. Check there is no continuity between positive and ground.
4. Buck OUT+ to the positive group, OUT− to the ground group, in 12AWG.
5. Run each positive takeoff through its inline fuse to its load or fan-out.

### PCA9685 servo power

- 16AWG from D-SERVO to the PCA9685 **V+ screw terminal**, with a 16AWG return to its ground screw.
- The board's copper plane feeds all channel headers. Each holo servo plugs straight into its header.
- The PCA9685's VCC pin is 3.3V logic from the ESP32. It is separate from V+.

## 5. Slip ring

| Contact | Body end | Dome end | Use |
| --- | --- | --- | --- |
| CH1 | F4 7.5A fused 12V | Dome buck IN+ | Dome power |
| CH2 | Body ground bus | Dome ground, buck IN− | Common ground |
| CH3–CH6 | Signals | Signals | See [Wiring](WIRING.md) §2 |

Identify contacts by continuity, not colour. Extend the power contacts with 16AWG. Never parallel contacts.

## 6. Current budget

| Load | Draw |
| --- | --- |
| Lights, ESP32, holo servos, dome servo, amplifier, receiver, DFPlayer | 5–6A peak |
| Drive: two VESCs at 5.0–5.5A battery current each | 10–11A |
| Headroom below the battery's 20A limit | 3–5A |

The VESC **Battery Current Max** setting enforces the drive budget (see [Drive Setup](DRIVE_SETUP.md)).

## 7. Wire allowances

Measure the actual routes before cutting.

| Gauge | Allowance | Use |
| --- | --- | --- |
| 10AWG | 4m red, 2m black | Battery and main supply |
| 12AWG | 15m assorted | Motor phases (six runs of 2m), VESC supply, buck 5V trunks |
| 16AWG | 6m red, 6m black | Buck inputs, slip ring power extensions, servo trunks |
| 18AWG | 3m red, 3m black | Amplifier supply, AstroPixels and fan-out feeds |
| 20AWG | 2m red, 2m black | DFPlayer power, speaker |
| 22AWG five-core | 5m | Motor Hall sensor extensions (two of 2m) |
| 22AWG singles | As measured | Receiver, serial and servo signals, level shifter, Hall sensor, I2C |

Workmanship:

- Use stranded copper, not CCA.
- Crimp with the correct tool, pull-test every crimp and heatshrink every joint.
- Use ferrules in screw terminals. Never tin wire ends for screw terminals.
- Grommet every hole in the shell. Leave service loops at the shoulder and leg joints.
- Keep signal wires away from motor phase wires and receiver antennas.
- Label both ends of every harness.

## 8. Audio

The HF82 (TPA3110) amplifier ([B09F2XR9MN](https://www.amazon.ca/dp/B09F2XR9MN)) runs from F5. The DFPlayer runs from B-LOGIC.

```text
DFPlayer DAC_L -> isolator input tip    -> isolator output tip    -> amp LINE L
DFPlayer GND   -> isolator input sleeve -> isolator output sleeve -> amp LINE GND
Amp L+ / L-    -> speaker + / -
```

- The [BESIGN B06XQYN77L](https://www.amazon.ca/dp/B06XQYN77L) ground-loop isolator sits in the line. No wire bypasses it.
- The TRS ring (right channel) is unused.
- The amplifier output is bridged: neither speaker wire connects to ground.
- DFPlayer SPK1/SPK2 are not used.
- The amplifier has no volume control. The transmitter's VrB dial sets the DFPlayer's digital volume (0–30).

## 9. Charging

- **Charger:** [ECO-WORTHY B09SYNQNC1](https://www.amazon.ca/dp/B09SYNQNC1), 14.4V 9A, in **LiFePO4 mode**.
- **Connection:** through its SAE ring-terminal lead, with the lead's 10A fuse close to battery positive. Check SAE polarity with a meter before the first connection.
- **The charging lead connects straight to the battery.** The master cutoff does not isolate it.
- **While charging:** master cutoff off.
- **Before driving or servicing:** unplug the charger.
- **Never:** use lead-acid or desulfation modes, or charge below 0°C.
- **Cover the SAE socket** when it isn't in use.
