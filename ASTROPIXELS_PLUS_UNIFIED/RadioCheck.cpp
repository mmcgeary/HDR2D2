#include "RadioCheck.h"

namespace {
const char* const kPrompts[RadioCheck::kSteps] = {
    "Push the right stick UP", "Push the right stick RIGHT", "Push the left stick RIGHT",
    "Flip SwA DOWN", "Move SwC through all three positions", "Flip SwB DOWN", "Flip SwD DOWN",
    "Turn the VrA knob fully both ways",
    "Set SwA, SwB and SwD DOWN, then turn the transmitter OFF"};
bool centred(uint16_t v) { return v >= 1460 && v <= 1540; }
}

void RadioCheck::start(uint32_t now) {
    state_ = State::Prompting; step_ = 0; seen_ = 0; step_ms_ = now; hold_ms_ = 0; armed_ = false;
    failsafe_ = Failsafe::Unknown;
}

void RadioCheck::tick(const BodyRcState& rc, uint32_t now) {
    if (state_ != State::Prompting) return;
    if (now - step_ms_ > kStepTimeoutMs) { state_ = State::Failed; return; }
    const uint16_t* ch = rc.channels;
    bool done = false;
    if (step_ < 8 && rc.valid) {
        switch (step_) {
        case 0: done = ch[1] > 1750; break;
        case 1: done = ch[0] > 1750; break;
        case 2: done = ch[3] > 1750; break;
        case 3: done = ch[5] > 1750; break;
        case 4:
            if (ch[4] < 1250) seen_ |= 1;
            if (ch[4] >= 1400 && ch[4] <= 1600) seen_ |= 2;
            if (ch[4] > 1750) seen_ |= 4;
            done = seen_ == 7; break;
        case 5: done = ch[7] > 1750; break;
        case 6: done = ch[8] > 1750; break;
        case 7:
            if (ch[6] < 1100) seen_ |= 8;
            done = (seen_ & 8) && ch[6] > 1900; break;
        }
    } else if (step_ == 8) {
        if (!armed_) {
            // Must first see the switches armed (DOWN) so already-failsafe positions cannot pass.
            armed_ = rc.valid && ch[5] > 1750 && ch[7] > 1750 && ch[8] > 1750;
            return;
        }
        if (!rc.valid) {                     // frames stopped: Teensy disarms on staleness
            failsafe_ = Failsafe::NoFrames; state_ = State::Passed; return;
        }
        if (ch[5] > 1750 || ch[7] > 1750 || ch[8] > 1750) { hold_ms_ = 0; return; }   // still armed: TX on
        if (!hold_ms_) hold_ms_ = now;
        if (now - hold_ms_ < kHoldMs) return;
        const bool good = centred(ch[0]) && centred(ch[1]) && centred(ch[3]) &&
                          ch[5] <= 1250 && ch[7] <= 1250 && ch[8] <= 1250;
        failsafe_ = good ? Failsafe::FrameValues : Failsafe::BadValues;
        state_ = good ? State::Passed : State::Failed;
        return;
    }
    if (done) { ++step_; seen_ = 0; step_ms_ = now; }
}

RadioCheck::State RadioCheck::state() const { return state_; }
uint8_t RadioCheck::step() const { return step_; }
const char* RadioCheck::prompt() const { return step_ < kSteps ? kPrompts[step_] : ""; }
RadioCheck::Failsafe RadioCheck::failsafe() const { return failsafe_; }
