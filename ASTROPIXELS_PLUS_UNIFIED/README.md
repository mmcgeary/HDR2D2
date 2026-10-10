# AstroPixels Plus Unified: Dome Firmware and Target Wiring

> **Safety Notice:** Do not connect this wiring to the old ESP32-only firmware. Both the Teensy 4.1 body controller and AstroPixels Plus ESP32 firmware are fully implemented; flash both boards before combined testing.

This firmware runs on the ESP32 in the dome for lighting (ReelTwo), Wi-Fi, holo servos, Hall reference sensors, sound selection, and web-based diagnostics/commissioning. All radio decoding, motor drive, continuous dome-servo pulsing, and DFPlayer UART ownership are handled by the Teensy body controller.

Follow [Body Wiring](../BODY_CONTROLLER_WIRING.md), [Dome Wiring](../DOME_WIRING_DIAGRAM.md) and [Commissioning](../BODY_CONTROLLER_COMMISSIONING.md).

## 1. Target ESP32 map

| ESP32 pin | Target role |
| --- | --- |
| GPIO16 | Serial2 RX, body link from TeensyTX17 via ringCH3 |
| GPIO17 | Serial2 TX, body link to TeensyRX16 via ringCH6 |
| GPIO18 | Rear Hall signal through dome Lonely Binary channel 3 |
| GPIO19 | Front Hall signal through dome Lonely Binary channel 2 |
| GPIO21/22 | PCA9685 SDA/SCL |
| GPIO15/33 | FLD/RLD |
| GPIO32/23 | FPSI/RPSI |
| GPIO25/26/27 | FHP/RHP/THP LEDs |
| GPIO2/4/5 | Unused / spare |

Serial2's sole reader is the framed body client at 115200; the old MarcDuino UART reader has been disabled while web/internal commands remain. Receiver, VESCs, continuous dome-servo pulses and DFPlayer UART are handled by Teensy.

| Contact | BODY end | DOME end | Function |
| --- | --- | --- | --- |
| CH1 | F4 7.5A fused battery positive | Dome buck IN+ | Dome 12V feed |
| CH2 | Body ground bus | Dome ground / buck IN- | Common return |
| CH3 | Teensy TX17 | ESP32 GPIO16 | Body to dome serial |
| CH4 | Unconnected | Unconnected | Spare |
| CH5 | Unconnected | Unconnected | Spare |
| CH6 | Teensy RX16 | ESP32 GPIO17 | Dome to body serial |

UART 3.3V bypasses shifters. Dome Lonely Binary HV=D-LOGIC 3A fused 5V, LV=ESP32 3.3V, GND common; Front Hall HV2->LV2->GPIO19, Rear Hall HV3->LV3->GPIO18. No receiver signal in dome.

PCA9685 VCC=3.3V, V+=D-SERVO 5A fused 5V with 16AWG feed/return; AstroPixels I2C D/C/G ->SDA/SCL/GND, V unconnected. Address 0x40,50Hz;0/1front pan/tilt,2/3rear,4/5top.

## 2. Target radio controls and safety

| Input | Target function |
| --- | --- |
| CH1 / right horizontal | Foot steering |
| CH2 / right vertical | Foot throttle |
| CH3 / left vertical | Unused |
| CH4 / left horizontal | Manual dome rotation |
| CH5 / SwB | Normalized duty rates35/70/100%;95% absolute cap |
| CH6 / SwA | Drive enable; OFF -> ON -> centered 500ms after boot/fault |
| CH7 / VrA + CH8 / SwC | Macro selection / trigger |
| CH9 / SwD | Auto Dome enable |
| CH10 | Unused |

Teensy validates RC, both VESC feedback links and actuator locks. Stale/faulted feedback on either wheel inhibits both; neutral uses brake current, not zero duty. Manual dome can work with CH9 OFF; automatic home requires fresh RC, CH9 ON and neutral dome stick.

