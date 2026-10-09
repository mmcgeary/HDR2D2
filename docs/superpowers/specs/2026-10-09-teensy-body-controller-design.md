# Teensy Body Controller Design

**Status:** Hardware/fuses and operator behaviour approved in conversation. Revised firmware design and implementation plan incorporate those decisions; firmware is unchanged. The physical documents completed earlier still require the second Hall/GPIO18 and radio-policy revisions listed in firmware Task10. Nothing has been wired.

**Execution order:** Review the first-build design, update physical documentation, then build firmware. Assemble and commission the hardware after both firmware builds are ready.

**First-build scope:** There is no installed harness to preserve or disconnect. Prior wiring assignments, fuse values, connector choices and assembly methods are design inputs, not compatibility requirements. Change them when the result is safer, simpler or easier to service. Keep the purchased Teensy 4.1 and Treedix carrier; identify any additional parts and changes to explicit user selections before incorporating them.

## 1. Decisions

### Approved dome-position revision

Use **two KY-003 / 3144E Hall modules and one magnet**, not an encoder. Mount both modules on the rotating dome, 180 degrees apart, and the single magnet on the stationary body structure at their sensing radius. Front detection identifies dome-forward; rear detection identifies dome-backward. Align both sensing faces so the same magnet pole reliably triggers each module, with clearance throughout rotation.

Both modules: `-` to dome ground; centre `+` to D-LOGIC fused 5V. Front `S` -> dome shifter HV2/B2 -> LV2/A2 -> ESP32 GPIO19. Rear `S` -> HV3/B3 -> LV3/A3 -> GPIO18. Shifter HV=5V, LV=ESP32 3.3V, ground common. GPIO18 is now assigned, not spare; only shifter channels 1 and 4 remain unused.

These provide two discrete reference positions, not continuous angle feedback. Estimate intermediate movement from calibrated direction/speed/time and re-reference when a sensor activates. Do not report estimated angles as measured positions.

ESP32 selects autonomous idle/event dome motion. Teensy owns the actuator, immediate manual override and driving alignment priority. CH9/SwD enables automatic dome motion independently of CH6 foot enable. Holos have no radio controls. The operator behaviour below incorporates the subsequent approved decisions. Firmware implementation remains a separate phase.

The body controller is a **Teensy 4.1 in the ordered Treedix socketed screw-terminal carrier**. Do not replace this with an Arduino, another ESP32, or a perfboard carrier.

The FlySky FS-iA6B receiver moves into the body. Teensy controls both foot motors, the dome rotation servo, and the DFPlayer Mini. AstroPixels ESP32 controls dome lighting, the six holo servos, Wi-Fi, the dashboard, and behavioural choreography.

Each VESC connects directly to a separate Teensy UART. Both links carry commands and feedback. Internal CAN forwarding is removed. Set the internal CAN switch OFF and disable multi-controller command forwarding on both controllers.

Two slip-ring contacts carry the body/dome serial link. Two contacts become spares.

The user selected these additional changes during planning:

- **CH6 / SwA is drive enable.** OFF stops and disarms the feet. ON permits drive after throttle and steering remain centered for 500ms. Receiver failsafe sets CH6 OFF.
- **VESC voltage supplies handheld battery telemetry.** FS-CVT01 is not installed in the active wiring. F6 becomes an empty spare. Keep the sensor's BOM entry and mark it unused in this architecture.
- Documentation uses plain, declarative language. State selections, connections, ownership, and measured setup steps. Do not surround documented specifications with hypothetical exceptions.
- **Keep the owned screw-terminal 5V distribution in body and dome.** Use selective inline fuse holders where branch protection is needed; do not add separate 5V fuse boxes.
- **All four 5V fuse ratings are approved:** B-SERVO body dome servo **5A**; B-LOGIC Teensy/receiver/DFPlayer/body shifter **2A**; D-SERVO PCA9685 V+ / six holo servos **5A**; D-LOGIC AstroPixels/Hall/dome shifter **3A**. These are the assembly values, not pending choices.
- **Dome holo-servo fuse is fixed at 5A by user decision.** Connect dome distribution positive through that inline fuse and a 16AWG feed to PCA9685 V+ screw terminal; use a 16AWG unfused ground return. Six MG90S servos use their normal three-pin sockets. PCA9685 logic VCC remains ESP32 3.3V. Do not increase the fuse after a fault; diagnose the fault.
- **Owned inline holders:** Amazon B0FDJYRGB7, indexed as Cooclensportey four-pack, 12AWG leads, standard automotive blade format. Use these for all four branches. The indexed assortment starts at 5A; obtain matching standard ATO/ATC **2A and 3A** separately. Use two included 5A fuses for the servo branches.
- **Use Lonely Binary MOSFET logic converters from kit B0FFMLDYNY in both body and dome.** Allocate one four-channel module to each location. The previous converters are being returned and are not part of this build. Do not substitute TXS0108E or Pololu boards.
- **No oscilloscope or logic analyzer is available or required.** Assembly, commissioning and troubleshooting use a multimeter, USB diagnostics, VESC Tool and the handheld sensor display.

The control policies below are part of this proposed design. Approval of these documents authorizes their implementation; ordering hardware alone does not authorize firmware changes.

## 2. Ownership

| Function | Owner | Other board's role |
| --- | --- | --- |
| Receiver channel decoding and input timeout | Teensy | ESP32 receives channel snapshots and their age |
| Drive enable, tank mixing, drive limits and braking | Teensy | ESP32 receives state; requests foot motion through the defined gated interface |
| Left/right VESC command and telemetry links | Teensy | ESP32 displays both controllers' feedback |
| Handheld telemetry | Teensy | Receiver carries supported sensor readings back to FS-i6X |
| Dome servo pulses and manual override | Teensy | ESP32 requests behavioural rotation |
| Front/rear Hall electrical input | ESP32 | Teensy receives both states and owns seek/position estimation |
| DFPlayer UART, device initialization and playback state | Teensy | ESP32 requests sounds and consumes status/events |
| Sound banks, ambient selection and sound preferences | ESP32 | Teensy executes selected tracks; it does not run a second ambient scheduler |
| Lighting, holo servo choreography and macro selection | ESP32 | Teensy supplies actuator acknowledgements and completion events |
| Wi-Fi, dashboard, preferences and dome OTA | ESP32 | Teensy acknowledges maintenance lock before updates |

There is one actuator owner. ESP32 never writes VESC packets, DFPlayer packets, or dome servo pulses after migration.

## 3. Power and physical pin map

The current design uses the selected battery, cutoff, shared VESC input, converters, motors, amplifier, isolator, speaker and dome mechanism. Their wiring, protection and mounting are reviewed for first assembly rather than copied as an installed system.

The approved 12V fuse schedule is: main 25A; F1 VESC 15A; F2 empty; F3 body buck 7.5A; F4 dome feed 7.5A; F5 amplifier 5A; F6 empty.

