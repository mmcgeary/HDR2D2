#pragma once

#include <stdint.h>
#include "body/VescLink.h"

namespace body {

enum class WheelTestKind : uint8_t { TimeoutBrake = 6, Direction = 7, Reversal = 8 };
enum class WheelTestPhase : uint8_t { Idle, Running, Passed, Failed };

struct WheelTestCommand {
    enum Mode : uint8_t { Disable, Duty, Brake } mode;
    int16_t duty_permille;
};

struct WheelTestResult {
    uint16_t stop_ms;
    int16_t peak_current_cA;
    int32_t peak_erpm;
    uint8_t fault;
    uint16_t error;
};

// Automated single-wheel test (raised wheel): timeout-brake, direction and
// reversal checks driven purely from VESC telemetry samples. No I/O here; the
// caller applies command() to the VESC link every loop.
class WheelTest {
public:
    static const int16_t kSpinPermille = 100;
    static const int32_t kTurningErpm = 100;
    static const uint32_t kSpinMs = 1000, kDirectionSpinMs = 1500, kStopLimitMs = 1500;
    static const uint32_t kBrakeHoldMs = 300, kTestLimitMs = 6000;
    // Error codes reported in WheelTestResult::error / CommissionStatus::error.
    static const uint16_t kErrNotTurning = 7, kErrTimeoutBrakeInactive = 8, kErrVescFault = 9,
                          kErrTelemetry = 10, kErrNoReversal = 11, kErrTimeLimit = 13;

    void begin(WheelTestKind kind, uint16_t reversal_erpm, uint16_t reversal_dwell_ms, uint32_t now_ms);
    void update(const VescSample& sample, uint32_t now_ms);  // once per loop with the tested wheel's sample
    void abort(uint32_t now_ms);                             // brake out, then Failed with error 0
    WheelTestCommand command() const;
    WheelTestPhase phase() const;
    bool busy() const;                                       // Running, including the final brake hold
    WheelTestKind kind() const;
    const WheelTestResult& result() const;

private:
    enum class Step : uint8_t { Idle, SpinUp, CoastWatch, BrakeToLow, Dwell, ReverseSpin, BrakeOut, Done };
    void finish(WheelTestPhase phase, uint16_t error, uint32_t now_ms);
    WheelTestKind kind_{WheelTestKind::TimeoutBrake};
    WheelTestPhase phase_{WheelTestPhase::Idle}, final_{WheelTestPhase::Idle};
    Step step_{Step::Idle};
    WheelTestResult result_{};
    uint16_t reversal_erpm_{0}, dwell_ms_{0};
    uint32_t begin_ms_{0}, step_ms_{0}, low_since_ms_{0};
    int32_t spin_erpm_{0};
    bool low_started_{false};
};

}  // namespace body
