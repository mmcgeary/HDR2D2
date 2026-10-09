# Teensy body controller

Commissioning-first PlatformIO target for the Teensy 4.1 body controller
(see `docs/superpowers/specs/2026-10-09-teensy-body-controller-design.md`).

This first image is inert: it starts USB serial and prints `[BODY] UNCOMMISSIONED`.
It configures no actuator pins, sends no VESC or audio commands and drives no
servo pulse. Pin constants live in `src/body/Pins.h`.

The shared protocol codec is `../shared/R2BodyLink`.

    python3 -m unittest discover -s tests -p test_body_link.py
    pio run -d TEENSY_BODY_CONTROLLER -e teensy41
