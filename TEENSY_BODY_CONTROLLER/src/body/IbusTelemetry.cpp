#include "IbusTelemetry.h"
#include <string.h>

namespace body {

IbusTelemetry::IbusTelemetry()
    : measurements_{}, counters_{}, rx_{}, rx_length_(0), rx_started_(0),
      tx_{}, tx_length_(0), tx_offset_(0), command_(0), pending_(false),
      deadline_counted_(false), poll_us_(0), echo_end_(0), echo_us_(0) {}

void IbusTelemetry::setMeasurements(const SensorSnapshot& s) { measurements_ = s; }

void IbusTelemetry::expire(uint32_t now) {
    if (rx_length_ && uint32_t(now - rx_started_) >= 5000) {
        rx_length_ = 0;
        ++counters_.partial_timeouts;
    }
    if (echo_end_ && uint32_t(now - echo_us_) > 1000)
        echo_end_ = 0;
}

void IbusTelemetry::discard() {
    --rx_length_;
    memmove(rx_, rx_ + 1, rx_length_);
}

void IbusTelemetry::handle(uint32_t now) {
    // Match a complete packet, not a speculative byte prefix: a real poll
    // can share length/command bytes with the last local response.
    if (echo_end_ == tx_length_ && rx_[0] == tx_length_ &&
        memcmp(rx_, tx_, tx_length_) == 0) {
        counters_.echo_bytes += tx_length_;
        echo_end_ = 0;
        return;
    }
    if (rx_[0] != 4) {
        ++counters_.ignored_responses;
        return;
    }
    const uint8_t address = rx_[1] & 0x0f, cmd = rx_[1] & 0xf0;
    if ((address != 1 && address != 2) || (cmd != 0x80 && cmd != 0x90 && cmd != 0xa0)) {
        ++counters_.unsupported_polls;
        return;
    }
    ++counters_.valid_polls;
    if (pending_) {
        ++counters_.busy_polls;
        return;
    }
    command_ = rx_[1];
    poll_us_ = now;
    pending_ = true;
    deadline_counted_ = false;
    tx_offset_ = tx_length_ = 0;
    echo_end_ = 0;
}

void IbusTelemetry::feed(uint8_t byte, uint32_t now) {
    expire(now);
    if (!rx_length_) rx_started_ = now;
    rx_[rx_length_++] = byte;
    while (rx_length_) {
        const uint8_t length = rx_[0];
        if (length != 4 && length != 6) {
            ++counters_.header_errors;
            discard();
            continue;
        }
        if (rx_length_ < length) return;
        uint16_t sum = 0xffff;
        for (uint8_t i = 0; i < length - 2; ++i) sum -= rx_[i];
        if (sum != uint16_t(rx_[length - 2] | uint16_t(rx_[length - 1]) << 8)) {
            ++counters_.checksum_errors;
            discard();
            continue;
        }
        handle(now);
        rx_length_ -= length;
        memmove(rx_, rx_ + length, rx_length_);
    }
}

bool IbusTelemetry::buildResponse() {
    const uint8_t address = command_ & 0x0f, cmd = command_ & 0xf0;
    tx_length_ = cmd == 0x80 ? 4 : 6;
    tx_[0] = tx_length_;
    tx_[1] = command_;
    if (cmd == 0x90) {
        tx_[2] = address == 1 ? 0x03 : 0x01;
        tx_[3] = 2;
    } else if (cmd == 0xa0) {
        int32_t value;
        if (address == 1) {
            value = measurements_.voltage_cV;
            if (!measurements_.voltage_valid || value < 0 || value > 65535) return false;
        } else {
            value = measurements_.hottest_mosfet_dC;
            if (!measurements_.temperature_valid || value < -400 || value > 65135) return false;
            value += 400; // range was checked before addition
        }
        tx_[2] = uint8_t(value);
        tx_[3] = uint8_t(uint32_t(value) >> 8);
    }
    uint16_t sum = 0xffff;
    for (uint8_t i = 0; i < tx_length_ - 2; ++i) sum -= tx_[i];
    tx_[tx_length_ - 2] = uint8_t(sum);
    tx_[tx_length_ - 1] = uint8_t(sum >> 8);
    return true;
}

void IbusTelemetry::tick(uint32_t now, r2link::BytePort& port) {
    expire(now);
    if (!pending_) return;
    const uint32_t elapsed = now - poll_us_;
    if (elapsed < 100) return;
    if (elapsed > 1000 && !deadline_counted_) {
        ++counters_.deadline_misses;
        deadline_counted_ = true;
    }
    if (!tx_offset_) {
        if (elapsed > 1000) {
            pending_ = false;
            return;
        }
        // Revalidate immediately before commit, including while waiting for
        // whole-frame capacity: a stale source cannot leak a queued old value.
        if (!buildResponse()) {
            ++counters_.invalid_measurements;
            pending_ = false;
            return;
        }
        if (port.writable() < tx_length_) return;
    }
    size_t n = tx_length_ - tx_offset_;
    const size_t capacity = port.writable();
    if (n > capacity) n = capacity;
    if (!n) return;
    const size_t written = port.write(tx_ + tx_offset_, n);
    if (!written) return;
    // BytePort's contract is written <= n. Production clips to UART capacity.
    tx_offset_ += uint8_t(written);
    echo_end_ = tx_offset_;
    echo_us_ = now;
    if (tx_offset_ == tx_length_) {
        pending_ = false;
        ++counters_.responses;
    } else {
        ++counters_.partial_writes;
    }
}

} // namespace body
