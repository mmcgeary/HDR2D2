#include "body/DomePosition.h"

namespace body {

DomePosition::DomePosition(const CommissioningProfile& profile)
    : profile_(&profile), angle_ddeg_(0), accum_milli_(0),
      prior_active_mask_(0), valid_(false), sensor_fault_(false) {}

int16_t DomePosition::normalize(int32_t ddeg) {
    while (ddeg >= 1800) ddeg -= 3600;
    while (ddeg < -1800) ddeg += 3600;
    return static_cast<int16_t>(ddeg);
}

void DomePosition::updateHall(const r2link::HallState& hall, uint32_t) {
    // Stale samples (>150ms) cannot anchor or confirm references
    if (hall.source_age_ms > 150) return;
    if ((hall.valid_mask & 0x03) != 0x03) return;

    // Simultaneous active references are physically invalid
    if ((hall.active_mask & 0x03) == 0x03) {
        sensor_fault_ = true;
        invalidate();
        prior_active_mask_ = hall.active_mask;
        return;
    }

    const bool front_now = (hall.active_mask & 0x01) != 0;
    const bool front_prior = (prior_active_mask_ & 0x01) != 0;
    if (front_now && !front_prior) {
        // Newly detected front reference anchors at 0 ddeg
        angle_ddeg_ = 0;
        accum_milli_ = 0;
        valid_ = true;
    }

    const bool rear_now = (hall.active_mask & 0x02) != 0;
    const bool rear_prior = (prior_active_mask_ & 0x02) != 0;
    if (rear_now && !rear_prior) {
        // Newly detected rear reference anchors at -1800 ddeg
        angle_ddeg_ = -1800;
        accum_milli_ = 0;
        valid_ = true;
    }

    prior_active_mask_ = hall.active_mask;
}

void DomePosition::integrate(int16_t speed_percent, uint32_t dt_ms) {
    if (!valid_ || dt_ms == 0 || speed_percent == 0 || !profile_) return;

    uint16_t rate_ddeg_s = 0;
    int8_t dir = 0;
    if (speed_percent > 0) {
        rate_ddeg_s = profile_->cw_ddeg_per_s;
        dir = 1;
    } else {
        rate_ddeg_s = profile_->ccw_ddeg_per_s;
        dir = -1;
    }
    if (rate_ddeg_s == 0) return;

    accum_milli_ += int64_t(rate_ddeg_s) * dt_ms * dir;
    int32_t delta_ddeg = static_cast<int32_t>(accum_milli_ / 1000);
    accum_milli_ %= 1000;

    if (delta_ddeg != 0) {
        angle_ddeg_ = normalize(angle_ddeg_ + delta_ddeg);
    }
}

void DomePosition::invalidate() {
    valid_ = false;
    accum_milli_ = 0;
}

int8_t DomePosition::seekDirection(r2link::DomeReference target) const {
    if (!valid_) return 1; // CW when unknown

    const int16_t target_angle = (target == r2link::DomeReference::Rear) ? -1800 : 0;
    int32_t diff = normalize(target_angle - angle_ddeg_);
    // In [-1800, 1800): if diff == -1800 (tie at 180°), resolve clockwise (+1)
    if (diff == -1800 || diff > 0) return 1;
    return -1;
}

} // namespace body
