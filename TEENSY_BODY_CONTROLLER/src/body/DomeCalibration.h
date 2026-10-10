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
    Accept = 6,
    ApplyBaseline = 7
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
    WheelTimeout = 6,
    WheelDirection = 7,
    WheelReversal = 8
};

class DomeCalibration {
public:
    DomeCalibration(ConfigStore& store, CommissioningProfile& staged_profile);

    void updateRc(const RcSnapshot& rc, uint32_t now_ms);
    void updateHall(const r2link::HallState& hall, uint32_t now_ms);
    void setMotionLocked(bool locked) { motion_locked_ = locked; }
    // Actuators run the saved profile; status() compares staged against it.
    void setSavedProfile(const CommissioningProfile& saved) { saved_ = &saved; }
    void setObservedFirmware(uint8_t wheel, bool valid, uint8_t major, uint8_t minor);

    r2link::Result handleRequest(const r2link::CommissionRequest& req, uint32_t now_ms);
    void tick(uint32_t now_ms);

    const r2link::CommissionStatus& status() const { return status_; }
    r2link::Diagnostics diagnostics(uint8_t subtype, uint8_t field, uint8_t wheel, uint32_t sample_counter) const;

    bool active() const { return status_.state == static_cast<uint8_t>(CommissionState::Running); }
    ServoCommand output() const;
    void cancel(uint32_t now_ms);

    // The Neutral test holds the trial pulse this long, then completes so the
    // operator can accept what they observed.
    static const uint32_t kNeutralObserveMs = 3000;

private:
    uint16_t speedToPulse(int16_t speed_percent, uint16_t neutral_us) const;
    bool isSticksNeutral() const;
    bool stationaryGate() const;   // CH6 OFF, sticks neutral, fresh RC (CH9 plays no part)
    void updateStatusFlags();
    void refreshStatus();
    r2link::Result handleRequestImpl(const r2link::CommissionRequest& req, uint32_t now_ms);
    // Digest of the configuration a dome run depends on (servo trims, auto speed).
    uint32_t evidenceDigest() const;
    void complete();
    bool evidenceFor(uint8_t bit, uint32_t& run_id, bool& cw, bool& ccw) const;

    ConfigStore& store_;
    CommissioningProfile& profile_;
    const CommissioningProfile* saved_{nullptr};
    ObservedFirmware observed_fw_[2]{};

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

    // Latest completed run of each test and the configuration it ran against.
    uint32_t done_run_[7]{};
    uint32_t done_digest_[7]{};

    uint8_t timing_phase_{0}; // 0 = find initial front edge, 1 = rev 0, 2 = rev 1, 3 = rev 2
    bool saw_rear_{false};
    uint32_t rev_start_ms_{0};
};

} // namespace body
