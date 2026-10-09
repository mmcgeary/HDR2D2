#pragma once
#include <stddef.h>
#include <stdint.h>
#include "Bytes.h"

namespace r2link {

enum class MessageType : uint8_t {
    Hello = 0x01, Heartbeat = 0x02,
    RcStatus = 0x10, VescStatus = 0x11, BodyStatus = 0x12, HallState = 0x13,
    DomeRequest = 0x20, AudioRequest = 0x21, DriveRequest = 0x22,
    ControlRequest = 0x23, CommissionRequest = 0x24,
    Reply = 0x30, AudioStatus = 0x31, Event = 0x32, CommissionStatus = 0x33,
    Diagnostics = 0x34
};

inline bool isKnownType(uint8_t t) {
    switch (t) {
        case 0x01: case 0x02: case 0x10: case 0x11: case 0x12: case 0x13:
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
            return true;
    }
    return false;
}

// Framing accepts any type in 0x01..0x3F so the endpoint can answer an
// in-range type it does not implement with an UNSUPPORTED reply. Types outside
// this bounded range are still rejected and counted at framing.
inline bool isFramingType(uint8_t t) { return t >= 0x01 && t <= 0x3F; }

enum class Status : uint8_t {
    Ok, NullArgument, BadLength, BadCapacity, BadEnum, BadReserved, BadRange, BadType
};

// Every rejected input increments exactly one counter. Session and queue are
// owned by the link layer but share the report structure.
struct ErrorCounters {
    uint32_t crc, length, version, reserved, type, cobs, overflow, timeout;
    uint32_t session, queue;
    uint32_t null_argument, capacity, payload_length, enum_value, reserved_bits, range;
    ErrorCounters() { clear(); }
    void clear() {
        crc = length = version = reserved = type = cobs = overflow = timeout = 0;
        session = queue = null_argument = capacity = payload_length = 0;
        enum_value = reserved_bits = range = 0;
    }
};

inline Status countStatus(Status s, ErrorCounters& c) {
    switch (s) {
        case Status::NullArgument: ++c.null_argument; break;
        case Status::BadLength: ++c.payload_length; break;
        case Status::BadCapacity: ++c.capacity; break;
        case Status::BadEnum: ++c.enum_value; break;
        case Status::BadReserved: ++c.reserved_bits; break;
        case Status::BadRange: ++c.range; break;
        case Status::BadType: ++c.type; break;
        case Status::Ok: break;
    }
    return s;
}

// Capability, role and state constants.
const uint8_t kRoleBody = 1, kRoleDome = 2;
const uint32_t kCapDualVesc = 1u << 0, kCapAudio = 1u << 1, kCapHall = 1u << 2,
               kCapTelemetry = 1u << 3, kCapRemoteDrive = 1u << 4;
const uint16_t kSafetyRevision = 1;
const uint8_t kReasonOperator = 0, kReasonReserved = 1, kReasonMaintenance = 2;
const uint8_t kMaxVolume = 30;
const uint16_t kMaxLeaseMs = 150;

enum class DriveState : uint8_t { Boot, Disarmed, Qualifying, Armed, Fault, Locked };
enum class DriveIntent : uint8_t { Stationary, Forward, Reverse, Pivot };
enum class DomeState : uint8_t { Inhibited = 0, Manual = 1, RemoteVelocity = 2, SeekingReference = 3, HoldingReference = 4 };
enum class DomeOwner : uint8_t { None = 0, Manual = 1, Drive = 2, Event = 3, Idle = 4, Startup = 5 };
enum class DomeOperation : uint8_t { Cancel = 0, Velocity = 1, SeekReference = 2 };
enum class DomeReference : uint8_t { Front = 0, Rear = 1 };
enum class EventKind : uint8_t {
    Completed = 0, Cancelled = 1, Timeout = 2, HardwareError = 3,
    PlaybackStarted = 4, DomeTakeover = 5
};
enum class Detail : uint16_t {
    None = 0, InputStale = 1, LeftVescUnavailable = 2, RightVescUnavailable = 3,
    ProfileUnavailable = 4, HallStale = 5, SeekTimeout = 6, LeaseExpired = 7,
    AudioUnavailable = 8, AudioGuardExpired = 9, TokenMismatch = 10,
    PeerLost = 11, QueueFull = 12, InitTimeout = 13, DeviceError = 14,
    SequenceConflict = 15
};

struct Hello {
    uint8_t role; uint32_t capabilities; uint16_t safety_revision;
    static MessageType type() { return MessageType::Hello; }
};
struct Heartbeat {
    uint8_t mode, ready;
    static MessageType type() { return MessageType::Heartbeat; }
};
struct RcStatus {
    uint32_t sample_counter; uint16_t source_age_ms, flags; uint16_t channels[10];
    uint8_t drive_state, dome_state; uint16_t control_epoch;
    static MessageType type() { return MessageType::RcStatus; }
};
struct VescStatus {
    uint8_t wheel; uint16_t valid_fields, source_age_ms; uint8_t fw_major, fw_minor;
    uint16_t pack_cV; int32_t motor_mA, input_mA, erpm; int16_t mosfet_dC, motor_dC;
    uint8_t fault; int16_t duty_permille;
    static MessageType type() { return MessageType::VescStatus; }
};
struct BodyStatus {
    uint32_t faults; uint8_t drive_state, dome_state, lock_reasons, profile_ready;
    uint16_t control_epoch; uint8_t drive_intent, dome_owner;
    uint32_t dome_authority_generation; uint8_t angle_valid; int16_t estimated_angle_ddeg;
    static MessageType type() { return MessageType::BodyStatus; }
};
struct HallState {
    uint8_t valid_mask, active_mask; uint32_t sample_counter; uint16_t source_age_ms;
    static MessageType type() { return MessageType::HallState; }
};
struct DomeRequest {
    uint8_t operation; int16_t speed_percent; uint16_t lease_ms, control_epoch;
    uint8_t owner, reference; uint32_t dome_authority_generation;
    static MessageType type() { return MessageType::DomeRequest; }
};
struct AudioRequest {
    uint8_t operation, folder; uint16_t track; uint8_t volume, priority;
    static MessageType type() { return MessageType::AudioRequest; }
};
struct DriveRequest {
    int16_t left_permille, right_permille; uint16_t lease_ms, control_epoch;
    static MessageType type() { return MessageType::DriveRequest; }
};
struct ControlRequest {
    uint8_t operation, reason; uint16_t token, control_epoch;
    static MessageType type() { return MessageType::ControlRequest; }
};
struct CommissionRequest {
    uint8_t operation, test; uint32_t run_id; uint8_t field, wheel; int32_t value;
    uint16_t control_epoch;
    static MessageType type() { return MessageType::CommissionRequest; }
};
struct Reply {
    uint8_t request_type; uint16_t request_seq; uint8_t result; uint16_t detail;
    static MessageType type() { return MessageType::Reply; }
};
struct AudioStatus {
    uint8_t state, folder; uint16_t track; uint8_t volume, validity;
    uint32_t elapsed_ms, duration_ms; uint16_t owner_request_seq;
    static MessageType type() { return MessageType::AudioStatus; }
};
struct Event {
    uint8_t kind, request_type; uint16_t request_seq, detail;
    static MessageType type() { return MessageType::Event; }
};
struct CommissionStatus {
    uint32_t run_id; uint8_t state, test; uint16_t error; uint32_t flags;
    uint16_t trial_neutral_us; uint8_t trial_speed_percent;
    uint16_t proposed_cw_ddeg_s, proposed_ccw_ddeg_s; uint32_t revolution_ms[3];
    uint32_t config_generation; uint8_t saved;
    static MessageType type() { return MessageType::CommissionStatus; }
};
struct Diagnostics {
    uint8_t subtype; uint32_t sample_counter;
    uint32_t counters[8];            // subtype 0 only
    uint8_t field, wheel; int32_t value;  // subtype 1 only
    static MessageType type() { return MessageType::Diagnostics; }
};

// Wire sizes.
inline size_t wireSize(const Hello&) { return 7; }
inline size_t wireSize(const Heartbeat&) { return 2; }
inline size_t wireSize(const RcStatus&) { return 32; }
inline size_t wireSize(const VescStatus&) { return 28; }
inline size_t wireSize(const BodyStatus&) { return 19; }
inline size_t wireSize(const HallState&) { return 8; }
inline size_t wireSize(const DomeRequest&) { return 13; }
inline size_t wireSize(const AudioRequest&) { return 6; }
inline size_t wireSize(const DriveRequest&) { return 8; }
inline size_t wireSize(const ControlRequest&) { return 6; }
inline size_t wireSize(const CommissionRequest&) { return 14; }
inline size_t wireSize(const Reply&) { return 6; }
inline size_t wireSize(const AudioStatus&) { return 16; }
inline size_t wireSize(const Event&) { return 6; }
inline size_t wireSize(const CommissionStatus&) { return 36; }
inline size_t wireSize(const Diagnostics& d) { return d.subtype == 0 ? 37 : 11; }

inline Status ok01(uint8_t v) { return v <= 1 ? Status::Ok : Status::BadEnum; }
inline Status maskOnly(uint32_t v, uint32_t mask) {
    return (v & ~mask) ? Status::BadReserved : Status::Ok;
}
#define R2_CHECK(expr) do { Status s_ = (expr); if (s_ != Status::Ok) return s_; } while (0)
inline Status inRange(bool good) { return good ? Status::Ok : Status::BadRange; }
inline Status enumLE(uint32_t v, uint32_t max) { return v <= max ? Status::Ok : Status::BadEnum; }
inline Status zero(uint32_t v) { return v == 0 ? Status::Ok : Status::BadReserved; }

// ---- validation (applied before encoding and after decoding) ----
inline Status validate(const Hello& m) {
    if (m.role != kRoleBody && m.role != kRoleDome) return Status::BadEnum;
    R2_CHECK(maskOnly(m.capabilities, 0x1F));
    return m.safety_revision == kSafetyRevision ? Status::Ok : Status::BadRange;
}
inline Status validate(const Heartbeat& m) {
    R2_CHECK(enumLE(m.mode, 2));
    return ok01(m.ready);
}
inline Status validate(const RcStatus& m) {
    R2_CHECK(maskOnly(m.flags, 0x0F));
    R2_CHECK(enumLE(m.drive_state, 5));
    return enumLE(m.dome_state, 4);
}
inline Status validate(const VescStatus& m) {
    R2_CHECK(ok01(m.wheel));
    return maskOnly(m.valid_fields, 0xFF);
}
inline Status validate(const BodyStatus& m) {
    R2_CHECK(maskOnly(m.faults, 0x0FFF));
    R2_CHECK(enumLE(m.drive_state, 5));
    R2_CHECK(enumLE(m.dome_state, 4));
    R2_CHECK(maskOnly(m.lock_reasons, 0x05));
    R2_CHECK(ok01(m.profile_ready));
    R2_CHECK(enumLE(m.drive_intent, 3));
    R2_CHECK(enumLE(m.dome_owner, 5));
    R2_CHECK(ok01(m.angle_valid));
    if (m.angle_valid) return inRange(m.estimated_angle_ddeg >= -1800 && m.estimated_angle_ddeg <= 1799);
    return zero(static_cast<uint16_t>(m.estimated_angle_ddeg));
}
inline Status validate(const HallState& m) {
    R2_CHECK(maskOnly(m.valid_mask, 0x03));
    R2_CHECK(maskOnly(m.active_mask, 0x03));
    return zero(m.active_mask & ~m.valid_mask);
}
inline Status validate(const DomeRequest& m) {
    R2_CHECK(enumLE(m.operation, 2));
    R2_CHECK(enumLE(m.owner, 1));
    R2_CHECK(enumLE(m.reference, 1));
    if (m.operation == 1) {
        if (m.speed_percent < -100 || m.speed_percent > 100) return Status::BadRange;
        if (m.lease_ms < 1 || m.lease_ms > kMaxLeaseMs) return Status::BadRange;
        return zero(m.reference);
    }
    R2_CHECK(zero(static_cast<uint16_t>(m.speed_percent)));
    R2_CHECK(zero(m.lease_ms));
    return m.operation == 0 ? zero(m.reference) : Status::Ok;
}
inline Status validate(const AudioRequest& m) {
    R2_CHECK(enumLE(m.operation, 5));
    R2_CHECK(ok01(m.priority));
    R2_CHECK(enumLE(m.volume, kMaxVolume));
    if (m.operation == 0) {
        if (m.folder == 0 || m.track == 0) return Status::BadRange;
        return zero(m.volume);
    }
    R2_CHECK(zero(m.folder));
    R2_CHECK(zero(m.track));
    return m.operation == 4 ? Status::Ok : zero(m.volume);
}
inline Status validate(const DriveRequest& m) {
    if (m.left_permille < -1000 || m.left_permille > 1000 ||
        m.right_permille < -1000 || m.right_permille > 1000) return Status::BadRange;
    return inRange(m.lease_ms >= 1 && m.lease_ms <= kMaxLeaseMs);
}
inline Status validate(const ControlRequest& m) {
    R2_CHECK(enumLE(m.operation, 4));
    if (m.reason == kReasonReserved) return Status::BadReserved;
    return m.reason == kReasonOperator || m.reason == kReasonMaintenance ? Status::Ok : Status::BadEnum;
}
inline Status validate(const CommissionRequest& m) {
    R2_CHECK(enumLE(m.operation, 6));
    R2_CHECK(enumLE(m.test, 6));
    R2_CHECK(ok01(m.wheel));
    if (m.operation == 1) {
        if (m.test == 0) return Status::BadRange;
        R2_CHECK(zero(m.field));
        R2_CHECK(zero(m.wheel));
        R2_CHECK(zero(static_cast<uint32_t>(m.value)));
        return Status::Ok;
    }
    if (m.operation == 0) {
        if (m.test != 0) return Status::BadRange;
        R2_CHECK(enumLE(m.field, 1));
        if (m.field == 0) {
            R2_CHECK(zero(m.wheel));
            return zero(static_cast<uint32_t>(m.value));
        }
        return inRange(m.value >= 0 && m.value <= 20);
    }
    R2_CHECK(zero(m.test));
    if (m.operation == 4) return inRange(m.field <= 20);
    if (m.operation == 6) {
        R2_CHECK(zero(m.field));
        R2_CHECK(zero(m.wheel));
        return inRange(m.value >= 0 && m.value <= 31);
    }
    R2_CHECK(zero(m.field));
    R2_CHECK(zero(m.wheel));
    return zero(static_cast<uint32_t>(m.value));
}
inline Status validate(const Reply& m) {
    // An UNSUPPORTED reply (result 5) echoes any bounded raw request type.
    const bool unsupported = m.result == 5 && isFramingType(m.request_type);
    if (!isKnownType(m.request_type) && !unsupported) return Status::BadType;
    R2_CHECK(enumLE(m.result, 8));
    return enumLE(m.detail, 15);
}
inline Status validate(const AudioStatus& m) {
    R2_CHECK(enumLE(m.state, 5));
    R2_CHECK(enumLE(m.volume, kMaxVolume));
    R2_CHECK(maskOnly(m.validity, 0x03));
    return (m.validity & 0x02) ? Status::Ok : zero(m.duration_ms);
}
inline Status validate(const Event& m) {
    if (!isKnownType(m.request_type)) return Status::BadType;
    R2_CHECK(enumLE(m.kind, 5));
    return enumLE(m.detail, 15);
}
inline Status validate(const CommissionStatus& m) {
    R2_CHECK(enumLE(m.state, 5));
    R2_CHECK(enumLE(m.test, 6));
    R2_CHECK(ok01(m.saved));
    R2_CHECK(enumLE(m.trial_speed_percent, 100));
    return inRange(m.trial_neutral_us == 0 || (m.trial_neutral_us >= 1400 && m.trial_neutral_us <= 1600));
}
inline Status validate(const Diagnostics& m) {
    R2_CHECK(enumLE(m.subtype, 1));
    if (m.subtype == 0) {
        R2_CHECK(zero(m.field));
        R2_CHECK(zero(m.wheel));
        return zero(static_cast<uint32_t>(m.value));
    }
    for (int i = 0; i < 8; ++i) R2_CHECK(zero(m.counters[i]));
    R2_CHECK(ok01(m.wheel));
    return inRange(m.field <= 20);
}

// ---- serialization (field order matches the spec's message catalog) ----
inline void put(Writer& w, const Hello& m) { w.u8(m.role); w.u32(m.capabilities); w.u16(m.safety_revision); }
inline void get(Reader& r, Hello& m) { m.role = r.u8(); m.capabilities = r.u32(); m.safety_revision = r.u16(); }
inline void put(Writer& w, const Heartbeat& m) { w.u8(m.mode); w.u8(m.ready); }
inline void get(Reader& r, Heartbeat& m) { m.mode = r.u8(); m.ready = r.u8(); }
inline void put(Writer& w, const RcStatus& m) {
    w.u32(m.sample_counter); w.u16(m.source_age_ms); w.u16(m.flags);
    for (int i = 0; i < 10; ++i) w.u16(m.channels[i]);
    w.u8(m.drive_state); w.u8(m.dome_state); w.u16(m.control_epoch);
}
inline void get(Reader& r, RcStatus& m) {
    m.sample_counter = r.u32(); m.source_age_ms = r.u16(); m.flags = r.u16();
    for (int i = 0; i < 10; ++i) m.channels[i] = r.u16();
    m.drive_state = r.u8(); m.dome_state = r.u8(); m.control_epoch = r.u16();
}
inline void put(Writer& w, const VescStatus& m) {
    w.u8(m.wheel); w.u16(m.valid_fields); w.u16(m.source_age_ms); w.u8(m.fw_major); w.u8(m.fw_minor);
    w.u16(m.pack_cV); w.i32(m.motor_mA); w.i32(m.input_mA); w.i32(m.erpm);
    w.i16(m.mosfet_dC); w.i16(m.motor_dC); w.u8(m.fault); w.i16(m.duty_permille);
}
inline void get(Reader& r, VescStatus& m) {
    m.wheel = r.u8(); m.valid_fields = r.u16(); m.source_age_ms = r.u16(); m.fw_major = r.u8(); m.fw_minor = r.u8();
    m.pack_cV = r.u16(); m.motor_mA = r.i32(); m.input_mA = r.i32(); m.erpm = r.i32();
    m.mosfet_dC = r.i16(); m.motor_dC = r.i16(); m.fault = r.u8(); m.duty_permille = r.i16();
}
inline void put(Writer& w, const BodyStatus& m) {
    w.u32(m.faults); w.u8(m.drive_state); w.u8(m.dome_state); w.u8(m.lock_reasons); w.u8(m.profile_ready);
    w.u16(m.control_epoch); w.u8(m.drive_intent); w.u8(m.dome_owner);
    w.u32(m.dome_authority_generation); w.u8(m.angle_valid); w.i16(m.estimated_angle_ddeg);
}
inline void get(Reader& r, BodyStatus& m) {
    m.faults = r.u32(); m.drive_state = r.u8(); m.dome_state = r.u8(); m.lock_reasons = r.u8(); m.profile_ready = r.u8();
    m.control_epoch = r.u16(); m.drive_intent = r.u8(); m.dome_owner = r.u8();
    m.dome_authority_generation = r.u32(); m.angle_valid = r.u8(); m.estimated_angle_ddeg = r.i16();
}
inline void put(Writer& w, const HallState& m) { w.u8(m.valid_mask); w.u8(m.active_mask); w.u32(m.sample_counter); w.u16(m.source_age_ms); }
inline void get(Reader& r, HallState& m) { m.valid_mask = r.u8(); m.active_mask = r.u8(); m.sample_counter = r.u32(); m.source_age_ms = r.u16(); }
inline void put(Writer& w, const DomeRequest& m) {
    w.u8(m.operation); w.i16(m.speed_percent); w.u16(m.lease_ms); w.u16(m.control_epoch);
    w.u8(m.owner); w.u8(m.reference); w.u32(m.dome_authority_generation);
}
inline void get(Reader& r, DomeRequest& m) {
    m.operation = r.u8(); m.speed_percent = r.i16(); m.lease_ms = r.u16(); m.control_epoch = r.u16();
    m.owner = r.u8(); m.reference = r.u8(); m.dome_authority_generation = r.u32();
}
inline void put(Writer& w, const AudioRequest& m) { w.u8(m.operation); w.u8(m.folder); w.u16(m.track); w.u8(m.volume); w.u8(m.priority); }
inline void get(Reader& r, AudioRequest& m) { m.operation = r.u8(); m.folder = r.u8(); m.track = r.u16(); m.volume = r.u8(); m.priority = r.u8(); }
inline void put(Writer& w, const DriveRequest& m) { w.i16(m.left_permille); w.i16(m.right_permille); w.u16(m.lease_ms); w.u16(m.control_epoch); }
inline void get(Reader& r, DriveRequest& m) { m.left_permille = r.i16(); m.right_permille = r.i16(); m.lease_ms = r.u16(); m.control_epoch = r.u16(); }
inline void put(Writer& w, const ControlRequest& m) { w.u8(m.operation); w.u8(m.reason); w.u16(m.token); w.u16(m.control_epoch); }
inline void get(Reader& r, ControlRequest& m) { m.operation = r.u8(); m.reason = r.u8(); m.token = r.u16(); m.control_epoch = r.u16(); }
inline void put(Writer& w, const CommissionRequest& m) {
    w.u8(m.operation); w.u8(m.test); w.u32(m.run_id); w.u8(m.field); w.u8(m.wheel); w.i32(m.value); w.u16(m.control_epoch);
}
inline void get(Reader& r, CommissionRequest& m) {
    m.operation = r.u8(); m.test = r.u8(); m.run_id = r.u32(); m.field = r.u8(); m.wheel = r.u8(); m.value = r.i32(); m.control_epoch = r.u16();
}
inline void put(Writer& w, const Reply& m) { w.u8(m.request_type); w.u16(m.request_seq); w.u8(m.result); w.u16(m.detail); }
inline void get(Reader& r, Reply& m) { m.request_type = r.u8(); m.request_seq = r.u16(); m.result = r.u8(); m.detail = r.u16(); }
inline void put(Writer& w, const AudioStatus& m) {
    w.u8(m.state); w.u8(m.folder); w.u16(m.track); w.u8(m.volume); w.u8(m.validity);
    w.u32(m.elapsed_ms); w.u32(m.duration_ms); w.u16(m.owner_request_seq);
}
inline void get(Reader& r, AudioStatus& m) {
    m.state = r.u8(); m.folder = r.u8(); m.track = r.u16(); m.volume = r.u8(); m.validity = r.u8();
    m.elapsed_ms = r.u32(); m.duration_ms = r.u32(); m.owner_request_seq = r.u16();
}
inline void put(Writer& w, const Event& m) { w.u8(m.kind); w.u8(m.request_type); w.u16(m.request_seq); w.u16(m.detail); }
inline void get(Reader& r, Event& m) { m.kind = r.u8(); m.request_type = r.u8(); m.request_seq = r.u16(); m.detail = r.u16(); }
inline void put(Writer& w, const CommissionStatus& m) {
    w.u32(m.run_id); w.u8(m.state); w.u8(m.test); w.u16(m.error); w.u32(m.flags);
    w.u16(m.trial_neutral_us); w.u8(m.trial_speed_percent); w.u16(m.proposed_cw_ddeg_s); w.u16(m.proposed_ccw_ddeg_s);
    for (int i = 0; i < 3; ++i) w.u32(m.revolution_ms[i]);
    w.u32(m.config_generation); w.u8(m.saved);
}
inline void get(Reader& r, CommissionStatus& m) {
    m.run_id = r.u32(); m.state = r.u8(); m.test = r.u8(); m.error = r.u16(); m.flags = r.u32();
    m.trial_neutral_us = r.u16(); m.trial_speed_percent = r.u8(); m.proposed_cw_ddeg_s = r.u16(); m.proposed_ccw_ddeg_s = r.u16();
    for (int i = 0; i < 3; ++i) m.revolution_ms[i] = r.u32();
    m.config_generation = r.u32(); m.saved = r.u8();
}
inline void put(Writer& w, const Diagnostics& m) {
    w.u8(m.subtype); w.u32(m.sample_counter);
    if (m.subtype == 0) { for (int i = 0; i < 8; ++i) w.u32(m.counters[i]); }
    else { w.u8(m.field); w.u8(m.wheel); w.i32(m.value); }
}
inline void get(Reader& r, Diagnostics& m) {
    for (int i = 0; i < 8; ++i) m.counters[i] = 0;
    m.field = m.wheel = 0; m.value = 0;
    m.subtype = r.u8(); m.sample_counter = r.u32();
    if (m.subtype == 0) { for (int i = 0; i < 8; ++i) m.counters[i] = r.u32(); }
    else { m.field = r.u8(); m.wheel = r.u8(); m.value = r.i32(); }
}

// ---- typed bounded-buffer API ----
// encodePayload: validates the message, writes exactly wireSize bytes, sets length.
template <class T>
Status encodePayload(const T& m, uint8_t* out, size_t capacity, size_t& length, ErrorCounters& c) {
    if (!out) return countStatus(Status::NullArgument, c);
    Status s = validate(m);
    if (s != Status::Ok) return countStatus(s, c);
    if (capacity < wireSize(m)) return countStatus(Status::BadCapacity, c);
    Writer w(out, capacity);
    put(w, m);
    length = w.size();
    return Status::Ok;
}

// decodePayload: exact length required; out is untouched unless Ok is returned.
template <class T>
Status decodePayload(const uint8_t* data, size_t length, T& out, ErrorCounters& c) {
    if (!data) return countStatus(Status::NullArgument, c);
    T m;
    Reader r(data, length);
    if (length == 0 || (T::type() == MessageType::Diagnostics && length != 37 && length != 11))
        return countStatus(Status::BadLength, c);
    get(r, m);
    if (!r.ok() || r.remaining() != 0 || wireSize(m) != length)
        return countStatus(Status::BadLength, c);
    Status s = validate(m);
    if (s != Status::Ok) return countStatus(s, c);
    out = m;
    return Status::Ok;
}

}  // namespace r2link