Wi-Fi STOP displays verified body confirmation or "Body stop unconfirmed." Maintenance locks remain latched across dome restart; Faint macro does not engage maintenance lock. These acknowledgements, the `/diagnostics` telemetry page, explicit lock recovery, `/commissioning` Web UI, and OTA preparation are fully implemented and integrated.

## 3. Sound library and choreography

Keep FAT32 microSD folder `/01/`, three-digit filenames. Ambient chirps/chatter use 001-080; macro tracks stay reserved. Default volume 10/30 unless saved settings override it; no physical volume potentiometer required.

| VrA position / command | Track | Intended routine |
| --- | --- | --- |
| 1 / `:DMH` | 011.mp3 | Home dome, then sound |
| 2 | Random | Normal / cheerful |
| 3 | Random | Normal / happy |
| 4 / `:SE01` | 102.mp3 | Scream / alarm 4.5s |
| 5 / `:SE05`, `:SE07` | 106.mp3 | Cantina lights and holo dance 30s |
| 6 / `:SE08` | 109.mp3 | Leia: home first, then playback-confirmed choreography 14s |
| 7 / `:SE09` | 110.mp3 | Disco 20s |
| 8 / `:SE06` | 107.mp3 | Faint / motion lock 5s |
| `:DMS`, `:SE00` | None | Stop |
| Startup | 255.mp3 | Startup chime |

Folder-play command 0x0F uses `/01/`. Supply your own legally obtained audio. ESP32 selects tracks while Teensy controls the DFPlayer and returns playback events; an accepted UART request alone does not prove audible sound.

## 4. Wi-Fi & Web Pages

SSID **AstroPixels**, password **Astromech**, dashboard **http://192.168.4.1**.
- **Main Dashboard (`/`):** Logic and PSI text controls, LED colours/speed, sound volume, manual and autonomous macros, and acknowledged emergency STOP.
- **Diagnostics (`/diagnostics`):** Real-time display of radio channels, VESC left/right status (voltage, current, eRPM, faults), body faults, lock reasons, and link latency.
- **Commissioning (`/commissioning`):** Dedicated calibration page for continuous dome servo and Hall sensors without requiring a laptop/USB tether. Provides live Hall Front/Rear readouts, RC snapshot, test controls (`Neutral`, `Front Ref`, `Rear Ref`, `Timing CW`, `Timing CCW`), emergency `Cancel Test`, acceptance bitmask, and `Save Profile` to Teensy EEPROM.
- **Firmware Upload (`/upload`):** Web and ArduinoOTA update portal gated by explicit body maintenance-lock handshake.

## 5. First bare ESP32 flash

Turn off/disconnect droid power. Remove ESP32 from AstroPixels **before** USB. Use a data cable matching the module's actual socket.

```sh
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus
pio device list
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus -t upload --upload-port PORT
pio device monitor -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus --baud 115200
```

Replace PORT with the discovered device. Use EN/Reset if needed, confirm Wi-Fi boots, then unplug USB before seating the ESP32 with droid power still off. Never connect ordinary computer USB to the mounted, externally powered ESP32.

The Teensy body controller firmware (`TEENSY_BODY_CONTROLLER/`) is fully implemented. Its build/flash sequence and one-time USB-power isolation are documented in [Commissioning](../BODY_CONTROLLER_COMMISSIONING.md).

## 6. Installed OTA procedure

**Maintenance-gated updates:** With CH6 OFF and motion sticks centered, press **Prepare update** on the web dashboard and wait for body maintenance-lock acknowledgement before starting web upload or ArduinoOTA. Both upload paths reject unprepared updates before flash writes.

Build with the command above; binary is `ASTROPIXELS_PLUS_UNIFIED/.pio/build/astropixelsplus/firmware.bin`. After upload or reboot, motion remains safely locked. Use the explicit **Recover body locks** action on the dashboard with fresh handshake, CH6 OFF and centered sticks for 500ms, which leaves drive disarmed until deliberate rearming. Complete the power, communication, servo and failure stages in [Commissioning](../BODY_CONTROLLER_COMMISSIONING.md) before floor operation.
