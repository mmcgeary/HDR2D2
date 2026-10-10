#include "body/DriveController.h"

namespace body {
namespace {
int32_t magnitude(int32_t v) { return v < 0 ? int32_t(-int64_t(v)) : v; }
int8_t sign(int32_t v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
bool centered(uint16_t v) { return v >= 1460 && v <= 1540; }
int32_t stick(uint16_t us) {
    if (centered(us)) return 0;
    if (us < 1000) us = 1000;
    if (us > 2000) us = 2000;
    if (us > 1540) return (int32_t(us) - 1540) * 1000 / 460;
    return (int32_t(us) - 1460) * 1000 / 460;
}
bool freshRc(const RcSnapshot& rc, uint32_t now) {
    return rc.valid && uint32_t(now - rc.sample_ms) <= 250;
}
bool healthy(const VescSample& s, const WheelProfile& p, uint8_t wheel, uint32_t now) {
    return s.wheel == wheel && s.valid && s.profile_match && !s.stale &&
        !s.unsupported && (s.valid_fields & 0x49) == 0x49 && !s.fault &&
        uint32_t(now - s.sample_ms) <= 500 && s.source_age_ms <= 500 &&
        s.fw_major == p.fw_major && s.fw_minor == p.fw_minor &&
        s.pack_cV >= p.undervoltage_cv && s.pack_cV <= p.overvoltage_cv;
}
bool lowSpeed(const VescSample& s, const WheelProfile& p) {
    return int64_t(s.erpm) >= -int64_t(p.reversal_erpm_limit) &&
           int64_t(s.erpm) <= p.reversal_erpm_limit;
}
r2link::DriveIntent facing(int32_t l, int32_t r) {
    if (!l && !r) return r2link::DriveIntent::Stationary;
    if (l + r > 0) return r2link::DriveIntent::Forward;
    if (l + r < 0) return r2link::DriveIntent::Reverse;
    return r2link::DriveIntent::Pivot;
}
uint32_t driveDigest(const CommissioningProfile& p) {
    uint32_t h = p.duty_slew_permille_per_s;
    for (uint8_t b = kAcceptVescConfig; b < kAcceptBitCount; ++b)
        h = (h ^ acceptanceDigest(p,b)) * 16777619u;
    return h;
}
}

MixedDuty mixDrive(uint16_t throttle, uint16_t steer, uint16_t rate) {
    const int32_t t = stick(throttle), s = stick(steer);
    int32_t l = t + s, r = t - s;
    int32_t divisor = 1000;
    if (magnitude(l) > divisor) divisor = magnitude(l);
    if (magnitude(r) > divisor) divisor = magnitude(r);
    if (rate > 1000) rate = 1000;
    MixedDuty out = {int16_t(l * rate / divisor), int16_t(r * rate / divisor)};
    return out;
}

DriveController::DriveController()
    : wheel_{}, commands_{}, state_(r2link::DriveState::Boot),
      intent_(r2link::DriveIntent::Stationary), locks_(0), epoch_(0),
      stopped_(false), observed_off_(false), neutral_started_(false),
      ticked_(false), off_started_(false), neutral_ms_(0), last_command_ms_(0),
      last_update_ms_(0), now_ms_(0), off_ms_(0), deadline_misses_(0),
      command_revision_(0), rc_sample_ms_(0), profile_digest_(0), profile_seen_(false) {}

ReversalState DriveController::reversalState(uint8_t w) const {
    return w < 2 ? wheel_[w].state : ReversalState::Braking;
}
void DriveController::braking() {
    WheelCommand* c[2] = {&commands_.left, &commands_.right};
    for (uint8_t w = 0; w < 2; ++w) {
        const WheelMode mode = c[w]->brake_mA ? WheelMode::Brake : WheelMode::Disabled;
        if (c[w]->mode != mode || c[w]->duty_permille) ++command_revision_;
        c[w]->mode = mode;
        c[w]->duty_permille = 0;
        wheel_[w].magnitude_milli = 0;
        wheel_[w].low_started = false;
        wheel_[w].state = ReversalState::Tracking;
    }
    intent_ = r2link::DriveIntent::Stationary;
}
void DriveController::disarm(r2link::DriveState state) {
    state_ = state;
    observed_off_ = neutral_started_ = false;
    // Retain powered sign until a new 500ms neutral + known-low-speed arm.
    braking();
}
void DriveController::stop(uint32_t now) {
    if (!stopped_) { stopped_ = true; ++epoch_; }
    off_started_ = false;
    now_ms_ = now;
    disarm(r2link::DriveState::Locked);
}
r2link::Result DriveController::setMotionLocks(uint8_t reasons) {
    if (reasons & ~uint8_t(0x05)) return r2link::Result::InvalidArgument;
    if (locks_ == reasons) return r2link::Result::Accepted;
    if ((locks_ & ~reasons) && (!off_started_ || uint32_t(now_ms_ - off_ms_) < 500))
        return r2link::Result::NotReady;
    locks_ = reasons; ++epoch_;
    disarm(locks_ || stopped_ ? r2link::DriveState::Locked : r2link::DriveState::Disarmed);
    return r2link::Result::Accepted;
}
r2link::Result DriveController::releaseStop(uint16_t epoch, uint32_t now) {
    if (epoch != epoch_) return r2link::Result::WrongEpoch;
    if (!ticked_ || uint32_t(now - rc_sample_ms_) > 250 ||
        uint32_t(now - last_update_ms_) > 250 ||
        !off_started_ || uint32_t(now_ms_ - off_ms_) < 500)
        return r2link::Result::NotReady;
    if (stopped_) {
        stopped_ = false; ++epoch_;
        disarm(locks_ ? r2link::DriveState::Locked : r2link::DriveState::Disarmed);
    }
    return r2link::Result::Accepted;
}
r2link::Result DriveController::submitRemote(const r2link::DriveRequest& r, uint32_t) {
    if (!r.lease_ms || r.lease_ms > 150 || r.left_permille < -1000 ||
        r.left_permille > 1000 || r.right_permille < -1000 || r.right_permille > 1000)
        return r2link::Result::InvalidArgument;
    // validateProfile rejects allow_remote_drive!=0 in this release.
    return r2link::Result::Inhibited;
}

int16_t DriveController::permit(Wheel& w, int16_t target, const VescSample& s,
                               const WheelProfile& p, uint32_t /* now */) {
    if (!target) {
        w.magnitude_milli = 0; w.low_started = false;
        w.state = ReversalState::Tracking;
        return 0;
    }
    const int8_t desired = sign(target);
    const int8_t measured = sign(s.erpm) * p.direction;
    const bool opposing = !lowSpeed(s, p) && measured != desired;
    if ((w.prior_sign && desired != w.prior_sign) || opposing) {
        w.magnitude_milli = 0;
        w.state = ReversalState::Braking;
        if (!lowSpeed(s, p)) { w.low_started = false; return 0; }
        // Only actual new measurements can finish a dwell. A cached sample
        // cannot acquire time merely because update is called repeatedly.
        const bool new_sample = !w.low_started || s.sample_ms != w.sample_ms;
        if (new_sample) {
            const bool changed_sign = measured && w.low_sign && measured != w.low_sign;
            if (!w.low_started || uint32_t(s.sample_ms - w.sample_ms) > 500 || changed_sign) {
                w.low_started = true; w.low_ms = s.sample_ms; w.low_sign = measured;
            } else if (measured) w.low_sign = measured;
            w.sample_ms = s.sample_ms;
            w.state = ReversalState::Qualifying;
            if (uint32_t(s.sample_ms - w.low_ms) >= p.reversal_dwell_ms) {
                w.prior_sign = desired; w.low_started = false;
                w.ramp_reset = true;
                w.state = ReversalState::Tracking;
                return target;
            }
        } else w.state = ReversalState::Qualifying;
        return 0;
    }
    w.low_started = false; w.state = ReversalState::Tracking;
    return target;
}
void DriveController::output(Wheel& w, WheelCommand& c, int16_t target,
                             int8_t direction, uint16_t slew, uint32_t dt) {
    if (!target) {
        c.mode = WheelMode::Brake; c.duty_permille = 0; w.magnitude_milli = 0;
        return;
    }
    const int32_t cap = magnitude(target) * 1000;
    if (w.ramp_reset) { dt = 0; w.ramp_reset = false; }
    // dt is bounded to one control period; stalled time never becomes a jump.
    // Partial reductions ramp down at the same commissioned slew: a duty step
    // down is hard regenerative braking, which can tip a tall droid. A centred
    // stick (target 0) still brakes at once, limited by the brake current.
    const int32_t step = int32_t(slew) * int32_t(dt);
    if (w.magnitude_milli > cap) {
        w.magnitude_milli -= step;
        if (w.magnitude_milli < cap) w.magnitude_milli = cap;
    } else {
        w.magnitude_milli += step;
        if (w.magnitude_milli > cap) w.magnitude_milli = cap;
    }
    c.duty_permille = int16_t(w.magnitude_milli / 1000 * sign(target) * direction);
    c.mode = c.duty_permille ? WheelMode::Duty : WheelMode::Brake;
    if (c.duty_permille) w.prior_sign = sign(target);
}

void DriveController::update(const RcSnapshot& rc, const VescSample& left,
                            const VescSample& right, const CommissioningProfile& p,
                            uint32_t now) {
    const bool ready = readiness(p).drive;
    const uint32_t digest = driveDigest(p);
    const bool changed_profile = profile_seen_ && profile_digest_ != digest;
    profile_seen_ = true; profile_digest_ = digest;
    if (commands_.left.brake_mA != (ready ? p.wheel[0].brake_ma : 0) ||
        commands_.right.brake_mA != (ready ? p.wheel[1].brake_ma : 0))
        ++command_revision_;
    commands_.left.brake_mA = ready ? p.wheel[0].brake_ma : 0;
    commands_.right.brake_mA = ready ? p.wheel[1].brake_ma : 0;
    const bool rc_ok = freshRc(rc, now);
    rc_sample_ms_ = rc.sample_ms;
    const bool gap = ticked_ && uint32_t(now - last_update_ms_) > 250;
    now_ms_ = now;
    // Release gate is independent of telemetry, but never of fresh local RC.
    if (rc_ok && !gap && rc.channels[kFeetEnable] <= 1250 &&
        centered(rc.channels[kThrottle]) && centered(rc.channels[kSteering]) &&
        centered(rc.channels[kManualDome])) {
        if (!off_started_) { off_started_ = true; off_ms_ = now; }
    } else off_started_ = false;
    const uint32_t elapsed = ticked_ ? uint32_t(now - last_command_ms_) : 0;
    const bool due = !ticked_ || elapsed >= 20;
    const bool missed = state_ == r2link::DriveState::Armed && elapsed > 50;
    ticked_ = true; last_update_ms_ = now;
    if (missed) {
        ++deadline_misses_;
        ++command_revision_;
        last_command_ms_ = now;
        disarm(r2link::DriveState::Fault);
        return;
    }
    if (due) { last_command_ms_ = now; ++command_revision_; }
    if (locks_ || stopped_) { disarm(r2link::DriveState::Locked); return; }
    if (changed_profile) { disarm(r2link::DriveState::Fault); return; }
    if (gap) disarm(r2link::DriveState::Fault);
    if (!ready || !rc_ok || !healthy(left,p.wheel[0],0,now) ||
        !healthy(right,p.wheel[1],1,now)) {
        disarm(r2link::DriveState::Fault); return;
    }
    const uint16_t enable = rc.channels[kFeetEnable];
    if (enable <= 1250) {
        disarm(r2link::DriveState::Disarmed); observed_off_ = true; return;
    }
    if (enable < 1750) { disarm(r2link::DriveState::Disarmed); return; }
    if (state_ != r2link::DriveState::Armed) {
        braking();
        if (!observed_off_) { state_ = r2link::DriveState::Disarmed; return; }
        if (!centered(rc.channels[kThrottle]) || !centered(rc.channels[kSteering]) ||
            !lowSpeed(left,p.wheel[0]) || !lowSpeed(right,p.wheel[1])) {
            neutral_started_ = false; state_ = r2link::DriveState::Disarmed; return;
        }
        if (!neutral_started_) { neutral_started_ = true; neutral_ms_ = now; }
        state_ = r2link::DriveState::Qualifying;
        if (uint32_t(now - neutral_ms_) >= 500) {
            state_ = r2link::DriveState::Armed;
            wheel_[0] = Wheel{}; wheel_[1] = Wheel{};
            last_command_ms_ = now;
        }
        return;
    }
    const uint16_t rate_us = rc.channels[kDutyRate];
    const uint16_t rate = rate_us <= 1250 ? 350 : rate_us >= 1750 ? 1000 : 700;
    MixedDuty target = mixDrive(rc.channels[kThrottle], rc.channels[kSteering], rate);
    if (target.left > 950) target.left = 950;
    if (target.left < -950) target.left = -950;
    if (target.right > 950) target.right = 950;
    if (target.right < -950) target.right = -950;
    const int16_t l = permit(wheel_[0],target.left,left,p.wheel[0],now);
    const int16_t r = permit(wheel_[1],target.right,right,p.wheel[1],now);
    intent_ = facing(l,r);
    if (!l && !r && (target.left || target.right))
        intent_ = facing(wheel_[0].prior_sign,wheel_[1].prior_sign);
    WheelCommand* c[2] = {&commands_.left,&commands_.right};
    const int16_t permitted[2] = {l,r};
    for (uint8_t w = 0; w < 2; ++w) {
        const WheelMode before = c[w]->mode;
        const int16_t duty_before = c[w]->duty_permille;
        if (!permitted[w] || elapsed >= 20)
            output(wheel_[w],*c[w],permitted[w],p.wheel[w].direction,
                   p.duty_slew_permille_per_s, elapsed >= 20 ? 20 : 0);
        if (c[w]->mode != before || c[w]->duty_permille != duty_before)
            ++command_revision_;
    }
}

void applyWheelCommands(const WheelCommands& c, VescLink& left, VescLink& right) {
    VescLink* link[2] = {&left,&right};
    const WheelCommand* command[2] = {&c.left,&c.right};
    for (uint8_t w = 0; w < 2; ++w) {
        if (command[w]->mode == WheelMode::Duty) link[w]->setDuty(command[w]->duty_permille);
        else if (command[w]->mode == WheelMode::Brake) link[w]->setBrake(command[w]->brake_mA);
        else link[w]->disableControl();
    }
}
} // namespace body
