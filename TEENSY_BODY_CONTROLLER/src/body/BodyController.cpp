#include "body/BodyController.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace body {

BodyController::BodyController(ConfigStore& config_store,
                               r2link::BytePort& link_port,
                               r2link::BytePort& left_vesc_port,
                               r2link::BytePort& right_vesc_port,
                               r2link::BytePort& audio_port,
                               uint32_t local_session)
    : config_store_(config_store),
      link_port_(link_port),
      link_endpoint_(link_port, r2link::kRoleBody, local_session),
      left_vesc_(left_vesc_port, 0),
      right_vesc_(right_vesc_port, 1),
      drive_(),
      dome_(active_),
      calibration_(config_store_, profile_),
      audio_(audio_port, kTrackCatalog, kTrackCatalogCount) {}

void BodyController::init(uint32_t now_ms) {
    uint32_t session = 0;
    if (config_store_.nextBootSession(session)) {
        link_endpoint_.setLocalSession(session);
    } else {
        link_endpoint_.setLocalSession(0);
    }
    CommissioningProfile loaded;
    if (config_store_.load(loaded) == ConfigResult::Ready) {
        profile_ = loaded;
    }
    activateSavedProfile();
    updateStatus(now_ms);  // the DFPlayer is reset and configured by audio_.tick()
}

void BodyController::activateSavedProfile() {
    active_ = profile_;
    left_vesc_.setProfile(VescProfile::fromSaved(active_, 0));
    right_vesc_.setProfile(VescProfile::fromSaved(active_, 1));
}

bool BodyController::releaseGateOpen(uint32_t now_ms) const {
    const bool rc_fresh = rc_snapshot_.valid && (now_ms >= rc_snapshot_.sample_ms) &&
                          (now_ms - rc_snapshot_.sample_ms <= 250);
    const bool ch6_off = rc_snapshot_.channels[5] < 1250;
    const bool centered_500ms = (centered_start_ms_ > 0) && (now_ms >= centered_start_ms_) &&
                                (now_ms - centered_start_ms_ >= 500);
    return rc_fresh && ch6_off && centered_500ms;
}

void BodyController::publishStatusNow(uint32_t now_ms) {
    updateStatus(now_ms);
    r2link::Frame frame{};
    r2link::ErrorCounters err{};
    if (r2link::encode(body_status_, frame, err) == r2link::Status::Ok) {
        link_endpoint_.publishLatest(frame, now_ms);
    }
}

void BodyController::engageLocks(uint8_t add, uint32_t now_ms) {
    lock_reasons_ |= add;
    if (add & 1) {
        drive_.stop(now_ms);
        dome_.stop(now_ms);
    }
    drive_.setMotionLocks(lock_reasons_ & 4);
    dome_.setMotionLocks(lock_reasons_ & 4);
    calibration_.setMotionLocked(true);
    calibration_.cancel(now_ms);
    ++control_epoch_;
    publishStatusNow(now_ms);
}

r2link::Result BodyController::releaseLocks(uint8_t clear, uint32_t now_ms) {
    const uint8_t current = lock_reasons_ | drive_.motionLocks();
    const uint8_t next = current & ~clear;
    if (next == current) return r2link::Result::Accepted;
    if (!releaseGateOpen(now_ms)) return r2link::Result::NotReady;

    // Subsystem latches are released with their own epochs: this request's epoch
    // was already validated against control_epoch_ by the caller.
    r2link::Result r = drive_.setMotionLocks(next & 4);
    if (r == r2link::Result::Accepted && !(next & 1) && drive_.stopLatched()) {
        r = drive_.releaseStop(drive_.controlEpoch(), now_ms);
    }
    // The drive's latches are the motion ground truth; the dome follows them.
    lock_reasons_ = drive_.motionLocks() & 5;
    if (!(lock_reasons_ & 1)) dome_.releaseStop(dome_.controlEpoch(), now_ms);
    dome_.setMotionLocks(lock_reasons_ & 4);
    calibration_.setMotionLocked(lock_reasons_ != 0);
    if (lock_reasons_ != current) {
        ++control_epoch_;
        publishStatusNow(now_ms);
    }
    return r == r2link::Result::Accepted ? r2link::Result::Accepted : r2link::Result::NotReady;
}

