# Home Depot R2-D2 Conversion

An in-progress build to turn the 2024 Home Depot R2-D2 animatronic into a fully remote-controlled droid with upgraded lights, sound, dome movement, and powered feet.

The build keeps the stock exterior shell while adding:
* **AstroPixels lighting:** Logic displays (FLD/RLD), process state indicators (PSIs), and 3 illuminated holoprojectors.
* **Split controllers:** Teensy 4.1/Treedix in the body for radio, feet, dome rotation and audio; AstroPixels ESP32 in the dome for lighting, six holo servos, Hall input, Wi-Fi and choreography.
* **Onboard sound:** DFPlayer Mini, 12V Class-D amplifier, and speaker firing through the front acoustic vents for chatter and movie sounds.
* **Dual-motor foot drive:** Razor Tekno Pop hub motors in the feet powered by a Flipsky Dual FSESC 4.20 motor controller.
* **FlySky radio control & Wi-Fi:** 10-channel RC control, plus an onboard Wi-Fi dashboard for triggering routines, adjusting volume, and flashing firmware updates.
* **Clean power harness:** 12V LiFePO4 battery, master cutoff switch, fused branch circuits, and dedicated 5V power converters for the body and dome.

---

## Quick Reference

**Approved split controller architecture:** Teensy 4.1 in the body handles radio decoding, dual VESC UART control, continuous dome servo, and DFPlayer audio. AstroPixels Plus ESP32 in the dome manages lighting (ReelTwo), holo servos, Hall reference sensors, Wi-Fi dashboard, and wireless `/commissioning` Web UI.

| Guide | Description |
| :--- | :--- |
| [System Architecture](SYSTEM_ARCHITECTURE.md) | High-level system overview, subsystem descriptions, and block diagrams |
| [Body Controller Wiring](BODY_CONTROLLER_WIRING.md) | Authoritative Teensy/Treedix terminal map, receiver, selected shifters and UART harnesses |
| [Commissioning](BODY_CONTROLLER_COMMISSIONING.md) | Wireless `/commissioning` UI workflow, USB isolation, and subsystem/failure checks |
| [Power Harness Guide](POWER_HARNESS_GUIDE.md) | Wire gauges, fuse sizes, crimping standards, and step-by-step electrical testing |
| [Interactive Wiring Diagram](wiring_visualizer.html) | Searchable terminal-to-terminal wiring schematic (open locally in your browser) |
| [System Wiring Overview](DOME_WIRING_DIAGRAM.md) | Logical circuit diagrams, slip-ring assignments, and logic-level shifter pinouts |
| [AstroPixels Plus Firmware Guide](ASTROPIXELS_PLUS_UNIFIED/README.md) | Dome firmware setup, target pin map, sound files, Web UI, and safety controls |
| [Teensy Body Controller Guide](TEENSY_BODY_CONTROLLER/README.md) | Teensy firmware architecture, scheduler, CLI commands, and test suites |
| [VESC Drive Setup](VESC_DRIVE_INTEGRATION.md) | Motor detection, current limits, radio mixing, and safety failsafes for the feet |
| [Bill of Materials](Master_R2D2_BOM.xls) | Complete parts list, hardware links, and purchase notes (Excel-compatible) |

Use the body guide for pins and the power guide for fuses. `ESP32_DOME_BRAIN_GUIDE.md` and standalone sketches are historical references, not alternative instructions for the selected hardware.

---

## Repository Contents

### Primary Firmware
* [`TEENSY_BODY_CONTROLLER/`](TEENSY_BODY_CONTROLLER/): PlatformIO firmware for the Teensy 4.1 body controller. Manages radio input, dual-motor VESC control, dome servo positioning, DFPlayer audio, and commissioning calibration state machine.
* [`ASTROPIXELS_PLUS_UNIFIED/`](ASTROPIXELS_PLUS_UNIFIED/): PlatformIO ESP32 firmware built on AstroPixels Plus/ReelTwo. Manages dome lighting, holo servos, Hall reference inputs, Wi-Fi dashboard, `/diagnostics` telemetry, and wireless `/commissioning` Web UI.
* [`shared/R2BodyLink/`](shared/R2BodyLink/): Shared wire protocol codec and packet framing for the bi-directional SLIP link between body and dome.
* [Firmware implementation plan](docs/superpowers/plans/2026-10-09-teensy-firmware.md): Comprehensive 12-task architecture and verification plan.

### Wiring & Power
* [`POWER_HARNESS_GUIDE.md`](POWER_HARNESS_GUIDE.md) and [`DOME_WIRING_DIAGRAM.md`](DOME_WIRING_DIAGRAM.md) detail the entire electrical layout, wire sizes, fusing, and testing steps.
* [`wiring_visualizer.html`](wiring_visualizer.html) is an interactive offline wiring inspector with a printable wire schedule.

