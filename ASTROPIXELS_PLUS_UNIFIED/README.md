# AstroPixels Plus Unified — Preferred ESP32 Firmware

This is the firmware selected for the build. It retains ReelTwo logic/PSI/holo lighting, the Wi-Fi dashboard, Wi-Fi Marcduino commands and OTA updates. `../ASTROPIXELS_UNIFIED_BRAIN.ino` is the earlier FastLED alternative. The Nano sketch is a reference only; its positional dome servo and blocking loops cannot be transplanted into a continuous-rotation ESP32 system.

## Hardware and Power

Follow [DOME_WIRING_DIAGRAM.md](../DOME_WIRING_DIAGRAM.md) for the complete diagram and level-shifter table. Body and dome use separate 12V-to-5V, 10A converters; fused 12V and ground cross the slip ring. Never join the two 5V positive outputs.

| ESP32 GPIO | Function | Connection |
| --- | --- | --- |
| 16 | i-Bus RX, 115200 baud | Slip-ring Ch 3 -> level shifter HV1/LV1 -> GPIO16 |
| 19 / AUX5 | Hall homing input | KY-003 signal -> HV2/LV2 -> GPIO19 |
| 4 / AUX2 | Continuous-rotation dome PWM | GPIO4 direct 3.3V -> slip-ring Ch 5 -> body servo signal; no shifter |
| 17 | DFPlayer TX, 9600 baud | GPIO17 direct 3.3V -> slip-ring Ch 4 -> 1k resistor near DFPlayer RX -> RX; no shifter |
| 21 / 22 | PCA9685 SDA / SCL | 3.3V logic and pull-ups; separate 5V servo V+ |
| 15 / 33 | Front / rear logics | Factory AstroPixels display connectors |
| 32 / 23 | Front / rear PSI | Factory connectors |
| 25 / 26 / 27 | Front / rear / top holo LEDs | Factory connectors |

Level-shifter **LV = 3.3V, HV = regulated 5V**, with common ground. Neither belongs on the 12V feed. Only iBUS and dome Hall use its two downshift channels; PWM/audio bypass it, and extra channels remain unused. ESP32 GPIO inputs are not 5V-tolerant. Verify the shifter's downshift capability and signal integrity at 115200 baud while rotating the slip ring. Verify the selected servo's3.3V signal acceptance and the actual DFPlayer module. GPIO19 is the chosen Hall input; no GPIO5 rewiring is implied for the factory LED connectors.

PCA9685 channels 0/1 control front pan/tilt, 2/3 rear pan/tilt, and 4/5 top pan/tilt. Configured travel is **1000–2000us**, with **1500us center**. Check each installed linkage for binding; these pulse limits are starting bounds, not a guarantee of a particular angle. Manual front tilt uses 1200–1800us; ambient motion and macro twitches use smaller ranges. Automatic motion is staggered rather than commanding every servo at once.

## Controls and Stops

Foot drive bypasses the ESP32 entirely. Use independent VESC inputs, transmitter tank mixing, receiver neutral failsafe and VESC input-loss timeout as described in [the drive guide](../VESC_DRIVE_INTEGRATION.md). Wi-Fi STOP only controls dome behavior, not the feet.

| RC control | Channel | Plus firmware action |
| --- | --- | --- |
| Left stick horizontal | CH4 | Dome rotation speed, not angle |
| Left stick vertical | CH3 | Front holo tilt; holds for three seconds after centering |
| SwD | CH9 | High (>1600us): ambient holo motion on; low: no new ambient moves |
| VrA + SwC down | CH7 + CH8 | Select/fire a routine; release SwC before firing again |
| SwA | CH6 | Unused; not a foot-drive lockout |

Missing valid i-Bus frames for **more than 250ms** cancels homing and deferred Leia playback, stops the dome and disables holo outputs. Motion begins inhibited at startup and after STOP, timeout or homing completion. A fresh radio connection and centered dome stick rearm manual rotation; holding a stale/deflected stick cannot restart it. Wi-Fi homing and macros also require a live radio and neutral rearm. If the receiver continues transmitting valid failsafe frames after RF loss, frame timeout cannot detect that loss: receiver failsafe channels must be configured and verified as well.

Manual tilt suspends front-holo library movement; ambient motion resumes only after its three-second hold. Macros own the servos while active, independent of SwD. Faint disables servo pulses, but whether the linkage physically droops depends on friction.

Wi-Fi, RC and animation callbacks are serviced on the same Arduino task to avoid cross-core actuator races. No fixed 200Hz loop timing is claimed. OTA entry stops dome PWM and disables holo outputs before flashing. This is not a physical emergency-stop circuit.

## Shared RC and Wi-Fi Macros

