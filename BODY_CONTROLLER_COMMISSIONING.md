# Body and Dome Commissioning

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Flash both the Teensy 4.1 body controller and the AstroPixels Plus ESP32 with the current firmware before combined testing; nothing counts as bench-verified until the commissioning record is filled in.

The Teensy body firmware, ESP32 body link client, and `/commissioning` Web UI are implemented and covered by host unit and two-board integration tests. Hardware behaviour is verified only by the checks in this guide.

**Wireless assembled-test workflow:** No laptop or USB cable is required while the dome is attached or rotating. The onboard Wi-Fi **Commissioning** page (`http://192.168.4.1/commissioning`) displays live radio/body diagnostics, both Hall sensors (Front and Rear), live test progress, revolution counts, and rotation rates. It provides guarded tests (`Neutral`, `Front Ref`, `Rear Ref`, `Timing CW`, `Timing CCW`), emergency `Cancel Test`, explicit acceptance buttons, and `Save Profile` to Teensy EEPROM. All actuator outputs remain inert and neutral until an approved commissioning profile is explicitly saved. USB instructions below apply to initial flashing or stationary setup; use the wireless page instead for installed diagnostics and rotation tests. Disconnect external programming cables before any dome rotation.

Use a multimeter, USB diagnostics, VESC Tool and the FlySky display. **No oscilloscope or logic analyzer** is required. Work in an open area with a reachable master cutoff, wheels on a secure stand and dome drive disengaged until its individual tests pass.

## 1. Carrier continuity and isolated rails

Disconnect battery, charger and USB. Remove fuses and unplug loads.

1. Confirm Treedix terminal labels by probing each terminal against its Teensy socket contact.
2. Check no continuity between the body/dome 5V positive buses. Grounds must have continuity through CH2.
3. Confirm all six ring contacts individually and no shorts between contacts; label CH4/5 spare and insulate both ends.
4. Check each inline fuse lies between its distribution takeoff and load. Removing it must break that positive feed. Verify no fan-out jumper bypasses it.
5. Fit main 25A, F3/F4 7.5A only; leave all four output fuses removed. Power the converters and measure each output: **4.9-5.1V**, correct polarity.
6. Switch off and disconnect before connecting loads. Fit B-LOGIC 2A/D-LOGIC3A first; servo fuses remain out.

Always use resistance/continuity mode with power disconnected. Capacitors can give a brief charging indication; a persistent low-resistance supply short must be investigated before applying power.

## 2. Bare Teensy USB flash

For the first flash, leave Teensy out of the carrier. Before cutting its VUSB/VIN link, USB can power the bare board without energizing any peripherals.

To build and flash the Teensy body controller:

```sh
pio run -d TEENSY_BODY_CONTROLLER -e teensy41
pio run -d TEENSY_BODY_CONTROLLER -e teensy41 -t upload
pio device monitor -d TEENSY_BODY_CONTROLLER -e teensy41 --baud 115200
```

Expect an uncommissioned state with motion disabled. Use Teensy's **Program** button if the loader requests it; it is not a general reset button. Do not enable a profile until the required physical tests are recorded.

### VUSB/VIN isolation

The approved modification separates USB5V from external VIN. It is **not** galvanic isolation; USB and droid ground still connect.

Use the **Teensy 4.1** underside card below. In the small **USB Device** detail at the lower right, PJRC's arrow says **"Cut to separate VIN from VUSB, if using external power."** It points to the short link between the two adjacent pads near the USB end. This is the cut location, not the USB-host pad group and not the Ethernet pads.