Use the owned screw-terminal distribution for each separate 5V rail with B-SERVO 5A, B-LOGIC 2A, D-SERVO 5A and D-LOGIC 3A inline branches. No separate 5V fuse boxes. Locate each fuse at its distribution takeoff, before the smaller branch cable; keep grounds unfused. Split each fused electronics output using insulated terminals separate from the unfused positive bus. Never jumper it back to that bus. Continuity-check the owned dual-row blocks before forming positive/ground groups with correctly sized jumpers. A fuse is wiring fault protection, not an accurate current limiter or a guarantee of semiconductor survival. Stop for hot leads/terminals or jams and diagnose a blown fuse; do not increase these values.

Body 5V powers Teensy VIN, receiver, DFPlayer, dome servo, and the body shifter HV reference. Teensy 3.3V powers only the body shifter LV reference. Do not power the servo, receiver, or DFPlayer from Teensy pins.

Dome 5V powers AstroPixels, PCA9685 V+, both Hall modules, and the dome shifter HV reference. ESP32 3.3V powers PCA9685 VCC and the dome shifter LV reference.

Body and dome 5V positives remain separate. Grounds are common.

### Teensy carrier terminals

Use the terminal labels, not terminal positions counted from a photograph. With all power disconnected, confirm carrier-terminal continuity to its Teensy socket before assembling cables.

| Teensy terminal | Interface | Connect to | Baud / signal |
| --- | --- | --- | --- |
| 1 | Serial1 TX | Left VESC COMM RX | 115200, 8N1, 3.3V |
| 0 | Serial1 RX | Left VESC COMM TX | 115200, 8N1, 3.3V |
| 8 | Serial2 TX | Right VESC COMM RX | 115200, 8N1, 3.3V |
| 7 | Serial2 RX | Right VESC COMM TX | 115200, 8N1, 3.3V |
| 14 | Serial3 TX | 1k ohm resistor, then DFPlayer RX | 9600, 8N1, 3.3V |
| 15 | Serial3 RX | DFPlayer TX | 9600, 8N1, 3.3V |
| 17 | Serial4 TX | Slip ring BODY CH3 | 115200, 8N1, 3.3V |
| 16 | Serial4 RX | Slip ring BODY CH6 | 115200, 8N1, 3.3V |
| 21 | Serial5 RX | Body shifter LV1 | 115200 iBUS channel input |
| 24 | Serial6 single-wire TX/RX | Body shifter LV2 | 115200 iBUS sensor bus |
| 2 | Dome servo pulses | Body shifter LV3 | 50Hz, calibrated 1000-2000us range |
| VIN | Supply | Body regulated 5V | Power input |
| GND | Reference | Body ground distribution | Common ground |
| 3.3V | Reference supply | Body shifter LV | 3.3V reference |

This uses **14 Teensy terminal connections: 11 signal terminals and three supply/reference terminals**. Serial5 TX20 and Serial6 RX25 are unused. Serial7 and Serial8 are spare. USB debug is independent.

DFPlayer BUSY is not wired in version 1. Playback status comes through DFPlayer TX.

### Body MOSFET shifter

Use a Lonely Binary four-channel module. In the tables, LV1-LV4 mean its low-side channel pins (A1-A4); HV1-HV4 mean its matching high-side pins (B1-B4). Follow the supplied PCB labels and confirm channel pairing before wiring. The manufacturer documents 10k ohm pull-ups and 115200-baud UART operation with short jumpers. LV/HV are supply references, not regulator outputs; this module does not generate 3.3V.

| Channel | HV side | LV side |
| --- | --- | --- |
| 1 | Receiver iBUS SERVO signal | Teensy RX21 |
| 2 | Receiver iBUS SENSOR signal | Teensy single-wire pin24 |
| 3 | Dome servo signal | Teensy pin2 |
| 4 | Unconnected | Unconnected |

HV connects to B-LOGIC 2A fused body 5V, LV to Teensy 3.3V, GND to body ground. Leave spare signal terminals insulated.

Serial6 uses Teensy's native half-duplex mode on TX24, with open-drain output. Start it with `Serial6.begin(115200, SERIAL_8N1_HALF_DUPLEX)` and then `Serial6.setTX(24, true)`. Configure open drain after `begin`, because initialization sets the pin pad configuration. RX25 is not connected. The MOSFET shifter supplies the two voltage-domain pull-ups; Teensy drives low or releases the line. Do not join push-pull TX and RX.

Mount the module beside Teensy and the receiver and keep the iBUS connections short. Commission with concurrent channel input and sensor telemetry, checking parser errors, valid-frame counts, sample ages and sensor responses. No waveform measurement or rise-time acceptance gate is required. USB timing counters verify firmware scheduling, not electrical edge shape. Functional communication tests demonstrate operation but do not measure electrical timing margin.

If communication fails, keep motion disabled. Check unpowered continuity and channel pairing, check powered LV/HV rails with a multimeter, inspect common ground and solder joints, test receiver input and sensor telemetry separately with the shortest practical leads, then replace the module with a spare from the selected kit. Repeat the concurrent test before enabling motion. Do not make acquiring an oscilloscope or logic analyzer a recovery step.

### Dome MOSFET shifter

Use a second Lonely Binary four-channel module, with the same low/high channel naming convention.

Front Hall S connects through HV2/LV2 to GPIO19. Rear Hall S connects through HV3/LV3 to GPIO18. Both modules use D-LOGIC fused 5V and common ground. Shifter HV uses D-LOGIC 3A fused dome 5V; LV uses ESP32 3.3V; GND uses dome ground. Channels 1 and 4 are unused.

### Direct signals

Both VESC UARTs, both body/dome UART directions, and both DFPlayer UART directions bypass level shifters. DFPlayer power is 5V; its UART interface is 3.3V. The 1k ohm resistor is in the command line only; it is not a voltage converter.

PCA9685 SDA/SCL remain at GPIO21/GPIO22 with 3.3V pull-ups. Its VCC and V+ remain separate.

Use AstroPixels I2C header G -> PCA9685 logic GND, C/GPIO22 -> SCL, and D/GPIO21 -> SDA. Leave the I2C header V pin disconnected. Supply PCA9685 logic VCC from ESP32's 3.3V pin, not the motherboard's 5V header. Servo power uses the separate approved 5A-fused 5V feed to the V+ screw terminal and 16AWG ground return. No logic shifter on SDA/SCL. Keep default address 0x40 and channel pairs 0/1 front pan/tilt, 2/3 rear pan/tilt, 4/5 top pan/tilt.

### Slip ring

| Contact | BODY end | DOME end | Function |
| --- | --- | --- | --- |
| CH1 | F4 fused battery positive | Dome buck IN+ | Existing 12V feed |
| CH2 | Common ground | Dome ground / buck IN- | Existing return |
| CH3 | Teensy TX17 | ESP32 GPIO16, Serial2 RX | Body to dome serial |
| CH4 | Insulated, unconnected | Insulated, unconnected | Spare |
| CH5 | Insulated, unconnected | Insulated, unconnected | Spare |
| CH6 | Teensy RX16 | ESP32 GPIO17, Serial2 TX | Dome to body serial |

