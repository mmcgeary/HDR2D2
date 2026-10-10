#include "BodyClient.h"

BodyClient::BodyClient() = default;

BodyClient::~BodyClient() {
    if (endpoint_) {
        endpoint_->~Endpoint();
        endpoint_ = nullptr;
    }
}

void BodyClient::begin(r2link::BytePort& port, uint32_t local_session) {
    if (endpoint_) {
        endpoint_->~Endpoint();
        endpoint_ = nullptr;
    }
    endpoint_ = new (endpoint_storage_) r2link::Endpoint(port, r2link::kRoleDome, local_session);
    has_rc_ = false;
    has_body_status_ = false;
    has_vesc_[0] = false;
    has_vesc_[1] = false;
    has_audio_status_ = false;
    event_count_ = 0;
    event_head_ = 0;
    completion_count_ = 0;
    completion_head_ = 0;
    last_peer_generation_ = 0;
    last_error_ = {};
}

void BodyClient::tick(uint32_t now_ms) {
    if (!endpoint_) return;
    endpoint_->tick(now_ms);

    if (endpoint_->peerGeneration() != last_peer_generation_) {
        last_peer_generation_ = endpoint_->peerGeneration();
        has_rc_ = false;
        has_body_status_ = false;
        has_vesc_[0] = false;
        has_vesc_[1] = false;
        has_audio_status_ = false;
        event_count_ = 0;
        event_head_ = 0;
        completion_count_ = 0;
        completion_head_ = 0;
    }

    processReceived(now_ms);
    processCompletions(now_ms);
}

void BodyClient::processReceived(uint32_t now_ms) {
    (void)now_ms;
    r2link::Frame frame{};
    uint32_t rx_ms = 0;
    while (endpoint_->takeReceived(frame, rx_ms)) {
        if (frame.type == r2link::MessageType::RcStatus) {
            r2link::RcStatus rc{};
            r2link::ErrorCounters err{};
            if (r2link::decode(frame, rc, err) == r2link::Status::Ok) {
                rc_raw_ = rc;
                rc_rx_ms_ = rx_ms;
                has_rc_ = true;
            }
        } else if (frame.type == r2link::MessageType::VescStatus) {
            r2link::VescStatus v{};
            r2link::ErrorCounters err{};
            if (r2link::decode(frame, v, err) == r2link::Status::Ok) {
                if (v.wheel < 2) {
                    vesc_raw_[v.wheel] = v;
                    vesc_rx_ms_[v.wheel] = rx_ms;
                    has_vesc_[v.wheel] = true;
                }
            }
        } else if (frame.type == r2link::MessageType::BodyStatus) {
            r2link::BodyStatus bs{};
            r2link::ErrorCounters err{};
            if (r2link::decode(frame, bs, err) == r2link::Status::Ok) {
                body_status_raw_ = bs;
                body_status_rx_ms_ = rx_ms;
                has_body_status_ = true;
            }
        } else if (frame.type == r2link::MessageType::AudioStatus) {
            r2link::AudioStatus as{};
            r2link::ErrorCounters err{};
            if (r2link::decode(frame, as, err) == r2link::Status::Ok) {
                audio_status_raw_ = as;
                audio_status_rx_ms_ = rx_ms;
                has_audio_status_ = true;
            }
        } else if (frame.type == r2link::MessageType::CommissionStatus) {
            r2link::CommissionStatus cs{};
            r2link::ErrorCounters err{};
            if (r2link::decode(frame, cs, err) == r2link::Status::Ok) {
                commission_status_raw_ = cs;
                commission_status_rx_ms_ = rx_ms;
                has_commission_status_ = true;
            }
        } else if (frame.type == r2link::MessageType::Diagnostics) {
            r2link::Diagnostics d{};
            r2link::ErrorCounters err{};
            if (r2link::decode(frame, d, err) == r2link::Status::Ok) {
                diagnostics_raw_ = d;
                diagnostics_rx_ms_ = rx_ms;
                has_diagnostics_ = true;
            }
        } else if (frame.type == r2link::MessageType::Event) {
            r2link::Event ev{};
            r2link::ErrorCounters err{};
            if (r2link::decode(frame, ev, err) == r2link::Status::Ok) {
                if (event_count_ < kEventCapacity) {
                    const size_t tail = (event_head_ + event_count_) % kEventCapacity;
                    event_queue_[tail] = ev;
                    ++event_count_;
                }
            }
            endpoint_->reply(frame, r2link::Result::Accepted, 0);
        }
    }
}

