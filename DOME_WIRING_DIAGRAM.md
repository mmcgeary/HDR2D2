# R2-D2 System Wiring & Architecture Guide

This guide details the electrical connections between the body and dome, the slip ring pinout, and the logic-level wiring for the ESP32 dome controller.

For wire gauges, fuse ratings, and shopping lists, see [POWER_HARNESS_GUIDE.md](POWER_HARNESS_GUIDE.md). For an interactive schematic of every terminal, open [wiring_visualizer.html](wiring_visualizer.html).

---

## 1. Six Core Design Principles

1. **Unified ESP32 Foot Drive & Tank Mixing:** The FlySky receiver sits in the dome and communicates directly with the ESP32 over 115,200-baud digital iBUS. The ESP32 calculates differential tank mixing, speed rates, and safety timeouts in firmware, transmitting VESC packets down Slip Ring CH6 to the Flipsky Dual FSESC 4.20 over internal CAN bus (`ON: dual`). No body microcontroller board is required.
2. **Firmware & Transmitter Speed Modes:** The ESP32 reads switch `SwB` (Slow 35%, Medium 70%, Fast 100%) and scales motor duty cycle in code, providing responsive cruise speeds without changing motor controller configs.
3. **Isolated Body Sound System:** The DFPlayer Mini, HF82 (TPA3110) 12V Class-D amplifier, and speaker are mounted in the body behind the front vents. A dedicated ground-loop isolator in the line-level audio path prevents digital noise from bleeding into the speaker.
4. **Unified Dome Brain:** The dome ESP32 runs [AstroPixels Plus Unified](ASTROPIXELS_PLUS_UNIFIED/README.md), managing logic displays, holoprojector LEDs, 6 holoprojector servos, sound triggering, dome rotation, and foot propulsion.
5. **Center-Post Rotation & Homing:** A 35kg continuous-rotation servo in the body drives the dome pivot post directly. A KY-003 Hall effect sensor in the dome detects a stationary magnet on the post to find the forward 0° home position.
6. **Two-Tier Power Safety:** Tier 1 is a 25A main fuse within 150mm of the battery. Tier 2 is an easily accessible high-current master cutoff switch. 250ms iBUS watchdog detection and VESC input timeouts ensure drive motors stop instantly if radio contact is lost.

---

## 2. System Block Diagram