**Selected map:** use the AstroPixels dedicated Serial2 pins, GPIO16 RX and GPIO17 TX, with contacts CH3/CH6. GPIO5/AUX3 and GPIO4/AUX2 are spare. GPIO19/AUX5 is front Hall; GPIO18/AUX4 is rear Hall. Use only serial signal and common-ground connections; no serial-header 5V connection between body and dome. The shared body protocol is Serial2's only reader; disable the legacy MarcDuino serial input parser on this UART. Retain Web/internal MarcDuino commands.

Build only the approved body/dome signal paths. Do not assemble the earlier VESC-through-ring, DFPlayer CH4 or servo CH5 wiring from superseded documents.

### VESC connectors

Both COMM connectors use their marked TX, RX, and GND terminals. Leave 5V, 3.3V, ADC, and ADC2 unconnected. Retain one shared battery input.

Document the actual board inventory: two CAN connectors, two USB ports, two motor Hall connectors, two SWD connectors, two COMM connectors, two sets of phase wires, one battery input, one RECEIVER GND/5V/SIN connector, and the internal CAN switch. RECEIVER/SIN, CAN, and SWD are unused in this design. Do not depict two independent receiver connectors.

### Carrier and cables

Mount the Treedix carrier on standoffs. Teensy plugs into its sockets. Stranded signal cable terminates in suitable ferrules at the screw terminals. Use labelled detachable connectors at devices and strain relief beside the carrier. Keep high-current phase/servo paths off the carrier.

**Approved service arrangement:** No new shell access panel or externally exposed USB ports. Mount Teensy and both VESC USB ports near the top of the body, accessible with the dome removed. Teensy firmware updates therefore require dome removal; assembled diagnostics/calibration remain wireless.

The slip ring stays with the removable dome/post assembly. Its stationary housing uses an anti-rotation feature that disengages by lifting vertically, without loosening the rotating collar's set screws during normal removal. Confirm the actual ring's rotor/stator and mounting features before making the restraint; do not use its wires as a torque restraint or make it support the dome's weight.

Provide accessible keyed detachable connectors on the body-side ring leads: CH1/CH2 power on a connector rated at least10A per power contact and accepting the selected wire gauges, and CH3/CH6 on a separately keyed signal connector. Protect the body-side positive contact against accidental contact/shorting. CH4/CH5 remain individually insulated spares. Provide enough supported service slack to reach/unplug connectors before lifting, without stretching either ring wire bundle; no slack may foul the rotating mechanism.

Removal: disconnect battery/charger/USB, unplug body-side ring connectors, release the dome/post wing bolt, and lift straight up so the anti-rotation restraint disengages. Do not lift the dome against attached cables. Reinstallation re-engages the restraint and secures the post; check free movement/clearances with power off before reconnecting power. Rehearse removal and access to Teensy USB/Program and both VESC USB ports before final closure.

**Selected USB workflow:** Cut the factory VUSB/VIN copper link between PJRC's designated underside pads once, with Teensy removed from its carrier and all power disconnected. The assembly guide must show the Teensy 4.1-specific location before the cut; do not cut a guessed trace. Check VUSB/VIN isolation with a multimeter before reinstalling. Confirm the carrier does not reconnect VUSB and VIN.

After isolation, regulated body 5V powers VIN and normal USB carries programming/debug data without joining USB5V to body5V. Ground remains common; this is power-source separation, not galvanic USB isolation. Teensy stays in the carrier for routine programming. USB alone no longer powers it: external VIN power is required. First flash can occur on the bare, unmodified Teensy before cutting; verify operation after the cut with externally powered VIN. Program with wheels elevated/drive disarmed and servo motion inhibited. Do not bridge the pads for routine programming.

## 4. Radio input and handheld telemetry

Use CH1 steering, CH2 throttle, CH4 manual dome, CH5/SwB duty rate, CH6/SwA foot-drive enable, CH7 mood selection, CH8 macro trigger and CH9/SwD Auto Dome. CH3 and CH10 are unused. Remove all radio holo tilt/random controls: holo movements are autonomous or choreographed.

Validate all 14 channel values in an iBUS frame before accepting its first ten channels. Accepted range is 900-2100us. Clamp stick endpoints to 1000-2000us before mapping. Check both header bytes and checksum. Incomplete packets expire after 5ms.

The input becomes stale after 250ms without a valid channel frame. Repeated invalid frames do not renew it.

Configure receiver failsafe with CH1/CH2/CH4 centered, CH6 OFF, CH8 trigger released, and CH9 Auto Dome OFF. Confirm these values on Teensy's USB monitor while switching off the transmitter. Continuing valid failsafe frames are not evidence of a live transmitter. The plan tests both transmitter loss and unplugged iBUS.

Drive enable thresholds: <=1250us OFF; >=1750us ON; intermediate values disarm. On boot and after faults, Teensy must observe OFF, then ON, then 500ms centered CH1/CH2 before arming. Missing input resets this sequence.

Manual dome control does not require CH6 ON. All automatic dome motion requires fresh receiver data and CH9 ON; CH9 uses OFF<=1250us, ON>=1750us and intermediate values disable automation. CH6 gates only feet. Manual CH4 control overrides automatic dome motion.

Teensy is the only external sensor-bus responder. FS-CVT01 is absent.

Expose two stock FlySky sensor types:

- Address 1: external voltage, type 0x03, unsigned hundredths of a volt.
- Address 2: temperature, type 0x01, hottest of the two VESC MOSFET temperatures, encoded as tenths of a degree C plus 400.

Use the lower of the two fresh pack-voltage readings. Both controllers must have valid readings no older than 500ms. On stale data, stop answering measurement requests for that sensor; report the stale state to ESP32. Do not send zero or keep publishing the last reading as fresh. Discovery/type replies still work. Commission the handheld's lost-sensor indication and voltage alarm.

Do not present electrical RPM as wheel speed. Detailed per-controller current, eRPM, voltage, fault, and temperature records go to ESP32; version 1 does not encode arbitrary fault text as a fake temperature or RPM.

## 5. Body/dome link protocol, version 1

Use one shared portable C++ library in `shared/R2BodyLink/`. Both firmware builds compile the same codec and typed payload definitions.

### Wire framing

The connection is 115200 baud, 8N1, full duplex, TTL 3.3V.

Each frame is COBS-encoded and terminated by byte 0x00. Before COBS encoding:

| Offset | Field | Size |
| --- | --- | --- |
| 0 | Version, fixed 1 | u8 |
| 1 | Message type | u8 |
| 2 | Flags: bit0 requests reply; other bits zero | u8 |
| 3 | Reserved, zero | u8 |
| 4 | Sequence | u16 |
| 6 | Source boot session | u32 |
| 10 | Destination boot session | u32 |
| 14 | Payload length, 0-96 | u16 |
| 16 | Typed payload | 0-96 bytes |
| 16 + length | CRC16-CCITT, polynomial 0x1021, initial 0 | u16 |

