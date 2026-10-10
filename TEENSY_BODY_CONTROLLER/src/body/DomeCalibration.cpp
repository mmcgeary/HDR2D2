#include "body/DomeCalibration.h"
#include <algorithm>

namespace body {

namespace {
// Fields of one wheel each automated test depends on: firmware, layout and
// limits for all three, plus the timeout settings or the reversal settings.
const uint8_t kTimeoutIds[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
const uint8_t kDirectionIds[] = {6, 7, 8, 9, 10, 11, 12, 13, 14};
const uint8_t kReversalIds[] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 18};
}

uint32_t DomeCalibration::wheelDigest(const CommissioningProfile& p, uint8_t k, uint8_t w) {
    if (k == 0) return fieldDigest(p, w, kTimeoutIds, sizeof kTimeoutIds);
    if (k == 1) return fieldDigest(p, w, kDirectionIds, sizeof kDirectionIds);
    return fieldDigest(p, w, kReversalIds, sizeof kReversalIds);
}

void DomeCalibration::setWheelReady(uint8_t wheel, bool ready) {
    if (wheel < 2) wheel_ready_[wheel] = ready;
}

void DomeCalibration::updateWheelSample(const VescSample& s, uint32_t now_ms) {
    wheel_sample_ = s;
    if (wheel_test_.busy()) wheel_test_.update(s, now_ms);
}

bool DomeCalibration::wheelTestBusy() const { return wheel_test_.busy(); }
uint8_t DomeCalibration::wheelUnderTest() const { return wheel_; }
WheelTestCommand DomeCalibration::wheelCommand() const { return wheel_test_.command(); }

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

bool DomeCalibration::stationaryGate() const {
    return rc_.valid && rc_.channels[5] < 1250 && isSticksNeutral() && !motion_locked_;
}

uint32_t DomeCalibration::evidenceDigest() const {
    const uint32_t v[] = {profile_.servo_neutral, profile_.servo_min, profile_.servo_max,
                          profile_.auto_speed_percent, profile_.set_mask & 0x0Fu};
    uint32_t h = 2166136261u;
    for (uint32_t x : v) {
        for (int k = 0; k < 4; ++k) h = (h ^ ((x >> (8 * k)) & 0xFFu)) * 16777619u;
    }
    return h;
}

void DomeCalibration::complete() {
    status_.state = static_cast<uint8_t>(CommissionState::Completed);
    if (status_.test < 7) {
        done_run_[status_.test] = status_.run_id;
        done_digest_[status_.test] = evidenceDigest();
    }
}

bool DomeCalibration::evidenceFor(uint8_t bit, uint32_t& run_id, bool& cw, bool& ccw) const {
    const uint32_t digest = evidenceDigest();
    auto done = [&](CommissionTest t) {
        const uint8_t i = static_cast<uint8_t>(t);
        return done_run_[i] != 0 && done_digest_[i] == digest;
    };
    CommissionTest required = CommissionTest::None;
    if (bit == kAcceptServoNeutral) required = CommissionTest::Neutral;
    else if (bit == kAcceptFrontRef) required = CommissionTest::FrontRef;
    else if (bit == kAcceptRearRef) required = CommissionTest::RearRef;
    if (required != CommissionTest::None) {
        run_id = done(required) ? done_run_[static_cast<uint8_t>(required)] : 0;
        return run_id != 0;
    }
    cw = done(CommissionTest::TimingCw);
    ccw = done(CommissionTest::TimingCcw);
    run_id = ccw ? done_run_[static_cast<uint8_t>(CommissionTest::TimingCcw)] : 0;
    return cw && ccw;
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
    const r2link::Result r = handleRequestImpl(req, now_ms);
    refreshStatus();
    return r;
}

void DomeCalibration::setObservedFirmware(uint8_t wheel, bool valid, uint8_t major, uint8_t minor) {
    if (wheel > 1) return;
    observed_fw_[wheel].valid = valid;
    observed_fw_[wheel].major = major;
    observed_fw_[wheel].minor = minor;
}

void DomeCalibration::refreshStatus() {
    const CommissioningProfile& saved = saved_ ? *saved_ : profile_;
    status_.staged_acceptance = static_cast<uint16_t>(profile_.acceptance & 0x0FFFu);
    status_.saved_acceptance = static_cast<uint16_t>(saved.acceptance & 0x0FFFu);
    status_.unsaved = sameProfile(profile_, saved) ? 0 : 1;
}

r2link::Result DomeCalibration::handleRequestImpl(const r2link::CommissionRequest& req, uint32_t now_ms) {
    const CommissionOp op = static_cast<CommissionOp>(req.operation);

    if (op == CommissionOp::Begin) {
        if (motion_locked_) return r2link::Result::Inhibited;
        if (profile_.allow_remote_drive != 0) return r2link::Result::Inhibited;
        if (!rc_.valid || (now_ms < rc_.sample_ms) || (now_ms - rc_.sample_ms > 250)) {
            return r2link::Result::NotReady;
        }
        if (rc_.channels[5] >= 1250) return r2link::Result::Inhibited; // CH6 must be OFF

        const CommissionTest test = static_cast<CommissionTest>(req.test);
        if (test == CommissionTest::None || static_cast<uint8_t>(test) > 8) {
            return r2link::Result::InvalidArgument;
        }

        if (!isSticksNeutral()) return r2link::Result::Inhibited;

        if (static_cast<uint8_t>(test) >= 6) {
            if (req.value != 1 || req.wheel > 1) return r2link::Result::InvalidArgument;   // value 1 = wheel raised
            // Never restart WheelTest while it runs or brakes out after an abort.
            if (status_.state == static_cast<uint8_t>(CommissionState::Running) || wheel_test_.busy())
                return req.run_id == status_.run_id ? r2link::Result::Accepted : r2link::Result::Busy;
            if (!wheel_ready_[req.wheel]) { status_.error = 12; return r2link::Result::NotReady; }
            if (wheel_sample_.erpm <= -WheelTest::kTurningErpm || wheel_sample_.erpm >= WheelTest::kTurningErpm) {
                status_.error = 14;
                return r2link::Result::Inhibited;
            }
            const CommissioningProfile& saved = saved_ ? *saved_ : profile_;
            wheel_ = req.wheel;
            wheel_done_run_[req.test - 6][wheel_] = 0;
            const uint32_t flags = status_.flags;
            const uint8_t saved_flag = status_.saved;
            status_ = r2link::CommissionStatus{};   // acceptance fields are refilled by refreshStatus()
            status_.flags = flags;
            status_.saved = saved_flag;
            status_.run_id = req.run_id;
            status_.test = req.test;
            status_.wheel = wheel_;
            status_.state = static_cast<uint8_t>(CommissionState::Running);
            status_.config_generation = store_.generation();
            keepalive_deadline_ms_ = now_ms + 300;
            wheel_test_.begin(static_cast<WheelTestKind>(req.test), saved.wheel[wheel_].reversal_erpm_limit,
                              saved.wheel[wheel_].reversal_dwell_ms, now_ms);
            return r2link::Result::Accepted;
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

        // Idempotence check (a wheel still braking out also blocks a dome run)
        if (status_.state == static_cast<uint8_t>(CommissionState::Running) || wheel_test_.busy()) {
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
        done_run_[req.test] = 0;
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
        if (status_.state == static_cast<uint8_t>(CommissionState::Running) || wheel_test_.busy()) {
            return r2link::Result::Busy;
        }
        if (!stationaryGate()) return r2link::Result::Inhibited;
        FieldResult fr = setField(profile_, req.field, req.wheel, req.value);
        if (fr == FieldResult::Ok) {
            status_.config_generation = store_.generation();
            return r2link::Result::Accepted;
        }
        return r2link::Result::InvalidArgument;
    }

    if (op == CommissionOp::Save) {
        if (status_.state == static_cast<uint8_t>(CommissionState::Running) || wheel_test_.busy()) {
            return r2link::Result::Busy;
        }
        if (!stationaryGate()) {
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
        if (status_.state == static_cast<uint8_t>(CommissionState::Running) || wheel_test_.busy()) {
            return r2link::Result::Busy;
        }
        if (!stationaryGate()) {
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
            // Evidence is the latest completed run of the matching test(s), gathered
            // against the servo/auto configuration that is staged right now.
            uint32_t run = 0;
            bool cw = false, ccw = false;
            ev.test_completed = evidenceFor(bit, run, cw, ccw);
            ev.commanded_run_id = run;
            ev.observed_run_id = run;
            ev.cw_completed = cw;
            ev.ccw_completed = ccw;
        }
        if (bit == kAcceptVescConfig || bit == kAcceptVescConfig + 1) {
            ev.vesc_operator_observed = true;   // operator compared VESC Tool with the staged record
        } else if (bit >= kAcceptTimeoutBrake && bit < kAcceptBitCount) {
            // A passed automated run whose saved settings still match what is staged.
            const uint8_t k = static_cast<uint8_t>((bit - kAcceptTimeoutBrake) / 2);   // 0 timeout, 1 direction, 2 reversal
            const uint8_t w = bit & 1;
            ev.vesc_operator_observed = wheel_done_run_[k][w] != 0 &&
                                        wheel_done_digest_[k][w] == wheelDigest(profile_, k, w);
        }
        ev.config_digest = acceptanceDigest(profile_, bit);

        AcceptResult ar = acceptBit(profile_, bit, ev);
        if (ar == AcceptResult::Ok) {
            return r2link::Result::Accepted;
        }
        return r2link::Result::Inhibited;
    }

    if (op == CommissionOp::ApplyBaseline) {
        if (status_.state == static_cast<uint8_t>(CommissionState::Running) || wheel_test_.busy())
            return r2link::Result::Busy;
        if (!stationaryGate()) return r2link::Result::Inhibited;
        applyBaseline(profile_, observed_fw_);
        refreshStatus();
        return r2link::Result::Accepted;
    }

    if (op == CommissionOp::Read) {
        return r2link::Result::Accepted;
    }

    return r2link::Result::InvalidArgument;
}

void DomeCalibration::tickWheelTest(uint32_t now_ms) {
    const bool rc_stale = !rc_.valid || (now_ms < rc_.sample_ms) || (now_ms - rc_.sample_ms > 250);
    const bool timed_out = now_ms >= keepalive_deadline_ms_;
    if (motion_locked_ || timed_out || rc_stale || rc_.channels[5] >= 1250 || !isSticksNeutral()) {
        wheel_test_.abort(now_ms);   // brake out; the caller keeps feeding samples until it finishes
        status_.state = static_cast<uint8_t>(timed_out ? CommissionState::TimedOut : CommissionState::Cancelled);
        if (timed_out) status_.error = 1;
        return;
    }
    if (wheel_test_.busy()) return;
    const WheelTestResult& r = wheel_test_.result();
    status_.stop_ms = r.stop_ms;
    status_.peak_current_cA = r.peak_current_cA;
    status_.peak_erpm = r.peak_erpm;
    status_.vesc_fault = r.fault;
    if (wheel_test_.phase() == WheelTestPhase::Passed) {
        const CommissioningProfile& saved = saved_ ? *saved_ : profile_;
        const uint8_t k = static_cast<uint8_t>(status_.test - 6);
        wheel_done_run_[k][wheel_] = status_.run_id;
        wheel_done_digest_[k][wheel_] = wheelDigest(saved, k, wheel_);
        status_.state = static_cast<uint8_t>(CommissionState::Completed);
    } else {
        status_.state = static_cast<uint8_t>(CommissionState::Failed);
        status_.error = r.error;
    }
}

void DomeCalibration::tick(uint32_t now_ms) {
    refreshStatus();
    if (status_.state != static_cast<uint8_t>(CommissionState::Running)) return;

    if (status_.test >= static_cast<uint8_t>(CommissionTest::WheelTimeout)) {
        tickWheelTest(now_ms);
        return;
    }

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
    if (!isSticksNeutral()) {
        cancel(now_ms);
        return;
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

    if (test == CommissionTest::Neutral) {
        if (now_ms - test_start_ms_ >= kNeutralObserveMs) complete();
    } else if (test == CommissionTest::FrontRef) {
        if (front_edge) {
            complete();
        } else if (now_ms >= ref_timeout_deadline_ms_) {
            status_.state = static_cast<uint8_t>(CommissionState::TimedOut);
            status_.error = 4;
        }
    } else if (test == CommissionTest::RearRef) {
        if (rear_edge) {
            complete();
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
                    uint32_t a = status_.revolution_ms[0];
                    uint32_t b = status_.revolution_ms[1];
                    uint32_t c = status_.revolution_ms[2];
                    uint32_t med = (a < b)
                        ? ((b < c) ? b : ((a < c) ? c : a))
                        : ((a < c) ? a : ((b < c) ? c : b));
                    const uint32_t rate = (med > 0) ? 3600000UL / med : 0;
                    // Stage the measured rate; acceptance and Save remain explicit.
                    const uint8_t field = (test == CommissionTest::TimingCw) ? kFieldCwRate : kFieldCcwRate;
                    if (setField(profile_, field, 0, static_cast<int32_t>(rate)) != FieldResult::Ok) {
                        status_.state = static_cast<uint8_t>(CommissionState::Failed);
                        status_.error = 6; // measured rate outside the profile range
                        return;
                    }
                    if (test == CommissionTest::TimingCw) status_.proposed_cw_ddeg_s = static_cast<uint16_t>(rate);
                    if (test == CommissionTest::TimingCcw) status_.proposed_ccw_ddeg_s = static_cast<uint16_t>(rate);
                    complete();
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
    wheel_test_.abort(now_ms);   // no-op unless a wheel test is still commanding
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
            d.known = 1;
        }
    }
    return d;
}

} // namespace body