void BodyController::updateStatus(uint32_t now_ms) {
    const Readiness ready = readiness(active_);
    const uint8_t profile_ready = (ready.drive ? 1 : 0) | (ready.manual_dome ? 2 : 0) | (ready.auto_dome ? 4 : 0);
    const VescSample left = left_vesc_.sample(now_ms);
    const VescSample right = right_vesc_.sample(now_ms);
    const bool rc_stale = !rc_snapshot_.valid || (now_ms < rc_snapshot_.sample_ms) || (now_ms - rc_snapshot_.sample_ms > 250);

    uint32_t faults = (!validateProfile(active_) ? 2u : !ready.drive ? 1u : 0u) |
                      (rc_stale ? (1u << 3) : 0u) |
                      (left.stale ? (1u << 4) : 0u) |
                      (right.stale ? (1u << 5) : 0u) |
                      (left.fault ? (1u << 6) : 0u) |
                      (right.fault ? (1u << 7) : 0u) |
                      (left.unsupported || right.unsupported ? (1u << 8) : 0u) |
                      (drive_.deadlineMisses() ? (1u << 9) : 0u);

    body_status_.faults = faults;
    body_status_.drive_state = static_cast<uint8_t>(drive_.driveState());
    body_status_.drive_intent = static_cast<uint8_t>(drive_.intent());
    body_status_.lock_reasons = lock_reasons_ | (drive_.stopLatched() ? 1 : 0);
    body_status_.profile_ready = profile_ready;
    body_status_.control_epoch = control_epoch_;
    body_status_.dome_state = static_cast<uint8_t>(dome_.state());
    body_status_.dome_owner = static_cast<uint8_t>(dome_.owner());
    body_status_.dome_authority_generation = dome_.authorityGeneration();
    // The wire contract requires a zero angle whenever it is not valid.
    body_status_.angle_valid = dome_.position().valid() ? 1 : 0;
    body_status_.estimated_angle_ddeg = dome_.position().valid() ? dome_.position().angleDdeg() : 0;
}

r2link::BodyStatus BodyController::status() const {
    r2link::BodyStatus s = body_status_;
    s.control_epoch = control_epoch_;
    s.lock_reasons = lock_reasons_ | (drive_.stopLatched() ? 1 : 0);
    return s;
}

void BodyController::updateRc(const RcSnapshot& rc, uint32_t now_ms) {
    rc_snapshot_ = rc;
    calibration_.updateRc(rc, now_ms);
    dome_.updateRc(rc, now_ms);
    const bool centered = (rc.channels[0] >= 1460 && rc.channels[0] <= 1540) &&
                          (rc.channels[1] >= 1460 && rc.channels[1] <= 1540) &&
                          (rc.channels[3] >= 1460 && rc.channels[3] <= 1540);
    if (centered) {
        if (centered_start_ms_ == 0) {
            centered_start_ms_ = now_ms;
        }
    } else {
        centered_start_ms_ = 0;
    }
}

void BodyController::onLinkDisconnected(uint32_t now_ms) {
    dome_.peerLost(now_ms);
    audio_.peerLost(now_ms);
    calibration_.cancel(now_ms);
}

