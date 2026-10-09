#pragma once
// Reliable body/dome link endpoint over a non-blocking BytePort.
//
// Threading: every method, including the static Codec encode counters it
// touches, must be called from the single control-loop context. There is no
// heap use and nothing waits; tick() does bounded work and returns.
//
// Unknown message types: framing accepts types 0x01..0x3F (isFramingType). A
// defined-range type this build does not implement reaches the endpoint, which
// counts it and answers UNSUPPORTED (Result::Unsupported, echoing the raw
// type) when the request asked for a reply. Types outside the range are
// rejected and counted by the codec before the endpoint sees them.
//
// Application API summary:
//   request()        queue a discrete reliable request (retried, deduplicated)
//   publishLatest()  queue a latest-value stream frame (one slot per key)
//   takeReceived()   poll delivered frames (discrete FIFO first, then streams)
//   reply()          answer a delivered frame (the frame is the reply handle)
//   takeCompletion() poll terminal outcomes of this endpoint's own requests
#include <stddef.h>
#include <stdint.h>
#include "BytePort.h"
#include "Codec.h"

namespace r2link {

enum class Result : uint8_t {
    Accepted = 0, InvalidArgument, NotReady, ManualOverride, Inhibited,
    Unsupported, Busy, WrongEpoch, HardwareError
};
enum class Outcome : uint8_t { Replied, TimedOut, Unsent, PeerLost, SessionChanged, EncodeFailed };
enum class Reject : uint8_t { None, NotConnected, BadFrame, Busy, WrongKind };

struct Completion {
    MessageType type;
    uint16_t sequence;
    Outcome outcome;
    uint8_t result;   // valid for Outcome::Replied
    uint16_t detail;  // valid for Outcome::Replied
};

struct LinkStats {
    uint32_t duplicates, sequence_conflicts, stale_streams, unsupported, invalid_payload;
    uint32_t hello_rejected, session_changes, link_losses, request_timeouts, unsent_expired;
    uint32_t retransmits, partial_writes, rx_queue_full, reply_queue_full, completion_overflow;
    uint32_t encode_failures, unmatched_replies, pending_expired, busy_local, rx_not_connected;
    uint32_t protocol_failures;   // same (type, sequence) re-sent with different content
};

// Link-loss, replay and receipt semantics
//  * Timeout (housekeeping): all pending requests complete PeerLost; stream sequence, rx queues, reply queue,
//    peer mode/ready are cleared and generation() increments. Clients must reinitialise on a generation change.
//    Unfinished cache entries become completed guards answering WrongEpoch(7); finished entries still replay
//    their cached result, so an old retransmit never re-executes after a same-session handshake.
//  * EVENT frames are acked (Accepted) only when the application takes them via takeReceived().
//    Events still queued at link loss are dropped unacked, so the sender sees TimedOut/PeerLost, never a false ack.
//  * Same (type, sequence) with different content: never replayed or executed; counts sequence_conflicts and
//    protocol_failures; a flagged frame gets the original's cached result only if the original is complete.
//    If the original is still pending the sender gets no reply and stays pending until TimedOut (outcome unknown).
//  * A partial frame left on the wire by a reset/drop is terminated with a 0x00 delimiter before new data.
class Endpoint {
public:
    static const uint8_t kSlots = 8, kOrdinarySlots = 6, kStreamKeys = 10, kRxFifo = 8;
    static const uint8_t kCache = 16, kReplies = 8, kCompletions = 16;
    static const uint8_t kMaxTransmissions = 3;
    static const uint32_t kHeartbeatMs = 100, kHelloMs = 500, kLinkTimeoutMs = 300;
    static const uint32_t kRetryMs = 100, kRequestDeadlineMs = 350, kCacheMs = 2000;

    // local_session == 0 disables the link (it never connects).
    Endpoint(BytePort& port, uint8_t role, uint32_t local_session);

    void setLocalState(uint8_t mode, uint8_t ready);
    void tick(uint32_t now_ms);

    bool connected(uint32_t now_ms) const;
    uint32_t localSession() const { return local_; }
    uint32_t peerSession() const { return peer_; }
    uint32_t peerGeneration() const { return generation_; }
    uint8_t peerMode() const { return peer_mode_; }
    uint8_t peerReady() const { return peer_ready_; }
    size_t pendingCount() const;