![PJRC Teensy 4.1 underside reference: designated VIN/VUSB cut beside the USB Device detail](https://www.pjrc.com/store/teensy41_card11b_rev4.png)

[Open the full-size official card](https://www.pjrc.com/store/teensy41_card11b_rev4.png) or use the printed card supplied with Teensy. [PJRC's Teensy 4.1 power instructions](https://www.pjrc.com/store/teensy41.html) specify separating these pads before combining USB and external VIN. Do not use underside pictures of Teensy 2.0/3.x to locate this cut.

1. Unplug USB and all power. Remove Teensy from the carrier.
2. Identify the two pads using the card's USB Device callout. Check VIN and VUSB are connected before cutting.
3. With a sharp hobby knife, cut **only the short surface copper link between those pads**. Use light passes; do not gouge into the multilayer board or cut adjacent tracks.
4. Measure between VIN and VUSB in both probe directions. There must be no direct low-resistance connection or continuous continuity tone. If still connected, inspect the cut; do not apply both power sources.
5. Check the empty Treedix carrier has no jumper/path joining VIN to VUSB. Reinstall Teensy and repeat the isolation check with all cables disconnected.
6. Supply body fused 5V to VIN and ground. Plug in a normal data USB cable and confirm diagnostics work. **USB alone will not power Teensy** after the cut.

Afterwards leave Teensy in its carrier for programming. External body power must be ON for uploads; CH6 OFF, wheels elevated and dome motion inhibited. Do not bridge the cut pads for routine programming.

If your physical board does not match the card's cut location, stop and obtain a matching photo before cutting. This is a visual-identification check, not a requirement to buy electronic test equipment.

### ESP32 USB flash

Remove ESP32 from AstroPixels with all power off. Flash the bare module from USB, then unplug USB before reinstalling. The Teensy modification does **not** isolate ESP32 USB power.

```sh
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus -t upload
```

Do not connect mounted, externally powered ESP32 to ordinary USB. Installed updates use the acknowledged maintenance/OTA procedure below.

## 3. Body boot and Logic-shifter communication check

Keep F1 VESC and both servo output fuses out. Power body electronics through B-LOGIC 2A.

1. Confirm receiver binds and Teensy stays DISARMED/uncommissioned.
2. Check body Lonely Binary LV at 3.3V and HV at 5V against ground with the multimeter.
3. In USB diagnostics, move each stick/switch and confirm channels update, centered values are near1500us, and valid-frame counters increase.
4. Observe CH6/SwA OFF and ON explicitly. Intermediate values disarm.
5. After VESC setup supplies feedback, confirm receiver SENSOR polls and voltage/temperature responses; compare external voltage to pack voltage, not receiver5V. Run input and telemetry together.
6. Stale telemetry must appear unavailable; it must not become a zero or frozen "fresh" reading. Confirm how the handheld indicates sensor loss.

Troubleshooting: disable motion, disconnect power, check continuity/channel pairing and solder joints, check supply rails powered, test SERVO and SENSOR separately on short leads, then swap in a spare Lonely Binary board and repeat the combined test. Do not change translators or buy an oscilloscope as the default recovery step.

## 4. Body/dome handshake, lights and audio

With servo fuses still out, connect CH3/CH6 to ESP32 RX16/TX17. Fit D-LOGIC3A. Confirm both link directions, fresh channel snapshots and separate left/right VESC records when available.

Connect to Wi-Fi `AstroPixels` / `Astromech`, dashboard `http://192.168.4.1`. Lighting and Wi-Fi must boot even with body offline; body actions must then be rejected visibly.

Prepare DFPlayer's FAT32 card with folder `/01`. At boot the Teensy resets the DFPlayer, retries every 5s until it answers, then sets volume 10/30. Audio stays `Offline` until the player answers. Request a track and confirm accepted -> playback started -> completion in diagnostics. Disconnect DFPlayer TX with power off and repeat: a track the player never confirms ends with a timeout after 2s; software must not claim confirmed playback without feedback. Check the isolator wiring and neither amplifier speaker terminal is grounded.

## 5. Dome and holo motion, unloaded first

Fit B-SERVO 5A with dome drive mechanically disengaged.

1. Calibrate servo neutral pulse until there is no creep. Check both directions, then centered stop.
2. Keep pulse-sleep disabled until the installed servo's signal-loss behaviour is tested. Test loss of pulses with gear disengaged; record whether it stops.
3. Test a deliberate Teensy firmware stall/reset with the dome unloaded. The VESC 150ms timeout does not stop this servo; the hardware watchdog reset target is 1second.
4. With receiver fresh, CH6 ON and dome stick centered, request home. A sensor already active must complete without motion. Otherwise pass a magnet over the Hall sensor: body commands neutral and reports completion.
5. Check manual stick motion cancels homing and Leia; a cancelled home must not start audio later.
6. Fit D-SERVO 5A and connect one holo servo at a time. Confirm connector orientation and channel allocation before installing horns. Calibrate mechanical travel so nothing hits its stop.
7. Run all six together through their intended movement. If a terminal, lead or board power connection becomes hot, stop. If a fuse blows, find the short/jam/load issue; do not increase it.

Only engage the dome drive after unloaded checks pass. Run several full dome rotations and watch link/error counters and clearances. Normal Hall-to-neutral command delay target is <=50ms; record actual dome overshoot separately.

### 5.1 Wireless Dome Commissioning Workflow (/commissioning)

The wireless commissioning web page allows complete calibration of the continuous-rotation dome servo and Hall reference sensors without any laptop or USB tether attached during rotation:

1. **Access the Web Interface:**
   - Connect your phone, tablet, or laptop to Wi-Fi SSID `AstroPixels` (password `Astromech`).
   - Navigate to `http://192.168.4.1/commissioning` or select **Commissioning** from the main web menu.
2. **Live Diagnostic Readouts:**
   - **Hall Sensors:** Live display of Front and Rear sensors (`ACTIVE` / `INACTIVE`), sensor validity bits, and telemetry sample age in milliseconds.
   - **RC Snapshot:** Real-time steering, throttle, manual dome stick (CH4), CH6 drive switch, and CH9 auto dome switch values.
   - **Calibration State:** Current test state (`Idle`, `Neutral`, `FrontRef`, `RearRef`, `TimingCw`, `TimingCcw`), completed revolutions, and measured CW/CCW angular rates in deg/s.
   - **Gates:** Live CH6/CH9/sticks/Hall gate state and the saved profile generation.
   - **Profile Fields:** Field ID / Wheel / Value boxes with **Set Field** and **Read Field** (read-back shown below them). Field ids are listed on the page and in `ConfigStore.h`.
3. **Two switch positions:** dome **tests** run with CH6 OFF, **CH9 ON** and sticks centred. **Set Field**, **Accept** and **Save** need CH6 OFF, **CH9 OFF** and sticks centred. Flip CH9 between them.
4. **Step-by-Step Commissioning Procedure:**
   - **Step 0 — Stage the dome fields (CH9 OFF):** Set field 0 `servo_neutral` (start at 1500), 1 `servo_min`, 2 `servo_max` and 3 `auto_speed` (percent, 1–25).
   - **Step 1 — Neutral Test (CH9 ON):** Disengage the dome drive gear. Click **Neutral Test**. The controller holds the staged `servo_neutral` pulse for 3s, then the test shows `Completed`. Watch the shaft the whole time. If it creeps, flip CH9 OFF, Set Field 0 to a new trim and repeat. When it stays still, flip CH9 OFF and click **Accept Neutral**.
   - **Step 2 — Front Reference Test (CH9 ON):** Click **Front Ref Test**. The dome slowly rotates until the Front Hall sensor detects the front magnet and halts automatically. Verify the Front Hall status indicates `ACTIVE`. Flip CH9 OFF and click **Accept Front Ref**.
   - **Step 3 — Rear Reference Test (CH9 ON):** Click **Rear Ref Test**. Same as the front, for the rear magnet. Flip CH9 OFF and click **Accept Rear Ref**. Each acceptance needs a completed run of *its own* test, made with the servo/auto-speed settings that are staged now; changing those fields later voids the run.
   - **Step 4 — Timing Calibrations (CW & CCW, CH9 ON):** Click **Timing CW**. The controller rotates 3 complete revolutions and stages the median CW rate into field 19 `cw_rate`. Then click **Timing CCW**, which stages field 20 `ccw_rate`; the CW result is kept. With both runs completed, flip CH9 OFF and click **Accept Timing**.
   - **Step 5 — Save Profile (CH9 OFF):** Click **Save Profile**. This sends `CommissionRequest(SaveProfile)` over the link to Teensy, persisting the staged fields and acceptance bitmask into EEPROM. The saved profile takes effect at once (no reboot). Actuators only ever run the saved profile; unsaved edits change nothing until the next Save.
4. **Safety Interlocks & Guards:**
   - **Automatic Keepalive Guard:** The browser sends a keepalive ping every 100ms. If the browser tab is closed, Wi-Fi drops, or keepalive is lost for >300ms, the Teensy automatically aborts any active test and commands neutral stop immediately.
   - **Emergency Cancel:** The **Cancel Test** button immediately terminates test motion and returns the servo to neutral.
   - **Radio Takeover Interlock:** Dome tests require CH6 OFF, CH9 ON and centred sticks. If the operator enables CH6, turns CH9 OFF or touches a stick, the controller cancels the test and returns the servo to neutral.
   - **Inert Boot Guard:** Until the servo neutral is accepted and saved, the dome servo receives no pulses and the CH4 stick does nothing. Until all eight VESC sign-offs are accepted and saved, the feet stay disarmed.

## 6. VESC commissioning and foot drive

Follow [VESC Drive Setup](VESC_DRIVE_INTEGRATION.md) for each independent USB/UART and motor. Keep both wheels elevated. Configure and verify both controllers' firmware/layout, currents, voltage limits,150ms timeout and timeout braking before saving an enabled profile.

### Stationary USB firmware and telemetry verification

Before enabling foot drive, verify that the Teensy correctly negotiates telemetry with both VESC controllers over their direct UART links. Teensy startup queries firmware versions and telemetry values; verify that the decoded fields match VESC Tool before saving an enabled profile.

1. Switch off and disconnect battery, charger and USB before removing the dome. Unplug body-side ring connectors, release the post fastener, and lift vertically to disengage the restraint. Access Teensy and both VESC USB ports at the top of the body; no cut shell panel or permanent external USB port is needed. Secure both wheels clear of the floor, inhibit foot drive (CH6 OFF, centered sticks) and inhibit dome motion (CH9 OFF, dome drive disengaged). Keep the cutoff reachable.
2. Verify the Teensy VUSB/VIN isolation above before using external body power with USB. With the dome removed and all motion inhibited, power the electronics and open Teensy's USB monitor at 115200 baud. Never rotate with an external USB/data/programming tether attached. Do not use old ESP32 VESC commands on this harness.
3. Type **one character** for one validated raw reply: `L` = left FW_VERSION, `R` = right FW_VERSION, `l` = left GET_VALUES, `r` = right GET_VALUES. Firmware is queried first and rechecked periodically; values are polled at 100ms intervals with only one outstanding request per UART. The monitor ignores line-ending characters. Wait for the complete report before requesting the next packet. A disconnected UART may yield no capture; that is not success.
4. Save all `VESC_RAW wheel=… offset=… length=… HEX…` lines for that packet. Concatenate the hexadecimal chunks in increasing offset order; `length` is the full frame length (not payload length). Each loop prints at most 16 captured bytes only when USB has buffer space; no continuous trace or actuator test is started. Capture both FW_VERSION and GET_VALUES separately for **each** controller. Retain controller identity, date, firmware/version, full raw bytes, decoded values, VESC Tool version and comparison notes as the fixture's provenance.
5. Strip the short (`02`, one-byte length) or long (`03`, two-byte big-endian length) header, CRC and `03` terminator for decoding. The FW payload starts `00 major minor`. Named layout **1 / kLayoutLegacyGetValues** has payload offsets including command byte: MOSFET signed dC at 1; motor/input signed current at 5/9 (wire 0.01A -> mA by multiplying by 10); signed duty permille at 21; signed eRPM at 23; signed voltage at 27 (wire 0.1V -> cV by multiplying by 10); fault byte at 53. Motor TEMP at 3 is not connected and is **invalid**, not a usable temperature. CRC and terminator must validate; required fields must fit their body-message representation.
6. Using each controller's stationary USB connection, compare the observed firmware and decoded voltage, MOSFET temperature, signed motor/input current, duty, eRPM and fault against **VESC Tool** on that same controller under stable conditions. Record differences and units; queries sample averaged current, so note timing differences rather than inventing agreement. Verify current sign and scaling during low-speed bench testing under light load before floor driving. Check the actual installed firmware's GET_VALUES layout if any field disagrees. Do not select a profile from length alone.
7. Only after the comparison passes, stage that wheel's fields (6 `fw_major`, 7 `fw_minor`, 8 `layout` = 1, currents 9-12, voltages 13/14, 15 `timeout_ms` = 150, 16 `timeout_brake_ma`, 17/18 reversal) and accept `vesc_config` (bit 4 left / 5 right). Use the `/commissioning` **Accept bit** box or `profile accept vesc_config_left` over USB, with CH6 OFF, CH9 OFF and sticks centred. Set/save does not imply acceptance, and changing a field clears the sign-offs that depend on it. Actuator writes additionally require the separately accepted `timeout_brake` (6/7), `direction` (8/9) and `reversal` (10/11) records; accept each only after observing that test. Then **Save Profile**: the drive uses it without a reboot. Verify current limits and braking behaviour according to the commissioned power budget.
8. Disconnect all external USB/data/programming cables and power before reassembly. Reconnect the keyed ring connectors and restraint, then verify clearances unpowered. Use the wireless commissioning page for installed rotation checks; **no rotating tether**.

Set CH6 OFF. Center steering/throttle, turn CH6 ON, keep centered 500ms, then check low-rate movement and wheel direction. A controller fault or stale feedback stops both wheels and requires OFF -> ON -> neutral rearming.

### Full-pack braking

Test initial slow braking on elevated wheels, then in an open floor area at the intended full battery charge. Watch both VESC voltages/faults and battery input current; confirm no overvoltage fault or BMS disconnect. Increase speed only after the previous stopping test passes. Record stopping distance. Brake-current commands do not guarantee a fixed mechanical stopping distance.

Do not deliberately lock a motor or servo shaft to "measure stall". Do not place a multimeter in current mode across the supply. Use VESC feedback for controller current; total battery-current measurement requires a properly rated inline meter or DC clamp meter if validating the20A system budget.

## 7. Failure acceptance matrix

Disconnect/reconnect signal cables only with power off, then repeat the indicated test. Leave the wheels elevated for fault tests.

| Test | Action | Required result |
| --- | --- | --- |
| Transmitter switched off | Turn transmitter off during slow motion | Receiver failsafe supplies centered sticks, CH6 OFF, CH8 released, CH9 OFF; feet disarm, dome stops |
| Receiver iBUS unplugged | Remove SERVO signal then power up | Channel data stale after 250ms; no arm/motion; invalid frames never extend freshness |
| Left VESC UART unplugged | Remove left feedback connection | Left telemetry stale after 500ms; both feet inhibited; rearming required |
| Right VESC UART unplugged | Remove right feedback connection | Same outcome for right; left cannot continue alone |
| VESC command stream stopped | Stop controller commands using diagnostic test | Each VESC executes configured timeout braking after 150ms without commands |
| Dome link unplugged | Remove one link direction | Peer heartbeat timeout 300ms; remote actions cancel; healthy manual foot drive remains available |
| Remote velocity renewal lost | Stop dome request renewals | Lease expires <=150ms plus one 20ms actuator cycle; servo commands neutral |
| Hall update lost during homing | Suppress Hall updates using diagnostic test | Hall age>150ms fails seek; no deferred Leia |
| Web STOP acknowledgement | Press STOP during a routine | Local routine stops; body stops feet/dome/audio and latches stop; UI confirms only after body reply. **Release STOP** is refused until CH6 OFF and sticks centred 500ms, then clears feet and dome together |
| STOP with body offline | Press STOP with link disconnected | "Body stop unconfirmed"; physical cutoff remains available |
| Manual dome override | Deflect CH4 during home/automatic rotation | Remote action cancels and manual control takes priority |
| Restart during lock | Reboot dome after a STOP or maintenance lock | Lock remains latched on the body; reconnect does not release it. Release STOP / Recover body locks clears it. Locks live in Teensy RAM, so a Teensy power cycle starts unlocked |
| Routine dome actions | Prepare update, start a commissioning test, lose RC on the dome | None of these latches a body STOP; only the STOP button does |

Software detection time is not wheel stopping time. Record physical stopping behaviour separately from software timeout measurements.

## 8. Maintenance lock before OTA

The firmware implements an acknowledged maintenance lock before OTA flashing:

1. CH6 OFF, center motion sticks, stop routine.
2. Press **Prepare update** on the firmware page; wait for body maintenance-lock acknowledgement.
3. Start web upload or ArduinoOTA only after that confirmation. Unprepared uploads are aborted before an image is activated (ArduinoOTA via `Update.abort()` in its start hook).
3a. Cancelling an update sends UNLOCK; the page stays Locked until the body confirms, which needs CH6 OFF and sticks centred 500ms.
4. Verify feet and dome remain inhibited throughout update/reboot and on failed upload.
5. After a dome restart loses its old token, use explicit **Recover body locks** with CH6 OFF and motion sticks centered 500ms. Recovery leaves feet disarmed; it does not start motion.
6. Dashboard **Reboot** and **Clear Prefs** take the same maintenance lock before restarting the dome when the body is connected (with no body link they restart straight away). Afterwards the body is still maintenance-locked; use **Recover body locks** as in step 5.

Do not use an old firmware upload path that lacks this handshake on the assembled target harness.

## 9. Slow floor run and results record

Use slow 35% duty mode initially, on smooth level ground with space to stop. Run ten minutes with receiver input, both VESC feedback links, audio, lighting and dashboard activity. Check no missed 20ms drive scheduling deadline, telemetry responses enqueued within 1ms, no recurring link/RC errors, and no hot connectors or binding mechanics. Firmware counters measure scheduling, not electrical edge shape.

Fill this table as checks pass; blank means **not commissioned**, not an assumed pass.

| Check | Recorded value/result | Date |
| --- | --- | --- |
| Body/dome buck voltage and polarity | | |
| Carrier continuity / VUSB-VIN isolation | | |
| Concurrent RC/telemetry counts and errors | | |
| Transmitter-off failsafe CH1-9 values | | |
| Left/right firmware and values-profile IDs | | |
| Per-wheel currents, voltage, brake and timeout settings | | |
| Wheel direction and CH6 neutral arming | | |
| DFPlayer start/completion/error feedback | | |
| Servo neutral/signal-loss/reset behaviour | | |
| Hall command delay and dome overshoot | | |
| Loaded stopping distance / full-pack braking | | |
| Ten-minute thermal and scheduling check | | |
| Web/Arduino OTA lock and recovery | | |