r2link::Result BodyController::handleControlRequest(const r2link::ControlRequest& req, uint32_t now_ms) {
    if (r2link::validate(req) != r2link::Status::Ok) {
        return r2link::Result::InvalidArgument;
    }

    // 0: STOP_ALL (Always valid, even with old epoch)
    if (req.operation == 0) {
        audio_.peerLost(now_ms);
        engageLocks(1, now_ms);
        return r2link::Result::Accepted;
    }

    // All subsequent operations require matching control_epoch
    if (req.control_epoch != control_epoch_) {
        return r2link::Result::WrongEpoch;
    }

    // 1: RELEASE_STOP
    if (req.operation == 1) {
        return releaseLocks(1, now_ms);
    }

    // 2: LOCK
    if (req.operation == 2) {
        if (req.reason == r2link::kReasonMaintenance) {
            if (req.token == 0) return r2link::Result::InvalidArgument;
            maintenance_token_ = req.token;
            engageLocks(4, now_ms);
            return r2link::Result::Accepted;
        }
        if (req.reason == r2link::kReasonOperator) {
            engageLocks(1, now_ms);
            return r2link::Result::Accepted;
        }
    }

    // 3: UNLOCK
    if (req.operation == 3) {
        if (req.reason == r2link::kReasonMaintenance) {
            if (req.token != maintenance_token_ || maintenance_token_ == 0) {
                return r2link::Result::Inhibited;
            }
            const r2link::Result r = releaseLocks(4, now_ms);
            if (r == r2link::Result::Accepted) maintenance_token_ = 0;
            return r;
        }
        if (req.reason == r2link::kReasonOperator) {
            return releaseLocks(1, now_ms);
        }
    }

    // 4: RECOVER_LOCKS
    if (req.operation == 4) {
        if (req.reason != r2link::kReasonOperator || req.token != 0) {
            return r2link::Result::InvalidArgument;
        }
        if (!releaseGateOpen(now_ms)) {
            return r2link::Result::Inhibited;
        }
        const r2link::Result r = releaseLocks(5, now_ms);
        if (r == r2link::Result::Accepted) {
            maintenance_token_ = 0;
            dome_.cancel(now_ms);
        }
        return r;
    }

    return r2link::Result::InvalidArgument;
}

r2link::Result BodyController::handleDomeRequest(const r2link::Frame& frame, uint32_t now_ms) {
    r2link::DomeRequest req{};
    r2link::ErrorCounters err{};
    if (r2link::decode(frame, req, err) != r2link::Status::Ok) {
        return r2link::Result::InvalidArgument;
    }
    if (req.control_epoch != control_epoch_) {
        return r2link::Result::WrongEpoch;
    }
    if (motionLocked()) {
        return r2link::Result::Inhibited;
    }
    return dome_.request(req, frame.sequence, now_ms);
}

r2link::Result BodyController::handleDriveRequest(const r2link::Frame& frame, uint32_t now_ms) {
    r2link::DriveRequest req{};
    r2link::ErrorCounters err{};
    if (r2link::decode(frame, req, err) != r2link::Status::Ok) {
        return r2link::Result::InvalidArgument;
    }
    if (req.control_epoch != control_epoch_) {
        return r2link::Result::WrongEpoch;
    }
    if (motionLocked()) {
        return r2link::Result::Inhibited;
    }
    return drive_.submitRemote(req, now_ms);
}

r2link::Result BodyController::handleAudioRequest(const r2link::Frame& frame, uint32_t now_ms) {
    r2link::AudioRequest req{};
    r2link::ErrorCounters err{};
    if (r2link::decode(frame, req, err) != r2link::Status::Ok) {
        return r2link::Result::InvalidArgument;
    }
    return audio_.request(req, frame.sequence, now_ms);
}

r2link::Result BodyController::handle(const r2link::Frame& frame, uint32_t now_ms) {
    if (frame.type == r2link::MessageType::ControlRequest) {
        r2link::ControlRequest req{};
        r2link::ErrorCounters err{};
        if (r2link::decode(frame, req, err) != r2link::Status::Ok) {
            return r2link::Result::InvalidArgument;
        }
        return handleControlRequest(req, now_ms);
    }

    if (frame.type == r2link::MessageType::DomeRequest) {
        return handleDomeRequest(frame, now_ms);
    }

    if (frame.type == r2link::MessageType::DriveRequest) {
        return handleDriveRequest(frame, now_ms);
    }

    if (frame.type == r2link::MessageType::AudioRequest) {
        return handleAudioRequest(frame, now_ms);
    }

    if (frame.type == r2link::MessageType::CommissionRequest) {
        return handleCommissionRequest(frame, now_ms);
    }

    if (frame.type == r2link::MessageType::HallState) {
        r2link::HallState hall{};
        r2link::ErrorCounters err{};
        if (r2link::decode(frame, hall, err) == r2link::Status::Ok) {
            dome_.updateHall(hall, now_ms);
            calibration_.updateHall(hall, now_ms);
            return r2link::Result::Accepted;
        }
        return r2link::Result::InvalidArgument;
    }

    return r2link::Result::InvalidArgument;
}