    // Discrete requests only. Stamps version, flags, sessions and sequence into
    // `frame` and returns the sequence on success. On failure `sequence` is
    // untouched and lastReject() says why.
    bool request(Frame& frame, uint32_t now_ms, uint16_t& sequence);
    Reject lastReject() const { return reject_; }

    // Latest-value streams (one slot per type, or per wheel for VescStatus).
    // A newer frame replaces an unsent older one. Source-age fields have the
    // queue residence added, saturating, when the frame is committed to the port.
    bool publishLatest(const Frame& frame);
    bool publishLatest(const Frame& frame, uint32_t now_ms);

    bool takeReceived(Frame& frame);
    bool takeReceived(Frame& frame, uint32_t& received_ms);

    // `request` must be a frame returned by takeReceived(). Fails for another
    // session, an invalid result, or an already answered request.
    bool reply(const Frame& request, Result result, uint16_t detail);
    bool takeCompletion(Completion& out);

    ErrorCounters counters() const;
    const LinkStats& stats() const { return stats_; }

private:
    struct Out {
        bool used;
        bool sent;
        bool safety;
        uint8_t tx_count;
        uint32_t order, accepted_ms, first_tx_ms, last_tx_ms;
        Frame frame;
    };
    struct StreamOut { bool used; uint32_t order, enq_ms; Frame frame; };
    struct StreamIn { bool present; uint32_t ms; Frame frame; };
    struct RxItem { Frame frame; uint32_t ms; };
    struct ReplyOut { bool used; uint8_t type; uint16_t seq; uint8_t result; uint16_t detail; };
    struct CacheEntry {
        bool used, done, replied;
        uint8_t type;
        uint16_t seq, length;
        uint8_t result;
        uint16_t detail;
        uint32_t ms;
        uint8_t payload[kMaxPayload];
    };

    void rxPump(uint32_t now);
    void handleFrame(const Frame& f, uint32_t now);
    void handleHello(const Frame& f);
    void handleHeartbeat(const Frame& f, uint32_t now);
    void handleReply(const Frame& f);
    void handleStream(const Frame& f, uint32_t now, int key);
    void handleDiscrete(const Frame& f, uint32_t now);
    bool validPayload(const Frame& f);
    void checkLinkTimeout(uint32_t now);
    void housekeeping(uint32_t now);
    void txPump(uint32_t now);
    bool startNext(uint32_t now);
    bool commitStream(StreamOut& s, uint32_t now);
    bool queueReply(uint8_t type, uint16_t seq, uint8_t result, uint16_t detail);
    void complete(const Out& o, Outcome outcome, uint8_t result, uint16_t detail);
    void failAll(Outcome outcome);
    void resetPeer(uint32_t new_peer);
    void clearRxState();
    void dropWire();
    void dropLink(uint32_t now);
    CacheEntry* findCache(uint8_t type, uint16_t seq);
    CacheEntry* allocCache(uint32_t now);
    uint16_t allocSequence();
    bool sequenceLive(uint16_t seq) const;
    bool encodeInto(const Frame& f);
    void stampStream(Frame& f, uint8_t flags);

    static int streamKey(const Frame& f);
    static bool isDiscrete(const Frame& f);
    static bool isSafety(const Frame& f);

    BytePort& port_;
    Codec codec_;
    ErrorCounters ec_;
    LinkStats stats_;
    uint8_t role_;
    uint32_t local_, peer_, generation_;
    uint8_t mode_, ready_, peer_mode_, peer_ready_;
    bool have_hb_, was_connected_, hello_due_, hello_sent_, hb_sent_;
    uint16_t hb_seq_;
    uint32_t last_hb_ms_, last_hello_ms_, last_hb_tx_ms_, last_now_, order_;
    uint16_t next_seq_, stream_seq_;
    Reject reject_;

    Out out_[kSlots];
    StreamOut stream_out_[kStreamKeys];
    StreamIn stream_in_[kStreamKeys];
    bool seq_have_[kStreamKeys];
    uint16_t seq_last_[kStreamKeys];
    RxItem fifo_[kRxFifo];
    uint8_t fifo_head_, fifo_count_;
    ReplyOut replies_[kReplies];
    CacheEntry cache_[kCache];
    Completion done_[kCompletions];
    uint8_t done_head_, done_count_;

    uint8_t wire_[kMaxWire];
    size_t wire_len_, wire_off_;
    bool wire_partial_counted_, need_delim_;
};

}  // namespace r2link
