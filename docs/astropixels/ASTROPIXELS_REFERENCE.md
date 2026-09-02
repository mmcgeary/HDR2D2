# AstroPixels Master Local Reference Guide

> **Source**: Crawled from official GitBook documentation: https://r2djp.gitbook.io/astropixels
> **Local Docs Directory**: `docs/astropixels/`

---

## 1. Hardware Architecture Overview

AstroPixels is an addressable LED system for R2-D2 droids powered by standard WS2812 RGB LEDs controlled by an **ESP32 (30-pin development board)** running the ReelTwo firmware library.

### Main Breakout Board Pinout
The AstroPixels motherboard sits behind the Rear Logic Display (RLD) and breaks out 3-pin headers (Signal / 5V / GND) for all display components:

| Component Header | ESP32 GPIO Pin | Description |
| :--- | :--- | :--- |
| **RLD** | GPIO 33 | Rear Logic Display |
| **FLD** | GPIO 15 | Front Logic Displays (daisy-chained FLD1 -> FLD2) |
| **FPSI** | GPIO 32 | Front Process State Indicator |
| **RPSI** | GPIO 23 | Rear Process State Indicator |
| **THP** | GPIO 27 | Top Holoprojector LED ring/core |
| **RHP** | GPIO 26 | Rear Holoprojector LED ring/core |
| **FHP** | GPIO 25 | Front Holoprojector LED ring/core |
| **AUX 1** | GPIO 2 | Auxiliary expansion GPIO |
| **AUX 2** | GPIO 4 | Auxiliary expansion GPIO |
| **AUX 3** | GPIO 5 | Auxiliary expansion GPIO |
| **AUX 4** | GPIO 18 | Auxiliary expansion GPIO (I2C / SPI / Servo) |
| **AUX 5** | GPIO 19 | Auxiliary expansion GPIO (I2C / SPI / Servo) |
| **Serial2 RX** | GPIO 16 | Hardware UART2 RX (input from MarcDuino / Arduino Mega) |
| **Serial2 TX** | GPIO 17 | Hardware UART2 TX (output to MarcDuino / Arduino Mega) |

---

## 2. Power Specifications

* **Operating Voltage**: **5.0V DC regulated** (Do NOT supply >5.5V or LEDs/ESP32 will be destroyed).
* **Current Draw**:
  * Normal idle/run: **~700 mA**
  * Peak brightness / full white / heavy effects: **up to 1.5A – 2.0A**
* **Dome Power Best Practice**:
  * Feed raw battery voltage (e.g., **12V DC**) through the slip ring to minimize $I^2R$ resistive voltage drop and wire heating.
  * Step down to **5.0V DC** directly inside the dome using a high-efficiency DC-DC Buck Converter (e.g., Pololu D30V30F5 or 5V 5A step-down module).
  * **Isolate Servos**: Use a separate 5V/6V buck converter for servos to avoid brownouts on the ESP32.

---

## 3. Communication & Command Protocol

AstroPixels listens on **Serial2 (9600 baud, 8N1)** and **I2C (Address: 0x0A)**.

### A. Logic Engine (Logics & PSIs) Commands
Format: `LE<designation><effect><colour><speed><time>\r`
*(Omit leading zeros)*

* **Designation**:
  * `0`: All Logics & PSIs
  * `1`: Front Logics
  * `3`: Rear Logics
  * `4`: Front PSI
  * `5`: Rear PSI
* **Effect Codes**:
  * `00`: Normal operation
  * `01`: Alarm (flips between color and red)
  * `02`: Failure (color cycle + fade out)
  * `03`: Leia (pale green / cyan)
  * `04`: Imperial March sequence
  * `05`: Single Color
  * `06`: Flashing Color
  * `07`: Flip Flop Color
  * `08`: Flip Flop Alt
  * `09`: Color Swap
  * `10`: Rainbow
  * `14`: Lights Out (Stealth)
  * `20`: Horizontal Scanline
  * `21`: Vertical Scanline
  * `22`: Fire effect
  * `23`: PSI Swipe
  * `24`: Pulse
  * `99`: Random Effect
* **Colour Codes**:
  * `0`: Default, `1`: Red, `2`: Orange, `3`: Yellow, `4`: Green, `5`: Cyan, `6`: Blue, `7`: Purple, `8`: Magenta, `9`: Pink
* **Speed**: `0` (fastest) to `9` (slowest)
* **Time**: `00` for continuous / default

### B. Holoprojector (HP) Commands
Format: `HP<designation><type><sequence><colour><speed><random><position>\r`

* **Designation**:
  * `F`: Front HP, `R`: Rear HP, `T`: Top HP, `A`: All 3 HPs
* **Type**: `0` = LED functions, `1` = Servo movements (if hardware configured)
* **Sequence**:
  * `01`: Leia sequence (blue hologram flicker)
  * `02`: Single colour flicker
  * `03`: Dim pulse
  * `04`: Cycle
  * `05`: Solid colour
  * `06`: Rainbow
  * `07`: Short circuit
  * `96`–`99`: Twitch control / reset functions
* **Colour**: `1`: Red, `2`: Yellow, `3`: Green, `4`: Cyan, `5`: Blue, `6`: Magenta, `7`: Orange, `8`: Purple, `9`: White, `0`: Random

---

## 4. MarcDuino Compatibility
If running MarcDuino standard firmware (`standard-md`):
* Connect MarcDuino Dome Slave `S` (Signal) -> AstroPixels `RX2` (GPIO 16)
* Connect MarcDuino `G` (Ground) -> AstroPixels `GND`
* Jawalite serial commands with `%` prefix are parsed automatically.
