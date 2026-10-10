#include "DomeBehaviour.h"

DomeBehaviour::DomeBehaviour(IDomeRequestSink& sink, IDomeRandom& random, int16_t auto_speed_percent)
    : sink_(sink), random_(random), auto_speed_percent_(auto_speed_percent) {}

void DomeBehaviour::resetToIdle(uint32_t now_ms) {
    state_ = State::WaitingIdle;
    req_pending_ = false;
    pause_after_sweep_ = false;
    pause_start_ms_ = 0;
    pause_duration_ms_ = 0;
    last_activity_ms_ = (now_ms != 0) ? now_ms : last_now_ms_;
}

void DomeBehaviour::onPeerLost(uint32_t now_ms) {
    last_now_ms_ = now_ms;
    resetToIdle(now_ms);
}

void DomeBehaviour::onReply(uint16_t sequence, r2link::Result result) {
    if (!req_pending_ || sequence != active_seq_) return;
    if (result == r2link::Result::Inhibited) {
        fault_ = true;
        resetToIdle(0);
    } else if (result != r2link::Result::Accepted) {
        resetToIdle(0);
    }
}

void DomeBehaviour::onEvent(const r2link::Event& ev) {
    const bool match_type = (ev.request_type == 0 ||
                             ev.request_type == uint8_t(r2link::MessageType::DomeRequest));
    if (match_type && req_pending_ && (ev.request_seq == 0 || ev.request_seq == active_seq_)) {
        if (ev.kind == uint8_t(r2link::EventKind::Completed)) {
            req_pending_ = false;
            if (state_ == State::Referencing || state_ == State::Returning) {
                pause_after_sweep_ = false;
                pause_duration_ms_ = static_cast<uint32_t>(random_.pick(2000, 6001));
                pause_start_ms_ = last_now_ms_;
                state_ = State::Pausing;
            }
        } else if (ev.kind == uint8_t(r2link::EventKind::Timeout) ||
                   ev.kind == uint8_t(r2link::EventKind::HardwareError)) {
            fault_ = true;
            resetToIdle(0);
        } else if (ev.kind == uint8_t(r2link::EventKind::Cancelled) ||
                   ev.kind == uint8_t(r2link::EventKind::DomeTakeover)) {
            resetToIdle(0);
        }
    } else if (ev.kind == uint8_t(r2link::EventKind::DomeTakeover)) {
        resetToIdle(0);
    }
}

