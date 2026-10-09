#pragma once
#include <stdint.h>
#include "Messages.h"
#include "Endpoint.h"
#include "body/IbusInput.h"
#include "body/ConfigStore.h"
#include "body/DomePosition.h"

namespace body {

struct ServoCommand {
    bool pulses;
    uint16_t pulse_us;
};

class DomeController {
public:
    explicit DomeController(const CommissioningProfile& profile);

    void updateRc(const RcSnapshot& rc, uint32_t now_ms);
    void updateHall(const r2link::HallState& hall, uint32_t received_ms);
    void updateDrive(r2link::DriveIntent drive_intent, uint32_t now_ms);

    r2link::DomeOwner owner() const { return owner_; }
    r2link::DomeState state() const { return state_; }
    uint32_t authorityGeneration() const { return authority_generation_; }
    uint16_t controlEpoch() const { return epoch_; }
    void setControlEpoch(uint16_t epoch) { epoch_ = epoch; }

    r2link::Result request(const r2link::DomeRequest& req, uint16_t request_seq, uint32_t now_ms);
    void tick(uint32_t now_ms);
    void cancel(uint32_t now_ms);
    void stop(uint32_t now_ms);
    r2link::Result setMotionLocks(uint8_t reasons);
    r2link::Result releaseStop(uint16_t current_epoch, uint32_t now_ms);
    void peerLost(uint32_t now_ms);

    ServoCommand output() const;
    bool takeEvent(r2link::Event& ev);

    const DomePosition& position() const { return position_; }
    DomePosition& position() { return position_; }
    bool seekFault() const { return seek_fault_; }

private:
    uint16_t speedToPulse(int16_t speed_percent) const;
    void pushEvent(r2link::EventKind kind, uint8_t req_type, uint16_t req_seq, r2link::Detail detail);
    void clearActiveRequest();
    bool isHallFresh(uint32_t now_ms) const;
    bool isRcFresh(uint32_t now_ms) const;

    const CommissioningProfile* profile_;
    DomePosition position_;

    r2link::DomeOwner owner_;
    r2link::DomeState state_;
    r2link::DriveIntent drive_intent_;

    RcSnapshot rc_;
    r2link::HallState hall_;
    uint32_t hall_rx_ms_;

    uint32_t authority_generation_;
    uint16_t epoch_;
    uint8_t motion_locks_;
    bool stopped_;

    // Manual tracking
    bool manual_active_;

    // Remote / autonomous seek tracking
    bool seek_active_;
    r2link::DomeReference seek_target_;
    uint32_t seek_start_ms_;
    uint16_t active_req_seq_;
    uint8_t active_req_owner_; // 0 idle, 1 event

    // Velocity lease tracking
    bool velocity_active_;
    int16_t velocity_speed_;
    uint32_t lease_expiry_ms_;

    // Drive seek tracking
    bool drive_seeking_;
    r2link::DomeReference drive_target_;
    uint32_t drive_seek_start_ms_;

    // Startup seek tracking
    bool startup_done_;
    bool startup_seeking_;
    uint32_t startup_seek_start_ms_;

    // Fault tracking
    bool seek_fault_;
    bool auto_dome_prior_on_;

    // Timing
    uint32_t last_tick_ms_;
    bool ticked_;

    // Event queue (bounded circular buffer)
    static const size_t kEventQueueCap = 8;
    r2link::Event event_queue_[kEventQueueCap];
    size_t event_head_;
    size_t event_tail_;
    size_t event_count_;
};

} // namespace body