| Dial | Command | Behavior |
| --- | --- | --- |
| 1 | `:DMH` | Home against the Hall magnet; RC reset also plays track 011 |
| 2 | — | Normal lights and general sound-bank chirp |
| 3 | — | Normal lights and happy sound-bank chirp |
| 4 | `:SE01` | Track 102; 4.5 seconds of alarm lights and staggered small holo twitches |
| 5 | `:SE05` / `:SE07` | Track 106; marching logics, pulsing holo lights and staggered dance steps for **30 seconds provisionally** |
| 6 | `:SE08` | Home first; only after Hall detection, play track 109 with Leia lighting and front down-tilt for 14 seconds |
| 7 | `:SE09` | Track 110; 20 seconds of rainbow lighting |
| 8 | `:SE06` | Track 107; brief failure flicker, then all displays black after 600ms; holo outputs off for the five-second routine |
| — | `:DMS` / `:SE00` | Cancel dome homing/active macro and inhibit dome motion until neutral rearm |

RC and Wi-Fi use the same routine implementation, not separate legacy panel sequences. This droid has no dome panels: panel commands and unrelated stock routines are not registered in this adapted sketch. Dome rotation is stopped during macros; completion requires a centered dome stick before rotation resumes. Macro completion re-centers the holos and restores normal lighting. Chatter is suspended during macros; the saved random-sound preference controls resumption.

Timings are software timers, **not DFPlayer BUSY feedback or beat detection**. Replace provisional durations with measured file lengths; Cantina's current 30 seconds is the agreed assumption.

## Audio Card

Use a FAT32 MicroSD card and place files in **`/01/`**, with numbered names such as `001.mp3`, `102.mp3`, `106.mp3`, `109.mp3`, `110.mp3` and `255.mp3` (startup). All playback uses DFPlayer folder-play command `0x0F`; root-file ordering and `/MP3` numbering are not used by this firmware.

`MarcduinoSound.h` retains its 25-track sound banks for generic chatter; automatic chatter uses **only banks 1–4** to avoid the reserved macro tracks in bank 5. It is not the legacy FastLED sketch's 20-track mood grouping. Macro tracks use the explicit numbers above. A single queued playback sender spaces commands by at least 100ms; a newer command may replace an unsent track, with a serial diagnostic. No DFPlayer TX/BUSY return wiring is required for this simplex arrangement.

## Wi-Fi and Uploads

Connect to SSID **AstroPixels**, password **Astromech**, then open **http://192.168.4.1**. The dashboard has logic controls, **Dome & Macros**, sound preferences, Wi-Fi settings and **Firmware** upload. UART assignments are fixed to avoid repurposing the i-Bus receiver. The unused stock ESP-NOW remote settings have been removed. Change the default Wi-Fi password before operating around other devices.

The factory-default sound volume is **10 out of30** on the DFPlayer (stored default333/1000, rounded up to its integer level). Existing saved volume preferences override this default. Adjust and save using the Wi-Fi Sound Volume slider; RC volume control is not mapped. Amplifier gain remains fixed, so start low and check for distortion.

Build the supplied PlatformIO project:

```sh
pio run -d ASTROPIXELS_PLUS_UNIFIED
pio run -d ASTROPIXELS_PLUS_UNIFIED -t upload
```

From inside this directory, omit `-d ASTROPIXELS_PLUS_UNIFIED`. The pinned ESP32 platform and ReelTwo version in `platformio.ini` are the verified build path. The Arduino IDE alternative needs matching dependencies and an ESP32 core compatible with that platform; newer cores are not validated merely because the sketch has an LEDC version guard.

Initial flashing uses USB; subsequent application uploads can use the web Firmware page or ArduinoOTA. The default 4MB partition layout has two OTA application slots and SPIFFS. Do not flash the Nano reference or combine it with this sketch.

## First USB Flash: Ordered Walkthrough

This procedure uses the **ESP32 removed from the AstroPixels motherboard**, as agreed for this build. USB is its only power source during flashing. Accessible USB while mounted does not establish safe motherboard power isolation.

### 1. Prepare the computer and preserve existing settings

