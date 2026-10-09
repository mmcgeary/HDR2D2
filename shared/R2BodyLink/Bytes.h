#pragma once
#include <stddef.h>
#include <stdint.h>

namespace r2link {

// Bounds-checked explicit little-endian serialization; never copies structs.
class Writer {
public:
    Writer(uint8_t* data, size_t capacity) : data_(data), cap_(capacity), pos_(0), ok_(data != 0) {}
    void u8(uint8_t v) { put(v, 1); }
    void u16(uint16_t v) { put(v, 2); }
    void u32(uint32_t v) { put(v, 4); }
    void i16(int16_t v) { put(static_cast<uint16_t>(v), 2); }
    void i32(int32_t v) { put(static_cast<uint32_t>(v), 4); }
    bool ok() const { return ok_; }
    size_t size() const { return pos_; }
private:
    void put(uint32_t v, size_t n) {
        if (!ok_ || pos_ + n > cap_) { ok_ = false; return; }
        for (size_t i = 0; i < n; ++i) data_[pos_++] = static_cast<uint8_t>(v >> (8 * i));
    }
    uint8_t* data_;
    size_t cap_, pos_;
    bool ok_;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t length) : data_(data), len_(length), pos_(0), ok_(data != 0) {}
    uint8_t u8() { return static_cast<uint8_t>(get(1)); }
    uint16_t u16() { return static_cast<uint16_t>(get(2)); }
    uint32_t u32() { return get(4); }
    int16_t i16() { return static_cast<int16_t>(get(2)); }
    int32_t i32() { return static_cast<int32_t>(get(4)); }
    bool ok() const { return ok_; }
    size_t remaining() const { return len_ - pos_; }
private:
    uint32_t get(size_t n) {
        if (!ok_ || pos_ + n > len_) { ok_ = false; return 0; }
        uint32_t v = 0;
        for (size_t i = 0; i < n; ++i) v |= static_cast<uint32_t>(data_[pos_++]) << (8 * i);
        return v;
    }
    const uint8_t* data_;
    size_t len_, pos_;
    bool ok_;
};

}  // namespace r2link
