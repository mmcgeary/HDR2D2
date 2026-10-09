#pragma once
#include <stdint.h>
#include "Messages.h"
#include "body/ConfigStore.h"

namespace body {

class DomePosition {
public:
    explicit DomePosition(const CommissioningProfile& profile);

    // Updates Hall detection from received HallState.
    // Anchors front to 0 ddeg and rear to -1800 ddeg on rising edges.
    // Broad magnet window does not pin the estimate continuously.
    // Simultaneous detection sets sensor fault and invalidates estimate.
    void updateHall(const r2link::HallState& hall, uint32_t received_ms);

    // Integrates calibrated speed over dt_ms.
    // CW (>0) uses cw_ddeg_per_s; CCW (<0) uses ccw_ddeg_per_s.
    // Accumulates fractionally to prevent roundoff error.
    void integrate(int16_t speed_percent, uint32_t dt_ms);

    // Invalidates position estimate (e.g. on manual uncalibrated movement or seek failure).
    void invalidate();

    bool valid() const { return valid_; }
    int16_t angleDdeg() const { return angle_ddeg_; }

    bool sensorFault() const { return sensor_fault_; }
    void clearSensorFault() { sensor_fault_ = false; }

    // Helper: determine seek direction to reach target reference (Front or Rear).
    // Returns +1 for CW, -1 for CCW.
    // If position estimate is valid, chooses shortest route (with 180° tie resolved CW).
    // If position estimate is unknown/invalid, returns +1 (CW).
    int8_t seekDirection(r2link::DomeReference target) const;

    // Normalizes angle to [-1800, 1800) ddeg.
    static int16_t normalize(int32_t ddeg);

private:
    const CommissioningProfile* profile_;
    int16_t angle_ddeg_;
    int64_t accum_milli_;
    uint8_t prior_active_mask_;
    bool valid_;
    bool sensor_fault_;
};

} // namespace body
