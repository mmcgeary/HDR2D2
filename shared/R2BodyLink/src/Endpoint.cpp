#include "Endpoint.h"
#include <string.h>

namespace r2link {

namespace {

template <class T>
bool payloadOk(const Frame& f, ErrorCounters& c) {
    T m;
    return decode(f, m, c) == Status::Ok;
}

template <class T>
bool addAge(Frame& f, uint32_t residence, ErrorCounters& c) {
    T m;
    if (decode(f, m, c) != Status::Ok) return false;
    uint32_t age = static_cast<uint32_t>(m.source_age_ms) + residence;
    if (age > 65535u) age = 65535u;
    m.source_age_ms = static_cast<uint16_t>(age);
    return encode(m, f, c) == Status::Ok;
}

void add(uint32_t& a, uint32_t b) { a += b; }

}  // namespace

Endpoint::Endpoint(BytePort& port, uint8_t role, uint32_t local_session)
    : port_(port), role_(role), local_(local_session), peer_(0), generation_(0),
      mode_(0), ready_(0), peer_mode_(0), peer_ready_(0),
      have_hb_(false), was_connected_(false), hello_due_(false), hello_sent_(false), hb_sent_(false),
      hb_seq_(0), last_hb_ms_(0), last_hello_ms_(0), last_hb_tx_ms_(0), last_now_(0), order_(0),
      next_seq_(0), stream_seq_(0), reject_(Reject::None),
      fifo_head_(0), fifo_count_(0), done_head_(0), done_count_(0),
      wire_len_(0), wire_off_(0), wire_partial_counted_(false), need_delim_(false) {
    memset(&stats_, 0, sizeof stats_);
    memset(out_, 0, sizeof out_);
    memset(stream_out_, 0, sizeof stream_out_);
    memset(stream_in_, 0, sizeof stream_in_);
    memset(seq_have_, 0, sizeof seq_have_);
    memset(seq_last_, 0, sizeof seq_last_);
    memset(fifo_, 0, sizeof fifo_);
    memset(replies_, 0, sizeof replies_);
    memset(cache_, 0, sizeof cache_);
    memset(done_, 0, sizeof done_);
}

void Endpoint::setLocalState(uint8_t mode, uint8_t ready) { mode_ = mode; ready_ = ready; }

bool Endpoint::connected(uint32_t now) const {
    return peer_ != 0 && have_hb_ && static_cast<uint32_t>(now - last_hb_ms_) < kLinkTimeoutMs;
}

size_t Endpoint::pendingCount() const {
    size_t n = 0;
    for (uint8_t i = 0; i < kSlots; ++i) if (out_[i].used) ++n;
    return n;
}

ErrorCounters Endpoint::counters() const {
    ErrorCounters c = codec_.counters();
    add(c.crc, ec_.crc); add(c.length, ec_.length); add(c.version, ec_.version);
    add(c.reserved, ec_.reserved); add(c.type, ec_.type); add(c.cobs, ec_.cobs);
    add(c.overflow, ec_.overflow); add(c.timeout, ec_.timeout); add(c.session, ec_.session);
    add(c.queue, ec_.queue); add(c.null_argument, ec_.null_argument); add(c.capacity, ec_.capacity);
    add(c.payload_length, ec_.payload_length); add(c.enum_value, ec_.enum_value);
    add(c.reserved_bits, ec_.reserved_bits); add(c.range, ec_.range);
    return c;
}

// ---- classification ----
bool Endpoint::isDiscrete(const Frame& f) {
    switch (f.type) {
        case MessageType::AudioRequest: case MessageType::ControlRequest:
        case MessageType::CommissionRequest: case MessageType::Event:
            return true;
        case MessageType::DomeRequest:
            return f.length >= 1 && f.payload[0] != 1;
        default:
            return false;
    }
}

bool Endpoint::isSafety(const Frame& f) {
    switch (f.type) {
        case MessageType::ControlRequest: case MessageType::Event: return true;
        case MessageType::DomeRequest: return f.payload[0] == 0;
        case MessageType::CommissionRequest: return f.payload[0] == 3;
        default: return false;
    }
}

int Endpoint::streamKey(const Frame& f) {
    switch (f.type) {
        case MessageType::RcStatus: return 0;
        case MessageType::VescStatus: return f.length >= 1 && f.payload[0] <= 1 ? 1 + f.payload[0] : -1;
        case MessageType::BodyStatus: return 3;
        case MessageType::HallState: return 4;
        case MessageType::AudioStatus: return 5;
        case MessageType::CommissionStatus: return 6;
        case MessageType::Diagnostics: return 7;
        case MessageType::DriveRequest: return 8;
        case MessageType::DomeRequest: return f.length >= 1 && f.payload[0] == 1 ? 9 : -1;
        default: return -1;
    }
}

bool Endpoint::validPayload(const Frame& f) {
    switch (f.type) {
        case MessageType::Hello: return payloadOk<Hello>(f, ec_);
        case MessageType::Heartbeat: return payloadOk<Heartbeat>(f, ec_);
        case MessageType::RcStatus: return payloadOk<RcStatus>(f, ec_);
        case MessageType::VescStatus: return payloadOk<VescStatus>(f, ec_);
        case MessageType::BodyStatus: return payloadOk<BodyStatus>(f, ec_);
        case MessageType::HallState: return payloadOk<HallState>(f, ec_);
        case MessageType::DomeRequest: return payloadOk<DomeRequest>(f, ec_);
        case MessageType::AudioRequest: return payloadOk<AudioRequest>(f, ec_);
        case MessageType::DriveRequest: return payloadOk<DriveRequest>(f, ec_);
        case MessageType::ControlRequest: return payloadOk<ControlRequest>(f, ec_);
        case MessageType::CommissionRequest: return payloadOk<CommissionRequest>(f, ec_);
        case MessageType::Reply: return payloadOk<Reply>(f, ec_);
        case MessageType::AudioStatus: return payloadOk<AudioStatus>(f, ec_);
        case MessageType::Event: return payloadOk<Event>(f, ec_);
        case MessageType::CommissionStatus: return payloadOk<CommissionStatus>(f, ec_);
        case MessageType::Diagnostics: return payloadOk<Diagnostics>(f, ec_);
    }
    ++ec_.type;
    return false;
}

// ---- application API ----
uint16_t Endpoint::allocSequence() {
    do { ++next_seq_; } while (sequenceLive(next_seq_));
    return next_seq_;
}

bool Endpoint::sequenceLive(uint16_t seq) const {
    for (uint8_t i = 0; i < kSlots; ++i) if (out_[i].used && out_[i].frame.sequence == seq) return true;
    return false;
}

bool Endpoint::request(Frame& frame, uint32_t now, uint16_t& sequence) {
    reject_ = Reject::None;
    if (!connected(now)) { reject_ = Reject::NotConnected; return false; }
    if (frame.length > kMaxPayload || !isDiscrete(frame)) { reject_ = Reject::WrongKind; return false; }
    frame.version = kVersion;
    if (!validPayload(frame)) { reject_ = Reject::BadFrame; return false; }
    const bool safety = isSafety(frame);
    uint8_t used = 0, ordinary = 0;
    int free_slot = -1;
    for (uint8_t i = 0; i < kSlots; ++i) {
        if (out_[i].used) { ++used; if (!out_[i].safety) ++ordinary; }
        else if (free_slot < 0) free_slot = i;
    }
    if (free_slot < 0 || (!safety && ordinary >= kOrdinarySlots)) {
        reject_ = Reject::Busy;
        ++ec_.queue;
        ++stats_.busy_local;
        return false;
    }
    frame.flags = 1;
    frame.source_session = local_;
    frame.destination_session = peer_;
    frame.sequence = allocSequence();
    Out& o = out_[free_slot];
    memset(&o, 0, sizeof o);
    o.used = true;
    o.safety = safety;
    o.order = ++order_;
    o.accepted_ms = now;
    o.frame = frame;
    sequence = frame.sequence;
    return true;
}

bool Endpoint::publishLatest(const Frame& frame) { return publishLatest(frame, last_now_); }

bool Endpoint::publishLatest(const Frame& frame, uint32_t now) {
    if (!connected(now) || frame.length > kMaxPayload) return false;
    const int key = streamKey(frame);
    if (key < 0 || isDiscrete(frame)) return false;
    Frame f = frame;
    f.version = kVersion;
    if (!validPayload(f)) return false;
    StreamOut& s = stream_out_[key];
    s.used = true;
    s.order = ++order_;
    s.enq_ms = now;
    s.frame = f;
    return true;
}

bool Endpoint::takeReceived(Frame& frame) {
    uint32_t ms;
    return takeReceived(frame, ms);
}

bool Endpoint::takeReceived(Frame& frame, uint32_t& ms) {
    if (fifo_count_ > 0) {
        frame = fifo_[fifo_head_].frame;
        ms = fifo_[fifo_head_].ms;
        fifo_head_ = static_cast<uint8_t>((fifo_head_ + 1) % kRxFifo);
        --fifo_count_;
        if (frame.type == MessageType::Event) {
            // The receipt is acknowledged only once the application owns the event.
            CacheEntry* c = findCache(static_cast<uint8_t>(frame.type), frame.sequence);
            if (c && !c->done) {
                c->done = true;
                c->result = static_cast<uint8_t>(Result::Accepted);
                c->detail = 0;
                c->ms = last_now_;
                if (frame.flags & 1)
                    queueReply(static_cast<uint8_t>(frame.type), frame.sequence, c->result, 0);
            }
        }
        return true;
    }
    for (uint8_t i = 0; i < kStreamKeys; ++i) {
        if (!stream_in_[i].present) continue;
        frame = stream_in_[i].frame;
        ms = stream_in_[i].ms;
        stream_in_[i].present = false;
        return true;
    }
    return false;
}

bool Endpoint::reply(const Frame& request, Result result, uint16_t detail) {
    if (peer_ == 0 || request.source_session != peer_ || request.destination_session != local_) return false;
    if (static_cast<uint8_t>(result) > static_cast<uint8_t>(Result::HardwareError) || detail > 15) return false;
    const bool flagged = (request.flags & 1) != 0;
    const uint8_t type = static_cast<uint8_t>(request.type);
    if (isDiscrete(request)) {
        CacheEntry* c = findCache(type, request.sequence);
        if (!c || c->done) return false;
        c->done = true;
        c->result = static_cast<uint8_t>(result);
        c->detail = detail;
        c->ms = last_now_;
        if (flagged) queueReply(type, request.sequence, c->result, detail);
        return true;
    }
    if (streamKey(request) >= 0 && flagged)
        return queueReply(type, request.sequence, static_cast<uint8_t>(result), detail);
    return false;
}

bool Endpoint::takeCompletion(Completion& out) {
    if (done_count_ == 0) return false;
    out = done_[done_head_];
    done_head_ = static_cast<uint8_t>((done_head_ + 1) % kCompletions);
    --done_count_;
    return true;
}

void Endpoint::complete(const Out& o, Outcome outcome, uint8_t result, uint16_t detail) {
    if (done_count_ == kCompletions) {
        done_head_ = static_cast<uint8_t>((done_head_ + 1) % kCompletions);
        --done_count_;
        ++stats_.completion_overflow;
    }
    Completion& c = done_[(done_head_ + done_count_) % kCompletions];
    c.type = o.frame.type;
    c.sequence = o.frame.sequence;
    c.outcome = outcome;
    c.result = result;
    c.detail = detail;
    ++done_count_;
}

void Endpoint::failAll(Outcome outcome) {
    for (uint8_t i = 0; i < kSlots; ++i) {
        if (!out_[i].used) continue;
        complete(out_[i], outcome, 0, 0);
        out_[i].used = false;
    }
}

// ---- cache and replies ----
Endpoint::CacheEntry* Endpoint::findCache(uint8_t type, uint16_t seq) {
    for (uint8_t i = 0; i < kCache; ++i)
        if (cache_[i].used && cache_[i].type == type && cache_[i].seq == seq) return &cache_[i];
    return 0;
}

Endpoint::CacheEntry* Endpoint::allocCache(uint32_t) {
    CacheEntry* oldest_done = 0;
    for (uint8_t i = 0; i < kCache; ++i) {
        if (!cache_[i].used) return &cache_[i];
        if (cache_[i].done && (!oldest_done || cache_[i].ms < oldest_done->ms)) oldest_done = &cache_[i];
    }
    return oldest_done;
}

bool Endpoint::queueReply(uint8_t type, uint16_t seq, uint8_t result, uint16_t detail) {
    if (peer_ == 0) return false;
    ReplyOut* freeSlot = 0;
    for (uint8_t i = 0; i < kReplies; ++i) {
        ReplyOut& r = replies_[i];
        if (r.used && r.type == type && r.seq == seq) { r.result = result; r.detail = detail; return true; }
        if (!r.used && !freeSlot) freeSlot = &r;
    }
    if (!freeSlot) { ++stats_.reply_queue_full; return false; }
    freeSlot->used = true;
    freeSlot->type = type;
    freeSlot->seq = seq;
    freeSlot->result = result;
    freeSlot->detail = detail;
    return true;
}

// ---- receive ----
void Endpoint::rxPump(uint32_t now) {
    codec_.tick(now);
    for (int n = 0; n < 512; ++n) {
        const int b = port_.read();
        if (b < 0) break;
        Frame f;
        if (codec_.feed(static_cast<uint8_t>(b), now, f) == DecodeResult::FrameReady) handleFrame(f, now);
    }
}

void Endpoint::handleFrame(const Frame& f, uint32_t now) {
    if (f.type == MessageType::Hello) { handleHello(f); return; }
    if (peer_ == 0 || f.source_session != peer_ || f.destination_session != local_) { ++ec_.session; return; }
    if (f.type == MessageType::Heartbeat) { handleHeartbeat(f, now); return; }
    if (!connected(now)) { ++stats_.rx_not_connected; return; }
    const bool flagged = (f.flags & 1) != 0;
    const uint8_t type = static_cast<uint8_t>(f.type);
    if (!isKnownType(type)) {
        ++stats_.unsupported;
        ++ec_.type;
        if (flagged) queueReply(type, f.sequence, static_cast<uint8_t>(Result::Unsupported), 0);
        return;
    }
    if (!validPayload(f)) {
        ++stats_.invalid_payload;
        if (flagged && f.type != MessageType::Reply)
            queueReply(type, f.sequence, static_cast<uint8_t>(Result::InvalidArgument), 0);
        return;
    }
    if (f.type == MessageType::Reply) { handleReply(f); return; }
    const int key = streamKey(f);
    if (key >= 0) handleStream(f, now, key);
    else if (isDiscrete(f)) handleDiscrete(f, now);
}

void Endpoint::handleHello(const Frame& f) {
    if (local_ == 0) return;
    // Boot counters are per-board namespaces, so equal values across roles are legal; the role check below
    // is what rejects a looped-back HELLO from ourselves.
    if (f.destination_session != 0 || f.source_session == 0) { ++ec_.session; return; }
    Hello h;
    if (decode(f, h, ec_) != Status::Ok) { ++stats_.invalid_payload; return; }
    const uint8_t peer_role = role_ == kRoleBody ? kRoleDome : kRoleBody;
    const uint32_t peer_caps = role_ == kRoleBody ? 0x04u : 0x1Bu;
    if (h.role != peer_role || h.capabilities != peer_caps) { ++stats_.hello_rejected; return; }
    if (f.source_session == peer_) return;
    resetPeer(f.source_session);
    hello_due_ = true;
}

void Endpoint::handleHeartbeat(const Frame& f, uint32_t now) {
    Heartbeat h;
    if (decode(f, h, ec_) != Status::Ok) { ++stats_.invalid_payload; return; }
    if (have_hb_ && !newer16(f.sequence, hb_seq_)) { ++stats_.stale_streams; return; }
    have_hb_ = true;
    hb_seq_ = f.sequence;
    last_hb_ms_ = now;
    peer_mode_ = h.mode;
    peer_ready_ = h.ready;
    was_connected_ = true;
}

void Endpoint::handleReply(const Frame& f) {
    Reply r;
    decode(f, r, ec_);
    for (uint8_t i = 0; i < kSlots; ++i) {
        Out& o = out_[i];
        if (!o.used || !o.sent || static_cast<uint8_t>(o.frame.type) != r.request_type ||
            o.frame.sequence != r.request_seq) continue;
        complete(o, Outcome::Replied, r.result, r.detail);
        o.used = false;
        return;
    }
    ++stats_.unmatched_replies;
}

void Endpoint::handleStream(const Frame& f, uint32_t now, int key) {
    if (seq_have_[key] && !newer16(f.sequence, seq_last_[key])) { ++stats_.stale_streams; return; }
    seq_have_[key] = true;
    seq_last_[key] = f.sequence;
    StreamIn& s = stream_in_[key];
    if (s.present && (s.frame.flags & 1))
        queueReply(static_cast<uint8_t>(s.frame.type), s.frame.sequence, static_cast<uint8_t>(Result::Busy), 12);
    s.present = true;
    s.ms = now;
    s.frame = f;
}

void Endpoint::handleDiscrete(const Frame& f, uint32_t now) {
    const bool flagged = (f.flags & 1) != 0;
    const uint8_t type = static_cast<uint8_t>(f.type);
    CacheEntry* c = findCache(type, f.sequence);
    if (c) {
        if (c->length == f.length && memcmp(c->payload, f.payload, f.length) == 0) {
            ++stats_.duplicates;
            if (c->done && flagged) queueReply(type, f.sequence, c->result, c->detail);
        } else {
            // Same key, different content: never executed, and never answered with a rejection that
            // the sender would attribute to the original request. An already-answered original keeps
            // answering with its true result; a still-pending original gets no reply on the shared key.
            ++stats_.sequence_conflicts;
            ++stats_.protocol_failures;
            if (c->done && flagged) queueReply(type, f.sequence, c->result, c->detail);
        }
        return;
    }
    c = fifo_count_ < kRxFifo ? allocCache(now) : 0;
    if (!c) {
        ++stats_.rx_queue_full;
        ++ec_.queue;
        if (flagged) queueReply(type, f.sequence, static_cast<uint8_t>(Result::Busy), 12);
        return;
    }
    memset(c, 0, sizeof *c);
    c->used = true;
    c->type = type;
    c->seq = f.sequence;
    c->length = f.length;
    memcpy(c->payload, f.payload, f.length);
    c->ms = now;
    RxItem& item = fifo_[(fifo_head_ + fifo_count_) % kRxFifo];
    item.frame = f;
    item.ms = now;
    ++fifo_count_;
}

// A fragment already on the wire is closed with a delimiter before anything else is sent, so the
// next frame can never be glued onto it.
void Endpoint::dropWire() {
    if (wire_len_ > 0 && wire_off_ > 0) need_delim_ = true;
    wire_len_ = wire_off_ = 0;
    wire_partial_counted_ = false;
}

// Peer lost without a new session: every authority granted by the old link ends. Completed results
// stay as replay guards; unanswered requests become explicit WrongEpoch failures so an old
// retransmission can neither execute nor look accepted.
void Endpoint::dropLink(uint32_t now) {
    failAll(Outcome::PeerLost);
    memset(stream_out_, 0, sizeof stream_out_);
    memset(replies_, 0, sizeof replies_);
    memset(seq_have_, 0, sizeof seq_have_);
    clearRxState();
    dropWire();
    peer_mode_ = peer_ready_ = 0;
    ++generation_;
    for (uint8_t i = 0; i < kCache; ++i) {
        CacheEntry& c = cache_[i];
        if (!c.used) continue;
        if (!c.done) {
            c.done = true;
            c.result = static_cast<uint8_t>(Result::WrongEpoch);
            c.detail = 0;
        }
        c.ms = now;
    }
}

void Endpoint::clearRxState() {
    fifo_head_ = fifo_count_ = 0;
    memset(stream_in_, 0, sizeof stream_in_);
}

void Endpoint::resetPeer(uint32_t new_peer) {
    if (peer_ != 0) ++stats_.session_changes;
    failAll(Outcome::SessionChanged);
    memset(stream_out_, 0, sizeof stream_out_);
    memset(replies_, 0, sizeof replies_);
    memset(cache_, 0, sizeof cache_);
    memset(seq_have_, 0, sizeof seq_have_);
    clearRxState();
    dropWire();
    have_hb_ = false;
    was_connected_ = false;
    hb_sent_ = false;
    peer_ = new_peer;
    ++generation_;
}

// ---- time-driven work ----
void Endpoint::housekeeping(uint32_t now) {
    if (peer_ != 0 && was_connected_ && !connected(now)) {
        ++stats_.link_losses;
        was_connected_ = false;
        dropLink(now);
    }
    for (uint8_t i = 0; i < kSlots; ++i) {
        Out& o = out_[i];
        if (!o.used) continue;
        if (!o.sent && static_cast<uint32_t>(now - o.accepted_ms) >= kRequestDeadlineMs) {
            ++stats_.unsent_expired;
            complete(o, Outcome::Unsent, 0, 0);
            o.used = false;
        } else if (o.sent && static_cast<uint32_t>(now - o.first_tx_ms) >= kRequestDeadlineMs) {
            ++stats_.request_timeouts;
            complete(o, Outcome::TimedOut, 0, 0);
            o.used = false;
        }
    }
    for (uint8_t i = 0; i < kCache; ++i) {
        CacheEntry& c = cache_[i];
        if (!c.used || static_cast<uint32_t>(now - c.ms) < kCacheMs) continue;
        if (!c.done) ++stats_.pending_expired;
        c.used = false;
    }
}

void Endpoint::stampStream(Frame& f, uint8_t flags) {
    f.version = kVersion;
    f.flags = flags;
    f.source_session = local_;
    f.destination_session = peer_;
    f.sequence = stream_seq_++;
}

bool Endpoint::encodeInto(const Frame& f) {
    const size_t n = Codec::encode(f, wire_, sizeof wire_);
    wire_off_ = 0;
    wire_partial_counted_ = false;
    if (n == 0) { ++stats_.encode_failures; wire_len_ = 0; return false; }
    wire_len_ = n;
    return true;
}

bool Endpoint::commitStream(StreamOut& s, uint32_t now) {
    Frame f = s.frame;
    const uint32_t residence = static_cast<uint32_t>(now - s.enq_ms);
    s.used = false;
    bool ok = true;
    switch (f.type) {
        case MessageType::RcStatus: ok = addAge<RcStatus>(f, residence, ec_); break;
        case MessageType::VescStatus: ok = addAge<VescStatus>(f, residence, ec_); break;
        case MessageType::HallState: ok = addAge<HallState>(f, residence, ec_); break;
        default: break;
    }
    if (!ok) { ++stats_.encode_failures; return false; }
    stampStream(f, f.flags & 1);
    return encodeInto(f);
}

bool Endpoint::startNext(uint32_t now) {
    Frame f;
    memset(&f, 0, sizeof f);
    const bool is_connected = connected(now);
    if (hello_due_ || (!is_connected && (!hello_sent_ || static_cast<uint32_t>(now - last_hello_ms_) >= kHelloMs))) {
        Hello h = {role_, role_ == kRoleBody ? 0x1Bu : 0x04u, kSafetyRevision};
        hello_due_ = false;
        hello_sent_ = true;
        last_hello_ms_ = now;
        f.version = kVersion;
        if (encode(h, f, ec_) != Status::Ok) { ++stats_.encode_failures; return true; }
        stampStream(f, 0);
        f.destination_session = 0;
        encodeInto(f);
        return true;
    }
    if (peer_ == 0) return false;
    if (!hb_sent_ || static_cast<uint32_t>(now - last_hb_tx_ms_) >= kHeartbeatMs) {
        Heartbeat hb = {mode_, ready_};
        hb_sent_ = true;
        last_hb_tx_ms_ = now;
        f.version = kVersion;
        if (encode(hb, f, ec_) != Status::Ok) { ++stats_.encode_failures; return true; }
        stampStream(f, 0);
        encodeInto(f);
        return true;
    }
    for (uint8_t i = 0; i < kReplies; ++i) {
        ReplyOut& r = replies_[i];
        if (!r.used) continue;
        Reply m = {r.type, r.seq, r.result, r.detail};
        r.used = false;
        f.version = kVersion;
        if (encode(m, f, ec_) != Status::Ok) { ++stats_.encode_failures; return true; }
        stampStream(f, 0);
        encodeInto(f);
        return true;
    }
    int best = -1;
    for (uint8_t i = 0; i < kSlots; ++i) {
        const Out& o = out_[i];
        if (!o.used) continue;
        const bool due = !o.sent ||
            (o.tx_count < kMaxTransmissions && static_cast<uint32_t>(now - o.last_tx_ms) >= kRetryMs);
        if (!due) continue;
        if (best < 0 || (o.safety && !out_[best].safety) ||
            (o.safety == out_[best].safety && o.order < out_[best].order)) best = i;
    }
    if (best >= 0) {
        Out& o = out_[best];
        if (!o.sent) { o.sent = true; o.first_tx_ms = now; }
        else ++stats_.retransmits;
        ++o.tx_count;
        o.last_tx_ms = now;
        if (!encodeInto(o.frame)) {
            complete(o, Outcome::EncodeFailed, 0, 0);
            o.used = false;
        }
        return true;
    }
    int oldest = -1;
    for (uint8_t i = 0; i < kStreamKeys; ++i)
        if (stream_out_[i].used && (oldest < 0 || stream_out_[i].order < stream_out_[oldest].order)) oldest = i;
    if (oldest >= 0) { commitStream(stream_out_[oldest], now); return true; }
    return false;
}

void Endpoint::txPump(uint32_t now) {
    for (;;) {
        if (need_delim_) {
            if (port_.writable() == 0) return;
            const uint8_t zero = 0;
            if (port_.write(&zero, 1) != 1) return;
            need_delim_ = false;
        }
        if (wire_len_ > 0) {
            const size_t remaining = wire_len_ - wire_off_;
            const size_t room = port_.writable();
            if (room == 0) return;
            const size_t want = remaining < room ? remaining : room;
            wire_off_ += port_.write(wire_ + wire_off_, want);
            if (wire_off_ < wire_len_) {
                if (!wire_partial_counted_) { ++stats_.partial_writes; wire_partial_counted_ = true; }
                return;
            }
            wire_len_ = 0;
        }
        if (port_.writable() == 0) return;
        if (!startNext(now)) return;
    }
}

void Endpoint::tick(uint32_t now) {
    last_now_ = now;
    if (local_ == 0) return;
    rxPump(now);
    housekeeping(now);
    txPump(now);
}

}  // namespace r2link