All multibyte fields are little endian. CRC covers the header and payload, not its own bytes. Maximum raw length is 114 bytes; maximum encoded length including delimiter is 116 bytes. Serialize fields explicitly; do not copy padded C++ structures.

Malformed frames do not update timestamps, RC state, command leases, or telemetry. Discard overflow until the next delimiter. Discard a partial frame after 20ms without another byte. Count and report CRC, length, version, session, and queue errors.

CRC detects wiring corruption. It is not authentication.

### Session and reliability rules

Each board increments a persisted boot counter before opening this link. Store the counter with CRC in alternating slots. A storage failure disables remote actuation and is reported; it does not create a success-shaped session.

HELLO uses destination session zero and carries role, capability bits, and safety-contract revision 1. Other messages must match both active sessions. A changed session clears pending requests and cached state, cancels remote-owned actions, and invalidates old events. Manual foot drive remains independent unless a motion lock or fault is active.

Send HELLO every 500ms until the handshake completes. Send HEARTBEAT every 100ms. Link timeout is 300ms without a valid peer heartbeat.

Discrete requests retry the identical frame after 100ms, at most twice. Deadline is 350ms from first transmission. Cache the last 16 completed request results for 2 seconds so a retry does not repeat a sound, home request, or lock operation. If a retained sequence has different content, reject it. Do not retry streaming velocity requests or refresh their leases from duplicates. Allocate discrete request/event sequence numbers when the queue accepts them so callers receive a stable request handle. Allocate streaming sequence numbers on transmission so replacement of a queued snapshot does not create an older on-wire sequence.

Replies and safety operations use reserved queue capacity and are transmitted before ordinary requests. Cap queued ordinary requests at six, leaving two slots for safety operations/events; replies use a separate bounded response buffer. Queue rejection returns BUSY locally and records a diagnostic. A request that remains unsent for 350ms fails visibly without executing.

Use 16-bit wrap-safe newer-sequence checks for streams. Separate last-sequence records by message type. A peer reboot resets those records. Discrete requests use the request cache instead of a stream comparison.

Queue capacity is eight discrete outgoing frames plus one replaceable latest frame per streaming key. Keys are message type, plus wheel index for VESC_STATUS: left and right must have separate slots. Reserve priority for STOP, lock, reply, and completion/error events. Coalesce periodic telemetry. Dropped requests fail visibly; they never become successful actions.

### Message catalog

Definitions live in `Messages.h`. Fields below are listed in wire order.

| ID | Message | Payload / behaviour |
| --- | --- | --- |
| 0x01 | HELLO | role u8 (1 body, 2 dome), capabilities u32, safety_revision u16 |
| 0x02 | HEARTBEAT | mode u8 (0 boot, 1 ready, 2 maintenance), ready u8 |
| 0x10 | RC_STATUS | sample_counter u32, source_age_ms u16, flags u16, channels[10] u16, drive_state u8, dome_state u8, control_epoch u16 |
| 0x11 | VESC_STATUS | wheel u8 (0 left, 1 right), valid_fields u16, source_age_ms u16, fw_major u8, fw_minor u8, pack_cV u16, motor_mA i32, input_mA i32, erpm i32, mosfet_dC i16, motor_dC i16, fault u8, duty_permille i16 |
| 0x12 | BODY_STATUS | faults u32, drive_state u8, dome_state u8, lock_reasons u8, profile_ready u8, control_epoch u16, drive_intent u8, dome_owner u8, dome_authority_generation u32, angle_valid u8, estimated_angle_ddeg i16 |
| 0x13 | HALL_STATE | valid_mask u8 (bit0 front, bit1 rear), active_mask u8 (same bits, normalized detected=true), sample_counter u32, source_age_ms u16 |
| 0x20 | DOME_REQUEST | operation u8 (0 cancel, 1 velocity, 2 seek_reference), speed_percent i16, lease_ms u16, control_epoch u16, owner u8 (0 idle, 1 event), reference u8 (0 front, 1 rear), dome_authority_generation u32 |
| 0x21 | AUDIO_REQUEST | operation u8 (0 play, 1 stop, 2 pause, 3 resume, 4 volume, 5 query), folder u8, track u16, volume u8, priority u8 (0 ambient, 1 foreground) |
| 0x22 | DRIVE_REQUEST | left_permille i16, right_permille i16, lease_ms u16, control_epoch u16 |
| 0x23 | CONTROL_REQUEST | operation u8 (0 STOP_ALL, 1 RELEASE_STOP, 2 LOCK, 3 UNLOCK, 4 RECOVER_LOCKS), reason u8 (0 operator, 1 reserved/rejected, 2 maintenance), token u16, control_epoch u16 |
| 0x24 | COMMISSION_REQUEST | operation u8 (0 read, 1 begin, 2 keepalive, 3 cancel, 4 set_field, 5 save, 6 accept), test u8 (0 none, 1 neutral, 2 front, 3 rear, 4 timing CW, 5 timing CCW, 6 VESC command-timeout), run_id u32, field u8, wheel u8, value i32, control_epoch u16 |
| 0x30 | REPLY | request_type u8, request_seq u16, result u8, detail u16 |
| 0x31 | AUDIO_STATUS | state u8, folder u8, track u16, volume u8, validity u8, elapsed_ms u32, duration_ms u32, owner_request_seq u16 |
| 0x32 | EVENT | kind u8, request_type u8, request_seq u16, detail u16 |
| 0x33 | COMMISSION_STATUS | run_id u32, state u8 (0 idle, 1 prepared, 2 running, 3 complete, 4 cancelled, 5 failed), test u8, error u16, flags u32, trial_neutral_us u16, trial_speed_percent u8, proposed_cw_ddeg_s u16, proposed_ccw_ddeg_s u16, revolution_ms[3] u32, config_generation u32, saved u8 |
| 0x34 | DIAGNOSTICS | subtype u8, sample_counter u32, subtype0 eight u32 counters / subtype1 field u8, wheel u8, value i32 |

REPLY results: 0 accepted, 1 invalid argument, 2 not ready, 3 manual override, 4 inhibited, 5 unsupported, 6 busy, 7 wrong epoch, 8 hardware error.

EVENT kinds: 0 completed, 1 cancelled, 2 timeout, 3 hardware error, 4 playback started, 5 dome takeover. Reliable events request a reply and retry under the same rules; REPLY itself never requests a reply. A receiver replies to an EVENT using its header sequence, while the EVENT payload identifies the originating action.

RC flags: bit0 channel stream valid, bit1 drive-enable ON, bit2 neutral qualification complete, bit3 Auto Dome ON. Do not label bit0 "radio RF link confirmed."

