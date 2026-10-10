#pragma once
#include "Endpoint.h"
#include "body/IbusInput.h"
#include "body/VescLink.h"

namespace body {

struct MixedDuty { int16_t left, right; };
MixedDuty mixDrive(uint16_t throttle_us, uint16_t steer_us, uint16_t rate_permille);

enum class WheelMode : uint8_t { Disabled, Duty, Brake };
struct WheelCommand {
    WheelMode mode;
    int16_t duty_permille; // signed physical motor command, not chassis direction
    uint32_t brake_mA;
};
struct WheelCommands { WheelCommand left, right; };
enum class ReversalState : uint8_t { Tracking, Braking, Qualifying };

// Loop context, fixed storage. Safety gates run on every update; powered
// commands renew at 20ms. A >50ms powered deadline miss latches a rearm fault.
class DriveController {
public:
    DriveController();
    void update(const RcSnapshot&, const VescSample&, const VescSample&,
                const CommissioningProfile&, uint32_t now_ms);
    r2link::DriveState driveState() const { return state_; }
    const WheelCommands& commands() const { return commands_; }
    r2link::DriveIntent intent() const { return intent_; }
    ReversalState reversalState(uint8_t wheel) const;
    void stop(uint32_t now_ms);
    // Reserved bits rejected atomically. Removing locks requires the local
    // OFF/centered500ms gate. The control owner must ALSO validate epoch,
    // handshake and token before calling this acknowledged-state API.
    r2link::Result setMotionLocks(uint8_t reasons);
    r2link::Result releaseStop(uint16_t current_epoch, uint32_t now_ms);
    r2link::Result submitRemote(const r2link::DriveRequest&, uint32_t now_ms);
    bool stopLatched() const { return stopped_; }
    uint8_t motionLocks() const { return locks_ | (stopped_ ? 1 : 0); }
    uint16_t controlEpoch() const { return epoch_; }
    uint32_t deadlineMisses() const { return deadline_misses_; }
    // Changes on each 20ms renewal or an immediate safety-output change.
    uint32_t commandRevision() const { return command_revision_; }
private:
    struct Wheel {
        ReversalState state;
        int8_t prior_sign, low_sign;
        bool low_started, ramp_reset;
        uint32_t low_ms, sample_ms;
        int32_t magnitude_milli;
    };
    void disarm(r2link::DriveState);
    void braking();
    int16_t permit(Wheel&, int16_t, const VescSample&, const WheelProfile&, uint32_t);
    void output(Wheel&, WheelCommand&, int16_t, int8_t direction, uint16_t slew, uint32_t dt);
    Wheel wheel_[2];
    WheelCommands commands_;
    r2link::DriveState state_;
    r2link::DriveIntent intent_;
    uint8_t locks_;
    uint16_t epoch_;
    bool stopped_, observed_off_, neutral_started_, ticked_, off_started_;
    uint32_t neutral_ms_, last_command_ms_, last_update_ms_, now_ms_, off_ms_;
    uint32_t deadline_misses_, command_revision_, rc_sample_ms_, profile_digest_;
    bool profile_seen_;
};

// Disabled cancels an outstanding demand; it never guesses a brake current.
void applyWheelCommands(const WheelCommands&, VescLink& left, VescLink& right);

} // namespace body
