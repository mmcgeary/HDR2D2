#include "../Codec.h"
#include "../Bytes.h"

namespace r2link {

uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0;
    if (!data) return crc;
    for (size_t i = 0; i < length; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

bool newer16(uint16_t candidate, uint16_t previous) {
    const uint16_t diff = static_cast<uint16_t>(candidate - previous);
    return diff != 0 && diff < 0x8000;
}

ErrorCounters& Codec::encodeCounters() {
    static ErrorCounters counters;
    return counters;
}

static size_t cobsEncode(const uint8_t* in, size_t n, uint8_t* out) {
    size_t code_at = 0, w = 1;
    uint8_t code = 1;
    for (size_t i = 0; i < n; ++i) {
        if (in[i] == 0) {
            out[code_at] = code;
            code_at = w++;
            code = 1;
        } else {
            out[w++] = in[i];
            if (++code == 0xFF) {
                out[code_at] = code;
                code_at = w++;
                code = 1;
            }
        }
    }
    out[code_at] = code;
    return w;
}

// In-place decode; returns false on a zero inside a block or a truncated block.
static bool cobsDecode(uint8_t* data, size_t n, size_t& decoded) {
    size_t r = 0, w = 0;
    while (r < n) {
        const uint8_t code = data[r++];
        if (code == 0 || r + (code - 1) > n) return false;
        for (uint8_t i = 1; i < code; ++i) data[w++] = data[r++];
        if (code != 0xFF && r < n) data[w++] = 0;
    }
    decoded = w;
    return true;
}

size_t Codec::encode(const Frame& f, uint8_t* out, size_t capacity) {
    ErrorCounters& c = encodeCounters();
    if (!out) { ++c.null_argument; return 0; }
    if (f.version != kVersion) { ++c.version; return 0; }
    if (f.length > kMaxPayload) { ++c.length; return 0; }
    if ((f.flags & ~1u) != 0) { ++c.reserved; return 0; }
    if (!isKnownType(static_cast<uint8_t>(f.type))) { ++c.type; return 0; }
    uint8_t raw[kMaxRaw];
    Writer w(raw, sizeof(raw));
    w.u8(f.version);
    w.u8(static_cast<uint8_t>(f.type));
    w.u8(f.flags);
    w.u8(0);
    w.u16(f.sequence);
    w.u32(f.source_session);
    w.u32(f.destination_session);
    w.u16(f.length);
    for (size_t i = 0; i < f.length; ++i) w.u8(f.payload[i]);
    w.u16(crc16(raw, w.size()));
    const size_t raw_len = w.size();
    uint8_t wire[kMaxWire];
    const size_t n = cobsEncode(raw, raw_len, wire);
    if (n + 1 > capacity) { ++c.capacity; return 0; }
    for (size_t i = 0; i < n; ++i) out[i] = wire[i];
    out[n] = 0;
    return n + 1;
}

bool Codec::expired(uint32_t now_ms) const {
    return count_ > 0 && static_cast<uint32_t>(now_ms - last_ms_) >= kParserTimeoutMs;
}

void Codec::tick(uint32_t now_ms) {
    if (expired(now_ms)) {
        if (!discarding_) ++counters_.timeout;
        reset();
    }
}

DecodeResult Codec::feed(uint8_t byte, uint32_t now_ms, Frame& out) {
    tick(now_ms);
    if (byte == 0) {
        if (discarding_) { reset(); return DecodeResult::None; }
        if (count_ == 0) return DecodeResult::None;
        return finish(out);
    }
    if (discarding_) { last_ms_ = now_ms; return DecodeResult::None; }
    if (count_ >= kMaxWire - 1) {
        ++counters_.overflow;
        discarding_ = true;
        last_ms_ = now_ms;
        return DecodeResult::Error;
    }
    buffer_[count_++] = byte;
    last_ms_ = now_ms;
    return DecodeResult::None;
}

DecodeResult Codec::finish(Frame& out) {
    size_t raw_len = 0;
    const bool cobs_ok = cobsDecode(buffer_, count_, raw_len);
    reset();
    if (!cobs_ok) { ++counters_.cobs; return DecodeResult::Error; }
    if (raw_len < kHeaderSize + 2 || raw_len > kMaxRaw) { ++counters_.length; return DecodeResult::Error; }
    const uint16_t wire_crc = static_cast<uint16_t>(buffer_[raw_len - 2] | (buffer_[raw_len - 1] << 8));
    if (crc16(buffer_, raw_len - 2) != wire_crc) { ++counters_.crc; return DecodeResult::Error; }
    Reader r(buffer_, raw_len - 2);
    Frame f;
    f.version = r.u8();
    const uint8_t type = r.u8();
    f.flags = r.u8();
    const uint8_t reserved = r.u8();
    f.sequence = r.u16();
    f.source_session = r.u32();
    f.destination_session = r.u32();
    f.length = r.u16();
    if (f.version != kVersion) { ++counters_.version; return DecodeResult::Error; }
    if (f.length > kMaxPayload || raw_len != kHeaderSize + f.length + 2) {
        ++counters_.length;
        return DecodeResult::Error;
    }
    if ((f.flags & ~1u) != 0 || reserved != 0) { ++counters_.reserved; return DecodeResult::Error; }
    if (!isKnownType(type)) { ++counters_.type; return DecodeResult::Error; }
    f.type = static_cast<MessageType>(type);
    for (size_t i = 0; i < kMaxPayload; ++i) f.payload[i] = i < f.length ? buffer_[kHeaderSize + i] : 0;
    out = f;
    return DecodeResult::FrameReady;
}

}  // namespace r2link