VESC validity bits: 0 voltage, 1 motor current, 2 input current, 3 eRPM, 4 MOSFET temperature, 5 motor temperature, 6 fault, 7 duty. Motor temperature is invalid with the unused motor TEMP wires.

AUDIO validity: bit0 playback confirmed, bit1 duration known. Unknown duration is zero with bit1 clear. State values: 0 stopped, 1 starting, 2 playing, 3 paused, 4 error, 5 offline.

Drive states: 0 boot, 1 disarmed, 2 qualifying, 3 armed, 4 fault, 5 locked. Dome states: 0 inhibited, 1 manual, 2 remote velocity, 3 seeking reference, 4 holding reference. Lock bits: bit0 operator stop, bit2 maintenance; bit1 is reserved and rejected. Control reason1/Faint is reserved and rejected; Faint never requests a motion lock.

DriveIntent: 0 stationary, 1 forward, 2 reverse, 3 pivot. DomeOwner: 0 none, 1 manual, 2 drive alignment, 3 event, 4 idle, 5 startup. Derive drive intent from permitted wheel targets after radio mapping/gating, not raw radio throttle alone. A braking reversal retains the previous travel facing until the new powered direction is allowed. Equal-opposite wheel targets are pivot; same/net-forward targets are forward, net-reverse targets reverse; no permitted drive is stationary. Thus a disarmed throttle stick does not claim the feet are driving.

Teensy increments `dome_authority_generation` on entry into manual/drive ownership, Auto Dome disable and peer-session change, not on every tick of unchanged ownership. Every remote dome request must carry the current generation; stale requests are rejected as INHIBITED and cannot resume after override. ESP32 also permanently marks the active event's dome participation cancelled on manual/drive takeover. Reliable takeover EVENT kind5 reports cancellation of an affected request, including a held/completed seek owner. BODY_STATUS publishes immediately on ownership/intent changes; periodic status includes estimated angle explicitly labelled as an estimate, never a measured absolute angle.

Capability bits: bit0 dual VESC feedback, bit1 audio status, bit2 Hall input, bit3 handheld telemetry, bit4 remote-drive interface. Body advertises bits 0/1/3/4; dome advertises bit2. The remote-drive capability means the interface exists; it does not permit movement.

BODY_STATUS fault bits: bit0 configuration missing, bit1 configuration corrupt, bit2 boot-session storage error, bit3 receiver stale, bit4 left VESC stale, bit5 right VESC stale, bit6 left VESC fault, bit7 right VESC fault, bit8 unsupported VESC profile, bit9 control deadline missed, bit10 audio unavailable, bit11 link protocol/queue error. These are diagnostics, not identical drive-inhibit conditions: audio/link errors alone do not inhibit healthy manual foot drive.

REPLY/EVENT detail codes: 0 none, 1 input stale, 2 left VESC unavailable, 3 right VESC unavailable, 4 profile unavailable, 5 Hall stale, 6 seek timeout, 7 lease expired, 8 audio unavailable, 9 audio guard expired, 10 token mismatch, 11 peer lost, 12 queue full, 13 initialization timeout, 14 device error, 15 sequence conflict. Device-specific raw errors remain in USB diagnostics. Reserved bits and undefined enum/detail values are rejected; zero unused request fields.

RC/VESC/Hall source ages saturate at 65535ms and remain invalid when stale. Never wrap a long-stale sample back to a small age. Add the sample's outgoing queue residence time before serializing a status frame; a delayed queue cannot make it appear younger. ESP32 checks incoming sample counters and stream sequences before replacing snapshots.

Publish RC_STATUS at 50Hz, BODY_STATUS and AUDIO_STATUS at 5Hz, and each VESC_STATUS at 5Hz. Ownership/intent changes publish BODY_STATUS immediately. HALL_STATE is sent every 20ms and on changes, from the main loop, not from the sensor ISR. Events send immediately. A 50Hz 32-byte RC payload uses approximately 2.55kB/s including framing; the complete status load stays below half of a 115200-baud direction's capacity. Verify the <=50ms Hall-to-neutral target against actual scheduler/serial latency.

ESP32 calculates freshness as source age plus local elapsed time since receipt. Repeating a snapshot cannot renew the originating sample. Telemetered timestamps are not assumed to share clock origins.

## 6. Drive and motion rules

### Manual foot drive

Teensy evaluates drive every 20ms. It requires a valid commissioned profile, valid receiver input, drive enable, neutral qualification, and fresh fault-free telemetry from both controllers.

Poll each VESC every 100ms; publish latest records to ESP32 every 200ms. No initial telemetry means no drive. A reading older than 500ms, missing required voltage/fault/eRPM validity, an unsupported firmware profile, either controller's fault, or invalid profile disarms both wheels. Recovery requires OFF -> ON -> neutral qualification. A healthy wheel never continues alone.

Map deadband 1460-1540us to zero and clamp endpoints. Normalize the mixed pair before applying the selected rate:

```text
left = throttle + steer
right = throttle - steer
divisor = max(1, abs(left), abs(right))
left = left / divisor * rate
right = right / divisor * rate
```

Rate is 0.35, 0.70, or 1.00. Final absolute duty is limited to 0.95. Thus slow diagonal input cannot produce 70% duty. These are duty limits, not measured road-speed limits.

Use `COMM_SET_DUTY` for nonzero drive and `COMM_SET_CURRENT_BRAKE` for neutral/stops. Brake-current packets contain positive current magnitude. The VESC configuration's negative braking limit is a separate setting.

Ramp normal duty acceleration using a commissioned slew rate and elapsed time. Neutral, CH6 OFF, radio loss, STOP and faults bypass that acceleration ramp and request braking promptly. For each wheel independently, a requested powered sign reversal enters BRAKING; permit opposite-sign duty only when fresh valid eRPM is within that wheel's commissioned low-speed threshold continuously for its commissioned dwell time. Missing speed feedback inhibits reversal and drive. Per-wheel gating preserves differential pivots without permitting one wheel's abrupt reversal. Do not use a fixed delay as proof a wheel stopped. This is a low-speed threshold, not proof of zero road speed.

Current and brake settings are commissioning values, not facts inferred from motor watts. Store a profile with both firmware versions, per-wheel direction, motor/battery current limits, brake current, voltage limits, timeout/brake settings, and passed-test flags. Shipping defaults keep motion disabled. Do not carry forward the current guide's claim that 12A winding current or -2.5A regeneration is guaranteed safe.

Add `duty_slew_permille_per_s` and each wheel's `reversal_erpm_limit`/`reversal_dwell_ms` to that profile. Calibration must be nonzero and accepted before feet enable. Use separate manual-dome, automatic-dome and drive readiness: feet do not depend on dome calibration. Automatic calibration records `auto_speed_percent` (1-25), `cw_ddeg_per_s` and `ccw_ddeg_per_s`, measured under installed load, plus both reference acceptance flags. Automatic movements all use that saved speed. Missing/zero calibration inhibits automatic movement, not a guessed timing default.

