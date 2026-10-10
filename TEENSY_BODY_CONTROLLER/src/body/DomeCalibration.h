#pragma once
#include <stdint.h>
#include <stddef.h>
#include "Messages.h"
#include "body/ConfigStore.h"
#include "body/DomeController.h"
#include "body/IbusInput.h"

namespace body {

enum class CommissionOp : uint8_t {
    Read = 0,
    Begin = 1,
    Keepalive = 2,
    Cancel = 3,
    SetField = 4,
    Save = 5,
    Accept = 6
};

enum class CommissionState : uint8_t {
    Idle = 0,
    Running = 1,
    Completed = 2,
    Cancelled = 3,
    Failed = 4,
    TimedOut = 5
};

enum class CommissionTest : uint8_t {
    None = 0,
    Neutral = 1,
    FrontRef = 2,
    RearRef = 3,
    TimingCw = 4,
    TimingCcw = 5,
    VescTimeout = 6
};

class DomeCalibration {
public:
    DomeCalibration(ConfigStore& store, CommissioningProfile& staged_profile);

    void updateRc(const RcSnapshot& rc, uint32_t now_ms);
    void updateHall(const r2link::HallState& hall, uint32_t now_ms);
    void setMotionLocked(bool locked) { motion_locked_ = locked; }

    r2link::Result handleRequest(const r2link::CommissionRequest& req, uint32_t now_ms);
    void tick(uint32_t now_ms);

    const r2link::CommissionStatus& status() const { return status_; }
    r2link::Diagnostics diagnostics(uint8_t subtype, uint8_t field, uint8_t wheel, uint32_t sample_counter) const;

    bool active() const { return status_.state == static_cast<uint8_t>(CommissionState::Running); }
    ServoCommand output() const;
    void cancel(uint32_t now_ms);

private:
    uint16_t speedToPulse(int16_t speed_percent, uint16_t neutral_us) const;
    bool isSticksNeutral() const;
    void updateStatusFlags();

    ConfigStore& store_;
    CommissioningProfile& profile_;

    r2link::CommissionStatus status_{};
    RcSnapshot rc_{};
    r2link::HallState hall_{};
    uint32_t hall_rx_ms_{0};
    uint8_t last_hall_active_{0};

    bool motion_locked_{false};
    uint32_t keepalive_deadline_ms_{0};
    uint32_t test_start_ms_{0};
    uint32_t ref_timeout_deadline_ms_{0};

    uint16_t trial_neutral_us_{1500};
    uint8_t trial_speed_percent_{15};

    uint8_t timing_phase_{0}; // 0 = find initial front edge, 1 = rev 0, 2 = rev 1, 3 = rev 2
    bool saw_rear_{false};
    uint32_t rev_start_ms_{0};
};

} // namespace body