```mermaid
flowchart TB
    subgraph BODY [BODY - Power, Drive & Audio]
        BAT[12V 20Ah LiFePO4 Battery]
        FUSE_MAIN[25A Main Fuse]
        CUTOFF[Master Cutoff Switch]
        FBOX[12V Fuse Box with Negative Bus]
        BUCK_B[Body 5V / 10A Buck Converter]
        
        VESC[Dual VESC 4.20 Controller - Switch ON: dual]
        MOTOR_L[Left Hub Motor]
        MOTOR_R[Right Hub Motor]
        
        DFPLAYER[DFPlayer Mini MP3 Player]
        ISO[BESIGN Ground-Loop Isolator]
        AMP[HF82 Class-D Amplifier]
        SPK[2.5 inch Speaker]
        
        DOME_SERVO[35kg Continuous Rotation Servo]
        POST[Dome Center Pivot Post]
        MAG[Stationary Magnet at Post Base]

        %% Power routing
        BAT --> FUSE_MAIN --> CUTOFF --> FBOX
        BAT -->|Negative Feed| FBOX
        FBOX -->|F1 15A| VESC
        FBOX -->|F3 7.5A| BUCK_B
        FBOX -->|F5 5A| AMP

        %% Body 5V distribution
        BUCK_B --> DFPLAYER
        BUCK_B --> DOME_SERVO

        %% Motor outputs
        VESC ==>|Master Left| MOTOR_L
        VESC ==>|Slave Right over CAN| MOTOR_R

        %% Dome drive
        DOME_SERVO --> POST

        %% Audio
        DFPLAYER --> ISO --> AMP --> SPK
    end

    subgraph RING [6-CHANNEL THROUGH-BORE SLIP RING]
        CH1[CH1: Fused 12V Power]
        CH2[CH2: Common Ground]
        CH3[CH3: Spare - Insulated]
        CH4[CH4: Audio Serial 9600 Baud]
        CH5[CH5: Dome Servo PWM 3.3V]
        CH6[CH6: VESC Drive UART 115200 Baud]
    end

    subgraph DOME [DOME - Brain, Lights, Servos & Radio]
        BUCK_D[Dome 5V / 10A Buck Converter]
        RX[FlySky FS-iA6B Receiver]
        TELEM[FS-CVT01 Voltage Sensor]
        SHIFTER[3.3V / 5V Logic Level Shifter]
        ESP32[AstroPixels ESP32 Brain]
        PCA[PCA9685 16-Channel Servo Driver]
        HALL[KY-003 Hall Effect Sensor]
        
        RLD[Rear Logic Display]
        FLD[Front Logic Displays]
        FPSI[Front PSI]
        RPSI[Rear PSI]
        HPS[3x Holoprojector LEDs]
        
        SERVOS[6x Holoprojector Servos MG90S]

        BUCK_D --> ESP32
        BUCK_D --> RX
        BUCK_D --> PCA
        BUCK_D --> RLD & FLD & FPSI & RPSI & HPS

        RX -->|iBUS 5V| SHIFTER -->|LV1 3.3V to GPIO16| ESP32
        RX <-->|SENS Port| TELEM
        ESP32 --> FLD & RLD & FPSI & RPSI & HPS
        ESP32 -->|I2C SDA/SCL| PCA --> SERVOS
        HALL -->|5V Pulse| SHIFTER -->|3.3V to GPIO19| ESP32
    end

    %% Slip ring connections
    FBOX -->|F4 7.5A| CH1 --> BUCK_D
    CH1 -.->|12V Sense Tap| TELEM
    FBOX -->|Negative Bus| CH2 --> BUCK_D
    VESC -->|Port 3 Pin 5 TX 3.3V| CH3 -->|GPIO5 (AUX 3)| ESP32
    ESP32 -->|GPIO17 direct 3.3V| CH4 -->|1k Resistor| DFPLAYER
    ESP32 -->|GPIO4 direct 3.3V| CH5 --> DOME_SERVO
    ESP32 -->|GPIO18 direct 3.3V (AUX 4)| CH6 -->|Port 3 COMM RX| VESC
    HALL -.->|Sweeps past| MAG
```

---

## 3. Slip Ring 6-Channel Allocation

A 30mm through-bore slip ring slides over the central pivot post to pass power and signals between body and dome:

| Channel | Label | Direction | Origin | Destination | Description |
| :---: | :--- | :---: | :--- | :--- | :--- |
| **CH 1** | `+12V_DOME` | Body &rarr; Dome | Fuse F4 (7.5A) | Dome 5V Buck IN(+) | 12V power feed to the dome. Fused before entering the slip ring. |
| **CH 2** | `GND_COMMON` | Body &harr; Dome | Fuse Box Negative Bus | Dome 5V Buck IN(&minus;) | Common power return and signal ground reference. |
| **CH 3** | `VESC_TELEM` | Body &rarr; Dome | Dual VESC Port 3 Pin 5 (`TX`) | ESP32 GPIO5 (AUX 3) | 115,200-baud VESC telemetry packet stream (`COMM_GET_VALUES`). Direct 3.3V logic match; bypasses level shifter. |
| **CH 4** | `SOUND_CMD` | Dome &rarr; Body | ESP32 GPIO17 (3.3V) | DFPlayer RX (via 1k&Omega; resistor) | 9,600-baud serial commands triggering sounds. Bypasses level shifter. |
| **CH 5** | `DOME_PWM` | Dome &rarr; Body | ESP32 GPIO4 (3.3V) | Dome Continuous Servo Signal | 50Hz PWM controlling rotation speed and direction. Bypasses level shifter. |
| **CH 6** | `VESC_UART` | Dome &rarr; Body | ESP32 GPIO18 (AUX 4) | Dual VESC Port 3 Pin 6 (`RX`) | 115,200-baud VESC packet stream. Direct 3.3V logic match; bypasses level shifter. |

### Notes on Slip Ring Power & Ground
* The slip ring carries **12V at ~4.5A peak**, not 5V at 10A. Stepping down to 5V inside the dome keeps voltage drop through the slip ring minimal.
* **Never connect the body 5V and dome 5V outputs together.** Both buck converters share the common ground reference via CH2.
* Battery voltage is monitored directly by the ESP32 via VESC telemetry on CH3, as well as on your transmitter using the **FS-CVT01 telemetry sensor** connected to the receiver in the dome, sensing incoming 12V on CH1.