The VESC input timeout remains 150ms, with timeout braking configured and tested on both halves. Teensy tracks its own 20ms output deadline. If it misses that deadline or detects a driver fault, it stops renewing drive commands and records a fault. A 1-second hardware watchdog resets a stalled Teensy; boot never rearms it. That watchdog is not a 150ms dome-servo cutoff. Commission the servo's behaviour during processor stall, reset and signal loss with the dome unloaded before loaded operation.

Keep the existing 10.5V software cutoff as a configurable secondary threshold during migration, and label it as a cutoff setting, not state-of-charge measurement. The installed controller voltage/current profile and battery documentation determine the final commissioning values.

### ESP32 foot requests

Implement and test DRIVE_REQUEST, but ship with `allow_remote_drive = false`. Teensy replies INHIBITED. No existing macro gains foot movement.

Activating this feature requires a separate approved operator-mode design. It must require local permission, fresh RC, CH6 ON, both VESCs healthy, centered manual controls, a matching control epoch, and renewed <=150ms requests. Manual stick deflection revokes permission. This is not a Wi-Fi throttle bypass.

### Dome: authority, references and estimation

Priority is STOP/maintenance/inhibit -> manual CH4 -> permitted driving alignment -> event -> idle. Fresh RC and commissioned servo neutral are needed for manual output. Auto Dome CH9 ON, both fresh Hall inputs, healthy peer link, calibrated automatic motion and no lock are needed for automatic output. CH6 OFF does not prevent idle/event motion. Feet never wait for dome alignment.

CH4 outside1460-1540us immediately cancels automatic motion and takes manual control. Centring during forward/reverse driving immediately starts returning toward the corresponding reference at controlled speed. When stationary, centring stops the servo and restarts the20-second idle delay. Ordinary peer loss leaves fresh-RC manual dome available; automatic motion stops. Existing locks still inhibit manual motion.

Forward travel targets front; reverse targets rear. Pivot suspends idle/event movement and holds the current facing with neutral pulses; do not seek a new direction merely for a pivot. Driving interrupts all event/idle dome motion. Interrupted event dome participation cannot return later in that event, even after driving/manual input ends.

Two sensors on the rotating dome180 degrees apart see one stationary magnet. ESP32 normalizes front/rear detection and publishes both. Simultaneous active references are invalid: stop automatic motion and report a sensor fault. Each newly detected reference anchors the angle estimate at0/front or-1800/rear in tenths of degrees, normalized to[-1800,1800). Unknown startup position is not silently treated as front. Intermediate angles are integrated from calibrated direction/speed/time, labelled estimates, and corrected at reference edges. Do not pin the estimate throughout a broad active magnet window. Uncalibrated manual speed invalidates the estimate until a reference is reacquired; never extrapolate an unmeasured speed curve.

Seek a reference at a commissioned low approach speed (initial target25%, not an accepted calibrated value). If the target sensor is already active, command neutral and complete without rotation. Use the estimated shortest route only when calibrated position is valid; otherwise seek clockwise. Limit each seek to10seconds; stale Hall>150ms, peer loss, Auto Dome OFF, manual/drive takeover or STOP cancels/fails it. An idle/drive/startup seek fault latches automatic-positioning unavailable until CH9 OFF->ON with healthy inputs; it never retries endlessly. Manual remains available. No delayed completion may restart an interrupted Leia.

On initial Auto Dome enable while stationary, find front, then start the20-second idle wait. While driving, align to travel instead. There is no feet-before-home interlock. Maintain startup initialization state so this search is not repeated every loop.

ESP32's nonblocking idle scheduler starts only after20seconds without drive/manual input and without an active event. Ending drive, releasing the dome stick or ending/cancelling an event restarts that timer. At expiry, re-reference front, then choose occasional calibrated timed movements to approximate targets within±45degrees, with2-6second pauses and front re-reference after each out-and-back excursion. Stop at a target estimate; do not call±45 a measured mechanical limit. Disable idle sweeps until clockwise/counterclockwise timing calibration is saved. Radio stick deflections restart inactivity even when feet are disarmed.

Remote velocity leases are1-150ms, renewed every50ms. Expiry or duplicates never extend motion. Idle uses bounded leases; reference seek is a bounded request with correlated completion. Servo mapping uses calibrated microseconds, not ESP32 LEDC counts. Pulse-sleep stays disabled until the installed signal-loss test passes.

### STOP and maintenance

STOP_ALL stops both wheels, dome motion, and playback; cancels remote requests; increments the control epoch; and latches operator stop. ESP32 cancels its macro and holo motions. Neither board reports success before Teensy replies.

Release operator stop only after fresh RC shows CH6 OFF and centered motion sticks for 500ms. ESP32 then requests RELEASE_STOP with the current epoch. Feet still require OFF -> ON -> centered arming. A stop issued while the link is down is displayed as "Body stop unconfirmed"; software must not claim it reached the body.

Faint is audio/lights/holo choreography only and never locks feet or dome. Maintenance acquires an all-motion lock before OTA/reboot. A missing release or peer reboot does not silently clear an existing lock; operator recovery uses CH6 OFF, centered sticks, a fresh session, and an explicit release.

UNLOCK requires its original nonzero token, reason and current epoch. After an ESP32 reboot loses that token, the dashboard's explicit recovery action sends RECOVER_LOCKS with reason operator, token zero and the current epoch. Teensy accepts this only with a completed handshake, fresh RC, CH6 OFF and CH1/CH2/CH4 centered continuously for 500ms. Recovery cancels pending actions, clears operator/maintenance locks, increments the epoch and leaves feet disarmed and dome at neutral. Restart the idle timer after recovery; do not immediately restore an old automatic action. It never runs automatically during startup or reconnection. STOP_ALL remains valid even with an old epoch; releasing or starting actions requires the current epoch.

Each successful lock-state change increments the epoch, cancels older remote actions and publishes BODY_STATUS immediately. Cached retries return the original result without repeating that change. An idempotent operation that changes no state does not increment the epoch.

Ordinary dome-link loss cancels remote automatic motion/audio and invalidates dome status. It does not disable healthy manual foot drive. Existing maintenance/operator locks remain latched. Disconnecting a slip-ring signal is not the same failure as disconnecting a VESC UART.

## 7. Audio and macro behaviour

Teensy runs a nonblocking 9600-baud DFPlayer driver. It waits for initialization, spaces commands by at least 100ms, validates replies/checksums, and consumes playback-finished and error messages. Missing initialization leaves audio OFFLINE and produces a visible fault.

All commands use one serializer. Discrete play requests are not repeated after a lost body/dome acknowledgement. Foreground playback interrupts ambient playback; ambient requests are rejected while foreground playback is active. STOP/pause/resume/volume/query return defined results.