r2link::Result BodyController::handleCommissionRequest(const r2link::Frame& frame, uint32_t now_ms) {
    r2link::CommissionRequest req{};
    r2link::ErrorCounters err{};
    if (r2link::decode(frame, req, err) != r2link::Status::Ok) {
        return r2link::Result::InvalidArgument;
    }
    if (req.control_epoch != control_epoch_) {
        return r2link::Result::WrongEpoch;
    }
    const r2link::Result res = calibration_.handleRequest(req, now_ms);
    if (res == r2link::Result::Accepted && req.operation == static_cast<uint8_t>(CommissionOp::Save)) {
        activateSavedProfile();
    }
    if (res == r2link::Result::Accepted) {
        r2link::Frame status_frame{};
        if (r2link::encode(calibration_.status(), status_frame, err) == r2link::Status::Ok) {
            link_endpoint_.publishLatest(status_frame, now_ms);
        }
        if (req.operation == static_cast<uint8_t>(CommissionOp::Read)) {
            // Read: field selects the subtype, value carries the field id (protocol section 9).
            const r2link::Diagnostics diag = calibration_.diagnostics(
                req.field, static_cast<uint8_t>(req.value), req.wheel, now_ms);
            r2link::Frame diag_frame{};
            if (r2link::encode(diag, diag_frame, err) == r2link::Status::Ok) {
                link_endpoint_.publishLatest(diag_frame, now_ms);
            }
        }
    }
    return res;
}

ServoCommand BodyController::domeOutput() const {
    if (calibration_.active()) {
        return calibration_.output();
    }
    return dome_.output();
}

void BodyController::drainEvents(uint32_t now_ms) {
    r2link::Event ev{};
    while (dome_.takeEvent(ev)) {
        r2link::Frame ev_frame{};
        r2link::ErrorCounters err{};
        if (r2link::encode(ev, ev_frame, err) == r2link::Status::Ok) {
            uint16_t seq = 0;
            link_endpoint_.request(ev_frame, now_ms, seq);
        }
    }
    while (audio_.takeEvent(ev)) {
        r2link::Frame ev_frame{};
        r2link::ErrorCounters err{};
        if (r2link::encode(ev, ev_frame, err) == r2link::Status::Ok) {
            uint16_t seq = 0;
            link_endpoint_.request(ev_frame, now_ms, seq);
        }
    }
}

