#include "IbusInput.h"
#include <string.h>

namespace body {

IbusInput::IbusInput() : bytes_{}, length_(0), arrived_ms_{}, latest_{}, counters_{} {}

void IbusInput::tick(uint32_t now) {
    while (length_ && uint32_t(now - arrived_ms_[0]) >= 5) {
        ++counters_.partial_timeouts;
        discard();
        while (length_ &&
               (bytes_[0] != 0x20 || (length_ >= 2 && bytes_[1] != 0x40))) {
            ++counters_.header_errors;
            discard();
        }
    }
}

void IbusInput::discard() {
    --length_;
    memmove(bytes_, bytes_ + 1, length_);
    memmove(arrived_ms_, arrived_ms_ + 1, length_ * sizeof arrived_ms_[0]);
}

bool IbusInput::accept(uint32_t now) {
    uint16_t sum = 0xffff;
    for (uint8_t i = 0; i < 30; ++i) sum -= bytes_[i];
    if (sum != uint16_t(bytes_[30] | uint16_t(bytes_[31]) << 8)) {
        ++counters_.checksum_errors;
        return false;
    }
    uint16_t channels[14];
    bool good = true;
    for (uint8_t i = 0; i < 14; ++i) {
        // Low 12 bits only: newer receivers pack channels 15-18 into the top nibbles.
        channels[i] = (bytes_[2 + 2*i] | uint16_t(bytes_[3 + 2*i]) << 8) & 0x0FFF;
        if (channels[i] < 900 || channels[i] > 2100) good = false;
    }
    if (!good) {
        ++counters_.channel_errors;
        return false;
    }
    for (uint8_t i = 0; i < 10; ++i) {
        const uint16_t v = channels[i];
        latest_.channels[i] = v < 1000 ? 1000 : (v > 2000 ? 2000 : v);
    }
    ++latest_.sample_counter;
    latest_.sample_ms = now;
    latest_.valid = true;
    ++counters_.valid_frames;
    return true;
}

void IbusInput::feed(uint8_t byte, uint32_t now) {
    tick(now);
    arrived_ms_[length_] = now;
    bytes_[length_++] = byte;
    while (length_) {
        if (bytes_[0] != 0x20 || (length_ >= 2 && bytes_[1] != 0x40)) {
            ++counters_.header_errors;
            discard();
        } else if (length_ == sizeof bytes_) {
            if (accept(now)) {
                length_ = 0;
                return;
            }
            // Retain an embedded header after a corrupt/truncated candidate.
            discard();
        } else {
            return;
        }
    }
}

void IbusInput::feed(const uint8_t* bytes, size_t length, uint32_t now) {
    for (size_t i = 0; i < length; ++i) feed(bytes[i], now);
}

RcSnapshot IbusInput::snapshot(uint32_t now) const {
    RcSnapshot s = latest_;
    s.valid = latest_.valid && uint32_t(now - s.sample_ms) <= 250;
    s.flags = 0;
    if (s.valid) {
        s.flags = 1;
        if (s.channels[kFeetEnable] >= 1750) s.flags |= 2;
        if (s.channels[kAutoDome] >= 1750) s.flags |= 8;
    }
    return s;
}

} // namespace body
