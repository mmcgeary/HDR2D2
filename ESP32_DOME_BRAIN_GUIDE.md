# AstroPixels Unified ESP32 Dome Brain Guide
## (Persistent Mood Engine, Sound Pools, FastLED & PCA9685)

This guide documents the architecture, sound directory structure, and transmitter mapping for the **AstroPixels 30-Pin ESP32** running as the **Sole Master Brain** for the R2-D2 droid.

---

## 1. System Architecture & Signal Flow

The ESP32 in the dome handles all lights, 6 holo servos, sound triggering, and dome rotation, while the FlySky receiver in the body directly commands the Dual VESC 4.20 for foot drive:

```mermaid
flowchart TB
    subgraph BODY [BODY ENCLOSURE]
        TX["FlySky FS-i6X Transmitter<br/>(D/R Speed Limiting & Mood Dial)"]
        RX["FlySky FS-iA6B Receiver"]
        VESC["Dual VESC 4.20<br/>(Master/Slave CAN-Bus)"]
        MOTORS["Razor Hub Motors (Left + Right)"]
        DFPLAYER["DFPlayer Mini + PAM8610 Amp + Speaker"]
        DOME_SERVO["35kg 360° Continuous Servo"]

        TX -.->|"2.4GHz Direct Link"| RX
        RX -->|"CH1 & CH2 PPM"| VESC --> MOTORS
    end

    subgraph SLIPRING [6-CHANNEL THROUGH-BORE SLIP RING]
        CH3["Ch 3: i-Bus Serial Stream (All 10 Channels @ 115200)"]
        CH4["Ch 4: Sound Serial Commands (Down to DFPlayer @ 9600)"]
        CH5["Ch 5: Dome Servo PWM (Down to 35kg Servo)"]
    end

    subgraph DOME [DOME ENCLOSURE]
        ESP32["AstroPixels ESP32 Motherboard<br/>(Sole Droid Brain)"]
        LIGHTS["AstroPixels WS2812 Displays<br/>(FLD, RLD, PSIs, 3x HPs)"]
        PCA["PCA9685 16-Ch I2C Driver"]
        SERVOS["6x HoloProjector Micro Servos"]
        HALL["KY-003 Hall Homing Sensor"]

        RX -->|"i-Bus Port"| CH3 -->|"Serial2 RX (GPIO 16)"| ESP32
        ESP32 -->|"Serial1 TX (GPIO 17)"| CH4 -->|"RX (via 1k res)"| DFPLAYER
        ESP32 -->|"LEDC PWM (GPIO 4)"| CH5 -->|"Signal Wire"| DOME_SERVO

        ESP32 -->|"FastLED Data Lines"| LIGHTS
        ESP32 ---|"I2C SDA (21) / SCL (22)"| PCA --> SERVOS
        HALL -->|"Home Interrupt (GPIO 5)"| ESP32
    end
```

---

## 2. MicroSD Card Directory & Sound Pool Matrix

Format your MicroSD card as **FAT32**. Place audio files in the root directory (or an `MP3` folder) using the following **3-digit numerical prefixes**:

### A. Categorized Ambient Mood Sound Pools
When R2 is idling in a persistent mood, background chatter automatically pulls randomly from that mood's assigned sound range:

| Track Range | Mood Category | Associated Mood | Sound Characteristics |
| :---: | :--- | :--- | :--- |
| `001 - 020` | **Happy & Chatty** | `MOOD_HAPPY` & `MOOD_NORMAL` | Upbeat whistling, harmonic chirps, friendly beeps |
| `021 - 040` | **Sassy & Annoyed** | `MOOD_SASSY` | Grumbling razzes, sarcastic buzzes, scoffing beeps |
| `041 - 060` | **Sad & Mournful** | `MOOD_SAD` | Low downward whines, melancholic chirps |
| `061 - 080` | **Alert & Alarm** | `MOOD_ALERT` | Fast warning pulses, emergency chirps, klaxons |

---

### B. One-Shot Interactive Macro Tracks
Triggered when you flip **`SwC` DOWN** on the transmitter:

| Track # | Macro Name | Dial Position (`VrA`) | Synchronized Droid Behavior |
| :---: | :--- | :---: | :--- |
| `102` | **Scream / Panic** | **Pos 4** | Red flashing strobe + erratic holo spasms ($4.5\text{s}$) |
| `106` | **Cantina Band** | **Pos 5** | Rhythmic marching step lights & dance steps ($18\text{s}$) |
| `109` | **Princess Leia** | **Pos 6** | **Auto-aligns head forward to audience ($0^\circ$)** + Pale green logics + Front HP aims down $35^\circ$ with blue flicker ($14\text{s}$) |
| `110` | **Star Wars Disco** | **Pos 7** | Full rainbow wave across all displays ($20\text{s}$) |
| `107` | **Short Circuit / Faint** | **Pos 8** | Dim spark flicker, total blackout, servos go limp ($5\text{s}$) |
| `011` | **Auto-Center Reset** | **Pos 1** | **Spins dome to lock onto magnet ($0^\circ$)**, centers all servos, and resets lights to normal |
| `255` | **Startup Chime** | *Boot* | Played on initial power-on |

---

## 3. FlySky FS-i6X Transmitter Channel Assignment

| Channel | Physical Control | Functional Role on Droid |
| :---: | :--- | :--- |
| **CH 1** | **Right Stick Horizontal** | **Steering (Left / Right)** $\rightarrow$ Direct PPM to Master VESC |
| **CH 2** | **Right Stick Vertical** | **Throttle (Forward / Reverse)** $\rightarrow$ Direct PPM to Slave VESC |
| **CH 3** | **Left Stick Vertical** | **Manual Front Holo Tilt (Up / Down)**: Overrides servo; holds position for 3s before resuming mood twitches |
| **CH 4** | **Left Stick Horizontal** | **Manual Dome Rotation Override**: Proportional continuous rotation; Zero-Creep sleep when centered |
| **CH 5** | **Switch `SwB` (3-Position)** | **Transmitter Dual Rates (Speed)**: Pos 1 = Slow (35%), Pos 2 = Med (70%), Pos 3 = Fast (100%) |
| **CH 6** | **Switch `SwA` (2-Position)** | **Drive Safety Lockout** |
| **CH 7** | **Rotary Knob `VrA`** | **Persistent Mood & Macro Selector (1 to 13)** |
| **CH 8** | **Switch `SwC` (3-Position)** | **Macro Fire Trigger**: Flip DOWN to execute selected routine |
| **CH 9** | **Switch `SwD`** | **HoloProjector Random Motion Toggle** |

---

## 4. Flash & Memory Optimization Architecture

* **Zero Dynamic Heap Allocation**: No `malloc()`, `free()`, or `String` class usage in loop cycles, preventing heap fragmentation and ensuring $200\text{Hz}$ deterministic execution.
* **PROGMEM Sound Tables**: All sound ranges and macro configurations are stored in Flash memory.
* **Hardware Offloading**:
  * Dome Servo PWM: Generated by ESP32 **LEDC hardware timer** ($0\%$ CPU load).
  * i-Bus & Audio: Handled by **Hardware UART1/UART2 FIFO** buffers.
  * 6 Holo Servos: Offloaded to **PCA9685 I2C coprocessor**.
