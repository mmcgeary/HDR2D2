#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Messages.h"

namespace r2link {

const uint8_t kVersion = 1;
const size_t kHeaderSize = 16;
const size_t kMaxPayload = 96;
const size_t kMaxRaw = kHeaderSize + kMaxPayload + 2;      // 114
const size_t kMaxWire = kMaxRaw + 2;                        // 116 with delimiter
const uint32_t kParserTimeoutMs = 20;

struct Frame {
    uint8_t version;
    MessageType type;
    uint8_t flags;
    uint16_t sequence;
    uint32_t source_session;
    uint32_t destination_session;
    uint16_t length;
    uint8_t payload[kMaxPayload];
};

enum class DecodeResult : uint8_t { None, FrameReady, Error };

uint16_t crc16(const uint8_t* data, size_t length);
bool newer16(uint16_t candidate, uint16_t previous);

class Codec {
public:
    Codec() { reset(); }
    // Returns zero on invalid frame, null pointer or insufficient capacity.
    static size_t encode(const Frame& frame, uint8_t* out, size_t capacity);
    DecodeResult feed(uint8_t byte, uint32_t now_ms, Frame& out);
    void tick(uint32_t now_ms);
    const ErrorCounters& counters() const { return counters_; }
    // Counts encode failures, which have no parser instance.
    static ErrorCounters& encodeCounters();
private:
    void reset() { count_ = 0; discarding_ = false; last_ms_ = 0; }
    bool expired(uint32_t now_ms) const;
    DecodeResult finish(Frame& out);
    uint8_t buffer_[kMaxWire];
    size_t count_;
    bool discarding_;
    uint32_t last_ms_;
    ErrorCounters counters_;
};

// Typed Frame overloads: leave the destination untouched on failure.
template <class T>
Status encode(const T& message, Frame& frame, ErrorCounters& counters) {
    uint8_t payload[kMaxPayload];
    size_t length = 0;
    const Status s = encodePayload(message, payload, sizeof(payload), length, counters);
    if (s != Status::Ok) return s;
    frame.type = T::type();
    frame.length = static_cast<uint16_t>(length);
    for (size_t i = 0; i < kMaxPayload; ++i) frame.payload[i] = i < length ? payload[i] : 0;
    return Status::Ok;
}

template <class T>
Status decode(const Frame& frame, T& message, ErrorCounters& counters) {
    if (frame.type != T::type()) return countStatus(Status::BadType, counters);
    if (frame.length > kMaxPayload) return countStatus(Status::BadLength, counters);
    return decodePayload(frame.payload, frame.length, message, counters);
}

}  // namespace r2link
