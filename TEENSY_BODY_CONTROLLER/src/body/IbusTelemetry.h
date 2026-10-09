#pragma once
#include <stdint.h>
#include "BytePort.h"

namespace body {

// Wide signed fields allow validation before any narrowing/addition. The
// supplier must set each validity false when either VESC source is stale
// (>500ms) or invalid; use the lower voltage and hotter MOSFET temperature.
// No source clocks/aggregation or VESC implementation belongs to this module.
struct SensorSnapshot {
    int32_t voltage_cV, hottest_mosfet_dC;
    bool voltage_valid, temperature_valid;
};

struct IbusTelemetryCounters {
    uint32_t valid_polls, checksum_errors, header_errors, partial_timeouts;
    uint32_t unsupported_polls, ignored_responses, echo_bytes, busy_polls;
    uint32_t invalid_measurements, responses, partial_writes, deadline_misses;
};

// Fixed single response slot; a new poll never replaces/interleaves it.
// Starts no sooner than 100us, and only with room for the entire response.
// Unstarted responses expire after 1ms. A transport returning a partial write
// retains the committed suffix, even on a missed deadline (counted once).
// One write per tick, no wait/flush; drive tick at sub-ms intervals.
class IbusTelemetry {
public:
    IbusTelemetry();
    void setMeasurements(const SensorSnapshot& measurements);
    void feed(uint8_t byte, uint32_t now_us);
    void tick(uint32_t now_us, r2link::BytePort& port);
    const IbusTelemetryCounters& counters() const { return counters_; }
private:
    void expire(uint32_t now);
    void discard();
    void handle(uint32_t now);
    bool buildResponse();
    SensorSnapshot measurements_;
    IbusTelemetryCounters counters_;
    uint8_t rx_[6], rx_length_;
    uint32_t rx_started_;
    uint8_t tx_[6], tx_length_, tx_offset_, command_;
    bool pending_, deadline_counted_;
    uint32_t poll_us_;
    uint8_t echo_end_;
    uint32_t echo_us_;
};

} // namespace body
