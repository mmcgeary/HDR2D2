# R2-D2 System Architecture

This document provides a quick overview of how the upgraded Home Depot R2-D2 works, how the subsystems connect, and where to find detailed build guides.

---

## 1. System Overview

The droid is split into two physical zones: the **Body** (power, drive motors, sound, and dome rotation servo) and the **Dome** (ESP32 brain, FlySky radio receiver, lighting displays, and holoprojector servos). They connect electrically through a 6-channel through-bore slip ring mounted around the central pivot post.

```mermaid
flowchart TB
    subgraph BODY [BODY]
        BAT[12V 20Ah LiFePO4 Battery]
        FUSE[25A Main Fuse & Cutoff Switch]
        FBOX[12V Fuse Block]
        BUCK_B[Body 5V / 10A Buck]
        VESC[Dual FSESC 4.20 Controller (Internal CAN ON)]
        MOTORS[Razor Hub Motors Left / Right]
        AUDIO[DFPlayer + HF82 Amp + Speaker]
        DOME_SERVO[35kg Continuous Servo]

        BAT --> FUSE --> FBOX
        FBOX -->|15A| VESC ==> MOTORS
        FBOX -->|7.5A| BUCK_B
        FBOX -->|5A| AUDIO
        BUCK_B --> AUDIO
        BUCK_B --> DOME_SERVO
    end

    subgraph RING [6-CHANNEL SLIP RING]
        R1[CH1: Fused 12V to Dome]
        R2[CH2: Common Ground]
        R3[CH3: VESC Telemetry to Dome]
        R4[CH4: Audio Serial from Dome]
        R5[CH5: Dome Servo PWM from Dome]
        R6[CH6: VESC Drive UART from Dome]
    end

    subgraph DOME [DOME]
        BUCK_D[Dome 5V / 10A Buck]
        RX[FlySky FS-iA6B Receiver]
        ESP[ESP32 Dome Brain]
        SHIFTER[3.3V / 5V Level Shifter]
        PCA[PCA9685 Servo Driver]
        SERVOS[6x Holoprojector Servos]
        LIGHTS[AstroPixels Logic & PSI Displays]
        HALL[KY-003 Hall Homing Sensor]

        BUCK_D --> ESP
        BUCK_D --> RX
        BUCK_D --> PCA
        BUCK_D --> LIGHTS
        RX -->|iBUS 5V| SHIFTER -->|LV1 3.3V| ESP
        PCA --> SERVOS
        ESP --> LIGHTS
        ESP --> PCA
        HALL --> SHIFTER --> ESP
    end

    FBOX -->|7.5A Fused 12V| R1 --> BUCK_D
    FBOX -->|Negative Bus| R2 --> BUCK_D
    VESC -->|Telemetry TX 3.3V (Port 3 Pin 5)| R3 -->|GPIO5 RX (AUX 3)| ESP
    ESP -->|Audio TX 3.3V| R4 --> AUDIO
    ESP -->|Dome PWM 3.3V| R5 --> DOME_SERVO
    ESP -->|VESC UART 3.3V (AUX 4)| R6 -->|Port 3 COMM RX| VESC
```

---

## 2. Key Subsystems

### Power & Safety
* **Battery:** 12V nominal (12.8V) 20Ah LiFePO4 battery (Renogy RBT1220LFP-TM).
* **Main Protection:** 25A blade fuse within 150mm of the battery positive post, followed immediately by an accessible high-current master cutoff switch.
* **12V Fuse Box:** A single 6-way fuse block with an integrated negative bus powers all major branches (Dual VESC, body buck, dome feed, amplifier, and optional voltage telemetry).
* **Independent 5V Rails:** Two separate 12V-to-5V, 10A buck converters power the body and dome electronics. **Never connect the body 5V and dome 5V rails together.** Ground remains common across the entire droid.

### Dome Brain (ESP32)
* **Primary Firmware:** [AstroPixels Plus Unified](ASTROPIXELS_PLUS_UNIFIED/README.md) running ReelTwo.
* **All-In-One Controller:** Acts as the central brain for the entire droid, decoding FlySky RC input, animating dome lighting, driving servos, and transmitting motor drive commands down to the body.
* **Lighting:** Controls Front Logic Displays (FLD), Rear Logic Displays (RLD), Front/Rear PSIs, and 3 Holoprojector LED cores.
* **Holoprojector Servos:** Six MG90S servos (pan and tilt for front, rear, and top holos) driven by a PCA9685 I2C driver on channels 0–5.
* **Dome Rotation:** Drives a 35kg continuous-rotation servo mounted in the body, with a stationary magnet and a KY-003 Hall effect sensor in the dome for automatic 0° homing.
* **Wi-Fi Dashboard:** Built-in web interface for triggering routines, adjusting sound volume, tweaking lighting settings, and uploading firmware updates over the air (OTA).

