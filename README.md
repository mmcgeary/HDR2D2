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

**AstroPixels Plus**, maintained in [reeltwo/AstroPixelsPlus](https://github.com/reeltwo/AstroPixelsPlus), is the direct application foundation for `ASTROPIXELS_PLUS_UNIFIED/ASTROPIXELS_PLUS_UNIFIED.ino` and its supporting dashboard, preferences, effects and command headers. Credit belongs to its authors and contributors for that underlying work. Our changes integrate this build's radio input, dome control, homing, holo servos, audio routines and motion safeguards; this is not an unmodified upstream release.

**AstroPixels**, maintained in [dpoulson/Astropixels](https://github.com/dpoulson/Astropixels), provides the lighting kit and standard firmware family. **ReelTwo** provides the shared lighting, animation and control framework used by both firmware families.

- [AstroPixels official documentation](https://r2djp.gitbook.io/astropixels)
- [AstroPixels upstream source](https://github.com/dpoulson/Astropixels)
- [AstroPixels Plus upstream source and license](https://github.com/reeltwo/AstroPixelsPlus), which includes an LGPL-2.1 license; retain applicable upstream notices and comply with its terms
- [ReelTwo library and documentation](https://reeltwo.github.io/Reeltwo/html/index.html), which supplies the lighting, animation and other firmware facilities used by AstroPixels and this project

**[The Real Home Depot R2D2 Mods and discussion](https://www.facebook.com/groups/969844001468804)** is the community source for STL designs used to upgrade and reinforce the droid, as well as practical build knowledge. Visit the group for the original files, creator attribution and instructions; access may require Facebook membership or group approval.

Community models are not necessarily included in this repository, and project-generated models should not be assumed to originate from the group. Respect each original creator's permissions and license when using, modifying or redistributing code, documentation and STL files. This README does not grant rights to third-party material.

This is an independent hobby project, not an official Home Depot, AstroPixels or Star Wars product.

## Firmware foundation comparison

Reviewed on **2026-10-07** against AstroPixels revision [`de031282`](https://github.com/dpoulson/Astropixels/tree/de031282ffd1725f162ea0840effe0621d07b50d) and AstroPixels Plus revision [`09ec5eb3`](https://github.com/reeltwo/AstroPixelsPlus/tree/09ec5eb3a73c7866589d375a704e52c44958abfc).

The complete standard application (`src/standard/main.cpp`), its latest file-specific change, the related `standard-md` application, and Plus initialization/control plumbing were compared with our adapted application:

| Upstream behaviour | Decision for this build |
| --- | --- |
| Standard adds a 9600-baud command receiver on Serial2 GPIO16/17 | Do not port: GPIO16 receives binary iBUS at115200 baud here; GPIO17 is assigned separately to DFPlayer output. A standard serial-command parser would conflict with that allocation. |
| Standard receives I2C commands as slave address0x0A | Do not port: our selected control paths are RC/Wi-Fi and the ESP32 uses I2C as master for PCA9685. Adding slave reception is a separate interface change, not a missing fix. |
| Standard uses the same AstroPixel display classes, RGB holo configuration and ReelTwo setup/animation lifecycle | Already represented in our application. Plus's explicit holo IDs1/2/3 are retained. |
| Standard starts a20-second holo lighting effect after startup | Treat as a presentation choice, not a correctness fix; retain this build's startup and macro behaviour. |
| Plus supplies Wi-Fi dashboard, saved preferences and OTA | Retain: these support the agreed Wi-Fi sound-volume and dome controls. The standard application does not supply equivalent facilities. |
| Standard PlatformIO uses unpinned platform/ReelTwo dependencies | Do not copy wholesale: our ESP32 platform5.2.0/ReelTwo23.5.3 combination is the existing verified build baseline. Dependency upgrades require a separate compatibility review. |

**No standard-application fix requiring a firmware port was identified in this comparison.** Newer repository activity includes documentation and web-support tooling, and is not evidence that the standard ESP32 application supersedes Plus for this build.

This conclusion is limited to the application comparison, not a complete audit of all newer ReelTwo/library changes, every firmware variant or hardware behaviour. Retaining Plus does not eliminate the staged physical acceptance requirements.
