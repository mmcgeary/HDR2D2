#include "body/DomeController.h"

namespace body {
namespace {
bool centered(uint16_t v) { return v >= 1460 && v <= 1540; }
int32_t stick(uint16_t us) {
    if (centered(us)) return 0;
    if (us < 1000) us = 1000;
    if (us > 2000) us = 2000;
    return (int32_t(us) - 1500) * 2;
}
}

DomeController::DomeController(const CommissioningProfile& profile)
    : profile_(&profile), position_(profile),
      owner_(r2link::DomeOwner::None), state_(r2link::DomeState::Inhibited),
      drive_intent_(r2link::DriveIntent::Stationary), rc_{}, hall_{},
      hall_rx_ms_(0), authority_generation_(1), epoch_(0), motion_locks_(0),
      stopped_(false), manual_active_(false), seek_active_(false),
      seek_target_(r2link::DomeReference::Front), seek_start_ms_(0),
      active_req_seq_(0), active_req_owner_(0), velocity_active_(false),
      velocity_speed_(0), lease_expiry_ms_(0), drive_seeking_(false),
      drive_target_(r2link::DomeReference::Front), drive_seek_start_ms_(0),
      startup_done_(false), startup_seeking_(false), startup_seek_start_ms_(0),
      seek_fault_(false), auto_dome_prior_on_(false), last_tick_ms_(0),
      ticked_(false), event_head_(0), event_tail_(0), event_count_(0) {}

uint16_t DomeController::speedToPulse(int16_t speed_percent) const {
    const uint16_t neutral = (profile_ && profile_->servo_neutral) ? profile_->servo_neutral : 1500;
    const uint16_t min_us = (profile_ && profile_->servo_min) ? profile_->servo_min : 1000;
    const uint16_t max_us = (profile_ && profile_->servo_max) ? profile_->servo_max : 2000;
    if (speed_percent == 0) return neutral;
    int32_t pulse = neutral;
    if (speed_percent > 0) {
        pulse += (int32_t(speed_percent) * (max_us - neutral)) / 100;
    } else {
        pulse += (int32_t(speed_percent) * (neutral - min_us)) / 100;
    }
    if (pulse < min_us) pulse = min_us;
    if (pulse > max_us) pulse = max_us;
    return static_cast<uint16_t>(pulse);
}

void DomeController::pushEvent(r2link::EventKind kind, uint8_t req_type, uint16_t req_seq, r2link::Detail detail) {
    if (event_count_ < kEventQueueCap) {
        event_queue_[event_tail_] = {
            static_cast<uint8_t>(kind), req_type, req_seq, static_cast<uint16_t>(detail)
        };
        event_tail_ = (event_tail_ + 1) % kEventQueueCap;
        ++event_count_;
    }
}

bool DomeController::takeEvent(r2link::Event& ev) {
    if (event_count_ == 0) return false;
    ev = event_queue_[event_head_];
    event_head_ = (event_head_ + 1) % kEventQueueCap;
    --event_count_;
    return true;
}

void DomeController::clearActiveRequest() {
    seek_active_ = false;
    velocity_active_ = false;
    velocity_speed_ = 0;
    active_req_seq_ = 0;
}

bool DomeController::isHallFresh(uint32_t now_ms) const {
    return (hall_.valid_mask & 0x03) == 0x03 &&
           hall_.source_age_ms <= 150 &&
           uint32_t(now_ms - hall_rx_ms_) <= 150;
}

bool DomeController::isRcFresh(uint32_t now_ms) const {
    return rc_.valid && uint32_t(now_ms - rc_.sample_ms) <= 250;
}

void DomeController::updateRc(const RcSnapshot& rc, uint32_t) {
    rc_ = rc;
}

void DomeController::updateHall(const r2link::HallState& hall, uint32_t received_ms) {
    hall_ = hall;
    hall_rx_ms_ = received_ms;
    position_.updateHall(hall, received_ms);
}

void DomeController::updateDrive(r2link::DriveIntent drive_intent, uint32_t) {
    drive_intent_ = drive_intent;
}

void DomeController::stop(uint32_t) {
    if (!stopped_) { stopped_ = true; ++epoch_; }
    if (seek_active_ || velocity_active_) clearActiveRequest();
    drive_seeking_ = false;
    startup_seeking_ = false;
    owner_ = r2link::DomeOwner::None;
    state_ = r2link::DomeState::Inhibited;
}

r2link::Result DomeController::setMotionLocks(uint8_t reasons) {
    if (reasons & ~uint8_t(0x05)) return r2link::Result::InvalidArgument;
    if (motion_locks_ == reasons) return r2link::Result::Accepted;
    motion_locks_ = reasons;
    ++epoch_;
    if (motion_locks_) {
        if (seek_active_ || velocity_active_) clearActiveRequest();
        drive_seeking_ = false;
        startup_seeking_ = false;
        owner_ = r2link::DomeOwner::None;
        state_ = r2link::DomeState::Inhibited;
    }
    return r2link::Result::Accepted;
}

r2link::Result DomeController::releaseStop(uint16_t current_epoch, uint32_t) {
    if (current_epoch != epoch_) return r2link::Result::WrongEpoch;
    if (stopped_) {
        stopped_ = false;
        ++epoch_;
    }
    return r2link::Result::Accepted;
}

void DomeController::cancel(uint32_t) {
    if (seek_active_ || velocity_active_) {
        clearActiveRequest();
    }
    drive_seeking_ = false;
    startup_seeking_ = false;
    state_ = r2link::DomeState::Inhibited;
}

void DomeController::peerLost(uint32_t) {
    // Ordinary peer loss cancels only remote-owned actions; manual remains available.
    if (owner_ == r2link::DomeOwner::Event || owner_ == r2link::DomeOwner::Idle) {
        clearActiveRequest();
        owner_ = r2link::DomeOwner::None;
        state_ = r2link::DomeState::Inhibited;
        ++authority_generation_;
    }
}

r2link::Result DomeController::request(const r2link::DomeRequest& req, uint16_t request_seq, uint32_t now_ms) {
    if (req.operation > 2 || req.owner > 1 || req.reference > 1)
        return r2link::Result::InvalidArgument;
    if (req.operation == 1 && (req.lease_ms < 1 || req.lease_ms > 150 || req.speed_percent < -100 || req.speed_percent > 100))
        return r2link::Result::InvalidArgument;
    if (req.operation != 1 && (req.speed_percent != 0 || req.lease_ms != 0))
        return r2link::Result::InvalidArgument;

    const bool auto_ready = profile_ && readiness(*profile_).auto_dome;
    if (!auto_ready) return r2link::Result::NotReady;

    if (stopped_ || motion_locks_ != 0) return r2link::Result::Inhibited;
    if (seek_fault_ || position_.sensorFault()) return r2link::Result::Inhibited;

    if (!isRcFresh(now_ms)) return r2link::Result::NotReady;
    if (rc_.channels[kAutoDome] < 1750) return r2link::Result::Inhibited;

    if (!isHallFresh(now_ms)) return r2link::Result::NotReady;

    if (req.control_epoch != epoch_) return r2link::Result::WrongEpoch;
    if (req.dome_authority_generation != authority_generation_) return r2link::Result::Inhibited;

    // Manual stick deflection or active drive overrides remote requests
    if (!centered(rc_.channels[kManualDome])) return r2link::Result::ManualOverride;
    if (drive_intent_ != r2link::DriveIntent::Stationary) return r2link::Result::Inhibited;

    // Higher priority event blocks lower priority idle
    if (owner_ == r2link::DomeOwner::Event && req.owner == 0) return r2link::Result::Busy;

    if (req.operation == 0) { // Cancel
        if (seek_active_ || velocity_active_) {
            pushEvent(r2link::EventKind::Cancelled, static_cast<uint8_t>(r2link::MessageType::DomeRequest), active_req_seq_, r2link::Detail::None);
            clearActiveRequest();
            owner_ = r2link::DomeOwner::None;
            state_ = r2link::DomeState::Inhibited;
        }
        return r2link::Result::Accepted;
    }

    if (req.operation == 1) { // Velocity
        clearActiveRequest();
        velocity_active_ = true;
        // Remote velocity runs at the commissioned auto speed in the requested
        // direction: that is the only speed dead reckoning is calibrated for.
        velocity_speed_ = req.speed_percent > 0 ? int16_t(profile_->auto_speed_percent)
                        : req.speed_percent < 0 ? int16_t(-int16_t(profile_->auto_speed_percent)) : 0;
        lease_expiry_ms_ = now_ms + req.lease_ms;
        active_req_seq_ = request_seq;
        active_req_owner_ = req.owner;
        owner_ = (req.owner == 1) ? r2link::DomeOwner::Event : r2link::DomeOwner::Idle;
        state_ = r2link::DomeState::RemoteVelocity;
        return r2link::Result::Accepted;
    }

    if (req.operation == 2) { // SeekReference
        clearActiveRequest();
        const r2link::DomeReference target = (req.reference == 1) ? r2link::DomeReference::Rear : r2link::DomeReference::Front;
        const uint8_t mask = (target == r2link::DomeReference::Rear) ? 0x02 : 0x01;
        if (hall_.active_mask & mask) {
            // Already active: complete without rotation
            state_ = r2link::DomeState::HoldingReference;
            owner_ = (req.owner == 1) ? r2link::DomeOwner::Event : r2link::DomeOwner::Idle;
            pushEvent(r2link::EventKind::Completed, static_cast<uint8_t>(r2link::MessageType::DomeRequest), request_seq, r2link::Detail::None);
            return r2link::Result::Accepted;
        }
        seek_active_ = true;
        seek_target_ = target;
        seek_start_ms_ = now_ms;
        active_req_seq_ = request_seq;
        active_req_owner_ = req.owner;
        owner_ = (req.owner == 1) ? r2link::DomeOwner::Event : r2link::DomeOwner::Idle;
        state_ = r2link::DomeState::SeekingReference;
        return r2link::Result::Accepted;
    }

    return r2link::Result::InvalidArgument;
}

void DomeController::tick(uint32_t now_ms) {
    const uint32_t dt = ticked_ ? (now_ms - last_tick_ms_) : 0;
    ticked_ = true;
    last_tick_ms_ = now_ms;

    // Check Auto Dome switch edge / state
    const bool auto_dome_on = isRcFresh(now_ms) && rc_.channels[kAutoDome] >= 1750;
    if (auto_dome_on && !auto_dome_prior_on_) {
        // OFF -> ON edge clears latched faults
        seek_fault_ = false;
        position_.clearSensorFault();
        startup_done_ = false;
    } else if (!auto_dome_on && auto_dome_prior_on_) {
        // ON -> OFF transition increments authority generation and halts remote actions
        ++authority_generation_;
        if (seek_active_ || velocity_active_) clearActiveRequest();
        drive_seeking_ = false;
        startup_seeking_ = false;
    }
    auto_dome_prior_on_ = auto_dome_on;

    // 1. Motion locks or operator STOP dominate everything
    if (stopped_ || motion_locks_ != 0) {
        owner_ = r2link::DomeOwner::None;
        state_ = r2link::DomeState::Inhibited;
        return;
    }

    // 2. Manual stick control (CH4) takes priority over everything else, once the
    //    servo neutral has been commissioned and saved.
    const bool rc_ok = isRcFresh(now_ms);
    const bool manual_ready = profile_ && readiness(*profile_).manual_dome;
    const bool stick_deflected = rc_ok && manual_ready && !centered(rc_.channels[kManualDome]);
    if (stick_deflected) {
        if (!manual_active_) {
            // Entry into manual ownership
            manual_active_ = true;
            ++authority_generation_;
            if (seek_active_ || velocity_active_) {
                pushEvent(r2link::EventKind::DomeTakeover, static_cast<uint8_t>(r2link::MessageType::DomeRequest), active_req_seq_, r2link::Detail::None);
                clearActiveRequest();
            }
            drive_seeking_ = false;
            startup_seeking_ = false;
            position_.invalidate();
        }
        owner_ = r2link::DomeOwner::Manual;
        state_ = r2link::DomeState::Manual;
        return;
    }

    if (manual_active_) {
        // Manual stick just released to neutral
        manual_active_ = false;
        owner_ = r2link::DomeOwner::None;
        // If driving, immediately start return to drive reference
        if (auto_dome_on && (drive_intent_ == r2link::DriveIntent::Forward || drive_intent_ == r2link::DriveIntent::Reverse)) {
            drive_seeking_ = false; // will restart in drive section below
        } else {
            state_ = r2link::DomeState::Inhibited;
        }
    }

    // 3. Permitted Driving Alignment
    if (auto_dome_on && !seek_fault_ && !position_.sensorFault() && isHallFresh(now_ms) &&
        profile_ && readiness(*profile_).auto_dome) {

        if (drive_intent_ == r2link::DriveIntent::Pivot) {
            // Pivot suspends remote motion and holds current facing with neutral pulses
            if (seek_active_ || velocity_active_) {
                pushEvent(r2link::EventKind::DomeTakeover, static_cast<uint8_t>(r2link::MessageType::DomeRequest), active_req_seq_, r2link::Detail::None);
                clearActiveRequest();
                ++authority_generation_;
            }
            drive_seeking_ = false;
            startup_seeking_ = false;
            owner_ = r2link::DomeOwner::Drive;
            state_ = r2link::DomeState::HoldingReference;
            return;
        }

        if (drive_intent_ == r2link::DriveIntent::Forward || drive_intent_ == r2link::DriveIntent::Reverse) {
            const r2link::DomeReference target = (drive_intent_ == r2link::DriveIntent::Forward)
                ? r2link::DomeReference::Front : r2link::DomeReference::Rear;
            const uint8_t mask = (target == r2link::DomeReference::Rear) ? 0x02 : 0x01;

            if (seek_active_ || velocity_active_) {
                pushEvent(r2link::EventKind::DomeTakeover, static_cast<uint8_t>(r2link::MessageType::DomeRequest), active_req_seq_, r2link::Detail::None);
                clearActiveRequest();
                ++authority_generation_;
            }
            startup_seeking_ = false;
            owner_ = r2link::DomeOwner::Drive;

            if (hall_.active_mask & mask) {
                // Target sensor is active: hold reference
                drive_seeking_ = false;
                state_ = r2link::DomeState::HoldingReference;
                return;
            }

            // Target sensor not active: start/continue drive seek
            if (!drive_seeking_ || drive_target_ != target) {
                drive_seeking_ = true;
                drive_target_ = target;
                drive_seek_start_ms_ = now_ms;
            }

            if (uint32_t(now_ms - drive_seek_start_ms_) >= 10000) {
                seek_fault_ = true;
                drive_seeking_ = false;
                state_ = r2link::DomeState::Inhibited;
                return;
            }

            state_ = r2link::DomeState::SeekingReference;
            const int8_t dir = position_.seekDirection(drive_target_);
            const int16_t speed = dir * profile_->auto_speed_percent;
            position_.integrate(speed, dt);
            return;
        }
    }

    if (drive_seeking_) {
        drive_seeking_ = false;
    }

    // 4. Startup Auto Dome alignment (stationary)
    if (auto_dome_on && !startup_done_ && !seek_fault_ && !position_.sensorFault() &&
        isHallFresh(now_ms) && drive_intent_ == r2link::DriveIntent::Stationary &&
        profile_ && readiness(*profile_).auto_dome && !seek_active_ && !velocity_active_) {

        if (hall_.active_mask & 0x01) {
            // Front already active: startup complete; the dome is free for idle/event use.
            startup_done_ = true;
            startup_seeking_ = false;
            owner_ = r2link::DomeOwner::None;
            state_ = r2link::DomeState::HoldingReference;
            return;
        }

        if (!startup_seeking_) {
            startup_seeking_ = true;
            startup_seek_start_ms_ = now_ms;
        }

        if (uint32_t(now_ms - startup_seek_start_ms_) >= 10000) {
            seek_fault_ = true;
            startup_seeking_ = false;
            owner_ = r2link::DomeOwner::None;
            state_ = r2link::DomeState::Inhibited;
            return;
        }

        owner_ = r2link::DomeOwner::Startup;
        state_ = r2link::DomeState::SeekingReference;
        const int8_t dir = position_.seekDirection(r2link::DomeReference::Front);
        const int16_t speed = dir * profile_->auto_speed_percent;
        position_.integrate(speed, dt);
        return;
    }

    // 5. Remote Velocity Lease
    if (velocity_active_) {
        if (!auto_dome_on || seek_fault_ || position_.sensorFault() || !isHallFresh(now_ms) ||
            now_ms >= lease_expiry_ms_) {
            // Lease expired or invalid state
            clearActiveRequest();
            owner_ = r2link::DomeOwner::None;
            state_ = r2link::DomeState::Inhibited;
            return;
        }
        owner_ = (active_req_owner_ == 1) ? r2link::DomeOwner::Event : r2link::DomeOwner::Idle;
        state_ = r2link::DomeState::RemoteVelocity;
        if (velocity_speed_ == profile_->auto_speed_percent ||
            velocity_speed_ == -static_cast<int16_t>(profile_->auto_speed_percent)) {
            position_.integrate(velocity_speed_, dt);
        } else {
            position_.invalidate();
        }
        return;
    }

    // 6. Remote Reference Seek
    if (seek_active_) {
        if (!auto_dome_on || seek_fault_ || position_.sensorFault()) {
            clearActiveRequest();
            owner_ = r2link::DomeOwner::None;
            state_ = r2link::DomeState::Inhibited;
            return;
        }

        if (!isHallFresh(now_ms)) {
            pushEvent(r2link::EventKind::HardwareError, static_cast<uint8_t>(r2link::MessageType::DomeRequest), active_req_seq_, r2link::Detail::HallStale);
            seek_fault_ = true;
            clearActiveRequest();
            owner_ = r2link::DomeOwner::None;
            state_ = r2link::DomeState::Inhibited;
            return;
        }

        if (uint32_t(now_ms - seek_start_ms_) >= 10000) {
            pushEvent(r2link::EventKind::Timeout, static_cast<uint8_t>(r2link::MessageType::DomeRequest), active_req_seq_, r2link::Detail::SeekTimeout);
            seek_fault_ = true;
            clearActiveRequest();
            owner_ = r2link::DomeOwner::None;
            state_ = r2link::DomeState::Inhibited;
            return;
        }

        const uint8_t mask = (seek_target_ == r2link::DomeReference::Rear) ? 0x02 : 0x01;
        if (hall_.active_mask & mask) {
            pushEvent(r2link::EventKind::Completed, static_cast<uint8_t>(r2link::MessageType::DomeRequest), active_req_seq_, r2link::Detail::None);
            clearActiveRequest();
            owner_ = (active_req_owner_ == 1) ? r2link::DomeOwner::Event : r2link::DomeOwner::Idle;
            state_ = r2link::DomeState::HoldingReference;
            return;
        }

        owner_ = (active_req_owner_ == 1) ? r2link::DomeOwner::Event : r2link::DomeOwner::Idle;
        state_ = r2link::DomeState::SeekingReference;
        const int8_t dir = position_.seekDirection(seek_target_);
        const int16_t speed = dir * profile_->auto_speed_percent;
        position_.integrate(speed, dt);
        return;
    }

    // 7. Neutral default
    owner_ = r2link::DomeOwner::None;
    if (state_ != r2link::DomeState::HoldingReference) {
        state_ = r2link::DomeState::Inhibited;
    }
}

ServoCommand DomeController::output() const {
    ServoCommand cmd{};
    // An uncommissioned continuous-rotation servo gets no signal at all: an
    // untrimmed "neutral" pulse can creep.
    if (!profile_ || !readiness(*profile_).manual_dome) {
        cmd.pulses = false;
        cmd.pulse_us = 0;
        return cmd;
    }
    cmd.pulses = true;

    if (stopped_ || motion_locks_ != 0) {
        cmd.pulse_us = speedToPulse(0);
        return cmd;
    }

    if (state_ == r2link::DomeState::Manual) {
        const int32_t s = stick(rc_.channels[kManualDome]);
        const int16_t speed = static_cast<int16_t>(s / 10);
        cmd.pulse_us = speedToPulse(speed);
        return cmd;
    }

    if (state_ == r2link::DomeState::SeekingReference) {
        r2link::DomeReference target = r2link::DomeReference::Front;
        if (drive_seeking_) target = drive_target_;
        else if (seek_active_) target = seek_target_;
        const int8_t dir = position_.seekDirection(target);
        cmd.pulse_us = speedToPulse(dir * profile_->auto_speed_percent);
        return cmd;
    }

    if (state_ == r2link::DomeState::RemoteVelocity && velocity_active_) {
        cmd.pulse_us = speedToPulse(velocity_speed_);
        return cmd;
    }

    cmd.pulse_us = speedToPulse(0);
    return cmd;
}

} // namespace body
