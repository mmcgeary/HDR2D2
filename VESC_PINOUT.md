# VESC 4.20 Pinout

The Flipsky FSESC4.20 is Flipsky's build of Benjamin Vedder's VESC 4.12 design: STM32F405 processor, DRV8302 gate driver. VESC Tool identifies it as hardware **410**.

Pin names and order are as printed on the Flipsky wiring sheet ([docs/images/IMG_2424.HEIC](docs/images/IMG_2424.HEIC)). "−" is ground. All logic is 3.3 V.

The Dual FSESC4.20 is two complete 4.20 controllers on one board, with these differences:

- **Battery:** one battery input pair feeds both halves.
- **Receiver input:** only one half has the soldered receiver lead (GND/5V/SIN). The other half has unpopulated pads in the mirrored position.
- **CAN switch:** joins the two halves' CAN buses ([CAN switch](#can-switch-dual-only)).

Every other connector below exists once per half.

## Power and motor

| Connection | Function |
|---|---|
| Battery + / − | Main supply, 8–60 V (3–13S). |
| Motor phases (3 wires) | Outputs to the motor windings. Any order works; the FOC wizard learns it. Wheel direction is set in VESC Tool, never by swapping phase wires. |

## 1. CAN (4-pin)

| Pin | Function |
|---|---|
| − | Ground |
| CAL | CAN Low |
| CAH | CAN High |
| 5V | 5 V output for CAN accessories |

CAN is a shared network for VESCs and add-ons (VESC Express, displays, battery monitors). Default speed is 500 kbit/s.

- **IDs:** every device on one network has a different VESC ID (App Settings → General).
- **Uses:** one VESC commands the others ("Multiple VESCs over CAN"), and VESC Tool reaches every VESC on the network through one USB cable.
- **Wiring between boards:** CAH to CAH, CAL to CAL, − to −. Never connect two controllers' 5V pins together.

## 2. SENSE (6-pin, motor sensors)

| Pin | Function |
|---|---|
| − | Sensor ground |
| H3, H2, H1 | Hall sensor inputs. The board has the pull-up resistors; Hall sensors wire straight in. |
| TMP | Motor temperature input (10 kΩ thermistor to ground) |
| 5V | Sensor power |

- **Hall sensors:** give rotor position at standstill. The FOC wizard learns the Hall table.
- **Encoders:** H1–H3 also take an ABI encoder or an AS5047 magnetic encoder (Sensor Port Mode in VESC Tool).
- **TMP:** with a thermistor, the VESC reduces current as the motor heats. With TMP unconnected, the motor temperature reads meaninglessly low and never limits. The Razor motors have no thermistor; TMP stays unconnected.

## 3. COMM (7-pin)

| Pin | Function | Limit |
|---|---|---|
| 5V | 5 V output | Shares the half's 5 V regulator |
| 3.3V | 3.3 V output | Small loads only |
| − | Ground | |
| ADC | Analog input 1 | 0–3.3 V |
| TX | Serial out (VESC → device), or I2C SCL | 3.3 V logic |
| RX | Serial in (device → VESC), or I2C SDA | 3.3 V logic |
| ADC2 | Analog input 2 | 0–3.3 V |

- **Serial (UART):** VESC command protocol, 115200 baud by default (App Settings → UART). Wires cross: VESC TX to device RX, VESC RX to device TX.
- **I2C:** TX and RX double as an I2C bus (for example a wired Wii Nunchuk). The pins carry serial or I2C, not both.
- **Analog inputs:** the ADC app reads a throttle on ADC and a brake or second throttle on ADC2. Power a throttle pot from the 3.3V pin so its signal stays at or below 3.3 V. Never put 5 V on either ADC pin.

## 4. SWD (6-pin, programming header)

| Pin | Function |
|---|---|
| NC | Not connected |
| RST | Processor reset |
| DIO | Programming data (SWDIO) |
| − | Ground |
| CLK | Programming clock (SWCLK) |
| 3.3V | Target voltage sense for the programmer |

This header takes an ST-Link programmer. It installs a bootloader or recovers a board that no longer connects over USB. Normal firmware updates go over USB. Nothing is powered from this 3.3V pin.

## USB (micro-USB)

- **Function:** VESC Tool setup, the FOC wizard, firmware updates, live data and the terminal.
- **Power:** the battery must be connected. USB does not power the controller.
- **Dual:** each half is a separate USB device. With the CAN switch ON, one port reaches both halves.

## Receiver input (GND / 5V / SIN)

| Pin | Function |
|---|---|
| GND | Ground |
| 5V | 5 V output to power a receiver |
| SIN | Servo pulse input: one receiver channel in PWM output mode, 1000–2000 µs |

- **PPM app:** reads SIN (App Settings → PPM). Despite its name, it reads the receiver's **PWM** output: one servo pulse per channel. The receiver's own PPM output mode carries every channel on one wire and does not work here.
- **Signal level:** SIN accepts 3.3 V and 5 V receiver signals.
- **Buttons:** in the ADC app's button modes, SIN reads a push-button (cruise control or reverse) switched to ground.
- **5V pin:** when the receiver has its own supply, remove this pin from the lead and insulate it.

## The 5 V supply on each half

All of a half's 5V pins (CAN, SENSE, COMM, receiver) come from one small regulator. The 3.3V pins come from a smaller regulator fed by it. Load it with Hall sensors and at most a receiver. In this build the receiver and servos run from the body 5 V supplies ([Power](POWER.md)).

## CAN switch (dual only)

- **OFF (single):** the halves are independent controllers. Each half's CAN connector is its own network. This build uses OFF.
- **ON (dual):** the board joins the two halves' CAN lines into one network. The half with the receiver lead reads it and sends the same command to the other half. The halves need different VESC IDs.
  - Both wheels get the same command, so ON mode does not steer a droid.

## Connector use in this build

| Connector | Use |
|---|---|
| Battery | Shared pair through F1 ([Power](POWER.md)) |
| Phases | Motor |
| SENSE | Hall cable, TMP unconnected |
| COMM | Unused |
| Receiver input | SIN + GND, 5V pin removed. Half B uses the soldered pads ([Drive Setup](DRIVE_SETUP.md) §2). |
| CAN | Unused |
| SWD | Unused |
| CAN switch | OFF |