### Foot Drive (Dual VESC 4.20)
* **Single-Brain Drive:** The dome ESP32 reads your right joystick throttle and steering over iBUS, calculates tank differential mixing, applies speed limits (switch SwB), and sends VESC UART packets down Slip Ring CH6 to the body.
* **Internal CAN Bus:** The Flipsky Dual 4.20 hardware switch is set to `ON: dual`. The Master controller (Left wheel, ID 1) forwards commands to the Slave controller (Right wheel, ID 2) over internal CAN bus via `COMM_FORWARD_CAN`. No body microcontroller is needed.
* **Full-Duplex VESC Telemetry:** In addition to sending drive commands on Slip Ring CH6 &rarr; Port 3 `RX`, the Master VESC transmits live telemetry packets (`COMM_GET_VALUES`) on Port 3 `TX` &rarr; Slip Ring CH3 &rarr; ESP32 `GPIO5 (AUX 3)`. The ESP32 monitors actual battery voltage, motor currents, ERPM, and hardware fault codes, cutting throttle automatically if a fault occurs or the pack drops below 10.5V.
* **Connection:** Dual VESC Port 3 (`COMM`): Pin 6 (`RX`) receives drive from CH6; Pin 5 (`TX`) transmits telemetry up CH3; Pin 3 (`GND`) links to negative bus. Pins 1 (5V), 2 (3.3V), and 4 (ADC) remain disconnected.
* **Power:** A single 12AWG power pair feeds the Dual VESC through fuse F1 (15A).

### Sound System
* **Location:** Mounted in the body behind the front acoustic vents.
* **Components:** DFPlayer Mini MP3 player, ground-loop isolator, HF82 (TPA3110) 12V Class-D amplifier, and a 2.5" speaker.
* **Control:** The dome ESP32 sends 9600-baud serial commands down slip ring channel 4 to trigger sound tracks for chatty sounds and synchronized routines.

---

## 3. Slip Ring Allocation

A 6-channel through-bore slip ring passes all power and signals between the body and dome:

| Channel | Function | Direction | Wire / Signal |
| :---: | :--- | :---: | :--- |
| **CH 1** | Fused 12V Power | Body &rarr; Dome | 12V battery power from Fuse F4 (7.5A) to dome buck converter |
| **CH 2** | Common Ground | Body &harr; Dome | Shared power return and signal ground reference |
| **CH 3** | VESC Telemetry UART | Body &rarr; Dome | 115,200-baud serial telemetry from Dual VESC Port 3 TX to ESP32 GPIO5 (AUX 3) |
| **CH 4** | Sound Commands | Dome &rarr; Body | 9,600-baud serial from ESP32 GPIO17 to DFPlayer RX (via 1k resistor) |
| **CH 5** | Dome Servo Signal | Dome &rarr; Body | 3.3V PWM from ESP32 GPIO4 to continuous-rotation dome servo |
| **CH 6** | VESC Drive UART | Dome &rarr; Body | 115,200-baud serial from ESP32 GPIO18 (AUX 4) to Dual VESC Port 3 RX |

---

## 4. Documentation Directory

Use these documents for specific parts of the build:

| Guide | Description |
| :--- | :--- |
| [Power Harness Guide](POWER_HARNESS_GUIDE.md) | Wire sizes, fuse ratings, crimping methods, and step-by-step electrical testing |
| [Interactive Wiring Diagram](wiring_visualizer.html) | Searchable terminal-by-terminal wiring schematic (open in any web browser) |
| [System Wiring Overview](DOME_WIRING_DIAGRAM.md) | Detailed block diagrams, slip-ring allocations, and level-shifter pinouts |
| [AstroPixels Plus Firmware Guide](ASTROPIXELS_PLUS_UNIFIED/README.md) | Primary firmware setup, flashing instructions, Wi-Fi controls, and sound files |
| [VESC Drive Setup](VESC_DRIVE_INTEGRATION.md) | Motor detection, current limits, radio mixing, and failsafe setup for the foot motors |
| [Bill of Materials](Master_R2D2_BOM.xls) | Full parts list, specifications, and purchase notes (Excel-compatible) |