ACK means the command was accepted. PLAYBACK_STARTED is emitted after the player reports playing. COMPLETED follows a matching playback-finished notification. Ignore unrelated completion packets and packets received before the current track starts. Correlate events with the originating body/dome request.

Poll status every 500ms when no higher-priority audio operation is pending. A device timeout/error terminates the current foreground request. Manual volume stays at 0-30; ESP32's existing 0-1000 slider maps with `ceil(value * 30 / 1000)`. Default is 10/30.

Keep `/01/001.mp3` through `/01/255.mp3` and current macro tracks 102, 106, 107, 109, 110 plus startup 255. Do not redistribute audio.

`config/tracks.csv` stores track, duration_ms, and completion_guard_ms. Duration zero means not measured. Existing 4.5s/30s/14s/20s/5s macro timings remain choreography fallbacks, not claimed file durations. A build-time catalog generator validates this CSV and produces a flash-resident table. Known durations are reported with the duration-known bit. DFPlayer does not supply track length.

Version 1 uses folder 1 for the catalog. Macro guards start at confirmed playback start and use the existing choreography time plus 2000ms when duration is unknown. Ordinary unknown tracks use a 600000ms guard. Measured duration plus 2000ms replaces that guard when supplied. Expiring the guard stops playback and reports timeout; it is not a successful completion.

With Auto Dome ON, stationary and dome automation available, Leia seeks front before submitting audio. Auto Dome OFF at trigger skips alignment and submits audio without rotation. Driving/manual takeover, Auto Dome disable or seek failure while waiting for front cancels the pending Leia start; it must not later play from an old completion. If drive/manual input already owns the dome at trigger with Auto Dome ON, reject pending alignment visibly rather than rotate or silently defer.

Once playback starts, drive/manual takeover removes only the event's dome participation; audio, lights and holo choreography continue. The same selective override applies to ordinary events. ESP32 tracks a per-event permanent `dome_cancelled` flag; old scheduled dome movements never regain ownership. STOP cancels the whole event. Auto Dome OFF at ordinary-event start skips dome movement without cancelling other effects.

ESP32 waits for playback-start confirmation before synchronized choreography. It ends a sound-led routine on audio completion, cancellation, error, or explicit guard expiry. Ambient selection and random preferences stay on ESP32. Track-finished events prevent chatter from interrupting foreground macros. Holos retain calibrated travel limits and autonomous preferences, but CH3/CH9 never control them.

## 8. Bounded ESP32 migration

Keep ReelTwo, AstroPixels displays, shaders, I2C/PCA9685, holo limits, lighting commands, Wi-Fi credentials, routes, preference keys, and macro selections.

Change only these integration surfaces:

1. Replace Serial2's VESC endpoint with the body client on dedicated GPIO16 RX / GPIO17 TX; remove any competing legacy serial parser.
2. Replace local iBUS parsing with RC_STATUS ingestion and independent freshness checks.
3. Remove `DfPlayerSerial`, direct audio packets, DFRobot hardware access, VESC encoder/parser/mixer, and dome LEDC setup/writes.
4. Turn existing dome helpers into typed body requests. Keep actuator arbitration, drive alignment, reference seek and angle estimation in Teensy. Add ESP32 idle/event scheduler; read front GPIO19 and rear GPIO18 in ESP32.
5. Preserve the MarcSound banks/parser/random scheduler; route its DFPlayer operations through `RemoteAudio`.
6. Add acknowledged macro phases and pending/error display. Keep existing choreography and commands.
7. Route Web STOP, `:DMS`, `:SE00`, reboot, preference reset, Wi-Fi-triggered reboot, ArduinoOTA, and web upload through the same body control interface. Faint uses remote audio only, not a body motion lock.
8. Add a read-only `/body` status page: link, input age, drive enable/armed, locks, both VESCs, playback, and last rejected request.

Prepare maintenance from the dashboard before serving either OTA path. Start/handle ArduinoOTA only after maintenance-lock acknowledgement; the library's update callback is too late to establish a new stop handshake. Web upload rejects its start before `Update.begin` unless maintenance is acknowledged. Do not patch or fork ArduinoOTA/ReelTwo. Upload failure leaves motion locked until explicit operator recovery.

A dome without a body link still boots its lights and Wi-Fi. It displays body OFFLINE and rejects body actions. It cannot present stale channels or cached telemetry as current.

The older `ASTROPIXELS_UNIFIED_BRAIN.ino`, `ESP32_DOME_BRAIN_GUIDE.md`, and Nano sketch remain historical references. Mark their status; do not port this architecture into them.

## 9. Build structure and validation

### Assembled testing without USB

No assembled dome movement, Hall check or timing calibration requires computer USB. Disconnect all external data/programming cables before rotation. Initial bare-board flashes and individual stationary VESC Tool setup use USB; installed dome checks use the ESP32 Wi-Fi dashboard on a phone. Teensy diagnostics and calibration results travel over the existing body/dome link. Do not require access to Teensy USB after dome installation.

Add a `/commissioning` page showing front/rear Hall detection and freshness, RC/Auto Dome state, body/link faults, calibration progress and save confirmation. Provide explicit low-speed front/rear test, neutral adjustment, clockwise/counterclockwise timing runs and Cancel. Separate stationary profile editing from guarded test motion; do not tunnel arbitrary USB CLI text through Wi-Fi.

COMMISSION_REQUEST is14 bytes; COMMISSION_STATUS is36 bytes. Exact little-endian codecs use the normal sessions/replies/idempotence rules. These messages never issue a foot-duty command. Commissioning is permitted only when `allow_remote_drive` is false. A deliberate VESC command-timeout test is separate: require both wheel brakes already acknowledged/configured, observed CH6 OFF and an operator-confirmed elevated stand; suppress command transmission without issuing powered duty and record diagnostics. Do not add a Wi-Fi wheel-spin button.

Field IDs and bounds: 0 servo neutral (1400-1600us); 1 servo minimum (1000-1499us); 2 servo maximum (1501-2000us); 3 automatic speed (1-25%); 4 duty slew (1-1000permille/s); 5 wheel direction (-1 or1); 6 firmware major (0-255); 7 firmware minor (0-255); 8 supported values layout (implementation-supported enum only); 9 motor-current limit (1-100000mA); 10 battery-current limit (1-20000mA); 11 regenerative limit (0-20000mA magnitude); 12 brake command (1-100000mA); 13 undervoltage cutoff (1000-1500cV); 14 overvoltage setting (1300-1600cV); 15 VESC timeout (exact150ms); 16 timeout brake (1-100000mA); 17 reversal speed threshold (1-10000eRPM); 18 reversal dwell (20-1000ms); 19 CW calibration (1-36000ddeg/s); 20 CCW calibration (1-36000ddeg/s). Per-wheel fields require wheel0/1; global fields require wheel0. These are validation envelopes, not recommendations or safe motor ratings. Field writes update a staged profile only; Save persists it after cross-field validation. Unknown fields or unused nonzero parameters are rejected.

