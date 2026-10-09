#pragma once
#include <stddef.h>
#include <stdint.h>

namespace body {

// Zero-based channel indices. CH3/CH10 are deliberately unused; there is no
// radio holo mapping. CH6 describes the switch, not qualified drive readiness.
enum RcChannel : uint8_t {
    kSteering = 0, kThrottle = 1, kUnusedCh3 = 2, kManualDome = 3,
    kDutyRate = 4, kFeetEnable = 5, kMood = 6, kMacroTrigger = 7,
    kAutoDome = 8, kUnusedCh10 = 9
};

struct RcSnapshot {
    uint16_t channels[10];
    uint32_t sample_counter, sample_ms;
    bool valid;
    uint16_t flags; // RC_STATUS bits 0,1,3; bit2 belongs to later qualification.
};

struct IbusInputCounters {
    uint32_t valid_frames, checksum_errors, channel_errors, header_errors, partial_timeouts;
};

// Single loop context, fixed storage. Each byte costs at most one 32-byte
// candidate check and a bounded suffix scan. Invalid frames never publish.
class IbusInput {
public:
    IbusInput();
    void feed(uint8_t byte, uint32_t now_ms);
    void feed(const uint8_t* bytes, size_t length, uint32_t now_ms);
    void tick(uint32_t now_ms);
    RcSnapshot snapshot(uint32_t now_ms) const;
    const IbusInputCounters& counters() const { return counters_; }
private:
    void discard();
    bool accept(uint32_t now_ms);
    uint8_t bytes_[32], length_;
    uint32_t started_ms_;
    RcSnapshot latest_;
    IbusInputCounters counters_;
};

} // namespace body
