# AstroPixels Plus Unified Firmware Guide

This is the primary dome firmware for the R2-D2 conversion. Built on ReelTwo and AstroPixels Plus, it provides authentic lighting animations, a phone-friendly Wi-Fi dashboard, Over-The-Air (OTA) wireless updates, and smooth servo control.

---

## 1. Hardware Pinout & Wiring

Follow [DOME_WIRING_DIAGRAM.md](../DOME_WIRING_DIAGRAM.md) for full circuit schematics.

| ESP32 Pin | Function | Wiring Connection |
| :---: | :--- | :--- |
| **GPIO 16** | FlySky iBUS Serial (115,200 baud) | Receiver in dome &rarr; Level shifter HV1 &rarr; LV1 &rarr; GPIO16 |
| **GPIO 18** | Dual VESC Drive UART (115,200 baud) | Direct 3.3V UART (AUX 4) &rarr; Slip ring CH6 &rarr; Dual VESC Port 3 RX |
| **GPIO 19** | KY-003 Hall Homing Sensor | Hall signal &rarr; Level shifter HV2 &rarr; LV2 &rarr; GPIO19 |
| **GPIO 4** | 35kg Continuous Dome Servo PWM | Direct 3.3V PWM &rarr; Slip ring CH5 &rarr; Servo signal lead (no shifter) |
| **GPIO 17** | DFPlayer Serial Commands (9,600 baud)| Direct 3.3V UART &rarr; Slip ring CH4 &rarr; 1k&Omega; resistor &rarr; DFPlayer RX (no shifter) |
| **GPIO 21 / 22** | PCA9685 I2C (SDA / SCL) | Motherboard I2C headers D/C &rarr; PCA9685 SDA/SCL (3.3V logic) |
| **GPIO 15 / 33** | Front / Rear Logic Displays | Standard AstroPixels display headers FLD / RLD |
| **GPIO 32 / 23** | Front / Rear PSI Displays | Standard AstroPixels headers FPSI / RPSI |
| **GPIO 25 / 26 / 27**| Front / Rear / Top Holo LEDs | Standard AstroPixels headers FHP / RHP / THP |

### Level Shifter & Logic Voltages
* **Level Shifter:** Connect **HV** to dome 5V, **LV** to ESP32 3.3V, and **GND** to common ground. Only 5V inputs (receiver iBUS and Hall sensor) pass through the shifter.
* **Direct 3.3V Outputs:** VESC Drive UART (GPIO18 / AUX 4), Dome servo PWM (GPIO4), and DFPlayer TX (GPIO17) originate from the ESP32 at 3.3V and bypass the shifter.
* **PCA9685 Servo Driver:** Logic power (**VCC**) runs on 3.3V from the ESP32. Servo power (**V+** green terminal) connects directly to the dome 5V distribution block. Servos 0–5 control Front (0/1), Rear (2/3), and Top (4/5) pan and tilt.

---

## 2. Radio & Wi-Fi Controls

The dome ESP32 serves as the unified central brain for the entire droid, decoding FlySky radio input, running lighting animations, positioning servos, and driving the foot motors via Dual VESC UART down the slip ring.

### Transmitter Controls (FlySky FS-i6X)
| Radio Control | Channel | Function in Plus Firmware |
| :--- | :---: | :--- |
| **Right Stick Vertical** | CH 2 | **Throttle:** Forward and reverse foot motor drive. |
| **Right Stick Horizontal** | CH 1 | **Steering:** Differential tank steering for foot motors. |
| **Left Stick Horizontal** | CH 4 | **Dome Rotation:** Proportional continuous speed; stops dead at center. |
| **Left Stick Vertical** | CH 3 | **Manual Front Holo Tilt:** Up/down control; holds position for 3s after centering. |
| **Switch `SwB`** | CH 5 | **Speed Rates:** Pos 1 = Slow (35%), Pos 2 = Medium (70%), Pos 3 = Fast (100%). |
| **Switch `SwD`** | CH 9 | **Ambient Holo Motion:** HIGH enables random autonomous twitches; LOW disables them. |
| **Knob `VrA` + Switch `SwC`** | CH 7 + CH 8 | **Macro Trigger:** Select routine with `VrA`, flip `SwC` DOWN to execute. |

### Motion & Safety Failsafes
* **Missing iBUS Signal (>250ms):** Automatically stops foot drive (sends 0 duty to VESC), cancels homing, stops dome rotation, and disables holoprojector servo outputs.
* **Startup & Failsafe Lockout:** Dome rotation starts disabled. Centering the dome joystick rearms manual control.
* **Wi-Fi STOP Button:** Immediately halts foot motors, dome rotation, and active routines.

---

## 3. Shared Sound & Motion Macros

These routines can be triggered from either the transmitter dial (`VrA` + `SwC`) or the Wi-Fi web dashboard:

