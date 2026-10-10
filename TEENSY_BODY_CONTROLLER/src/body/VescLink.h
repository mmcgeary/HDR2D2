#pragma once
#include <stddef.h>
#include <stdint.h>
#include "BytePort.h"
#include "body/ConfigStore.h"
#include "body/IbusTelemetry.h"

namespace body {

// No firmware whitelist. fromSaved injects external acceptance; packet shape
// alone cannot construct an approved profile. This is not a paired drive gate.
class VescProfile {
public:
    VescProfile();
    static VescProfile fromSaved(const CommissioningProfile& saved, uint8_t wheel);
private:
    friend class VescCodec;
    friend class VescLink;
    uint8_t major_, minor_, layout_;
    bool config_accepted_, control_accepted_;
    uint32_t brake_ma_;
};

struct VescSample {
    uint8_t wheel, fw_major, fw_minor;
    uint16_t valid_fields, source_age_ms, pack_cV;
    int32_t motor_mA, input_mA, erpm;
    int16_t mosfet_dC, motor_dC, duty_permille;
    uint8_t fault;
    uint32_t sample_ms;
    bool valid, profile_match, stale, unsupported, motor_temperature_valid;
};

// Call with sample(now) for BOTH links. Invalid/stale/faulted/unapproved
// sources never become zero-valued but valid handheld measurements.
SensorSnapshot vescMeasurements(const VescSample& left, const VescSample& right);

class VescCodec {
public:
    static size_t encodeDuty(int16_t permille, uint8_t* out, size_t capacity);
    static size_t encodeBrake(uint32_t current_mA, uint8_t* out, size_t capacity);
    // Payload includes command byte, not framing. Atomic: output unchanged on
    // failure. Requires accepted layout, but observed firmware is Link's gate.
    static bool decodeValues(const uint8_t*, size_t, const VescProfile&, VescSample&);
};

struct VescCounters {
    uint32_t frames, crc_errors, terminator_errors, frame_timeouts;
    uint32_t oversized_frames, unsupported_packets, invalid_values;
    uint32_t query_timeouts, partial_writes, aborted_commands, inhibited_commands;
};

struct VescCapture {
    uint8_t wheel;
    uint16_t length;
    uint8_t bytes[263]; // 256 payload + long header + CRC + terminator
};

// Loop-context only. Each tick consumes at most 64 RX bytes and writes at most
// kTxBudget (32) bytes: finish any partial frame, then start at most two new
// frames; a frame after the first write of the tick starts only if it fits
// whole. Order: brake > waiting query > duty > due query, so a due query goes
// out in the same tick after control when room allows, and a query deferred by
// control once goes ahead of a duty renewal next tick (<=6 bytes, <1ms at
// 115200). Brake never waits on an unstarted query or an outstanding reply.
// FW query first; 100ms query cadence, one outstanding, 150ms reply timeout.
// Frame expiry 50ms from header (not last byte). Values fresh through 500ms.
// Control is replaceable live demand, not a FIFO. An in-flight query finishes
// before a command; superseded partial duty is deliberately invalidated before
// its terminator so a receiver cannot execute the old command. Brake takes
// priority until completed; intervening duty requests are dropped. Identical
// live renewals preserve a partial frame. Unrenewed duty expires after 20ms
// (setters use the last tick time; call tick regularly). Duty requires fresh
// valid unfaulted telemetry. Brake requires only actuator-accepted profile and
// observed matching firmware (it stays available when telemetry is stale) and
// must equal the saved approved brake magnitude; no guessed current is sent.
class VescLink {
public:
    VescLink(r2link::BytePort&, uint8_t wheel);
    void tick(uint32_t now_ms);
    void setDuty(int16_t permille);
    void setBrake(uint32_t current_mA);
    // Cancel demand/unsafe unsent suffixes without transmitting guessed current.
    void disableControl();
    VescSample sample(uint32_t now_ms) const;
    void setProfile(const VescProfile&);
    const VescCounters& counters() const { return counters_; }
    // Explicit one-packet, full validated wire-frame capture; available even
    // for unaccepted firmware/layout. Never streams automatically.
    void requestCapture(uint8_t command = 255);
    bool takeCapture(VescCapture&);
private:
    void parse(uint32_t);
    void discard(size_t);
    void handle(const uint8_t*, size_t, uint32_t);
    static const size_t kTxBudget = 32;
    // An identical brake already written is renewed at this cadence, not on every
    // loop pass, so stationary braking cannot saturate the UART and starve polls.
    static const uint32_t kBrakeRenewMs = 20;
    void invalidateCommand(bool brake);
    void resetSample();
    bool match() const;
    bool brakePermitted() const;
    bool dutyPermitted(uint32_t) const;
    bool queryDue(uint32_t) const;
    bool stage(uint32_t, size_t room);
    void pumpTx(uint32_t);
    void startQuery(uint8_t, uint32_t);
    r2link::BytePort& port_;
    uint8_t wheel_;
    VescProfile profile_;
    VescSample cached_;
    VescCounters counters_;
    bool have_sample_, have_firmware_;
    uint8_t major_, minor_;
    uint8_t rx_[263];
    uint32_t rx_ms_[263];
    size_t rx_length_;
    bool recovering_;
    uint8_t tx_[16], tx_length_, tx_offset_, tx_command_;
    bool tx_aborted_;
    uint8_t outstanding_;
    bool queried_, query_waiting_;
    uint32_t last_query_, query_ms_, last_fw_, now_;
    uint8_t demand_;
    int16_t duty_;
    uint32_t brake_, demand_ms_;
    uint8_t sent_command_;          // last command fully written (0 none, 5 duty, 7 brake)
    uint32_t tx_brake_, sent_brake_, sent_ms_;
    bool capture_armed_, capture_ready_;
    uint8_t capture_command_;
    VescCapture capture_;
};

} // namespace body
