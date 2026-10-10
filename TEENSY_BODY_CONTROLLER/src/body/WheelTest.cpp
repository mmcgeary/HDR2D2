#include "body/WheelTest.h"
#include <stdlib.h>

namespace body {
namespace {
int32_t mag(int32_t v) { return v < 0 ? -v : v; }
}

void WheelTest::begin(WheelTestKind kind, uint16_t reversal_erpm, uint16_t reversal_dwell_ms, uint32_t now) {
    kind_ = kind; reversal_erpm_ = reversal_erpm; dwell_ms_ = reversal_dwell_ms;
    result_ = WheelTestResult{};
    phase_ = WheelTestPhase::Running; final_ = WheelTestPhase::Running;
    step_ = Step::SpinUp; begin_ms_ = step_ms_ = now; spin_erpm_ = 0; low_started_ = false;
}

void WheelTest::finish(WheelTestPhase phase, uint16_t error, uint32_t now) {
    final_ = phase; result_.error = error;
    step_ = Step::BrakeOut; step_ms_ = now;   // always leave the wheel braked, then released
}

void WheelTest::abort(uint32_t now) {
    if (!busy() || step_ == Step::BrakeOut) return;
    finish(WheelTestPhase::Failed, 0, now);
}

void WheelTest::update(const VescSample& s, uint32_t now) {
    if (!busy()) return;
    if (step_ == Step::BrakeOut) {
        if (now - step_ms_ >= kBrakeHoldMs) { step_ = Step::Done; phase_ = final_; }
        return;
    }
    if (now - begin_ms_ >= kTestLimitMs) { finish(WheelTestPhase::Failed, kErrTimeLimit, now); return; }
    if (s.fault) { result_.fault = s.fault; finish(WheelTestPhase::Failed, kErrVescFault, now); return; }
    if (!s.valid || s.stale) { finish(WheelTestPhase::Failed, kErrTelemetry, now); return; }
    if (mag(s.erpm) > mag(result_.peak_erpm)) result_.peak_erpm = s.erpm;
    const uint32_t in_step = now - step_ms_;
    switch (step_) {
    case Step::SpinUp: {
        const uint32_t spin = kind_ == WheelTestKind::Direction ? kDirectionSpinMs : kSpinMs;
        if (mag(s.erpm) > mag(spin_erpm_)) spin_erpm_ = s.erpm;
        if (in_step < spin) return;
        if (mag(spin_erpm_) < kTurningErpm) { finish(WheelTestPhase::Failed, kErrNotTurning, now); return; }
        if (kind_ == WheelTestKind::Direction) { finish(WheelTestPhase::Passed, 0, now); return; }
        step_ = kind_ == WheelTestKind::TimeoutBrake ? Step::CoastWatch : Step::BrakeToLow;
        step_ms_ = now;
        return;
    }
    case Step::CoastWatch: {
        const int32_t current_cA = int32_t(mag(s.motor_mA) / 10);
        if (current_cA > mag(result_.peak_current_cA)) result_.peak_current_cA = int16_t(-current_cA);
        if (mag(s.erpm) * 10 <= mag(spin_erpm_)) {
            result_.stop_ms = uint16_t(in_step);
            step_ = Step::Done; phase_ = WheelTestPhase::Passed;   // already stopped: no brake needed
            return;
        }
        if (in_step >= kStopLimitMs) finish(WheelTestPhase::Failed, kErrTimeoutBrakeInactive, now);
        return;
    }
    case Step::BrakeToLow: {
        const int32_t current_cA = int32_t(mag(s.motor_mA) / 10);
        if (current_cA > mag(result_.peak_current_cA)) result_.peak_current_cA = int16_t(-current_cA);
        if (mag(s.erpm) <= reversal_erpm_) {
            result_.stop_ms = uint16_t(in_step);
            step_ = Step::Dwell; step_ms_ = now; low_since_ms_ = now;
        } else if (in_step >= kStopLimitMs) {
            finish(WheelTestPhase::Failed, kErrNoReversal, now);
        }
        return;
    }
    case Step::Dwell:
        if (mag(s.erpm) > reversal_erpm_) low_since_ms_ = now;
        if (now - low_since_ms_ >= dwell_ms_) { step_ = Step::ReverseSpin; step_ms_ = now; }
        return;
    case Step::ReverseSpin:
        if (in_step < kSpinMs) return;
        if ((spin_erpm_ > 0 ? s.erpm <= -kTurningErpm : s.erpm >= kTurningErpm))
            finish(WheelTestPhase::Passed, 0, now);
        else
            finish(WheelTestPhase::Failed, kErrNoReversal, now);
        return;
    default:
        return;
    }
}

WheelTestCommand WheelTest::command() const {
    switch (step_) {
    case Step::SpinUp: return {WheelTestCommand::Duty, kSpinPermille};
    case Step::ReverseSpin: return {WheelTestCommand::Duty, int16_t(-kSpinPermille)};
    case Step::BrakeToLow: case Step::Dwell: case Step::BrakeOut: return {WheelTestCommand::Brake, 0};
    default: return {WheelTestCommand::Disable, 0};   // Idle, CoastWatch (no commands), Done
    }
}

WheelTestPhase WheelTest::phase() const { return phase_; }
bool WheelTest::busy() const { return step_ != Step::Idle && step_ != Step::Done; }
WheelTestKind WheelTest::kind() const { return kind_; }
const WheelTestResult& WheelTest::result() const { return result_; }

}  // namespace body