void DomeBehaviour::tick(const DomeBehaviourInput& input, uint32_t now_ms) {
    last_now_ms_ = now_ms;
    const bool auto_dome_on = input.rc_fresh && (input.rc.channels[8] >= 1750);

    if (!initialized_) {
        initialized_ = true;
        last_generation_ = input.status.dome_authority_generation;
        auto_dome_prior_on_ = auto_dome_on;
    } else {
        if (auto_dome_on && !auto_dome_prior_on_) {
            fault_ = false;
            last_activity_ms_ = now_ms;
        } else if (!auto_dome_on && auto_dome_prior_on_) {
            resetToIdle(now_ms);
        }
        auto_dome_prior_on_ = auto_dome_on;
    }

    if (input.status_fresh && input.status.dome_authority_generation != last_generation_) {
        last_generation_ = input.status.dome_authority_generation;
        if (state_ != State::WaitingIdle) {
            resetToIdle(now_ms);
            return;
        }
    }

    if (!input.rc_fresh || !input.status_fresh || !auto_dome_on ||
        ((input.status.profile_ready & 4) == 0) ||
        (input.status.lock_reasons != 0) ||
        (input.status.drive_state == uint8_t(r2link::DriveState::Locked)) ||
        fault_) {
        if (state_ != State::WaitingIdle) {
            resetToIdle(now_ms);
        } else {
            last_activity_ms_ = now_ms;
        }
        return;
    }

    const bool stick_deflected = (input.rc.channels[0] < 1460 || input.rc.channels[0] > 1540) ||
                                 (input.rc.channels[1] < 1460 || input.rc.channels[1] > 1540) ||
                                 (input.rc.channels[3] < 1460 || input.rc.channels[3] > 1540);
    const bool drive_active = (input.status.drive_intent != uint8_t(r2link::DriveIntent::Stationary));
    const bool dome_takeover = (input.status.dome_owner == uint8_t(r2link::DomeOwner::Manual)) ||
                               (input.status.dome_owner == uint8_t(r2link::DomeOwner::Drive)) ||
                               (input.status.dome_owner == uint8_t(r2link::DomeOwner::Event)) ||
                               (input.status.dome_owner == uint8_t(r2link::DomeOwner::Startup));
    const bool active = input.event_active || stick_deflected || drive_active || dome_takeover;

    if (active) {
        if (state_ != State::WaitingIdle) {
            resetToIdle(now_ms);
        } else {
            last_activity_ms_ = now_ms;
        }
        return;
    }

    switch (state_) {
    case State::WaitingIdle: {
        if (uint32_t(now_ms - last_activity_ms_) >= 20000) {
            r2link::DomeRequest req{};
            req.operation = uint8_t(r2link::DomeOperation::SeekReference);
            req.reference = uint8_t(r2link::DomeReference::Front);
            req.owner = r2link::kDomeRequestOwnerIdle;
            req.control_epoch = input.status.control_epoch;
            req.dome_authority_generation = input.status.dome_authority_generation;
            req.speed_percent = 0;
            req.lease_ms = 0;
            uint16_t seq = 0;
            if (sink_.submit(req, now_ms, seq)) {
                active_seq_ = seq;
                req_pending_ = true;
                state_ = State::Referencing;
            }
        }
        break;
    }

    case State::Referencing:
    case State::Returning: {
        break;
    }

    case State::Pausing: {
        if (pause_start_ms_ == 0) {
            pause_start_ms_ = now_ms;
        }
        if (uint32_t(now_ms - pause_start_ms_) >= pause_duration_ms_) {
            if (pause_after_sweep_) {
                pause_after_sweep_ = false;
                r2link::DomeRequest req{};
                req.operation = uint8_t(r2link::DomeOperation::SeekReference);
                req.reference = uint8_t(r2link::DomeReference::Front);
                req.owner = r2link::kDomeRequestOwnerIdle;
                req.control_epoch = input.status.control_epoch;
                req.dome_authority_generation = input.status.dome_authority_generation;
                req.speed_percent = 0;
                req.lease_ms = 0;
                uint16_t seq = 0;
                if (sink_.submit(req, now_ms, seq)) {
                    active_seq_ = seq;
                    req_pending_ = true;
                    state_ = State::Returning;
                }
            } else {
                if (!input.status.angle_valid) {
                    resetToIdle(now_ms);
                    return;
                }
                int32_t target = random_.pick(-450, 451);
                int attempts = 0;
                while (target == input.status.estimated_angle_ddeg && attempts++ < 10) {
                    target = random_.pick(-450, 451);
                }
                target_angle_ddeg_ = static_cast<int16_t>(target);
                state_ = State::Sweeping;

                const int16_t current = input.status.estimated_angle_ddeg;
                const int16_t speed = (target_angle_ddeg_ > current) ? auto_speed_percent_ : -auto_speed_percent_;
                r2link::DomeRequest req{};
                req.operation = uint8_t(r2link::DomeOperation::Velocity);
                req.speed_percent = speed;
                req.lease_ms = 100;
                req.owner = r2link::kDomeRequestOwnerIdle;
                req.control_epoch = input.status.control_epoch;
                req.dome_authority_generation = input.status.dome_authority_generation;
                req.reference = 0;
                uint16_t seq = 0;
                if (sink_.submit(req, now_ms, seq)) {
                    active_seq_ = seq;
                    req_pending_ = true;
                    last_lease_ms_ = now_ms;
                }
            }
        }
        break;
    }

    case State::Sweeping: {
        if (!input.status.angle_valid) {
            resetToIdle(now_ms);
            return;
        }

        const int16_t current = input.status.estimated_angle_ddeg;
        const bool reached = (target_angle_ddeg_ > 0 && current >= target_angle_ddeg_) ||
                             (target_angle_ddeg_ < 0 && current <= target_angle_ddeg_) ||
                             (current == target_angle_ddeg_);

        if (reached) {
            pause_after_sweep_ = true;
            pause_duration_ms_ = static_cast<uint32_t>(random_.pick(2000, 6001));
            pause_start_ms_ = now_ms;
            req_pending_ = false;
            state_ = State::Pausing;
        } else {
            if (uint32_t(now_ms - last_lease_ms_) >= 50) {
                const int16_t speed = (target_angle_ddeg_ > current) ? auto_speed_percent_ : -auto_speed_percent_;
                r2link::DomeRequest req{};
                req.operation = uint8_t(r2link::DomeOperation::Velocity);
                req.speed_percent = speed;
                req.lease_ms = 100;
                req.owner = r2link::kDomeRequestOwnerIdle;
                req.control_epoch = input.status.control_epoch;
                req.dome_authority_generation = input.status.dome_authority_generation;
                req.reference = 0;
                uint16_t seq = 0;
                if (sink_.submit(req, now_ms, seq)) {
                    active_seq_ = seq;
                    req_pending_ = true;
                    last_lease_ms_ = now_ms;
                }
            }
        }
        break;
    }
    }
}