void BodyClient::processCompletions(uint32_t now_ms) {
    (void)now_ms;
    r2link::Completion c{};
    while (endpoint_->takeCompletion(c)) {
        if (c.outcome != r2link::Outcome::Replied) {
            last_error_.code = 5; // TimedOut / PeerLost
            last_error_.result = static_cast<uint8_t>(r2link::Result::HardwareError);
            last_error_.detail = static_cast<uint16_t>(c.outcome);
        } else if (c.result != static_cast<uint8_t>(r2link::Result::Accepted)) {
            last_error_.code = 4; // Rejected
            last_error_.result = c.result;
            last_error_.detail = c.detail;
        }
        if (completion_count_ < kCompletionCapacity) {
            const size_t tail = (completion_head_ + completion_count_) % kCompletionCapacity;
            completion_queue_[tail] = c;
            ++completion_count_;
        }
    }
}

BodyRcState BodyClient::rcSnapshot(uint32_t now_ms) const {
    BodyRcState res;
    if (!endpoint_ || !endpoint_->connected(now_ms) || !has_rc_) {
        res.valid = false;
        return res;
    }
    const uint32_t elapsed = (now_ms >= rc_rx_ms_) ? (now_ms - rc_rx_ms_) : 0;
    const uint32_t total_age = static_cast<uint32_t>(rc_raw_.source_age_ms) + elapsed;
    if (total_age > 250) {
        res.valid = false;
        res.source_age_ms = (total_age > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(total_age);
        return res;
    }
    res.valid = (rc_raw_.flags & 1) != 0;
    res.source_age_ms = static_cast<uint16_t>(total_age);
    res.sample_counter = rc_raw_.sample_counter;
    res.flags = rc_raw_.flags;
    for (int i = 0; i < 10; ++i) res.channels[i] = rc_raw_.channels[i];
    res.drive_state = rc_raw_.drive_state;
    res.dome_state = rc_raw_.dome_state;
    res.control_epoch = rc_raw_.control_epoch;
    return res;
}

BodyVescState BodyClient::vescStatus(uint8_t wheel, uint32_t now_ms) const {
    static const uint32_t kFreshMs = 500;
    static const uint16_t kLiveMask = 0x49;   // voltage, motor current, eRPM (Teensy VescLink validity)
    BodyVescState res;
    if (wheel >= 2 || !endpoint_ || !endpoint_->connected(now_ms) || !has_vesc_[wheel]) {
        return res;
    }
    const r2link::VescStatus& raw = vesc_raw_[wheel];
    const uint32_t elapsed = (now_ms >= vesc_rx_ms_[wheel]) ? (now_ms - vesc_rx_ms_[wheel]) : 0;
    const uint32_t total_age = static_cast<uint32_t>(raw.source_age_ms) + elapsed;
    res.wheel = wheel;
    res.source_age_ms = (total_age > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(total_age);
    if (elapsed > kFreshMs) {
        return res;   // no recent frame at all
    }
    // The body publishes firmware before commissioning with valid_fields = 0 and zeroed
    // measurements; those zeros are never reported as readings.
    res.present = true;
    res.fw_major = raw.fw_major;
    res.fw_minor = raw.fw_minor;
    if (total_age > kFreshMs || (raw.valid_fields & kLiveMask) != kLiveMask) {
        return res;
    }
    res.valid = true;
    res.valid_fields = raw.valid_fields;
    res.pack_cV = raw.pack_cV;
    res.motor_mA = raw.motor_mA;
    res.input_mA = raw.input_mA;
    res.erpm = raw.erpm;
    res.mosfet_dC = raw.mosfet_dC;
    res.motor_dC = raw.motor_dC;
    res.fault = raw.fault;
    res.duty_permille = raw.duty_permille;
    return res;
}

BodyStatusSnapshot BodyClient::bodyStatus(uint32_t now_ms) const {
    BodyStatusSnapshot res;
    if (!endpoint_ || !endpoint_->connected(now_ms) || !has_body_status_) {
        res.fresh = false;
        return res;
    }
    const uint32_t elapsed = (now_ms >= body_status_rx_ms_) ? (now_ms - body_status_rx_ms_) : 0;
    res.value = body_status_raw_;
    res.effective_age_ms = elapsed;
    res.fresh = (elapsed <= 300);
    return res;
}

r2link::AudioStatus BodyClient::audioStatus(uint32_t now_ms) const {
    (void)now_ms;
    if (!endpoint_ || !endpoint_->connected(now_ms) || !has_audio_status_) {
        r2link::AudioStatus off{};
        off.state = static_cast<uint8_t>(r2link::AudioState::Offline);
        return off;
    }
    return audio_status_raw_;
}

CommissionStatusSnapshot BodyClient::commissionStatus(uint32_t now_ms) const {
    CommissionStatusSnapshot res{};
    if (!endpoint_ || !endpoint_->connected(now_ms) || !has_commission_status_) {
        res.fresh = false;
        return res;
    }
    const uint32_t elapsed = (now_ms >= commission_status_rx_ms_) ? (now_ms - commission_status_rx_ms_) : 0;
    res.value = commission_status_raw_;
    res.effective_age_ms = elapsed;
    res.fresh = (elapsed <= 1000);
    return res;
}

DiagnosticsSnapshot BodyClient::diagnostics(uint32_t now_ms) const {
    DiagnosticsSnapshot res{};
    if (!endpoint_ || !endpoint_->connected(now_ms) || !has_diagnostics_) {
        res.fresh = false;
        return res;
    }
    const uint32_t elapsed = (now_ms >= diagnostics_rx_ms_) ? (now_ms - diagnostics_rx_ms_) : 0;
    res.value = diagnostics_raw_;
    res.effective_age_ms = elapsed;
    res.rx_ms = diagnostics_rx_ms_;
    res.fresh = (elapsed <= 1000);
    return res;
}


RequestHandle BodyClient::requestDome(const r2link::DomeRequest& req, uint32_t now_ms) {
    if (!endpoint_ || !endpoint_->connected(now_ms)) {
        last_error_.code = 1; // NotConnected
        last_error_.result = static_cast<uint8_t>(r2link::Result::NotReady);
        last_error_.detail = static_cast<uint16_t>(endpoint_ ? endpoint_->lastReject() : r2link::Reject::NotConnected);
        return RequestHandle{0, false};
    }
    r2link::Frame frame{};
    r2link::ErrorCounters err{};
    if (r2link::encode(req, frame, err) != r2link::Status::Ok) {
        last_error_.code = 3; // EncodeFailed
        last_error_.result = static_cast<uint8_t>(r2link::Result::InvalidArgument);
        return RequestHandle{0, false};
    }
    if (req.operation == static_cast<uint8_t>(r2link::DomeOperation::Velocity)) {
        // Velocity leases are a latest-value stream, not a reliable request: there
        // is no sequence to track, the next renewal supersedes this one.
        if (!endpoint_->publishLatest(frame, now_ms)) {
            last_error_.code = 4;
            return RequestHandle{0, false};
        }
        return RequestHandle{0, true};
    }
    uint16_t seq = 0;
    if (!endpoint_->request(frame, now_ms, seq)) {
        last_error_.code = (endpoint_->lastReject() == r2link::Reject::Busy) ? 2 : 4;
        last_error_.detail = static_cast<uint16_t>(endpoint_->lastReject());
        return RequestHandle{0, false};
    }
    return RequestHandle{seq, true};
}

RequestHandle BodyClient::requestAudio(const r2link::AudioRequest& req, uint32_t now_ms) {
    if (!endpoint_ || !endpoint_->connected(now_ms)) {
        last_error_.code = 1; // NotConnected
        last_error_.result = static_cast<uint8_t>(r2link::Result::NotReady);
        last_error_.detail = static_cast<uint16_t>(endpoint_ ? endpoint_->lastReject() : r2link::Reject::NotConnected);
        return RequestHandle{0, false};
    }
    r2link::Frame frame{};
    r2link::ErrorCounters err{};
    if (r2link::encode(req, frame, err) != r2link::Status::Ok) {
        last_error_.code = 3; // EncodeFailed
        last_error_.result = static_cast<uint8_t>(r2link::Result::InvalidArgument);
        return RequestHandle{0, false};
    }
    uint16_t seq = 0;
    if (!endpoint_->request(frame, now_ms, seq)) {
        last_error_.code = (endpoint_->lastReject() == r2link::Reject::Busy) ? 2 : 4;
        last_error_.detail = static_cast<uint16_t>(endpoint_->lastReject());
        return RequestHandle{0, false};
    }
    return RequestHandle{seq, true};
}

RequestHandle BodyClient::requestControl(const r2link::ControlRequest& req, uint32_t now_ms) {
    if (!endpoint_ || !endpoint_->connected(now_ms)) {
        last_error_.code = 1; // NotConnected
        last_error_.result = static_cast<uint8_t>(r2link::Result::NotReady);
        last_error_.detail = static_cast<uint16_t>(endpoint_ ? endpoint_->lastReject() : r2link::Reject::NotConnected);
        return RequestHandle{0, false};
    }
    r2link::Frame frame{};
    r2link::ErrorCounters err{};
    if (r2link::encode(req, frame, err) != r2link::Status::Ok) {
        last_error_.code = 3; // EncodeFailed
        last_error_.result = static_cast<uint8_t>(r2link::Result::InvalidArgument);
        return RequestHandle{0, false};
    }
    uint16_t seq = 0;
    if (!endpoint_->request(frame, now_ms, seq)) {
        last_error_.code = (endpoint_->lastReject() == r2link::Reject::Busy) ? 2 : 4;
        last_error_.detail = static_cast<uint16_t>(endpoint_->lastReject());
        return RequestHandle{0, false};
    }
    return RequestHandle{seq, true};
}

RequestHandle BodyClient::requestDrive(const r2link::DriveRequest& req, uint32_t now_ms) {
    if (!endpoint_ || !endpoint_->connected(now_ms)) {
        last_error_.code = 1; // NotConnected
        last_error_.result = static_cast<uint8_t>(r2link::Result::NotReady);
        last_error_.detail = static_cast<uint16_t>(endpoint_ ? endpoint_->lastReject() : r2link::Reject::NotConnected);
        return RequestHandle{0, false};
    }
    r2link::Frame frame{};
    r2link::ErrorCounters err{};
    if (r2link::encode(req, frame, err) != r2link::Status::Ok) {
        last_error_.code = 3; // EncodeFailed
        last_error_.result = static_cast<uint8_t>(r2link::Result::InvalidArgument);
        return RequestHandle{0, false};
    }
    uint16_t seq = 0;
    if (!endpoint_->request(frame, now_ms, seq)) {
        last_error_.code = (endpoint_->lastReject() == r2link::Reject::Busy) ? 2 : 4;
        last_error_.detail = static_cast<uint16_t>(endpoint_->lastReject());
        return RequestHandle{0, false};
    }
    return RequestHandle{seq, true};
}

RequestHandle BodyClient::requestCommission(const r2link::CommissionRequest& req, uint32_t now_ms) {
    if (!endpoint_ || !endpoint_->connected(now_ms)) {
        last_error_.code = 1; // NotConnected
        last_error_.result = static_cast<uint8_t>(r2link::Result::NotReady);
        last_error_.detail = static_cast<uint16_t>(endpoint_ ? endpoint_->lastReject() : r2link::Reject::NotConnected);
        return RequestHandle{0, false};
    }
    r2link::Frame frame{};
    r2link::ErrorCounters err{};
    if (r2link::encode(req, frame, err) != r2link::Status::Ok) {
        last_error_.code = 3; // EncodeFailed
        last_error_.result = static_cast<uint8_t>(r2link::Result::InvalidArgument);
        return RequestHandle{0, false};
    }
    uint16_t seq = 0;
    if (!endpoint_->request(frame, now_ms, seq)) {
        last_error_.code = (endpoint_->lastReject() == r2link::Reject::Busy) ? 2 : 4;
        last_error_.detail = static_cast<uint16_t>(endpoint_->lastReject());
        return RequestHandle{0, false};
    }
    return RequestHandle{seq, true};
}

bool BodyClient::submit(const r2link::DomeRequest& req, uint32_t now_ms, uint16_t& sequence) {
    RequestHandle h = requestDome(req, now_ms);
    if (h.queued) {
        sequence = h.sequence;
        return true;
    }
    return false;
}

bool BodyClient::takeEvent(r2link::Event& out) {
    if (event_count_ == 0) return false;
    out = event_queue_[event_head_];
    event_head_ = (event_head_ + 1) % kEventCapacity;
    --event_count_;
    return true;
}

bool BodyClient::takeCompletion(r2link::Completion& out) {
    if (completion_count_ == 0) return false;
    out = completion_queue_[completion_head_];
    completion_head_ = (completion_head_ + 1) % kCompletionCapacity;
    --completion_count_;
    return true;
}

void BodyClient::publishHall(uint8_t valid_mask, uint8_t active_mask, uint32_t sample_counter, uint32_t now_ms) {
    if (!endpoint_) return;
    r2link::HallState hall{};
    hall.valid_mask = valid_mask;
    hall.active_mask = active_mask;
    hall.sample_counter = sample_counter;
    hall.source_age_ms = 0;

    r2link::Frame frame{};
    r2link::ErrorCounters err{};
    if (r2link::encode(hall, frame, err) == r2link::Status::Ok) {
        endpoint_->publishLatest(frame, now_ms);
    }
}

DomeBehaviourInput BodyClient::makeDomeBehaviourInput(uint32_t now_ms, bool event_active) const {
    DomeBehaviourInput input{};
    input.rc = rc_raw_;
    input.status = body_status_raw_;
    input.rc_fresh = rcSnapshot(now_ms).valid;
    input.status_fresh = bodyStatus(now_ms).fresh;
    input.event_active = event_active;
    return input;
}

bool BodyClient::requestRead(uint8_t field, uint8_t wheel, uint32_t now_ms) {
    r2link::CommissionRequest req{};
    req.operation = 0; req.field = 1; req.wheel = wheel; req.value = field;
    req.control_epoch = bodyStatus(now_ms).value.control_epoch;
    return requestCommission(req, now_ms).queued;
}

bool BodyClient::sendCommission(const r2link::CommissionRequest& req, uint32_t now_ms, uint16_t& seq) {
    const RequestHandle h = requestCommission(req, now_ms);
    seq = h.sequence;
    return h.queued;
}
