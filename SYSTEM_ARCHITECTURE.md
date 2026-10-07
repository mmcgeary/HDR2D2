# R2-D2 System Architecture

The authoritative system diagram, slip-ring allocation, power distribution and level-shifter connections are in [DOME_WIRING_DIAGRAM.md](DOME_WIRING_DIAGRAM.md).

The construction reference is [POWER_HARNESS_GUIDE.md](POWER_HARNESS_GUIDE.md), with approved gauges, proposed fuse sizes, connection methods and staged acceptance gates. [wiring_visualizer.html](wiring_visualizer.html) is the interactive electrical terminal schedule; `index.html` remains the mechanical foot visualizer.

The preferred ESP32 firmware is [AstroPixels Plus Unified](ASTROPIXELS_PLUS_UNIFIED/README.md), with ReelTwo lighting, Wi-Fi controls and OTA. The dome uses GPIO19 for Hall homing, six PCA9685-controlled holo servos and a body-mounted continuous-rotation servo. All external 5V logic crosses the dome's 3.3V/5V level shifter before reaching ESP32 inputs.

Foot drive remains independent of the ESP32: transmitter-side tank mixing sends two separate receiver outputs to the Dual FSESC4.20, with slave mode disabled. Configure receiver neutral failsafe and VESC input-loss timeout separately; see [VESC_DRIVE_INTEGRATION.md](VESC_DRIVE_INTEGRATION.md).

Two independent 12V-to-5V, 10A buck converters supply body and dome loads. Fused battery voltage and common ground cross the slip ring; the dome converter produces 5V locally. Do not join the converters' 5V positive outputs.

`ASTROPIXELS_UNIFIED_BRAIN.ino` is the earlier FastLED alternative, not the firmware selected for this build. `Dome_Control_with-Audio-Nano2-Body.ino` is a Nano reference for easing and sound coordination, not ESP32 firmware.