| Dial `VrA` | Web Command | Audio Track | Description |
| :---: | :---: | :---: | :--- |
| **Pos 1** | `:DMH` | `011.mp3` | **Home Dome:** Rotates dome until Hall sensor detects 0° magnet, centers servos. |
| **Pos 2** | &mdash; | Random | Normal lighting and a cheerful chirp. |
| **Pos 3** | &mdash; | Random | Normal lighting and a happy chirp. |
| **Pos 4** | `:SE01` | `102.mp3` | **Scream / Panic:** Red alarm lighting and fast holoprojector twitches (4.5s). |
| **Pos 5** | `:SE05` / `:SE07` | `106.mp3` | **Cantina Band:** Marching logic lights and synchronized servo dance steps (30s). |
| **Pos 6** | `:SE08` | `109.mp3` | **Princess Leia:** Homes dome forward to 0°, pale green lights, front holo dips 35° with blue flicker (14s). |
| **Pos 7** | `:SE09` | `110.mp3` | **Disco:** Rainbow color wave across all displays (20s). |
| **Pos 8** | `:SE06` | `107.mp3` | **Short Circuit / Faint:** Flickers briefly, turns displays black, disables servo PWM for 5s, then restores normal lights. |
| &mdash; | `:DMS` / `:SE00` | &mdash; | **Stop:** Cancels homing or active macro and pauses dome rotation until stick is re-centered. |

---

## 4. MicroSD Card Audio Setup

1. Format your MicroSD card as **FAT32**.
2. Create a folder named **/01/** on the card.
3. Place your MP3 files inside `/01/` using 3-digit numerical filenames:
   * `001.mp3` through `080.mp3` &rarr; Ambient chirps and chatter.
   * `102.mp3` &rarr; Scream.
   * `106.mp3` &rarr; Cantina Band.
   * `107.mp3` &rarr; Short Circuit / Faint.
   * `109.mp3` &rarr; Princess Leia message.
   * `110.mp3` &rarr; Disco.
   * `255.mp3` &rarr; Startup chime.

The firmware plays sounds using folder-play commands (`0x0F`), so files must be placed inside the `/01/` directory.

---

## 5. Wi-Fi Dashboard & Web GUI

When powered, the ESP32 broadcasts its own Wi-Fi network:
* **SSID:** `AstroPixels`
* **Password:** `Astromech`
* **Dashboard URL:** `http://192.168.4.1`

Open this address in any browser on your phone, tablet, or laptop to access:
* **Logics & PSIs:** Custom text messages, speed settings, and color palettes.
* **Dome & Macros:** One-click buttons for Cantina, Leia, Scream, Disco, and Faint routines.
* **Sound Settings:** Sound volume slider (default is 10 out of 30) and chatter frequency controls.
* **Firmware:** Over-The-Air wireless firmware upload page.

---

## 6. First USB Flash: Step-by-Step

Before mounting the ESP32 into the dome motherboard, flash it cleanly over USB using PlatformIO.

### Step 1: Build the Firmware
In VS Code with the PlatformIO extension (or via PlatformIO CLI):
```sh
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus
```
Verify that the build completes with `SUCCESS`.

### Step 2: Unmount the ESP32
* Remove the ESP32 module from the AstroPixels dome motherboard.
* Flashing the bare ESP32 over USB ensures your computer's USB port does not power dome servos or LED displays.

### Step 3: Flash Over USB
1. Plug the ESP32 into your computer using a data-capable USB cable.
2. Find the port:
   ```sh
   pio device list
   ```
3. Upload firmware (substitute your actual serial port):
   ```sh
   pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus -t upload --upload-port PORT
   ```

### Step 4: Verify Boot
Open the serial monitor at 115,200 baud:
```sh
pio device monitor -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus --baud 115200
```
Press the EN/Reset button on the ESP32. You should see:
```
[SYSTEM] AstroPixels Plus Unified Brain Online & Ready!
```
Connect your phone to the `AstroPixels` Wi-Fi network and verify the dashboard opens at `http://192.168.4.1`.

### Step 5: Reinstall on the Motherboard
Unplug USB, ensure main power is turned off, and seat the ESP32 firmly into the motherboard headers in its correct orientation.

---

## 7. Staged Hardware Testing

Test each subsystem in order to verify wiring:

1. **AstroPixels Displays:** Power the dome 5V rail. Confirm startup text scrolls across FLD and RLD, and PSIs cycle through normal animations.
2. **Radio Link & Level Shifter:** Turn on the transmitter and receiver. Verify the receiver binds and the dashboard remains responsive.
3. **Dome Rotation Servo (Unloaded):** Connect the dome servo with its drive gear disengaged. Verify stick center keeps the servo completely stopped, and stick deflection rotates it left and right.
4. **Failsafe & STOP Test:** Deflect the dome stick while pressing **STOP** on the web dashboard. Verify rotation immediately halts. Turn off the transmitter to confirm failsafe stop.
5. **Hall Homing:** With the transmitter on, request **Home**. Wave a small magnet past the KY-003 sensor to verify it halts rotation and registers 0° center.
6. **PCA9685 & Servos:** Connect PCA9685 logic and 5V servo power. Plug in one holoprojector servo at a time and verify smooth motion without jitter.
7. **Audio System:** Connect DFPlayer, isolator, and amplifier. Power up and verify startup track `255.mp3` plays cleanly through the speaker.
8. **Macros:** Trigger the Cantina and Leia routines from the transmitter and web dashboard to verify synchronized lights, sound, and servo movements.

---

## 8. Wireless Over-The-Air (OTA) Updates

Once R2 is fully assembled, you can update the dome firmware wirelessly without taking the droid apart:

1. Build the updated binary in PlatformIO:
   ```sh
   pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus
   ```
   The compiled file will be located at `.pio/build/astropixelsplus/firmware.bin`.
2. Connect your computer or phone to the `AstroPixels` Wi-Fi network.
3. Open `http://192.168.4.1` and navigate to the **Firmware** page.
4. Select `firmware.bin` and click **Upload**.
5. The ESP32 will flash the new binary and reboot automatically within 30 seconds.
