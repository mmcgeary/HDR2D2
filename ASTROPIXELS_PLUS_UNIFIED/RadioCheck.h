#pragma once

#include <cstdint>
#include "BodyRcState.h"

// Operator-prompted FlySky transmitter check (sticks, switches, knob) followed by
// a failsafe check (transmitter turned off).
class RadioCheck {
public:
    enum class State : uint8_t { Idle, Prompting, Passed, Failed };
    enum class Failsafe : uint8_t { Unknown, FrameValues, NoFrames, BadValues };
    static const uint32_t kStepTimeoutMs = 15000, kHoldMs = 1000;
    static const uint8_t kSteps = 9;   // 8 channel prompts + failsafe

    void start(uint32_t now_ms);
    void tick(const BodyRcState& rc, uint32_t now_ms);
    State state() const;
    uint8_t step() const;
    const char* prompt() const;
    Failsafe failsafe() const;

private:
    State state_{State::Idle};
    uint8_t step_{0};
    uint8_t seen_{0};
    uint32_t step_ms_{0}, hold_ms_{0};
    bool armed_{false};
    Failsafe failsafe_{Failsafe::Unknown};
};