Accept uses `value` as a single named acceptance-bit index and only while stationary with CH6/CH9 OFF. Display each check's concrete instructions and observed result before allowing explicit operator acceptance. Synthetic defaults never set acceptance. Automatic timing acceptance requires a completed uncancelled run and matching saved calibration. Current limits in Teensy's record do not configure the VESC by magic: VESC Tool applies them, and the operator confirms the record agrees. Set/save requests do not silently imply hardware configuration.

Teensy owns a dedicated bounded commissioning state machine. It permits dome-only test motion before automatic timing is calibrated, but requires commissioned radio/servo neutral, fresh valid Hall inputs, CH6 OFF, CH9 ON, centered sticks, healthy link and no STOP/maintenance lock. It suspends idle/event requests and rejects foot drive throughout the run. Manual stick movement, CH6 ON, CH9 OFF, radio/link/Hall loss or Cancel ends the run at neutral and discards incomplete results. These gates do not bypass STOP or enable Wi-Fi foot drive.

Neutral commissioning is the bootstrap exception to the saved servo-neutral prerequisite: with the dome gear disengaged, an explicitly acknowledged neutral test applies only a trial1400-1600us pulse, never a velocity/seek. It still requires fresh radio, CH6 OFF, CH9 ON, centered sticks, healthy link and keepalive; Cancel/loss disables pulses if neutral is not yet accepted. Verify no creep visually, then stop, switch CH9 OFF and explicitly save/accept neutral. Radio/Hall diagnostics are available without motion acceptance so the initial profile can be established without a circular gate. Once neutral is accepted, cancellation uses that accepted neutral; normal operation never uses a trial value.

Each commanded run has a local10-second reference-acquisition timeout and requires a dashboard keepalive every100ms with a300ms expiry; browser loss stops the run. Use the saved low automatic speed, starting from an explicitly selected trial value before timing acceptance. Measure three complete revolutions per direction between successive rising detection edges of the same front reference, ignoring the initial edge used to find front. This avoids pretending the width of the magnet's active window is exactly180degrees. Require a rear detection between front edges, reject simultaneous detections, show each measured duration and derive direction speed from the median. Each revolution has its own10-second timeout; an interrupted run never reports completion. Results are proposed calibration, not automatically accepted.

An explicit Save applies validated values only with motion stopped, CH6/CH9 OFF and centered sticks. Body acknowledgement follows successful persistent storage and read-back; the page displays saved values or a concrete failure. No ordinary profile update sets unperformed acceptance flags. Calibration/status message formats and portable fixtures are implementation-plan deliverables before either board integration.

The wireless page reads body diagnostics through message0x34 DIAGNOSTICS, subtype u8 and sample_counter u32 followed by fixed records: subtype0 counters (valid_rc, invalid_rc, sensor_polls, sensor_replies, protocol_errors, queue_errors, missed_drive_deadlines, max_loop_us; each u32), subtype1 profile field (field u8, wheel u8, value i32). Repeated reads request one bounded record using COMMISSION_REQUEST read with field0/subtype counters or field1/profile and wheel/`value` identifying the profile field. Do not dump EEPROM or arbitrary memory. Configuration generation is returned with status so the page can detect stale staged data. Persist failures report HARDWARE_ERROR/storage diagnostics; never display saved success from a queued request alone.

The novice's role is to inspect clearances, keep the cutoff reachable, observe that each movement/reference is correct, press Cancel if necessary, and accept consistent results. Software captures timings/counters. Multimeter checks remain unpowered continuity and stationary supply checks, never probes held beside a moving dome.

Create `TEENSY_BODY_CONTROLLER/` as a separate PlatformIO Arduino project for `board = teensy41`, pinned `platform = teensy@6.0.0`. Use bundled Servo, EEPROM, hardware UARTs, and a target-supported hardware watchdog implementation. Do not add Wi-Fi or an audio shield.

Compile shared protocol code through `lib_extra_dirs = ../shared` in both firmware projects. Keep current ESP32 platform/ReelTwo pins unchanged.

Portable codecs and state machines compile on the host with fake clocks and byte ports. Hardware adapters remain thin. Tests compile production `.cpp` files, not a Python reimplementation of their algorithms.

Required validation:

- Wire graph: every terminal exists, rails remain separate, power schedule matches, active signals have one owner, spares are unconnected.
- Protocol: golden packets, corruption, split/concatenated frames, overflow, reboot/session changes, retries, duplicates, queue saturation and rollover.
- Radio/drive: all channel validation, continued failsafe CH6/CH9 OFF frames, neutral arming, duty caps, acceleration ramp, per-wheel brake-before-reverse with fresh eRPM, both-controller freshness and recovery.
- Dome: dual references, invalid simultaneous detection, angle estimation/re-anchoring, startup search, ownership generations, front/rear drive priority, pivot holding, manual override,20-second idle reset, CH9 permission independent of CH6, lease expiry and seek timeout.
- Choreography: selective dome override, no resumed event movement, Auto Dome OFF event start, cancelled pre-start Leia versus continued started audio, Faint without motion locks, no radio holo control.
- Audio: initialization, spacing, status, ownership, completion/error correlation, unknown/known duration, no duplicated playback.
- ESP32: all entry points use the body API; existing light/holo/macro contracts survive.
- Build both targets. Hardware acceptance records measurements; software tests alone do not close hardware commissioning.

Current baseline at planning time: `python3 -m unittest discover -s tests -v` passes 33 tests. Current HEAD is `94bcf98`. This is the old architecture's baseline, not a result for the new firmware.

## 10. References and implementation plans

- [Physical documentation plan](../plans/2026-10-09-teensy-physical-documentation.md)
- [Firmware implementation plan](../plans/2026-10-09-teensy-firmware.md)
- [Teensy 4.1 specifications and power](https://www.pjrc.com/store/teensy41.html)
- [Teensy UART pin map](https://www.pjrc.com/teensy/td_uart.html)
- [Teensy core half-duplex and open-drain implementation](https://github.com/PaulStoffregen/cores/blob/master/teensy4/HardwareSerial.cpp)
- [DFPlayer manual, serial levels on pages 9 and 12](https://dfimg.dfrobot.com/enshop/image/data/DFR0299/DFPlayer%20Mini%20Manul.pdf)
- [iBUS channel/sensor protocol reference](https://github.com/bmellink/IBusBM)
- [Ordered Treedix carrier](https://www.amazon.ca/dp/B09NXYWYK7)
- [Selected Lonely Binary converter kit](https://www.amazon.ca/dp/B0FFMLDYNY)
- [Lonely Binary converter circuit and pull-ups](https://learn.lonelybinary.com/manuals/llc/one-transistor-two-resistors)
- [Lonely Binary serial timing guidance](https://learn.lonelybinary.com/manuals/llc/the-slow-rising-edge)

Read protocol references for formats; do not copy third-party implementation code without following its license. The new firmware must record its own implemented protocol tests.
