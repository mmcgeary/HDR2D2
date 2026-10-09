#pragma once
#include <stdint.h>

// Fake Arduino Servo for host tests
struct Servo {
    int pin = -1;
    bool attached_ = false;
    uint16_t pulse_us = 0;
    void attach(int p) { pin = p; attached_ = true; }
    void detach() { attached_ = false; }
    bool attached() const { return attached_; }
    void writeMicroseconds(int us) { pulse_us = static_cast<uint16_t>(us); }
    int readMicroseconds() const { return pulse_us; }
};