void BodyController::publishPeriodic(uint32_t now_ms) {
    // Body status every 200ms
    if (now_ms - last_status_pub_ms_ >= 200) {
        updateStatus(now_ms);
        r2link::Frame frame{};
        r2link::ErrorCounters err{};
        if (r2link::encode(body_status_, frame, err) == r2link::Status::Ok) {
            link_endpoint_.publishLatest(frame, now_ms);
        }
        last_status_pub_ms_ = now_ms;
    }

    // Audio status every 500ms
    if (now_ms - last_audio_pub_ms_ >= 500) {
        const r2link::AudioStatus current = audio_.status(now_ms);
        r2link::Frame frame{};
        r2link::ErrorCounters err{};
        if (r2link::encode(current, frame, err) == r2link::Status::Ok) {
            link_endpoint_.publishLatest(frame, now_ms);
        }
        last_audio_pub_ms_ = now_ms;
    }

    // Commission status periodic / active publishing
    if (calibration_.active() || (now_ms - last_commission_pub_ms_ >= 500)) {
        r2link::Frame frame{};
        r2link::ErrorCounters err{};
        if (r2link::encode(calibration_.status(), frame, err) == r2link::Status::Ok) {
            link_endpoint_.publishLatest(frame, now_ms);
        }
        last_commission_pub_ms_ = now_ms;
    }

    // RC status every 100ms
    if (now_ms - last_rc_pub_ms_ >= 100) {
        if (rc_snapshot_.valid) {
            r2link::RcStatus rs{};
            rs.sample_counter = rc_snapshot_.sample_counter;
            rs.source_age_ms = (now_ms >= rc_snapshot_.sample_ms) ? static_cast<uint16_t>(now_ms - rc_snapshot_.sample_ms) : 0;
            rs.flags = rc_snapshot_.flags;
            rs.drive_state = body_status_.drive_state;
            rs.dome_state = body_status_.dome_state;
            rs.control_epoch = control_epoch_;
            for (uint8_t i = 0; i < 10; ++i) rs.channels[i] = rc_snapshot_.channels[i];
            r2link::Frame frame{};
            r2link::ErrorCounters err{};
            if (r2link::encode(rs, frame, err) == r2link::Status::Ok) {
                link_endpoint_.publishLatest(frame, now_ms);
            }
        }
        last_rc_pub_ms_ = now_ms;
    }

    // VESC status every 100ms
    if (now_ms - last_vesc_pub_ms_ >= 100) {
        for (uint8_t w = 0; w < 2; ++w) {
            const body::VescSample sl = (w == 0) ? left_vesc_.sample(now_ms) : right_vesc_.sample(now_ms);
            if (sl.valid) {
                r2link::VescStatus vs{};
                vs.wheel = w;
                vs.fw_major = sl.fw_major;
                vs.fw_minor = sl.fw_minor;
                vs.valid_fields = sl.valid_fields;
                vs.source_age_ms = sl.source_age_ms;
                vs.pack_cV = sl.pack_cV;
                vs.motor_mA = sl.motor_mA;
                vs.input_mA = sl.input_mA;
                vs.erpm = sl.erpm;
                vs.mosfet_dC = sl.mosfet_dC;
                vs.motor_dC = sl.motor_dC;
                vs.fault = sl.fault;
                vs.duty_permille = sl.duty_permille;
                r2link::Frame frame{};
                r2link::ErrorCounters err{};
                if (r2link::encode(vs, frame, err) == r2link::Status::Ok) {
                    link_endpoint_.publishLatest(frame, now_ms);
                }
            }
        }
        last_vesc_pub_ms_ = now_ms;
    }
}

void BodyController::tick(uint32_t now_ms, uint32_t now_us) {
    if (last_loop_us_ > 0) {
        const uint32_t dt = now_us - last_loop_us_;
        if (dt > max_loop_us_) max_loop_us_ = dt;
        deadline_healthy_ = (dt <= 50000);
    }
    last_loop_us_ = now_us;

    // 1. Service VESC links
    left_vesc_.tick(now_ms);
    right_vesc_.tick(now_ms);

    // 2. Link endpoint and frame handling
    link_endpoint_.tick(now_ms);
    r2link::Frame rx_frame{};
    uint32_t rx_ms = 0;
    while (link_endpoint_.takeReceived(rx_frame, rx_ms)) {
        const r2link::Result res = handle(rx_frame, now_ms);
        link_endpoint_.reply(rx_frame, res, 0);
    }
    if (!link_endpoint_.connected(now_ms)) {
        onLinkDisconnected(now_ms);
    }

    // 3. Actuator updates
    calibration_.tick(now_ms);
    if (!calibration_.active()) {
        drive_.update(rc_snapshot_, left_vesc_.sample(now_ms), right_vesc_.sample(now_ms), active_, now_ms);
        applyWheelCommands(drive_.commands(), left_vesc_, right_vesc_);
    } else {
        WheelCommands neut{};
        applyWheelCommands(neut, left_vesc_, right_vesc_);
    }

    dome_.updateDrive(drive_.intent(), now_ms);
    dome_.setControlEpoch(control_epoch_);
    dome_.tick(now_ms);

    audio_.tick(now_ms);

    // 4. Update status, drain events and publish periodic frames
    updateStatus(now_ms);
    drainEvents(now_ms);
    publishPeriodic(now_ms);
}

