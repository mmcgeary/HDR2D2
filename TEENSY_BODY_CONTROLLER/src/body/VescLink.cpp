#include "body/VescLink.h"
#include <limits.h>
#include <string.h>

namespace body {
namespace {
uint16_t crc16(const uint8_t* b, size_t n) {
    uint16_t c = 0;
    for (size_t j = 0; j < n; ++j) {
        c ^= uint16_t(b[j]) << 8;
        for (uint8_t k = 0; k < 8; ++k)
            c = (c & 0x8000) ? uint16_t((c << 1) ^ 0x1021) : uint16_t(c << 1);
    }
    return c;
}
size_t frame(const uint8_t* payload, uint8_t n, uint8_t* out, size_t capacity) {
    if (!out || capacity < size_t(n) + 5) return 0;
    out[0] = 2; out[1] = n;
    memcpy(out + 2, payload, n);
    const uint16_t c = crc16(payload, n);
    out[n + 2] = uint8_t(c >> 8); out[n + 3] = uint8_t(c); out[n + 4] = 3;
    return size_t(n) + 5;
}
void put32(uint8_t* b, uint32_t n) {
    for (uint8_t i = 0; i < 4; ++i) b[i] = uint8_t(n >> ((3 - i) * 8));
}

class Reader {
public:
    Reader(const uint8_t* b, size_t n) : bytes_(b), length_(n) {}
    bool u8(size_t at, uint8_t& v) const {
        if (!bytes_ || at >= length_) return false;
        v = bytes_[at]; return true;
    }
    bool i16(size_t at, int32_t& v) const {
        if (!bytes_ || at > length_ || length_ - at < 2) return false;
        const uint32_t u = (uint32_t(bytes_[at]) << 8) | bytes_[at + 1];
        v = u <= INT16_MAX ? int32_t(u) : int32_t(u) - 65536;
        return true;
    }
    bool i32(size_t at, int32_t& v) const {
        if (!bytes_ || at > length_ || length_ - at < 4) return false;
        const uint32_t u = (uint32_t(bytes_[at]) << 24) | (uint32_t(bytes_[at + 1]) << 16) |
                           (uint32_t(bytes_[at + 2]) << 8) | bytes_[at + 3];
        v = u <= INT32_MAX ? int32_t(u) : -1 - int32_t(UINT32_MAX - u);
        return true;
    }
private:
    const uint8_t* bytes_;
    size_t length_;
};
} // namespace

VescProfile::VescProfile() : major_(0), minor_(0), layout_(kLayoutUnknown),
    config_accepted_(false), control_accepted_(false), brake_ma_(0) {}

VescProfile VescProfile::fromSaved(const CommissioningProfile& p, uint8_t wheel) {
    VescProfile v;
    if (wheel > 1 || !validateProfile(p)) return v;
    const WheelProfile& w = p.wheel[wheel];
    v.major_ = w.fw_major; v.minor_ = w.fw_minor; v.layout_ = w.layout; v.brake_ma_ = w.brake_ma;
    v.config_accepted_ = (p.acceptance & (1u << (kAcceptVescConfig + wheel))) != 0;
    const uint32_t required = (1u << (kAcceptVescConfig + wheel)) |
        (1u << (kAcceptTimeoutBrake + wheel)) | (1u << (kAcceptDirection + wheel)) |
        (1u << (kAcceptReversal + wheel));
    v.control_accepted_ = (p.acceptance & required) == required;
    return v;
}

SensorSnapshot vescMeasurements(const VescSample& left, const VescSample& right) {
    SensorSnapshot s{};
    const bool pair = left.valid && right.valid && !left.fault && !right.fault;
    s.voltage_valid = pair && (left.valid_fields & right.valid_fields & 1);
    s.temperature_valid = pair && (left.valid_fields & right.valid_fields & 0x10);
    if (s.voltage_valid) s.voltage_cV = left.pack_cV < right.pack_cV ? left.pack_cV : right.pack_cV;
    if (s.temperature_valid) s.hottest_mosfet_dC =
        left.mosfet_dC > right.mosfet_dC ? left.mosfet_dC : right.mosfet_dC;
    return s;
}

size_t VescCodec::encodeDuty(int16_t permille, uint8_t* out, size_t capacity) {
    if (permille < -1000 || permille > 1000) return 0;
    uint8_t payload[5] = {5};
    put32(payload + 1, uint32_t(int32_t(permille) * 100));
    return frame(payload, 5, out, capacity);
}

size_t VescCodec::encodeBrake(uint32_t current_mA, uint8_t* out, size_t capacity) {
    if (!current_mA || current_mA > INT32_MAX) return 0;
    uint8_t payload[5] = {7};
    put32(payload + 1, current_mA);
    return frame(payload, 5, out, capacity);
}

bool VescCodec::decodeValues(const uint8_t* b, size_t n, const VescProfile& profile, VescSample& out) {
    if (!profile.config_accepted_ || profile.layout_ != kLayoutLegacyGetValues || n > 256) return false;
    Reader r(b, n);
    uint8_t command = 0;
    VescSample s{};
    int32_t mosfet, motor, input, duty, voltage;
    // Legacy GET_VALUES prefix: checked against commands.c and prior-art;
    // the offset/provenance table is in the Task 4 report. Tail extensions
    // are tolerated ONLY after external exact-version/layout acceptance.
    if (!r.u8(0, command) || command != 4 || !r.i16(1, mosfet) ||
        !r.i32(5, motor) || !r.i32(9, input) || !r.i16(21, duty) ||
        !r.i32(23, s.erpm) || !r.i16(27, voltage) || !r.u8(53, s.fault)) return false;
    // These are representation/protocol checks, not guessed physical limits.
    if (voltage < 0 || voltage > UINT16_MAX / 10 || duty < -1000 || duty > 1000 ||
        motor < INT32_MIN / 10 || motor > INT32_MAX / 10 ||
        input < INT32_MIN / 10 || input > INT32_MAX / 10) return false;
    s.pack_cV = uint16_t(voltage * 10);
    s.motor_mA = motor * 10; s.input_mA = input * 10;
    s.mosfet_dC = int16_t(mosfet); s.duty_permille = int16_t(duty);
    s.valid_fields = 0xdf; // motor TEMP is not wired
    s.motor_temperature_valid = false;
    out = s;
    return true;
}

VescLink::VescLink(r2link::BytePort& port, uint8_t wheel) : port_(port), wheel_(wheel),
    profile_(), cached_{}, counters_{}, have_sample_(false), have_firmware_(false),
    major_(0), minor_(0), rx_{}, rx_ms_{}, rx_length_(0), recovering_(false), tx_{}, tx_length_(0),
    tx_offset_(0), tx_command_(0), tx_aborted_(false), outstanding_(255),
    queried_(false), query_waiting_(false), last_query_(0), query_ms_(0), last_fw_(0), now_(0),
    demand_(0), duty_(0), brake_(0), demand_ms_(0), capture_armed_(false),
    capture_ready_(false), capture_command_(255), capture_{} {}

void VescLink::resetSample() { cached_ = VescSample{}; have_sample_ = false; demand_ = 0; }
bool VescLink::match() const {
    return wheel_ < 2 && have_firmware_ && profile_.config_accepted_ &&
        profile_.layout_ == kLayoutLegacyGetValues && major_ == profile_.major_ && minor_ == profile_.minor_;
}

void VescLink::invalidateCommand(bool brake) {
    // Only an urgent brake displaces an unstarted query; duty waits <=6 bytes.
    if (brake && tx_length_ && !tx_offset_ && (tx_command_ == 0 || tx_command_ == 4)) {
        tx_length_ = 0; outstanding_ = 255; queried_ = false;
    }
    if (tx_length_ && (tx_command_ == 5 || tx_command_ == 7) && !tx_aborted_) {
        if (!tx_offset_) tx_length_ = 0;
        else {
            // A committed prefix cannot be retracted. Poison an unsent CRC
            // byte (or terminator) while retaining frame length/alignment.
            if (tx_offset_ <= 8) tx_[8] ^= 1;
            else tx_[9] = 0;
            tx_aborted_ = true;
        }
        ++counters_.aborted_commands;
    }
}
void VescLink::setProfile(const VescProfile& profile) {
    invalidateCommand(true);
    profile_ = profile; resetSample(); have_firmware_ = false;
    major_ = minor_ = 0; outstanding_ = 255; queried_ = false; rx_length_ = 0; recovering_ = false;
    // A partially emitted query must finish before the new FW request.
    if (tx_length_ && !tx_offset_) tx_length_ = 0;
}
void VescLink::setDuty(int16_t duty) {
    if (duty == 0) { setBrake(profile_.brake_ma_); return; }
    // Brake must reach the wire first. Requests made while it is pending are
    // dropped, not replayed as delayed drive after that brake completes.
    if (demand_ == 7 || (tx_length_ && tx_command_ == 7 && !tx_aborted_)) return;
    if (tx_length_ && tx_command_ == 5 && !tx_aborted_ && duty_ == duty) {
        demand_ms_ = now_; return;
    }
    invalidateCommand(false);
    duty_ = duty; demand_ = 5; demand_ms_ = now_;
}
void VescLink::setBrake(uint32_t brake) {
    if (tx_length_ && tx_command_ == 7 && !tx_aborted_ && brake_ == brake) return;
    invalidateCommand(true);
    brake_ = brake; demand_ = 7; demand_ms_ = now_;
}
void VescLink::disableControl() {
    invalidateCommand(false);
    demand_ = 0;
}
VescSample VescLink::sample(uint32_t now) const {
    VescSample s = cached_;
    s.wheel = wheel_; s.fw_major = major_; s.fw_minor = minor_;
    s.profile_match = match(); s.unsupported = !s.profile_match;
    const uint32_t age = have_sample_ ? uint32_t(now - s.sample_ms) : UINT32_MAX;
    s.source_age_ms = age > UINT16_MAX ? UINT16_MAX : uint16_t(age);
    s.stale = !have_sample_ || age > 500;
    s.valid = s.profile_match && !s.stale && (s.valid_fields & 0x49) == 0x49;
    return s;
}

void VescLink::discard(size_t n) {
    if (n >= rx_length_) { rx_length_ = 0; return; }
    rx_length_ -= n;
    memmove(rx_, rx_ + n, rx_length_);
    memmove(rx_ms_, rx_ms_ + n, rx_length_ * sizeof(rx_ms_[0]));
}

void VescLink::handle(const uint8_t* b, size_t n, uint32_t now) {
    if (b[0] == 0) {
        if (n < 3) { ++counters_.unsupported_packets; return; }
        if (!have_firmware_ || major_ != b[1] || minor_ != b[2]) {
            invalidateCommand(true); resetSample();
        }
        major_ = b[1]; minor_ = b[2]; have_firmware_ = true; last_fw_ = now;
        if (outstanding_ == 0) outstanding_ = 255;
    } else if (b[0] == 4) {
        // Unsolicited/late replies may be captured, but cannot renew fields.
        if (outstanding_ != 4 || (tx_length_ && tx_command_ == 4)) return;
        outstanding_ = 255;
        VescSample s{};
        if (!match()) { ++counters_.unsupported_packets; return; }
        if (!VescCodec::decodeValues(b, n, profile_, s)) { ++counters_.invalid_values; return; }
        s.sample_ms = now; cached_ = s; have_sample_ = true;
    } else ++counters_.unsupported_packets;
}

void VescLink::parse(uint32_t now) {
    while (rx_length_) {
        if (rx_[0] != 2 && rx_[0] != 3) { discard(1); continue; }
        const size_t header = rx_[0] == 2 ? 2 : 3;
        if (uint32_t(now - rx_ms_[0]) >= 50) {
            ++counters_.frame_timeouts; recovering_ = true; discard(1); continue;
        }
        if (rx_length_ < header) return;
        const size_t length = header == 2 ? rx_[1] : (size_t(rx_[1]) << 8) | rx_[2];
        if (!length || length > 256) {
            ++counters_.oversized_frames; recovering_ = true; discard(1); continue;
        }
        const size_t total = header + length + 3;
        if (rx_length_ < total) {
            if (!recovering_) return;
            // After a known-bad candidate, a plausible length in its payload
            // must not mask a complete valid suffix. Keep each header's own
            // arrival time, and never search inside an intact active frame.
            bool found = false;
            for (size_t at = 1; at < rx_length_; ++at) {
                if (rx_[at] != 2 && rx_[at] != 3) continue;
                if (uint32_t(now - rx_ms_[at]) >= 50) continue;
                const size_t h = rx_[at] == 2 ? 2 : 3;
                if (rx_length_ - at < h) continue;
                const size_t len = h == 2 ? rx_[at + 1] :
                    (size_t(rx_[at + 1]) << 8) | rx_[at + 2];
                if (!len || len > 256 || rx_length_ - at < h + len + 3) continue;
                const size_t end = at + h + len;
                if (rx_[end + 2] != 3) continue;
                const uint16_t c = (uint16_t(rx_[end]) << 8) | rx_[end + 1];
                if (crc16(rx_ + at + h, len) != c) continue;
                discard(at); found = true; break;
            }
            if (found) continue;
            return;
        }
        if (rx_[total - 1] != 3) {
            ++counters_.terminator_errors; recovering_ = true; discard(1); continue;
        }
        const uint16_t expected = (uint16_t(rx_[header + length]) << 8) | rx_[header + length + 1];
        if (crc16(rx_ + header, length) != expected) {
            ++counters_.crc_errors; recovering_ = true; discard(1); continue;
        }
        recovering_ = false;
        ++counters_.frames;
        if (capture_armed_ && !capture_ready_ && (capture_command_ == 255 || rx_[header] == capture_command_)) {
            capture_.wheel = wheel_; capture_.length = uint16_t(total);
            memcpy(capture_.bytes, rx_, total);
            capture_ready_ = true; capture_armed_ = false;
        }
        handle(rx_ + header, length, now);
        discard(total);
    }
}
void VescLink::startQuery(uint8_t command, uint32_t now) {
    tx_length_ = uint8_t(frame(&command, 1, tx_, sizeof(tx_)));
    tx_offset_ = 0; tx_command_ = command; tx_aborted_ = false;
    outstanding_ = command; queried_ = true; last_query_ = query_ms_ = now;
}
bool VescLink::brakePermitted() const {
    return profile_.control_accepted_ && match() && brake_ && brake_ == profile_.brake_ma_;
}
bool VescLink::dutyPermitted(uint32_t now) const {
    const VescSample s = sample(now);
    return profile_.control_accepted_ && s.valid && !s.fault && uint32_t(now - demand_ms_) <= 20;
}
bool VescLink::queryDue(uint32_t now) const {
    return outstanding_ == 255 && (!queried_ || uint32_t(now - last_query_) >= 100);
}
bool VescLink::stage(uint32_t now, size_t room) {
    const bool due = queryDue(now);
    if (demand_ == 7 || (demand_ == 5 && !(due && query_waiting_))) {
        if (room < 10) return false; // demand stays staged for the next tick
        const bool brake = demand_ == 7;
        tx_length_ = uint8_t(brake ? (brakePermitted() ? VescCodec::encodeBrake(brake_, tx_, sizeof(tx_)) : 0) :
            (dutyPermitted(now) ? VescCodec::encodeDuty(duty_, tx_, sizeof(tx_)) : 0));
        if (!tx_length_) ++counters_.inhibited_commands;
        tx_command_ = demand_; tx_offset_ = 0; tx_aborted_ = false; demand_ = 0;
        if (tx_length_) return true;
    }
    if (!due || room < 6) return false;
    startQuery(!have_firmware_ || uint32_t(now - last_fw_) >= 1000 ? 0 : 4, now);
    query_waiting_ = false;
    return true;
}
void VescLink::pumpTx(uint32_t now) {
    if (tx_length_ && tx_command_ == 5 && !tx_aborted_ && !dutyPermitted(now)) invalidateCommand(false);
    if (tx_length_ && tx_command_ == 7 && !tx_aborted_ && !brakePermitted()) invalidateCommand(false);
    size_t budget = kTxBudget;
    uint8_t started = 0;
    bool wrote = false;
    for (;;) {
        if (!tx_length_) {
            size_t room = port_.writable();
            if (room > budget) room = budget;
            if (started == 2 || !stage(now, wrote ? room : SIZE_MAX)) break;
            ++started;
        }
        const size_t remaining = size_t(tx_length_ - tx_offset_);
        size_t n = remaining;
        size_t room = port_.writable();
        if (room > budget) room = budget;
        if (n > room) n = room;
        if (!n) break;
        const size_t written = port_.write(tx_ + tx_offset_, n);
        if (written > n) break; // a transport contract violation cannot advance state
        if (written) wrote = true;
        budget -= written;
        tx_offset_ += uint8_t(written);
        if (written < remaining) { ++counters_.partial_writes; break; }
        tx_length_ = tx_offset_ = 0;
        if (tx_command_ == 0 || tx_command_ == 4) query_ms_ = now;
    }
    // A due query that control kept off the wire goes ahead of duty next tick.
    query_waiting_ = queryDue(now);
}
void VescLink::tick(uint32_t now) {
    now_ = now;
    // Expire BEFORE consuming queued RX; a reply after a stalled loop cannot
    // hide a missed query deadline or freshen the old telemetry sample.
    const bool query_in_tx = tx_length_ && (tx_command_ == 0 || tx_command_ == 4);
    if (!query_in_tx && outstanding_ != 255 && uint32_t(now - query_ms_) >= 150) {
        ++counters_.query_timeouts; outstanding_ = 255;
    }
    parse(now);
    for (uint8_t i = 0; i < 64; ++i) {
        const int byte = port_.read();
        if (byte < 0) break;
        if (rx_length_ == sizeof(rx_)) discard(1);
        rx_[rx_length_] = uint8_t(byte); rx_ms_[rx_length_++] = now;
        parse(now);
    }
    pumpTx(now);
}
void VescLink::requestCapture(uint8_t command) {
    capture_ready_ = false; capture_armed_ = true; capture_command_ = command;
}
bool VescLink::takeCapture(VescCapture& out) {
    if (!capture_ready_) return false;
    out = capture_; capture_ready_ = false; return true;
}
} // namespace body
