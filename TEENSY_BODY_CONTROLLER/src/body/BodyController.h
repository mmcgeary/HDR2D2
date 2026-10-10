#pragma once

#include <stdint.h>
#include <stddef.h>
#include "BytePort.h"
#include "Messages.h"
#include "Endpoint.h"
#include "body/ConfigStore.h"
#include "body/IbusInput.h"
#include "body/IbusTelemetry.h"
#include "body/VescLink.h"
#include "body/DriveController.h"
#include "body/DomePosition.h"
#include "body/DomeController.h"
#include "body/DfPlayer.h"
#include "TrackCatalog.h"

namespace body {

class BodyController {
public:
    BodyController(ConfigStore& config_store,
                   r2link::BytePort& link_port,
                   r2link::BytePort& left_vesc_port,
                   r2link::BytePort& right_vesc_port,
                   r2link::BytePort& audio_port,
                   uint32_t local_session = 1);

    void init(uint32_t now_ms);

    // Main scheduler tick
    void tick(uint32_t now_ms, uint32_t now_us);

    // Frame dispatch from body link
    r2link::Result handle(const r2link::Frame& frame, uint32_t now_ms);

    // Status queries
    r2link::BodyStatus status() const;
    bool motionLocked() const { return lock_reasons_ != 0; }
    uint8_t lockReasons() const { return lock_reasons_; }
    uint16_t controlEpoch() const { return control_epoch_; }
    bool deadlineHealthy() const { return deadline_healthy_; }

    // External notifications
    void updateRc(const RcSnapshot& rc, uint32_t now_ms);
    void onLinkDisconnected(uint32_t now_ms);

    // Direct subsystem access
    DriveController& drive() { return drive_; }
    const DriveController& drive() const { return drive_; }
    DomeController& dome() { return dome_; }
    const DomeController& dome() const { return dome_; }
    DfPlayer& audio() { return audio_; }
    const DfPlayer& audio() const { return audio_; }
    VescLink& leftVesc() { return left_vesc_; }
    VescLink& rightVesc() { return right_vesc_; }
    const CommissioningProfile& profile() const { return profile_; }
    CommissioningProfile& profile() { return profile_; }
    ConfigStore& configStore() { return config_store_; }
    r2link::Endpoint& linkEndpoint() { return link_endpoint_; }
    const r2link::Endpoint& linkEndpoint() const { return link_endpoint_; }

    // CLI line processing
    bool processCli(const char* line, char* out, size_t out_max, uint32_t now_ms);

private:
    void applyMotionLocks(uint8_t reasons, uint32_t now_ms);
    void updateStatus(uint32_t now_ms);
    void pumpLink(uint32_t now_ms);
    void drainEvents(uint32_t now_ms);
    void publishPeriodic(uint32_t now_ms);

    r2link::Result handleControlRequest(const r2link::ControlRequest& req, uint32_t now_ms);
    r2link::Result handleDomeRequest(const r2link::Frame& frame, uint32_t now_ms);
    r2link::Result handleDriveRequest(const r2link::Frame& frame, uint32_t now_ms);
    r2link::Result handleAudioRequest(const r2link::Frame& frame, uint32_t now_ms);
    r2link::Result handleCommissionRequest(const r2link::Frame& frame, uint32_t now_ms);

    ConfigStore& config_store_;
    CommissioningProfile profile_;

    r2link::BytePort& link_port_;
    r2link::Endpoint link_endpoint_;

    VescLink left_vesc_;
    VescLink right_vesc_;
    DriveController drive_;
    DomeController dome_;
    DfPlayer audio_;

    r2link::BodyStatus body_status_{};
    uint16_t control_epoch_{1};
    uint8_t lock_reasons_{0};
    uint16_t maintenance_token_{0};

    RcSnapshot rc_snapshot_{};
    uint32_t centered_start_ms_{0};

    uint32_t last_drive_ms_{0};
    uint32_t last_vesc_poll_ms_{0};
    uint32_t last_status_pub_ms_{0};
    uint32_t last_audio_pub_ms_{0};
    uint32_t last_rc_pub_ms_{0};

    uint32_t last_loop_us_{0};
    uint32_t max_loop_us_{0};
    bool deadline_healthy_{true};
};

} // namespace body
