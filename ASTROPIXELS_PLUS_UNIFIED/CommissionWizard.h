#pragma once
#include <cstdint>
#include "Endpoint.h"
#include "Messages.h"
#include "ProfileMirror.h"

// Sequences commissioning requests to the Teensy body (dome calibration run, wheel
// tests, neutral nudges, baseline, accept-and-save) and tracks replies plus the
// body's CommissionStatus to report Running / Done / Failed.
class CommissionWizard {
public:
    enum class State : uint8_t { Idle, Running, Done, Failed };
    explicit CommissionWizard(ICommissionSink& sink);
    bool startDomeCalibration(uint32_t now_ms);                         // FrontRef, RearRef, TimingCw, TimingCcw
    bool startWheelTest(uint8_t test, uint8_t wheel, bool raised, uint32_t now_ms);   // test 6/7/8
    bool startNeutral(uint32_t now_ms);                                 // Neutral hold
    bool nudgeNeutral(int16_t delta_us, int32_t current_us, uint32_t now_ms); // SetField 0 then Neutral
    bool acceptAndSave(const uint8_t* bits, uint8_t count, uint32_t now_ms);
    bool applyBaseline(uint32_t now_ms);
    void cancel(uint32_t now_ms);
    void tick(uint32_t now_ms, const r2link::CommissionStatus& status, bool status_fresh, uint16_t epoch);
    void onCompletion(const r2link::Completion& c);
    State state() const;
    uint8_t currentTest() const;
    uint16_t lastError() const;   // CommissionStatus.error of a failed test
    uint8_t lastResult() const;   // r2link::Result of a refused request

private:
    bool begin(uint8_t test, uint8_t wheel, int32_t value, uint32_t now_ms);
    bool send(r2link::CommissionRequest req, uint32_t now_ms);
    void fail(uint16_t error, uint8_t result);
    ICommissionSink& sink_;
    State state_{State::Idle};
    uint8_t queue_[4]{}; uint8_t queue_len_{0}, queue_pos_{0};
    uint8_t test_{0};
    uint32_t run_id_{0}, run_counter_{0};
    uint16_t epoch_{0};
    uint16_t pending_[13]{}; uint8_t pending_count_{0};   // sequences awaiting replies
    uint16_t last_error_{0}; uint8_t last_result_{0};
    bool waiting_status_{false};
};
