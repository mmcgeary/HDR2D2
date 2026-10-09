#pragma once
// Host fixtures for the portable body/dome link: a scriptable BytePort, a
// two-endpoint rig that moves whole frames between ports, and fake storage.
#include "R2BodyLink/Endpoint.h"
#include "body/ConfigStore.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <vector>

namespace fakes {

typedef std::vector<uint8_t> Bytes;

struct FakePort : public r2link::BytePort {
    std::deque<uint8_t> rx;
    Bytes tx;
    size_t window;       // bytes the port accepts right now (non-blocking writable space)
    size_t per_call;     // bytes accepted by one write call
    FakePort() : window(1u << 30), per_call(1u << 30) {}
    int read() override {
        if (rx.empty()) return -1;
        const int v = rx.front();
        rx.pop_front();
        return v;
    }
    size_t writable() const override { return window; }
    size_t write(const uint8_t* data, size_t n) override {
        size_t take = n < window ? n : window;
        if (take > per_call) take = per_call;
        tx.insert(tx.end(), data, data + take);
        if (window < (1u << 30)) window -= take;
        return take;
    }
    void push(const Bytes& b) { rx.insert(rx.end(), b.begin(), b.end()); }
};

inline Bytes wireOf(const r2link::Frame& f) {
    uint8_t buf[r2link::kMaxWire];
    const size_t n = r2link::Codec::encode(f, buf, sizeof buf);
    assert(n > 0);
    return Bytes(buf, buf + n);
}

template <class T>
r2link::Frame frameOf(const T& m) {
    r2link::Frame f;
    memset(&f, 0, sizeof f);
    f.version = r2link::kVersion;
    r2link::ErrorCounters c;
    const r2link::Status s = r2link::encode(m, f, c);
    assert(s == r2link::Status::Ok);
    (void)s;
    return f;
}

inline r2link::Frame stamp(r2link::Frame f, uint16_t seq, uint32_t src, uint32_t dst, uint8_t flags) {
    f.version = r2link::kVersion;
    f.sequence = seq;
    f.source_session = src;
    f.destination_session = dst;
    f.flags = flags;
    return f;
}

// body <-> dome rig. Frames written by one endpoint are moved to the peer's
// port before that peer is ticked.
struct Rig {
    FakePort bport, dport;
    r2link::Endpoint body, dome;
    uint32_t now;
    bool raw_bytes;                       // forward raw bytes instead of whole frames
    int drop_to_dome[256], drop_to_body[256];
    std::vector<r2link::Frame> to_dome, to_body;   // every frame written, decoded
    Bytes partial[2];
    r2link::Codec decoder[2];

    Rig(uint32_t body_session = 0xB0B0, uint32_t dome_session = 0xD0D0)
        : body(bport, r2link::kRoleBody, body_session),
          dome(dport, r2link::kRoleDome, dome_session), now(1000), raw_bytes(false) {
        memset(drop_to_dome, 0, sizeof drop_to_dome);
        memset(drop_to_body, 0, sizeof drop_to_body);
    }

    void move(FakePort& from, FakePort& to, int idx, int* drops, std::vector<r2link::Frame>& log) {
        Bytes bytes;
        bytes.swap(from.tx);
        if (raw_bytes) {
            to.push(bytes);
            for (size_t i = 0; i < bytes.size(); ++i) {
                r2link::Frame f;
                if (decoder[idx].feed(bytes[i], now, f) == r2link::DecodeResult::FrameReady) log.push_back(f);
            }
            return;
        }
        for (size_t i = 0; i < bytes.size(); ++i) {
            partial[idx].push_back(bytes[i]);
            if (bytes[i] != 0) continue;
            r2link::Frame f;
            bool ready = false;
            r2link::Codec c;
            for (size_t k = 0; k < partial[idx].size(); ++k)
                if (c.feed(partial[idx][k], now, f) == r2link::DecodeResult::FrameReady) ready = true;
            if (ready) {
                log.push_back(f);
                const uint8_t t = static_cast<uint8_t>(f.type);
                if (drops[t] > 0) --drops[t];
                else to.push(partial[idx]);
            } else {
                to.push(partial[idx]);
            }
            partial[idx].clear();
        }
    }

    void step(uint32_t ms = 1) {
        now += ms;
        move(dport, bport, 0, drop_to_body, to_body);
        body.tick(now);
        move(bport, dport, 1, drop_to_dome, to_dome);
        dome.tick(now);
    }
    void run(uint32_t ms) { for (uint32_t i = 0; i < ms; ++i) step(1); }
    void connect() {
        for (int i = 0; i < 1000 && !(body.connected(now) && dome.connected(now)); ++i) step(1);
        assert(body.connected(now) && dome.connected(now));
    }
    size_t countToBody(r2link::MessageType t) const {
        size_t n = 0;
        for (size_t i = 0; i < to_body.size(); ++i) if (to_body[i].type == t) ++n;
        return n;
    }
    size_t countToDome(r2link::MessageType t) const {
        size_t n = 0;
        for (size_t i = 0; i < to_dome.size(); ++i) if (to_dome[i].type == t) ++n;
        return n;
    }
};

// Storage fake with explicit read/write failures, torn writes and silent loss.
struct FakeStorage : public body::RawStorage {
    Bytes mem;
    size_t writes;                 // bytes written
    long fail_write_after;         // -1: never; else bytes still accepted before WriteError
    bool fail_read;
    bool drop_writes;              // reports success without storing
    size_t fail_read_at;
    FakeStorage(size_t size = 4096)
        : mem(size, 0xFF), writes(0), fail_write_after(-1), fail_read(false), drop_writes(false), fail_read_at(static_cast<size_t>(-1)) {}
    size_t size() const override { return mem.size(); }
    body::StorageResult read(size_t addr, uint8_t* dst, size_t n) override {
        if (addr + n > mem.size()) return body::StorageResult::OutOfRange;
        if (fail_read && (fail_read_at == static_cast<size_t>(-1) || (fail_read_at >= addr && fail_read_at < addr + n))) return body::StorageResult::ReadError;
        for (size_t i = 0; i < n; ++i) dst[i] = mem[addr + i];
        return body::StorageResult::Ok;
    }
    body::StorageResult write(size_t addr, const uint8_t* src, size_t n) override {
        if (addr + n > mem.size()) return body::StorageResult::OutOfRange;
        if (drop_writes) return body::StorageResult::Ok;
        for (size_t i = 0; i < n; ++i) {
            if (fail_write_after == 0) return body::StorageResult::WriteError;
            if (fail_write_after > 0) --fail_write_after;
            mem[addr + i] = src[i];
            ++writes;
        }
        return body::StorageResult::Ok;
    }
};

}  // namespace fakes
