#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <new>
#include "BodyRcState.h"
#include "BytePort.h"
#include "Endpoint.h"
#include "Messages.h"
#include "DomeBehaviour.h"
#include "ProfileMirror.h"

struct RequestHandle {
    uint16_t sequence{0};
    bool queued{false};

    RequestHandle() = default;
    RequestHandle(uint16_t seq, bool q) : sequence(seq), queued(q) {}
};

struct BodyVescState {
    bool valid{false};
    uint8_t wheel{0};
    uint16_t source_age_ms{0};
    uint16_t valid_fields{0};
    uint8_t fw_major{0};
    uint8_t fw_minor{0};
    uint16_t pack_cV{0};
    int32_t motor_mA{0};
    int32_t input_mA{0};
    int32_t erpm{0};
    int16_t mosfet_dC{0};
    int16_t motor_dC{0};
    uint8_t fault{0};
    int16_t duty_permille{0};
};

struct BodyStatusSnapshot {
    r2link::BodyStatus value{};
    bool fresh{false};
    uint32_t effective_age_ms{0};
};

struct CommissionStatusSnapshot {
    r2link::CommissionStatus value{};
    bool fresh{false};
    uint32_t effective_age_ms{0};
};

struct DiagnosticsSnapshot {
    r2link::Diagnostics value{};
    bool fresh{false};
    uint32_t effective_age_ms{0};
    uint32_t rx_ms{0};
};

struct ClientError {
    uint8_t code{0};
    uint8_t result{0};
    uint16_t detail{0};
};

class BodyClient : public IDomeRequestSink, public IFieldReader, public ICommissionSink {
public:
    BodyClient();
    ~BodyClient() override;

    void begin(r2link::BytePort& port, uint32_t local_session);
    void tick(uint32_t now_ms);
    bool linkUp(uint32_t now_ms) const { return endpoint_ && endpoint_->connected(now_ms); }

    // Subsystem queries
    BodyRcState rcSnapshot(uint32_t now_ms) const;
    BodyVescState vescStatus(uint8_t wheel, uint32_t now_ms) const;
    BodyStatusSnapshot bodyStatus(uint32_t now_ms) const;
    r2link::AudioStatus audioStatus(uint32_t now_ms) const;
    CommissionStatusSnapshot commissionStatus(uint32_t now_ms) const;
    DiagnosticsSnapshot diagnostics(uint32_t now_ms) const;

    // Requests
    RequestHandle requestDome(const r2link::DomeRequest& req, uint32_t now_ms);
    RequestHandle requestAudio(const r2link::AudioRequest& req, uint32_t now_ms);
    RequestHandle requestControl(const r2link::ControlRequest& req, uint32_t now_ms);
    RequestHandle requestDrive(const r2link::DriveRequest& req, uint32_t now_ms);
    RequestHandle requestCommission(const r2link::CommissionRequest& req, uint32_t now_ms);
    bool requestRead(uint8_t field, uint8_t wheel, uint32_t now_ms) override;
    bool sendCommission(const r2link::CommissionRequest& req, uint32_t now_ms, uint16_t& seq) override;

    // IDomeRequestSink interface implementation
    bool submit(const r2link::DomeRequest& req, uint32_t now_ms, uint16_t& sequence) override;

    // Event and completion extraction
    bool takeEvent(r2link::Event& out);
    bool takeCompletion(r2link::Completion& out);

    // Telemetry publishing
    void publishHall(uint8_t valid_mask, uint8_t active_mask, uint32_t sample_counter, uint32_t now_ms);

    // Input generator for Task 6b autonomous dome scheduler
    DomeBehaviourInput makeDomeBehaviourInput(uint32_t now_ms, bool event_active = false) const;

    // Diagnostics and endpoint access
    ClientError lastError() const { return last_error_; }
    r2link::Endpoint& endpoint() { return *endpoint_; }
    const r2link::Endpoint& endpoint() const { return *endpoint_; }

private:
    void processReceived(uint32_t now_ms);
    void processCompletions(uint32_t now_ms);

    static const size_t kEventCapacity = 8;
    static const size_t kCompletionCapacity = 8;
    alignas(r2link::Endpoint) uint8_t endpoint_storage_[sizeof(r2link::Endpoint)];
    r2link::Endpoint* endpoint_{nullptr};

    r2link::RcStatus rc_raw_{};
    uint32_t rc_rx_ms_{0};
    bool has_rc_{false};

    r2link::VescStatus vesc_raw_[2]{};
    uint32_t vesc_rx_ms_[2]{0, 0};
    bool has_vesc_[2]{false, false};

    r2link::BodyStatus body_status_raw_{};
    uint32_t body_status_rx_ms_{0};
    bool has_body_status_{false};

    r2link::AudioStatus audio_status_raw_{};
    uint32_t audio_status_rx_ms_{0};
    bool has_audio_status_{false};

    r2link::CommissionStatus commission_status_raw_{};
    uint32_t commission_status_rx_ms_{0};
    bool has_commission_status_{false};

    r2link::Diagnostics diagnostics_raw_{};
    uint32_t diagnostics_rx_ms_{0};
    bool has_diagnostics_{false};

    r2link::Event event_queue_[kEventCapacity]{};
    size_t event_head_{0};
    size_t event_count_{0};

    r2link::Completion completion_queue_[kCompletionCapacity]{};
    size_t completion_head_{0};
    size_t completion_count_{0};

    uint32_t last_peer_generation_{0};
    ClientError last_error_{};
};