Use a data-capable USB cable, a stable computer USB port, and either VS Code with the official PlatformIO IDE extension or the official PlatformIO Core CLI. Install using [PlatformIO's installation instructions](https://docs.platformio.org/en/latest/core/installation/index.html). In VS Code, run the commands below in its PlatformIO terminal; an ordinary terminal may not have `pio` on its PATH. Internet access is needed for the first dependency/toolchain download.

Open this repository, not the Nano reference or the older FastLED sketch. From the repository root:

```sh
pio --version
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus
```

**Expected:** a final `SUCCESS` for `astropixelsplus`. Do not upload after a failed build. If `pio` is missing, use the PlatformIO terminal or repair its installation. For dependency/network failures, record the first error and resolve it without changing the pinned platform/library versions to "latest".

Before overwriting a previously used ESP32, record its known Wi-Fi credentials and settings and retain its original firmware/source if available. Uploading this application can replace the factory program; existing NVS preferences may remain and override this build's Wi-Fi and volume defaults. Do not erase all flash as a routine troubleshooting step.

### 2. Isolate and remove the ESP32

1. Disconnect battery, charger and USB. The master cutoff alone does not make every battery-connected wire safe.
2. Photograph/mark the ESP32's motherboard orientation and pin alignment. Carefully remove it without bending pins. Do not offset or reverse it on reinstallation.
3. Disconnect every external lead from the removed module. Place it on a clean nonconductive surface. No motherboard, LEDs, PCA, receiver, shifter, servo or audio wiring remains attached.
4. Connect USB only. Never power the servos or lighting array from the computer USB port. Stop and unplug for abnormal heat, smell or damaged connectors.

### 3. Identify the USB port and upload

```sh
pio device list
```

Identify the ESP32 port by comparing the list before/after plugging it in. Do not select another connected device. On macOS it commonly resembles `/dev/cu.usbserial-...` or `/dev/cu.SLAB_USBtoUART`; Windows commonly uses `COM...`. If no port appears, try a known data cable and another port, then identify the module's USB-serial chip and install its manufacturer's driver if required.

Close any serial monitor using that port. Substitute the actual port for `PORT`:

```sh
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus -t upload --upload-port PORT
```

**Expected:** upload/write verification completes and PlatformIO reports `SUCCESS`. If it stalls at `Connecting...`, use the module's documented BOOT/EN download-mode procedure; on a typical ESP32 DevKit, hold BOOT while connection is attempted and release when writing begins. Do not short unidentified pins. If that fails, stop and record the upload error and board/USB-chip identification rather than changing flash settings blindly.

### 4. Confirm isolated boot and Wi-Fi

```sh
pio device monitor -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus --port PORT --baud 115200
```

Press EN/reset once if necessary to capture startup. Exit the monitor with Ctrl+C before uploading again.

**Expected:** the `ASTROPIXELS PLUS - UNIFIED R2-D2 DOME BRAIN (REELTWO)` banner and `[SYSTEM] AstroPixels Plus Unified Brain Online & Ready!`, with no repeating reset/exception/brownout loop. No lights, audio or motion can be confirmed with peripherals disconnected; the ready message is not an electrical acceptance result.

For unchanged default preferences, join **AstroPixels** / **Astromech** and open **http://192.168.4.1**. A phone may report "no internet"; remain on that network. The dashboard should load. Saved credentials/AP mode may differ: use the recorded settings and serial-reported address. If Wi-Fi or the dashboard is absent, record the boot log and retained settings; do not assume a blank display or missing audio means the upload failed.

The web assets are supplied in this project's headers; there is no supplied `data/` image requiring an `uploadfs` step. Investigate filesystem warnings or missing web content rather than inventing a filesystem upload.

Change the default Wi-Fi password and record it. Set/save a low sound volume explicitly before connecting the amplifier: the factory default is 10/30, but a retained preference can be louder.

### 5. Reinstall with all power isolated

Close the monitor, unplug USB and disconnect battery/charger before reinstalling the ESP32 in its photographed orientation. Inspect pin seating. For the stages below, use the verified dome 5V supply, **not USB**, with an accessible cutoff and foot-motor propulsion disconnected.

The build omits5V output/device fuses by user decision and relies on the converter's advertised protection; downstream fault protection is unverified, as recorded in the harness guide. There is no requirement to add those fuses before this walkthrough. Verify normal-load wire/connector ratings, insulation and strain relief. Add only the stage being tested, changing wiring with all power disconnected. Until motherboard USB/external-supply isolation is established, do not attach USB during external-powered testing. If a serial log is needed, isolate/remove the ESP32 again; do not improvise a powered dual-supply connection.

## Staged Hardware Acceptance

Record each stage's result, voltage measurements, observed behaviour and any fault. A failed stage is a stop: isolate power, correct the cause and repeat it before adding more loads.

| Order | Setup and action | Expected result / failure action |
| --- | --- | --- |
| 1. AstroPixels lights only | Connect factory display leads in their labelled headers. Keep drive and holo servos, audio and RC disconnected. Boot from verified dome 5V and open the dashboard. | Front/rear scrolling startup text, then normal display operation; dashboard lighting changes affect the intended display. No motion is possible. Stop for resets, incorrect display routing or voltage sag; check power and connector orientation, not random GPIO changes. |
| 2. Receiver and shifter | Verify LV = 3.3V, HV = 5V and common ground before connecting GPIO16. Use the confirmed HV1/LV1 path for iBUS. Set receiver failsafe CH4 to dome neutral and macro trigger CH8 inactive; CH9 low disables ambient moves. Verify actual received values later through functional tests. | Dashboard remains responsive with receiver powered. Reliable RC behaviour must be demonstrated in later stages; an idle shifter voltage alone does not prove 115200-baud operation. Stop for instability or input misbehaviour. |
| 3. Dome servo, mechanically unloaded | Remove drive engagement so the dome cannot turn. Connect the servo with power isolated. Start with CH4 held deflected, then centre it before applying a small command. Keep hands clear of the shaft. | Startup does not command rotation until neutral rearm. Centred command stops the continuous servo; small commands rotate both ways. If it creeps at neutral, stop and calibrate the servo's actual neutral before engaging the dome; software neutral is not a guarantee of mechanical zero. |
| 4. Dome STOP and signal loss | On the unloaded servo, test dashboard STOP while CH4 is deflected, then centre/recommand. Separately test transmitter off and absent iBUS frames using a prepared signal-only break installed unpowered. Keep supply and ground intact. Restore link while CH4 remains deflected, then centre. | STOP and missing frames stop/inhibit dome motion; missing-frame threshold is >250ms, not guaranteed complete mechanical stopping time. Deflected-stick recovery must not restart rotation. Radio loss must produce neutral/inactive receiver failsafe values; valid failsafe frames are not detected as missing frames. Stop if either safeguard fails. |
| 5. Hall homing, still unloaded | Verify HV2/LV2 sensor path and a 3.3V-safe GPIO19 signal: magnet detection LOW, absence HIGH. With live radio and neutral rearm, request Home and present the magnet, then repeat without it. Also test STOP and signal loss during seeking. | Magnet detection stops/inhibits seeking; no magnet causes seek timeout after about 10s. STOP/loss cancels it. If the sensor is always LOW/HIGH, stop and check polarity, magnet orientation and wiring before mechanical engagement. |
| 6. PCA and one holo servo | Verify motherboard G/C/D -> PCA GND/SCL/SDA, separate 3V3 -> VCC, dome 5V -> V+, default address 0x40 and 3.3V I2C pull-ups. Leave I2C V unused. Initially attach one horn-free/unloaded servo on channel 0 or 1; SwD low prevents new ambient moves. | A controlled command moves the expected axis within configured pulse bounds without resets. The firmware ready message does not prove PCA presence. Stop for no response, wrong axis or jitter; inspect address, signal-row orientation, rails and wiring. Do not diagnose by forcing linkage movement. |
| 7. All holo axes | Add one unloaded servo at a time, then establish centres before fitting horns/linkages. Test CH3 manual front tilt, its three-second hold, and SwD ambient enable/disable. | Channels 0/1 front, 2/3 rear, 4/5 top. No binding through commanded travel; SwD low prevents new ambient moves but does not promise an immediate stop to a move already underway. Stop and adjust linkage/travel before loading it. |
| 8. Audio | Prepare FAT32 `/01/` files listed above. Connect DFPlayer, isolator and amplifier per harness schedule; use low saved volume and keep speaker negatives isolated from ground. | Startup track 255 and explicitly requested macro tracks play cleanly. Simplex UART has no playback confirmation: silence requires checking card/file names, power, RX resistor/path and line wiring. Do not raise volume to diagnose missing audio. |
| 9. Macros and cancellation | With mechanics clear, run each mapped routine from RC and Wi-Fi. Test Faint output disable/recovery, STOP during routines, and Leia with no home magnet. | RC/Wi-Fi invoke the same routine/track; dome rotation remains inhibited during macros and needs neutral rearm afterward. Leia must not play after failed/cancelled homing. Durations remain provisional timers, not audio-position feedback. |
| 10. Engaged dome and slip ring | Only after unloaded stop/Hall tests pass, engage dome drive and begin small movements. Repeat STOP, both loss tests and homing through actual travel. Measure rails under realistic lights/servos/audio. | No snagging, dropouts, resets or progressively warm terminals. Actual stopping/seek clearance must be acceptable. Isolate power immediately for runaway or collision risk; power removal can allow coasting. |

Foot-drive acceptance is separate: follow [VESC_DRIVE_INTEGRATION.md](../VESC_DRIVE_INTEGRATION.md). **Wi-Fi STOP does not stop the feet.** Do not combine propulsion with dome tests until each subsystem passes independently.

## Later OTA Updates

Use only the application image **`.pio/build/astropixelsplus/firmware.bin`** from a successful build of this project on the dashboard Firmware page; do not upload bootloader/partition images, another sketch or an arbitrary merged binary. Retain stable externally supplied power throughout the upload. Keep foot propulsion disabled and mechanically disconnect or isolate actuator loads for the update; software OTA inhibition is not a physical safety guarantee.

After an update, verify boot/dashboard access and repeat stop/rearm tests before restoring mechanical loads. For an OTA error or unresponsive device, keep motion isolated, record the error and return to the removed-module USB procedure if necessary. Do not repeatedly attempt updates while brownouts or power instability remain unresolved.