---

## 4. Logic-Level Shifting & Wiring

The ESP32 runs at 3.3V logic and is **not 5V tolerant**. A bidirectional 4-channel logic-level shifter in the dome steps down 5V incoming signals:

```
  5V Side (High Voltage)           3.3V Side (Low Voltage)
  ──────────────────────           ───────────────────────
  Dome 5V Rail ─────────> [ HV ]   [ LV ] <───────── ESP32 3.3V Pin
  Common Ground ────────> [GND ]   [GND ] <───────── Common Ground
  Receiver iBUS (5V) ───> [HV1 ]   [LV1 ] ─────────> ESP32 GPIO16 (RX2)
  KY-003 Hall Sensor ───> [HV2 ]   [LV2 ] ─────────> ESP32 GPIO19 (Homing)
  (Unused) ─────────────> [HV3 ]   [LV3 ]
  (Unused) ─────────────> [HV4 ]   [LV4 ]
```

| Signal | 5V Side (HV) | 3.3V Side (LV) | ESP32 Pin | Direction |
| :--- | :--- | :--- | :--- | :--- |
| **RC iBUS Stream** | Receiver iBUS Pin &rarr; HV1 | LV1 | GPIO16 (RX2) | Input to ESP32 |
| **Dome Hall Sensor** | KY-003 Signal &rarr; HV2 | LV2 | GPIO19 (AUX5) | Input to ESP32 |

### Signals Bypassing the Shifter
* **VESC Telemetry UART (GPIO5 / AUX 3):** Connects directly from Dual VESC Port 3 Pin 5 (`TX`) through slip ring CH3 to ESP32 GPIO5. The VESC STM32 MCU transmits 3.3V logic natively.
* **VESC Drive UART (GPIO18 / AUX 4):** Connects directly from GPIO18 through slip ring CH6 to Dual VESC Port 3 Pin 6 (`RX`). The VESC STM32 MCU inputs 3.3V logic natively.
* **Dome Servo PWM (GPIO4):** Connects directly from GPIO4 through slip ring CH5 to the servo signal wire. The TD-8135MG-360 servo accepts 3.3V logic signals reliably.
* **Audio Commands (GPIO17):** Connects directly from GPIO17 through slip ring CH4, through a 1k&Omega; resistor, into the DFPlayer RX pin. 3.3V UART drives the DFPlayer input cleanly.

---

## 5. PCA9685 Servo Driver Wiring

The PCA9685 16-channel PWM driver controls the 6 holoprojector micro servos (MG90S):

```
  ESP32 Pin                    PCA9685 Terminal
  ─────────────────            ────────────────
  ESP32 I2C Ground  ─────────> Logic GND
  ESP32 3.3V Pin    ─────────> VCC (Logic Power)
  GPIO22 (SCL)      ─────────> SCL
  GPIO21 (SDA)      ─────────> SDA
  Dome 5V Rail      ─────────> V+ (Green Screw Terminal - Servo Power)
  Dome Ground       ─────────> GND (Green Screw Terminal - Servo Ground)
```

### Channel Allocation
* **Channels 0 & 1:** Front Holoprojector (CH0 = Pan, CH1 = Tilt)
* **Channels 2 & 3:** Rear Holoprojector (CH2 = Pan, CH3 = Tilt)
* **Channels 4 & 5:** Top Holoprojector (CH4 = Pan, CH5 = Tilt)

Keep default I2C address jumpers at `0x40`. Verify servo connector orientation (Ground = brown/black, Power = red, Signal = yellow/orange) before plugging in.

---

## 6. AstroPixels Motherboard Display Connections

All LED displays plug into the standard factory AstroPixels headers on the motherboard:

| Display Header | ESP32 GPIO | Function |
| :--- | :---: | :--- |
| **FLD** | GPIO 15 | Front Logic Displays (FLD1 and FLD2 daisy-chained) |
| **RLD** | GPIO 33 | Rear Logic Display |
| **FPSI** | GPIO 32 | Front Process State Indicator |
| **RPSI** | GPIO 23 | Rear Process State Indicator |
| **FHP** | GPIO 25 | Front Holoprojector LED Core |
| **RHP** | GPIO 26 | Rear Holoprojector LED Core |
| **THP** | GPIO 27 | Top Holoprojector LED Core |

Motherboard power comes from the dome 5V distribution block into the motherboard's main 5V screw terminal.