static const struct {
    const char* name;
    uint8_t id;
    bool is_wheel;
} kCliFields[] = {
    {"servo_neutral", kFieldServoNeutral, false},
    {"servo_min", kFieldServoMin, false},
    {"servo_max", kFieldServoMax, false},
    {"auto_speed", kFieldAutoSpeed, false},
    {"slew", kFieldSlew, false},
    {"direction", kFieldDirection, true},
    {"fw_major", kFieldFwMajor, true},
    {"fw_minor", kFieldFwMinor, true},
    {"layout", kFieldLayout, true},
    {"motor_ma", kFieldMotorMa, true},
    {"battery_ma", kFieldBatteryMa, true},
    {"regen_ma", kFieldRegenMa, true},
    {"brake_ma", kFieldBrakeMa, true},
    {"undervoltage", kFieldUndervoltage, true},
    {"overvoltage", kFieldOvervoltage, true},
    {"timeout_ms", kFieldTimeoutMs, true},
    {"timeout_brake_ma", kFieldTimeoutBrakeMa, true},
    {"reversal_erpm", kFieldReversalErpm, true},
    {"reversal_dwell", kFieldReversalDwell, true},
    {"cw_rate", kFieldCwRate, false},
    {"ccw_rate", kFieldCcwRate, false},
};

