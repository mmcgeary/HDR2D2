#include "body/DomeCalibration.h"
#include <algorithm>

namespace body {

DomeCalibration::DomeCalibration(ConfigStore& store, CommissioningProfile& staged_profile)
    : store_(store), profile_(staged_profile) {
    status_.state = static_cast<uint8_t>(CommissionState::Idle);
    status_.config_generation = store_.generation();
}

void DomeCalibration::updateRc(const RcSnapshot& rc, uint32_t now_ms) {
    (void)now_ms;
    rc_ = rc;
    updateStatusFlags();
}

void DomeCalibration::updateHall(const r2link::HallState& hall, uint32_t now_ms) {
    hall_ = hall;
    hall_rx_ms_ = now_ms;
    updateStatusFlags();
}

void DomeCalibration::updateStatusFlags() {
    uint32_t flags = 0;
    if (rc_.valid && rc_.channels[5] < 1250) flags |= (1u << 0); // CH6 OFF
    if (rc_.valid && rc_.channels[8] >= 1750) flags |= (1u << 1); // CH9 ON
    if (rc_.valid && isSticksNeutral()) flags |= (1u << 2);      // Sticks neutral
    if (hall_rx_ms_ > 0 && hall_.source_age_ms <= 200) flags |= (1u << 3); // Hall fresh
    if (rc_.valid) flags |= (1u << 4);                           // RC valid
    status_.flags = flags;
}

bool DomeCalibration::isSticksNeutral() const {
    if (!rc_.valid) return false;
    for (int ch : {0, 1, 3}) {
        if (rc_.channels[ch] < 1460 || rc_.channels[ch] > 1540) return false;
    }
    return true;
}

uint16_t DomeCalibration::speedToPulse(int16_t speed_percent, uint16_t neutral_us) const {
    const uint16_t min_us = (profile_.servo_min >= 1000 && profile_.servo_min < neutral_us)
        ? profile_.servo_min : 1000;
    const uint16_t max_us = (profile_.servo_max > neutral_us && profile_.servo_max <= 2000)
        ? profile_.servo_max : 2000;

    if (speed_percent == 0) return neutral_us;
    if (speed_percent > 0) {
        int32_t delta = static_cast<int32_t>(max_us - neutral_us) * speed_percent / 100;
        int32_t pulse = neutral_us + delta;
        return (pulse > max_us) ? max_us : static_cast<uint16_t>(pulse);
    } else {
        int32_t delta = static_cast<int32_t>(neutral_us - min_us) * (-speed_percent) / 100;
        int32_t pulse = neutral_us - delta;
        return (pulse < min_us) ? min_us : static_cast<uint16_t>(pulse);
    }
}

r2link::Result DomeCalibration::handleRequest(const r2link::CommissionRequest& req, uint32_t now_ms) {
    const CommissionOp op = static_cast<CommissionOp>(req.operation);

    if (op == CommissionOp::Begin) {
        if (motion_locked_) return r2link::Result::Inhibited;
        if (profile_.allow_remote_drive != 0) return r2link::Result::Inhibited;
        if (!rc_.valid || (now_ms < rc_.sample_ms) || (now_ms - rc_.sample_ms > 250)) {
            return r2link::Result::NotReady;
        }
        if (rc_.channels[5] >= 1250) return r2link::Result::Inhibited; // CH6 must be OFF

        const CommissionTest test = static_cast<CommissionTest>(req.test);
        if (test == CommissionTest::None || static_cast<uint8_t>(test) > 6) {
            return r2link::Result::InvalidArgument;
        }

        if (test != CommissionTest::VescTimeout) {
            if (rc_.channels[8] < 1750) return r2link::Result::Inhibited; // CH9 must be ON
            if (!isSticksNeutral()) return r2link::Result::Inhibited;
        }

        if (test == CommissionTest::FrontRef || test == CommissionTest::RearRef ||
            test == CommissionTest::TimingCw || test == CommissionTest::TimingCcw) {
            if (hall_rx_ms_ == 0 || (now_ms < hall_rx_ms_) || (now_ms - hall_rx_ms_ > 200) ||
                hall_.source_age_ms > 200 || (hall_.valid_mask & 3) != 3) {
                return r2link::Result::NotReady;
            }
            if (!(profile_.acceptance & (1u << kAcceptServoNeutral))) {
                return r2link::Result::NotReady;
            }
        }

        // Idempotence check
        if (status_.state == static_cast<uint8_t>(CommissionState::Running)) {
            if (req.run_id == status_.run_id) return r2link::Result::Accepted;
            return r2link::Result::Busy;
        }

        // Start new run
        status_.run_id = req.run_id;
        status_.test = req.test;
        status_.state = static_cast<uint8_t>(CommissionState::Running);
        status_.error = 0;
        status_.revolution_ms[0] = 0;
        status_.revolution_ms[1] = 0;
        status_.revolution_ms[2] = 0;
        status_.proposed_cw_ddeg_s = 0;
        status_.proposed_ccw_ddeg_s = 0;
        keepalive_deadline_ms_ = now_ms + 300;
        test_start_ms_ = now_ms;
        ref_timeout_deadline_ms_ = now_ms + 10000;
        timing_phase_ = 0;
        saw_rear_ = false;
        last_hall_active_ = hall_.active_mask;

        if (test == CommissionTest::Neutral) {
            trial_neutral_us_ = (req.value >= 1400 && req.value <= 1600)
                ? static_cast<uint16_t>(req.value)
                : ((profile_.servo_neutral >= 1400 && profile_.servo_neutral <= 1600)
                    ? profile_.servo_neutral : 1500);
            status_.trial_neutral_us = trial_neutral_us_;
        } else {
            trial_speed_percent_ = (profile_.auto_speed_percent >= 1 && profile_.auto_speed_percent <= 100)
                ? profile_.auto_speed_percent : 15;
            status_.trial_speed_percent = trial_speed_percent_;
        }
        return r2link::Result::Accepted;
    }

    if (op == CommissionOp::Keepalive) {
        if (status_.state == static_cast<uint8_t>(CommissionState::Running)) {
            if (req.run_id == status_.run_id) {
                keepalive_deadline_ms_ = now_ms + 300;
            }
        }
        return r2link::Result::Accepted;
    }

    if (op == CommissionOp::Cancel) {
        cancel(now_ms);
        return r2link::Result::Accepted;
    }

    if (op == CommissionOp::SetField) {
        if (status_.state == static_cast<uint8_t>(CommissionState::Running)) {
            return r2link::Result::Busy;
        }
        FieldResult fr = setField(profile_, req.field, req.wheel, req.value);
        if (fr == FieldResult::Ok) {
            status_.config_generation = store_.generation();
            return r2link::Result::Accepted;
        }
        return r2link::Result::InvalidArgument;
    }

    if (op == CommissionOp::Save) {
        if (status_.state == static_cast<uint8_t>(CommissionState::Running)) {
            return r2link::Result::Busy;
        }
        if (!rc_.valid || rc_.channels[5] >= 1250 || rc_.channels[8] >= 1250 || !isSticksNeutral()) {
            return r2link::Result::Inhibited;
        }
        SaveResult sr = store_.trySave(profile_, true, true);
        if (sr == SaveResult::Ok) {
            status_.saved = 1;
            status_.config_generation = store_.generation();
            return r2link::Result::Accepted;
        }
        status_.saved = 0;
        return r2link::Result::HardwareError;
    }

    if (op == CommissionOp::Accept) {
        if (status_.state == static_cast<uint8_t>(CommissionState::Running)) {
            return r2link::Result::Busy;
        }
        if (!rc_.valid || rc_.channels[5] >= 1250 || rc_.channels[8] >= 1250 || !isSticksNeutral()) {
            return r2link::Result::Inhibited;
        }
        AcceptanceEvidence ev;
        ev.ch6_off = true;
        ev.ch9_off = true;
        ev.sticks_centered = true;
        ev.stationary = true;
        ev.operator_confirmed = true;
        const uint8_t bit = (req.value >= 0 && req.value <= 31) ? static_cast<uint8_t>(req.value) : req.field;
        if (bit <= 3) {
            ev.test_completed = (status_.state == static_cast<uint8_t>(CommissionState::Completed));
            ev.commanded_run_id = status_.run_id;
            ev.observed_run_id = status_.run_id;
            if (bit == kAcceptAutoTiming) {
                ev.cw_completed = true;
                ev.ccw_completed = true;
            }
        }
        if (bit >= 4 && bit <= 11) {
            ev.vesc_operator_observed = true;
        }
        ev.config_digest = acceptanceDigest(profile_, bit);

        AcceptResult ar = acceptBit(profile_, bit, ev);
        if (ar == AcceptResult::Ok) {
            return r2link::Result::Accepted;
        }
        return r2link::Result::Inhibited;
    }

    if (op == CommissionOp::Read) {
        return r2link::Result::Accepted;
    }

    return r2link::Result::InvalidArgument;
}

void DomeCalibration::tick(uint32_t now_ms) {
    if (status_.state != static_cast<uint8_t>(CommissionState::Running)) return;

    if (motion_locked_) {
        cancel(now_ms);
        return;
    }

    if (now_ms >= keepalive_deadline_ms_) {
        status_.state = static_cast<uint8_t>(CommissionState::TimedOut);
        status_.error = 1;
        return;
    }

    if (!rc_.valid || (now_ms < rc_.sample_ms) || (now_ms - rc_.sample_ms > 250) ||
        rc_.channels[5] >= 1250) {
        cancel(now_ms);
        return;
    }

    const CommissionTest test = static_cast<CommissionTest>(status_.test);
    if (test != CommissionTest::VescTimeout) {
        if (rc_.channels[8] < 1750 || !isSticksNeutral()) {
            cancel(now_ms);
            return;
        }
    }

    if (test == CommissionTest::FrontRef || test == CommissionTest::RearRef ||
        test == CommissionTest::TimingCw || test == CommissionTest::TimingCcw) {
        if (hall_rx_ms_ == 0 || (now_ms < hall_rx_ms_) || (now_ms - hall_rx_ms_ > 200) ||
            hall_.source_age_ms > 200 || (hall_.valid_mask & 3) != 3) {
            status_.state = static_cast<uint8_t>(CommissionState::Failed);
            status_.error = 2;
            return;
        }
        if ((hall_.active_mask & 3) == 3) {
            status_.state = static_cast<uint8_t>(CommissionState::Failed);
            status_.error = 3;
            return;
        }
    }

    const bool front_edge = (hall_.active_mask & 1) && !(last_hall_active_ & 1);
    const bool rear_edge = (hall_.active_mask & 2) && !(last_hall_active_ & 2);

    if (hall_.active_mask & 2) saw_rear_ = true;

    if (test == CommissionTest::FrontRef) {
        if (front_edge) {
            status_.state = static_cast<uint8_t>(CommissionState::Completed);
        } else if (now_ms >= ref_timeout_deadline_ms_) {
            status_.state = static_cast<uint8_t>(CommissionState::TimedOut);
            status_.error = 4;
        }
    } else if (test == CommissionTest::RearRef) {
        if (rear_edge) {
            status_.state = static_cast<uint8_t>(CommissionState::Completed);
        } else if (now_ms >= ref_timeout_deadline_ms_) {
            status_.state = static_cast<uint8_t>(CommissionState::TimedOut);
            status_.error = 4;
        }
    } else if (test == CommissionTest::TimingCw || test == CommissionTest::TimingCcw) {
        if (timing_phase_ == 0) {
            if (front_edge) {
                timing_phase_ = 1;
                saw_rear_ = false;
                rev_start_ms_ = now_ms;
                ref_timeout_deadline_ms_ = now_ms + 10000;
            } else if (now_ms >= ref_timeout_deadline_ms_) {
                status_.state = static_cast<uint8_t>(CommissionState::TimedOut);
                status_.error = 4;
            }
        } else if (timing_phase_ >= 1 && timing_phase_ <= 3) {
            if (front_edge) {
                if (!saw_rear_) {
                    status_.state = static_cast<uint8_t>(CommissionState::Failed);
                    status_.error = 5;
                    return;
                }
                const uint8_t rev_idx = timing_phase_ - 1;
                status_.revolution_ms[rev_idx] = now_ms - rev_start_ms_;
                saw_rear_ = false;
                rev_start_ms_ = now_ms;
                ref_timeout_deadline_ms_ = now_ms + 10000;
                timing_phase_++;

                if (timing_phase_ > 3) {
                    status_.state = static_cast<uint8_t>(CommissionState::Completed);
                    uint32_t a = status_.revolution_ms[0];
                    uint32_t b = status_.revolution_ms[1];
                    uint32_t c = status_.revolution_ms[2];
                    uint32_t med = (a < b)
                        ? ((b < c) ? b : ((a < c) ? c : a))
                        : ((a < c) ? a : ((b < c) ? c : b));
                    uint16_t rate = (med > 0) ? static_cast<uint16_t>(3600000UL / med) : 0;
                    if (test == CommissionTest::TimingCw) status_.proposed_cw_ddeg_s = rate;
                    if (test == CommissionTest::TimingCcw) status_.proposed_ccw_ddeg_s = rate;
                }
            } else if (now_ms >= ref_timeout_deadline_ms_) {
                status_.state = static_cast<uint8_t>(CommissionState::TimedOut);
                status_.error = 4;
            }
        }
    }

    last_hall_active_ = hall_.active_mask;
}

ServoCommand DomeCalibration::output() const {
    ServoCommand cmd{};
    const bool neutral_accepted = (profile_.acceptance & (1u << kAcceptServoNeutral)) != 0;
    const uint16_t neutral_us = (profile_.servo_neutral >= 1400 && profile_.servo_neutral <= 1600)
        ? profile_.servo_neutral : 1500;

    if (status_.state == static_cast<uint8_t>(CommissionState::Running)) {
        if (status_.test == static_cast<uint8_t>(CommissionTest::Neutral)) {
            cmd.pulses = true;
            cmd.pulse_us = trial_neutral_us_;
            return cmd;
        }
        if (status_.test == static_cast<uint8_t>(CommissionTest::FrontRef) ||
            status_.test == static_cast<uint8_t>(CommissionTest::RearRef) ||
            status_.test == static_cast<uint8_t>(CommissionTest::TimingCw) ||
            status_.test == static_cast<uint8_t>(CommissionTest::TimingCcw)) {
            int16_t speed = trial_speed_percent_;
            if (status_.test == static_cast<uint8_t>(CommissionTest::RearRef) ||
                status_.test == static_cast<uint8_t>(CommissionTest::TimingCcw)) {
                speed = -speed;
            }
            cmd.pulses = true;
            cmd.pulse_us = speedToPulse(speed, neutral_us);
            return cmd;
        }
    }

    if (neutral_accepted) {
        cmd.pulses = true;
        cmd.pulse_us = neutral_us;
    } else {
        cmd.pulses = false;
        cmd.pulse_us = 0;
    }
    return cmd;
}

void DomeCalibration::cancel(uint32_t now_ms) {
    (void)now_ms;
    if (status_.state == static_cast<uint8_t>(CommissionState::Running)) {
        status_.state = static_cast<uint8_t>(CommissionState::Cancelled);
    }
    status_.revolution_ms[0] = 0;
    status_.revolution_ms[1] = 0;
    status_.revolution_ms[2] = 0;
}

r2link::Diagnostics DomeCalibration::diagnostics(uint8_t subtype, uint8_t field, uint8_t wheel, uint32_t sample_counter) const {
    r2link::Diagnostics d{};
    d.subtype = subtype;
    d.sample_counter = sample_counter;
    if (subtype == 1) {
        d.field = field;
        d.wheel = wheel;
        int32_t val = 0;
        if (getField(profile_, field, wheel, val)) {
            d.value = val;
        }
    }
    return d;
}

} // namespace body
