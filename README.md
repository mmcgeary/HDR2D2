# Home Depot R2-D2 Conversion

An in-progress project to turn the Home Depot R2-D2 "life-size ornament" into a remote-controlled droid with upgraded lighting, sound, dome movement and powered feet.

The goal is to retain the original shell while adding:

- AstroPixels logic displays, process state indicators (PSIs) and holoprojector lights.
- ESP32-controlled dome rotation, Hall-sensor homing and six holoprojector servos.
- Sound playback and coordinated lighting/motion routines.
- Independent left/right foot drive using Razor Tekno Pop hub motors and a dual VESC controller.
- FlySky radio control, plus a Wi-Fi dashboard for dome functions, sound settings and firmware updates.
- A documented battery, power-distribution and wiring plan with staged assembly and commissioning.

This repository records the selected hardware, adapted firmware, build decisions and remaining checks. It is a working build plan, **not a completed or fully hardware-validated conversion kit**.

## Start here

| Reference | Purpose |
| --- | --- |
| [System architecture](SYSTEM_ARCHITECTURE.md) | Overview and links to the primary construction references |
| [Power harness guide](POWER_HARNESS_GUIDE.md) | Wire sizes, fuse assignments, terminations, shopping allowances and electrical acceptance |
| [Interactive wiring diagram](wiring_visualizer.html) | Searchable component and wire inspector; open the file locally in a browser |
| [AstroPixels Plus Unified firmware guide](ASTROPIXELS_PLUS_UNIFIED/README.md) | Selected firmware, controls, sound-file layout, first USB flash and staged dome acceptance |
| [VESC drive setup](VESC_DRIVE_INTEGRATION.md) | Powered identification, motor detection, current limits, transmitter setup and drive safeguards |
| [Bill of materials](Master_R2D2_BOM.xls) | Component inventory and purchase notes; Excel-compatible SpreadsheetML |
| [System wiring overview](DOME_WIRING_DIAGRAM.md) | Logical block diagram, slip-ring allocation and signal-level rules |

Use the harness guide and interactive diagram for detailed electrical construction, and the Plus firmware README for programming. Some older overview/BOM statements still need reconciliation with the latest decisions; do not treat a legacy description as an alternative wiring instruction.

## What is in this repository?

### Selected firmware

[`ASTROPIXELS_PLUS_UNIFIED/`](ASTROPIXELS_PLUS_UNIFIED/) contains the preferred ESP32 firmware and its PlatformIO project. It adapts the AstroPixels/ReelTwo foundation with FlySky iBUS input, dome homing, PCA9685 holoprojector control, DFPlayer sound commands and shared RC/Wi-Fi routines.

Foot drive is separate from the ESP32: receiver outputs command the two VESC channels directly. **Wi-Fi STOP controls dome behaviour, not the feet.**

### Planning and wiring

The Markdown guides and BOM describe the electrical architecture, selected components, harness construction and staged checks. `wiring_visualizer.html` is a standalone electrical inspector that works offline and provides a printable wire schedule.

### Mechanical references

[`index.html`](index.html), `app.js` and `styles.css` provide a separate mechanical foot-layout visualizer with alternative design concepts. Its dimensions and performance assumptions must be checked against the actual build.

`Razor_Hub_Motor_Slotted_Mount.stl` and `generate_slotted_mount_stl.py` provide a project-specific motor-mount model and generator. The repository does not contain every reinforcement, bracket, coupler or gimbal needed to finish the droid.

[`mechanical/tekno-pop-motor-mounts-wip/`](mechanical/tekno-pop-motor-mounts-wip/) contains supplied **work-in-progress mounting models for the Razor Tekno Pop scooter hub motors**:

- [`r2d2_clean_motor_mount.stl`](mechanical/tekno-pop-motor-mounts-wip/r2d2_clean_motor_mount.stl)
- [`r2d2_inboard_axle_keeper_plate.stl`](mechanical/tekno-pop-motor-mounts-wip/r2d2_inboard_axle_keeper_plate.stl)
- [`case_bottom_locators_only.stl`](mechanical/tekno-pop-motor-mounts-wip/case_bottom_locators_only.stl)

These are design candidates, not finalized or load-validated parts. Confirm dimensions, fit, axle retention, print material/orientation and attachment to the foot structure before powered use. Their inclusion does not replace the remaining mechanical assembly work or establish their creator/license.

### Supporting references and checks

- [`docs/astropixels/`](docs/astropixels/) contains local AstroPixels documentation references; consult the upstream documentation for current information.
- [`tests/`](tests/) contains host-side firmware behaviour/contract and wiring-diagram regression checks. These do not replace electrical or mechanical commissioning.
- `ASTROPIXELS_UNIFIED_BRAIN.ino` and `ESP32_DOME_BRAIN_GUIDE.md` describe an earlier FastLED alternative.
- `Dome_Control_with-Audio-Nano2-Body.ino` is a Nano reference, not the selected ESP32 program.

Do not combine the alternative sketches with the selected PlatformIO project.

## Build status and precautions

Assembly and commissioning are deliberately staged: inspect the unpowered harness, verify supplies, flash the isolated ESP32, test dome subsystems individually, and commission the feet separately before combined operation.

Remaining work includes final mechanical layouts, actual-board pin/terminal checks, transmitter menu verification, VESC firmware-specific settings, motor/detection current limits, braking and battery/BMS regeneration behaviour, and loaded voltage/current/temperature measurements. Consult the detailed guides for the applicable stop conditions.

The build uses separate body and dome 5V converters. **Never join their positive outputs.** The current plan omits 5V output/device fuses by the builder's decision; advertised converter protection has not established downstream fault-protection performance. Battery-side fuses and the accessible master cutoff remain part of the plan.

Keep propulsion and mechanical loads isolated during initial programming and tests. Do not infer safe operation from a successful firmware build, a rendered diagram or a component's advertised rating.

## Credits and community

**AstroPixels** provides the lighting kit and the original firmware foundation on which this project's preferred `.ino` application is based. Credit belongs to the AstroPixels authors and contributors for that underlying work; the adaptations here are specific to this Home Depot conversion.

- [AstroPixels official documentation](https://r2djp.gitbook.io/astropixels)
- [AstroPixels upstream source](https://github.com/dpoulson/Astropixels)
- [ReelTwo library and documentation](https://reeltwo.github.io/Reeltwo/html/index.html), which supplies the lighting, animation and other firmware facilities used by AstroPixels and this project

**[The Real Home Depot R2D2 Mods and discussion](https://www.facebook.com/groups/969844001468804)** is the community source for STL designs used to upgrade and reinforce the droid, as well as practical build knowledge. Visit the group for the original files, creator attribution and instructions; access may require Facebook membership or group approval.

Community models are not necessarily included in this repository, and project-generated models should not be assumed to originate from the group. Respect each original creator's permissions and license when using, modifying or redistributing code, documentation and STL files. This README does not grant rights to third-party material.

This is an independent hobby project, not an official Home Depot, AstroPixels or Star Wars product.