bool BodyController::processCli(const char* line, char* out, size_t out_max, uint32_t now_ms) {
    if (!line || !out || out_max == 0) return false;
    out[0] = '\0';

    if (std::strcmp(line, "status") == 0) {
        updateStatus(now_ms);
        snprintf(out, out_max, "STATUS drive=%u dome=%u locks=%u epoch=%u faults=%lu\n",
                 body_status_.drive_state, body_status_.dome_state,
                 body_status_.lock_reasons, body_status_.control_epoch,
                 (unsigned long)body_status_.faults);
        return true;
    }

    if (std::strcmp(line, "rc") == 0) {
        snprintf(out, out_max, "RC valid=%u age=%lu ch1=%u ch2=%u ch4=%u ch6=%u ch9=%u\n",
                 unsigned(rc_snapshot_.valid),
                 (unsigned long)(now_ms >= rc_snapshot_.sample_ms ? now_ms - rc_snapshot_.sample_ms : 0),
                 rc_snapshot_.channels[0], rc_snapshot_.channels[1], rc_snapshot_.channels[3],
                 rc_snapshot_.channels[5], rc_snapshot_.channels[8]);
        return true;
    }

    if (std::strcmp(line, "vesc") == 0) {
        const VescSample left = left_vesc_.sample(now_ms);
        const VescSample right = right_vesc_.sample(now_ms);
        snprintf(out, out_max, "VESC L:V=%u I=%ld rpm=%ld F=%u | R:V=%u I=%ld rpm=%ld F=%u\n",
                 left.pack_cV, (long)left.motor_mA, (long)left.erpm, left.fault,
                 right.pack_cV, (long)right.motor_mA, (long)right.erpm, right.fault);
        return true;
    }

    if (std::strcmp(line, "stop") == 0) {
        r2link::ControlRequest stop_req{};
        stop_req.operation = 0;
        stop_req.reason = r2link::kReasonOperator;
        stop_req.control_epoch = control_epoch_;
        handleControlRequest(stop_req, now_ms);
        snprintf(out, out_max, "OK stopped\n");
        return true;
    }

    if (std::strcmp(line, "profile show") == 0) {
        const Readiness ready = readiness(active_);
        const Readiness staged = readiness(profile_);
        snprintf(out, out_max, "PROFILE saved: drive=%u manual=%u auto=%u acceptance=0x%03lX | "
                 "staged: drive=%u manual=%u auto=%u acceptance=0x%03lX neutral=%u auto_speed=%u\n",
                 unsigned(ready.drive), unsigned(ready.manual_dome), unsigned(ready.auto_dome),
                 (unsigned long)active_.acceptance,
                 unsigned(staged.drive), unsigned(staged.manual_dome), unsigned(staged.auto_dome),
                 (unsigned long)profile_.acceptance, profile_.servo_neutral, profile_.auto_speed_percent);
        return true;
    }

    if (std::strncmp(line, "profile accept ", 15) == 0) {
        const char* name = line + 15;
        int bit = -1;
        for (uint8_t b = 0; b < kAcceptBitCount; ++b) {
            if (std::strcmp(name, acceptanceBitName(b)) == 0) { bit = b; break; }
        }
        if (bit < 0) {
            snprintf(out, out_max, "ERROR unknown bit %s\n", name);
            return true;
        }
        // Same evidence rules as the wireless Accept: dome bits need their completed
        // run, VESC bits record that the operator observed the check.
        r2link::CommissionRequest req{};
        req.operation = static_cast<uint8_t>(CommissionOp::Accept);
        req.value = bit;
        req.control_epoch = control_epoch_;
        const r2link::Result res = calibration_.handleRequest(req, now_ms);
        if (res == r2link::Result::Accepted) {
            snprintf(out, out_max, "OK accepted %s (staged; profile save to apply)\n", name);
        } else {
            snprintf(out, out_max, "ERROR accept refused code=%u (needs CH6 OFF, CH9 OFF, sticks centred, prerequisites)\n",
                     static_cast<unsigned>(res));
        }
        return true;
    }

    if (std::strncmp(line, "profile set ", 12) == 0) {
        const bool ch6_off = rc_snapshot_.channels[5] < 1250;
        const bool ch9_off = rc_snapshot_.channels[8] < 1250;
        const bool neutral = (drive_.intent() == r2link::DriveIntent::Stationary) &&
                             (dome_.owner() == r2link::DomeOwner::None || dome_.owner() == r2link::DomeOwner::Manual);

        if (!ch6_off || !ch9_off || !neutral) {
            snprintf(out, out_max, "ERROR gate closed (requires CH6 OFF, CH9 OFF, neutral)\n");
            return true;
        }

        char field_name[32] = {0};
        int32_t val = 0;
        int wheel = 0;
        const int n = sscanf(line + 12, "%31s %ld %d", field_name, (long*)&val, &wheel);
        if (n < 2) {
            snprintf(out, out_max, "ERROR usage: profile set FIELD VALUE [WHEEL]\n");
            return true;
        }

        int field_idx = -1;
        for (size_t i = 0; i < sizeof(kCliFields)/sizeof(kCliFields[0]); ++i) {
            if (std::strcmp(field_name, kCliFields[i].name) == 0) {
                field_idx = i;
                break;
            }
        }
        if (field_idx < 0) {
            snprintf(out, out_max, "ERROR unknown field %s\n", field_name);
            return true;
        }

        const FieldResult res = setField(profile_, kCliFields[field_idx].id, static_cast<uint8_t>(wheel), val);
        if (res == FieldResult::Ok) {
            snprintf(out, out_max, "OK set %s=%ld\n", field_name, (long)val);
        } else {
            snprintf(out, out_max, "ERROR setField failed code=%u\n", static_cast<unsigned>(res));
        }
        return true;
    }

    if (std::strcmp(line, "profile save") == 0) {
        const bool ch6_off = rc_snapshot_.channels[5] < 1250;
        const bool neutral = (drive_.intent() == r2link::DriveIntent::Stationary);
        const SaveResult res = config_store_.trySave(profile_, ch6_off, neutral);
        if (res == SaveResult::Ok) {
            activateSavedProfile();
            snprintf(out, out_max, "OK saved\n");
        } else {
            snprintf(out, out_max, "ERROR save failed code=%u\n", static_cast<unsigned>(res));
        }
        return true;
    }

    if (std::strcmp(line, "profile enable") == 0) {
        const Readiness ready = readiness(active_);
        snprintf(out, out_max, "ENABLE drive=%u manual_dome=%u auto_dome=%u\n",
                 unsigned(ready.drive), unsigned(ready.manual_dome), unsigned(ready.auto_dome));
        return true;
    }

    snprintf(out, out_max, "ERROR unknown command\n");
    return true;
}

} // namespace body