### Mechanical Files & Models
* [`index.html`](index.html), `app.js`, and `styles.css`: Mechanical visualizer for exploring foot-drive clearance and design concepts.
* [`Razor_Hub_Motor_Slotted_Mount.stl`](Razor_Hub_Motor_Slotted_Mount.stl) & [`generate_slotted_mount_stl.py`](generate_slotted_mount_stl.py): 3D-printable slotted motor mount and generator script.
* [`mechanical/tekno-pop-motor-mounts-wip/`](mechanical/tekno-pop-motor-mounts-wip/): Work-in-progress 3D models for mounting Razor Tekno Pop scooter hub motors into the droid feet:
  - [`r2d2_clean_motor_mount.stl`](mechanical/tekno-pop-motor-mounts-wip/r2d2_clean_motor_mount.stl)
  - [`r2d2_inboard_axle_keeper_plate.stl`](mechanical/tekno-pop-motor-mounts-wip/r2d2_inboard_axle_keeper_plate.stl)
  - [`case_bottom_locators_only.stl`](mechanical/tekno-pop-motor-mounts-wip/case_bottom_locators_only.stl)
  Check dimensions, fit, and print orientation before final assembly.

### Alternative & Reference Code
* `ASTROPIXELS_UNIFIED_BRAIN.ino` and `ESP32_DOME_BRAIN_GUIDE.md`: Earlier FastLED-based alternative firmware. Kept for reference; do not mix with the PlatformIO build.
* `Dome_Control_with-Audio-Nano2-Body.ino`: Earlier Arduino Nano reference sketch for sound coordination.
* [`tests/`](tests/): Host-side Python unit tests that verify firmware timing contracts, macro behavior, and wiring diagram routes.

---

## Build Safety & Testing Steps

To avoid damaging electronics or running into wiring issues, test your build in stages:

1. **Inspect wiring unpowered:** Check crimps, wire gauges, and screw terminals with a multimeter for shorts before connecting any power.
2. **Verify power rails:** Power the 12V bus through the main fuse and verify voltage at each buck converter input. Check that both buck converters output a clean 5.0V before connecting any boards.
3. **Keep 5V rails separate:** The body and dome each use an independent 5V buck converter. **Never connect their +5V outputs together.** Common ground connects everywhere.
4. **Flash the ESP32 unmounted:** Remove the ESP32 module from the AstroPixels dome motherboard before plugging into your computer via USB. Reinstall it only with all power turned off.
5. **Elevate wheels during motor tests:** When configuring the Dual VESC or testing radio transmitter mixes, always elevate the droid so the wheels can spin freely in the air.
6. **Set up radio failsafes:** Test transmitter-off and unplugged receiver separately; CH6 must go OFF. Record actual stopping distance and each VESC's timeout brake action.
7. **Accessible master cutoff:** Keep the 25A main battery fuse close to the battery (<=150mm), and mount the master power switch where you can hit it instantly from the outside.

---

## Upstream Software & Community Credits

* **[AstroPixels Plus](https://github.com/reeltwo/AstroPixelsPlus)** (maintained by the ReelTwo team) is the foundation for `ASTROPIXELS_PLUS_UNIFIED/`. It provides the Wi-Fi dashboard, settings storage, lighting animations, and web interface. Licensed under LGPL-2.1.
* **[AstroPixels](https://github.com/dpoulson/Astropixels)** (by David Poulson) provides the original AstroPixels display board designs and core firmware. [Official Documentation](https://r2djp.gitbook.io/astropixels).
* **[ReelTwo](https://reeltwo.github.io/Reeltwo/html/index.html)** provides the underlying robotics, lighting, and animation framework used across the astromech builder community.
* **[The Real Home Depot R2D2 Mods Group](https://www.facebook.com/groups/969844001468804)** is the community hub for 3D-printable parts, reinforcement ideas, and conversion guides.

This is an independent hobbyist build, not affiliated with or endorsed by Home Depot, Lucasfilm, or Disney.

---

## Upstream Comparison: Why AstroPixels Plus?

We reviewed the standard AstroPixels firmware (`src/standard/main.cpp`) alongside AstroPixels Plus to decide which base to use for this project:

| Feature | Standard AstroPixels | AstroPixels Plus (Selected) | Notes for this Build |
| :--- | :--- | :--- | :--- |
| **User Interface** | Serial / I2C commands only | Built-in Wi-Fi dashboard & web GUI | Plus gives us an easy phone/browser dashboard to trigger sounds, test macros, and adjust settings. |
| **Remote Control** | Serial2 (9600 baud) or I2C slave | Body client on Serial2 (115200 baud) | Reserved GPIO16/17 for the bidirectional body link; radio/drive/audio handled by Teensy. |
| **Firmware Updates** | USB cable required | Wireless ArduinoOTA & web uploads | Gated by body maintenance-lock acknowledgement before either OTA path writes flash. |
| **Lighting Effects** | ReelTwo display classes & sequences | ReelTwo display classes & sequences | Both firmware families use the same high-quality ReelTwo lighting engines. |
| **Library Versions** | Unpinned dependencies | Pinned PlatformIO environment (`esp32@5.2.0`, `ReelTwo@23.5.3`) | Pinned dependencies ensure consistent, reliable builds without unexpected breakage from newer library versions. |

AstroPixels Plus gives us the web dashboard, OTA updating, and flexible serial setup we need without losing any lighting features.
