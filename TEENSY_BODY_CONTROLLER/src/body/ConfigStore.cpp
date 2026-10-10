#include "body/ConfigStore.h"
#include <string.h>
#include "Bytes.h"
#include "Codec.h"

namespace body {

namespace {

const uint8_t kCommitted = 0xA5, kInvalidated = 0x00, kBlank = 0xFF;
const uint32_t kBootMagic = 0x314C5342, kProfileMagic = 0x31465250;
const uint8_t kRecordVersion = 1;

struct Spec { uint8_t id; bool wheel; int32_t lo, hi; };
const Spec kSpecs[] = {
    {0, false, 1400, 1600}, {1, false, 1000, 1499}, {2, false, 1501, 2000}, {3, false, 1, 25},
    {4, false, 1, 1000}, {19, false, 1, 36000}, {20, false, 1, 36000},
    {5, true, -1, 1}, {6, true, 0, 255}, {7, true, 0, 255}, {8, true, 1, 1},
    {9, true, 1, 100000}, {10, true, 1, 20000}, {11, true, 0, 20000}, {12, true, 1, 100000},
    {13, true, 1000, 1500}, {14, true, 1300, 1600}, {15, true, 150, 150}, {16, true, 1, 100000},
    {17, true, 1, 10000}, {18, true, 20, 1000}
};
const size_t kSpecCount = sizeof(kSpecs) / sizeof(kSpecs[0]);

const Spec* specFor(uint8_t id) {
    for (size_t i = 0; i < kSpecCount; ++i) if (kSpecs[i].id == id) return &kSpecs[i];
    return 0;
}

int32_t rawGet(const CommissioningProfile& p, uint8_t id, uint8_t w) {
    const WheelProfile& x = p.wheel[w & 1];
    switch (id) {
        case 0: return p.servo_neutral;
        case 1: return p.servo_min;
        case 2: return p.servo_max;
        case 3: return p.auto_speed_percent;
        case 4: return p.duty_slew_permille_per_s;
        case 19: return p.cw_ddeg_per_s;
        case 20: return p.ccw_ddeg_per_s;
        case 5: return x.direction;
        case 6: return x.fw_major;
        case 7: return x.fw_minor;
        case 8: return x.layout;
        case 9: return static_cast<int32_t>(x.motor_ma);
        case 10: return static_cast<int32_t>(x.battery_ma);
        case 11: return static_cast<int32_t>(x.regen_ma);
        case 12: return static_cast<int32_t>(x.brake_ma);
        case 13: return x.undervoltage_cv;
        case 14: return x.overvoltage_cv;
        case 15: return x.timeout_ms;
        case 16: return static_cast<int32_t>(x.timeout_brake_ma);
        case 17: return x.reversal_erpm_limit;
        case 18: return x.reversal_dwell_ms;
    }
    return 0;
}

void rawSet(CommissioningProfile& p, uint8_t id, uint8_t w, int32_t v) {
    WheelProfile& x = p.wheel[w & 1];
    switch (id) {
        case 0: p.servo_neutral = static_cast<uint16_t>(v); break;
        case 1: p.servo_min = static_cast<uint16_t>(v); break;
        case 2: p.servo_max = static_cast<uint16_t>(v); break;
        case 3: p.auto_speed_percent = static_cast<uint8_t>(v); break;
        case 4: p.duty_slew_permille_per_s = static_cast<uint16_t>(v); break;
        case 19: p.cw_ddeg_per_s = static_cast<uint16_t>(v); break;
        case 20: p.ccw_ddeg_per_s = static_cast<uint16_t>(v); break;
        case 5: x.direction = static_cast<int8_t>(v); break;
        case 6: x.fw_major = static_cast<uint8_t>(v); break;
        case 7: x.fw_minor = static_cast<uint8_t>(v); break;
        case 8: x.layout = static_cast<uint8_t>(v); break;
        case 9: x.motor_ma = static_cast<uint32_t>(v); break;
        case 10: x.battery_ma = static_cast<uint32_t>(v); break;
        case 11: x.regen_ma = static_cast<uint32_t>(v); break;
        case 12: x.brake_ma = static_cast<uint32_t>(v); break;
        case 13: x.undervoltage_cv = static_cast<uint16_t>(v); break;
        case 14: x.overvoltage_cv = static_cast<uint16_t>(v); break;
        case 15: x.timeout_ms = static_cast<uint16_t>(v); break;
        case 16: x.timeout_brake_ma = static_cast<uint32_t>(v); break;
        case 17: x.reversal_erpm_limit = static_cast<uint16_t>(v); break;
        case 18: x.reversal_dwell_ms = static_cast<uint16_t>(v); break;
    }
}

bool isSet(const CommissioningProfile& p, uint8_t id, uint8_t w) {
    const Spec* s = specFor(id);
    if (!s) return false;
    const uint32_t mask = s->wheel ? p.wheel[w & 1].set_mask : p.set_mask;
    return (mask >> id) & 1u;
}

bool fieldOk(const CommissioningProfile& p, uint8_t id, uint8_t w) {
    const Spec* s = specFor(id);
    if (!s || !isSet(p, id, w)) return false;
    const int32_t v = rawGet(p, id, w);
    if (id == 5) return v == 1 || v == -1;
    return v >= s->lo && v <= s->hi;
}

bool allOk(const CommissioningProfile& p, const uint8_t* ids, size_t n, uint8_t w) {
    for (size_t i = 0; i < n; ++i) if (!fieldOk(p, ids[i], w)) return false;
    return true;
}

bool has(const CommissioningProfile& p, uint8_t bit) { return (p.acceptance >> bit) & 1u; }

const uint8_t kServoIds[] = {0, 1, 2};
const uint8_t kAutoIds[] = {3, 19, 20};
const uint8_t kConfigIds[] = {6, 7, 8, 9, 10, 11, 12, 13, 14};
const uint8_t kBrakeIds[] = {15, 16};
const uint8_t kDirIds[] = {5};
const uint8_t kRevIds[] = {17, 18};

bool servoOrdered(const CommissioningProfile& p) {
    return p.servo_min < p.servo_neutral && p.servo_neutral < p.servo_max;
}

bool prerequisites(const CommissioningProfile& p, uint8_t bit) {
    const uint8_t w = bit & 1;
    switch (bit) {
        case kAcceptServoNeutral:
            return allOk(p, kServoIds, 3, 0) && servoOrdered(p);
        case kAcceptFrontRef: case kAcceptRearRef:
            return has(p, kAcceptServoNeutral);
        case kAcceptAutoTiming:
            return has(p, kAcceptServoNeutral) && allOk(p, kAutoIds, 3, 0);
        case kAcceptVescConfig: case kAcceptVescConfig + 1:
            return allOk(p, kConfigIds, 9, w) &&
                   p.wheel[w].undervoltage_cv < p.wheel[w].overvoltage_cv;
        case kAcceptTimeoutBrake: case kAcceptTimeoutBrake + 1:
            return has(p, kAcceptVescConfig + w) && allOk(p, kBrakeIds, 2, w);
        case kAcceptDirection: case kAcceptDirection + 1:
            return allOk(p, kDirIds, 1, w);
        case kAcceptReversal: case kAcceptReversal + 1:
            return allOk(p, kRevIds, 2, w);
    }
    return false;
}

void clearBits(CommissioningProfile& p, uint32_t mask) { p.acceptance &= ~mask; }

// Acceptance bits invalidated when a field's value changes.
void invalidateFor(CommissioningProfile& p, uint8_t id, uint8_t w) {
    if (id <= 2) clearBits(p, 0x0Fu);
    else if (id == 3 || id == 19 || id == 20) clearBits(p, 1u << kAcceptAutoTiming);
    else if (id == 5) clearBits(p, 1u << (kAcceptDirection + w));
    else if (id >= 6 && id <= 14) clearBits(p, (1u << (kAcceptVescConfig + w)) | (1u << (kAcceptTimeoutBrake + w)));
    else if (id == 15 || id == 16) clearBits(p, 1u << (kAcceptTimeoutBrake + w));
    else if (id == 17 || id == 18) clearBits(p, 1u << (kAcceptReversal + w));
}

bool driveFieldsOk(const CommissioningProfile& p, uint8_t w) {
    return allOk(p, kDirIds, 1, w) && allOk(p, kConfigIds, 9, w) && allOk(p, kBrakeIds, 2, w) &&
           allOk(p, kRevIds, 2, w) && p.wheel[w].undervoltage_cv < p.wheel[w].overvoltage_cv;
}

// ---- record serialization ----
// Profile body: magic, version, generation, globals, two wheels, crc16.
const size_t kProfileBodyLen = 4 + 1 + 4 + (2 + 2 + 2 + 1 + 2 + 2 + 2 + 4 + 4 + 1) +
                               2 * (1 + 1 + 1 + 1 + 4 * 4 + 2 + 2 + 2 + 4 + 2 + 2 + 4);
static_assert(kProfileBodyLen + 2 < ConfigStore::kProfileSlotSize, "profile record must fit its slot");

void encodeProfile(const CommissioningProfile& p, uint32_t generation, uint8_t* img) {
    memset(img, 0, ConfigStore::kProfileSlotSize);
    r2link::Writer w(img, ConfigStore::kProfileSlotSize - 1);
    w.u32(kProfileMagic); w.u8(kRecordVersion); w.u32(generation);
    w.u16(p.servo_neutral); w.u16(p.servo_min); w.u16(p.servo_max); w.u8(p.auto_speed_percent);
    w.u16(p.duty_slew_permille_per_s); w.u16(p.cw_ddeg_per_s); w.u16(p.ccw_ddeg_per_s);
    w.u32(p.set_mask); w.u32(p.acceptance); w.u8(p.allow_remote_drive);
    for (int i = 0; i < 2; ++i) {
        const WheelProfile& x = p.wheel[i];
        w.u8(static_cast<uint8_t>(x.direction)); w.u8(x.fw_major); w.u8(x.fw_minor); w.u8(x.layout);
        w.u32(x.motor_ma); w.u32(x.battery_ma); w.u32(x.regen_ma); w.u32(x.brake_ma);
        w.u16(x.undervoltage_cv); w.u16(x.overvoltage_cv); w.u16(x.timeout_ms);
        w.u32(x.timeout_brake_ma); w.u16(x.reversal_erpm_limit); w.u16(x.reversal_dwell_ms);
        w.u32(x.set_mask);
    }
    w.u16(r2link::crc16(img, w.size()));
    img[ConfigStore::kProfileSlotSize - 1] = kCommitted;
}

void decodeProfile(const uint8_t* img, CommissioningProfile& p) {
    r2link::Reader r(img, ConfigStore::kProfileSlotSize - 1);
    r.u32(); r.u8(); p.generation = r.u32();
    p.servo_neutral = r.u16(); p.servo_min = r.u16(); p.servo_max = r.u16(); p.auto_speed_percent = r.u8();
    p.duty_slew_permille_per_s = r.u16(); p.cw_ddeg_per_s = r.u16(); p.ccw_ddeg_per_s = r.u16();
    p.set_mask = r.u32(); p.acceptance = r.u32(); p.allow_remote_drive = r.u8();
    for (int i = 0; i < 2; ++i) {
        WheelProfile& x = p.wheel[i];
        x.direction = static_cast<int8_t>(r.u8()); x.fw_major = r.u8(); x.fw_minor = r.u8(); x.layout = r.u8();
        x.motor_ma = r.u32(); x.battery_ma = r.u32(); x.regen_ma = r.u32(); x.brake_ma = r.u32();
        x.undervoltage_cv = r.u16(); x.overvoltage_cv = r.u16(); x.timeout_ms = r.u16();
        x.timeout_brake_ma = r.u32(); x.reversal_erpm_limit = r.u16(); x.reversal_dwell_ms = r.u16();
        x.set_mask = r.u32();
    }
}

enum class SlotState : uint8_t { Blank, Valid, Invalid, Torn };

SlotState classify(const uint8_t* buf, size_t n, uint32_t magic, size_t body_len) {
    bool blank = true;
    for (size_t i = 0; i < n && blank; ++i) blank = buf[i] == kBlank;
    if (blank) return SlotState::Blank;
    if (buf[n - 1] == kBlank) return SlotState::Torn;   // body started, commit marker never written
    if (buf[n - 1] != kCommitted) return SlotState::Invalid;
    r2link::Reader r(buf, 5);
    if (r.u32() != magic || r.u8() != kRecordVersion) return SlotState::Invalid;
    const uint16_t crc = static_cast<uint16_t>(buf[body_len] | (buf[body_len + 1] << 8));
    return crc == r2link::crc16(buf, body_len) ? SlotState::Valid : SlotState::Invalid;
}

const size_t kBootBodyLen = 4 + 1 + 4 + 4;  // magic, version, generation, counter

uint32_t u32At(const uint8_t* b, size_t off) {
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

enum class Commit : uint8_t { Ok, IoError, VerifyFailed };

// Marker-last commit: an interrupted write never leaves a committed slot.
Commit commitSlot(RawStorage& s, size_t addr, const uint8_t* img, size_t n, bool invalidate) {
    if (invalidate && s.write(addr + n - 1, &kInvalidated, 1) != StorageResult::Ok) return Commit::IoError;
    if (s.write(addr, img, n - 1) != StorageResult::Ok) return Commit::IoError;
    if (s.write(addr + n - 1, img + n - 1, 1) != StorageResult::Ok) return Commit::IoError;
    uint8_t back[ConfigStore::kProfileSlotSize];
    if (s.read(addr, back, n) != StorageResult::Ok) return Commit::IoError;
    return memcmp(back, img, n) == 0 ? Commit::Ok : Commit::VerifyFailed;
}

struct Scan {
    SlotState state[2];
    uint32_t generation[2];
    uint8_t newest;       // index of newest valid slot, 2 when none
    uint8_t target;       // slot a new record should be written to
    bool any_invalid;     // Invalid or Torn
    bool io_error;
};

Scan scanSlots(RawStorage& s, bool profile) {
    Scan sc;
    sc.state[0] = sc.state[1] = SlotState::Blank;
    sc.generation[0] = sc.generation[1] = 0;
    sc.newest = 2;
    sc.any_invalid = sc.io_error = false;
    const size_t n = profile ? ConfigStore::kProfileSlotSize : ConfigStore::kBootSlotSize;
    if (s.size() < ConfigStore::kRequiredBytes) { sc.io_error = true; sc.target = 0; return sc; }
    for (uint8_t i = 0; i < 2; ++i) {
        uint8_t buf[ConfigStore::kProfileSlotSize];
        const size_t addr = profile ? ConfigStore::profileSlotAddress(i) : ConfigStore::bootSlotAddress(i);
        if (s.read(addr, buf, n) != StorageResult::Ok) { sc.io_error = true; continue; }
        sc.state[i] = classify(buf, n, profile ? kProfileMagic : kBootMagic, profile ? kProfileBodyLen : kBootBodyLen);
        if (sc.state[i] == SlotState::Invalid || sc.state[i] == SlotState::Torn) sc.any_invalid = true;
        if (sc.state[i] == SlotState::Valid) {
            sc.generation[i] = u32At(buf, 5);
            if (sc.newest == 2 || sc.generation[i] > sc.generation[sc.newest]) sc.newest = i;
        }
    }
    sc.target = sc.newest == 0 ? 1 : 0;
    return sc;
}

}  // namespace

CommissioningProfile::CommissioningProfile() {
    memset(this, 0, sizeof *this);
}

FieldResult setField(CommissioningProfile& p, uint8_t id, uint8_t wheel, int32_t value) {
    const Spec* s = specFor(id);
    if (!s) return FieldResult::UnknownField;
    if ((s->wheel && wheel > 1) || (!s->wheel && wheel != 0)) return FieldResult::BadWheel;
    const bool ok = id == 5 ? (value == 1 || value == -1) : (value >= s->lo && value <= s->hi);
    if (!ok) return FieldResult::OutOfRange;
    uint32_t& mask = s->wheel ? p.wheel[wheel].set_mask : p.set_mask;
    if (((mask >> id) & 1u) && rawGet(p, id, wheel) != value) invalidateFor(p, id, wheel);
    rawSet(p, id, wheel, value);
    mask |= 1u << id;
    return FieldResult::Ok;
}

bool getField(const CommissioningProfile& p, uint8_t id, uint8_t wheel, int32_t& value) {
    const Spec* s = specFor(id);
    if (!s || (s->wheel && wheel > 1) || (!s->wheel && wheel != 0) || !isSet(p, id, wheel)) return false;
    value = rawGet(p, id, wheel);
    return true;
}

const char* acceptanceBitName(uint8_t bit) {
    static const char* const names[kAcceptBitCount] = {
        "servo_neutral", "front_reference", "rear_reference", "auto_timing",
        "vesc_config_left", "vesc_config_right", "timeout_brake_left", "timeout_brake_right",
        "direction_left", "direction_right", "reversal_left", "reversal_right"};
    return bit < kAcceptBitCount ? names[bit] : 0;
}

bool layoutSupported(uint8_t layout) { return layout == kLayoutLegacyGetValues; }

AcceptanceEvidence::AcceptanceEvidence()
    : ch6_off(false), ch9_off(false), sticks_centered(false), stationary(false), operator_confirmed(false),
      test_completed(false), test_cancelled(false), cw_completed(false), ccw_completed(false),
      vesc_operator_observed(false), observed_run_id(0), commanded_run_id(0), config_digest(0) {}

uint32_t acceptanceDigest(const CommissioningProfile& p, uint8_t bit) {
    const uint8_t* ids = 0;
    size_t n = 0;
    if (bit <= kAcceptRearRef) { ids = kServoIds; n = 3; }
    else if (bit == kAcceptAutoTiming) { ids = kAutoIds; n = 3; }
    else if (bit < kAcceptTimeoutBrake) { ids = kConfigIds; n = 9; }
    else if (bit < kAcceptDirection) { ids = kBrakeIds; n = 2; }
    else if (bit < kAcceptReversal) { ids = kDirIds; n = 1; }
    else if (bit < kAcceptBitCount) { ids = kRevIds; n = 2; }
    else return 0;
    const uint8_t w = bit >= kAcceptVescConfig ? (bit & 1) : 0;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t v = static_cast<uint32_t>(rawGet(p, ids[i], w));
        const uint8_t bytes[6] = {ids[i], static_cast<uint8_t>(isSet(p, ids[i], w)),
                                  static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                                  static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
        for (size_t k = 0; k < sizeof bytes; ++k) h = (h ^ bytes[k]) * 16777619u;
    }
    return h == 0 ? 1 : h;
}

AcceptResult acceptBit(CommissioningProfile& p, uint8_t bit, const AcceptanceEvidence& e) {
    if (bit >= kAcceptBitCount) return AcceptResult::UnsupportedBit;
    if (!e.ch6_off || !e.ch9_off || !e.sticks_centered || !e.stationary) return AcceptResult::NotStationary;
    if (!e.operator_confirmed) return AcceptResult::NotConfirmed;
    if (!prerequisites(p, bit)) return AcceptResult::Prerequisite;
    if (bit <= kAcceptAutoTiming) {
        const bool run = e.test_completed && !e.test_cancelled && e.observed_run_id != 0 &&
                         e.observed_run_id == e.commanded_run_id;
        const bool both = bit != kAcceptAutoTiming || (e.cw_completed && e.ccw_completed);
        if (!run || !both) return AcceptResult::TestEvidence;
    } else if (!e.vesc_operator_observed) {
        return AcceptResult::TestEvidence;
    }
    if (e.config_digest == 0 || e.config_digest != acceptanceDigest(p, bit)) return AcceptResult::ConfigMismatch;
    p.acceptance |= 1u << bit;
    return AcceptResult::Ok;
}

bool validateProfile(const CommissioningProfile& p) {
    if (p.allow_remote_drive != 0) return false;
    if (p.acceptance >> kAcceptBitCount) return false;
    for (size_t i = 0; i < kSpecCount; ++i) {
        const Spec& s = kSpecs[i];
        for (uint8_t w = 0; w < (s.wheel ? 2 : 1); ++w)
            if (isSet(p, s.id, w) && !fieldOk(p, s.id, w)) return false;
    }
    const bool servo_all = (p.set_mask & 7u) == 7u;
    if (servo_all && !servoOrdered(p)) return false;
    for (uint8_t w = 0; w < 2; ++w)
        if (isSet(p, 13, w) && isSet(p, 14, w) && p.wheel[w].undervoltage_cv >= p.wheel[w].overvoltage_cv) return false;
    for (uint8_t b = 0; b < kAcceptBitCount; ++b)
        if (has(p, b) && !prerequisites(p, b)) return false;
    return true;
}

Readiness readiness(const CommissioningProfile& p) {
    Readiness r = {false, false, false};
    if (!validateProfile(p)) return r;
    r.manual_dome = has(p, kAcceptServoNeutral) && allOk(p, kServoIds, 3, 0);
    r.auto_dome = r.manual_dome && allOk(p, kAutoIds, 3, 0) && has(p, kAcceptFrontRef) &&
                  has(p, kAcceptRearRef) && has(p, kAcceptAutoTiming);
    bool drive = fieldOk(p, kFieldSlew, 0);
    for (uint8_t w = 0; w < 2; ++w)
        drive = drive && driveFieldsOk(p, w) && has(p, kAcceptVescConfig + w) &&
                has(p, kAcceptTimeoutBrake + w) && has(p, kAcceptDirection + w) && has(p, kAcceptReversal + w);
    r.drive = drive;
    return r;
}

uint8_t applyBaseline(CommissioningProfile& p, const ObservedFirmware fw[2]) {
    struct B { uint8_t id; int32_t value; };
    static const B kGlobal[] = {{kFieldServoNeutral, 1500}, {kFieldServoMin, 1000}, {kFieldServoMax, 2000},
                                {kFieldAutoSpeed, 15}, {kFieldSlew, 500}};
    static const B kWheel[] = {{kFieldLayout, 1}, {kFieldMotorMa, 12000}, {kFieldBatteryMa, 5000},
                               {kFieldRegenMa, 2500}, {kFieldBrakeMa, 3000}, {kFieldUndervoltage, 1100},
                               {kFieldOvervoltage, 1480}, {kFieldTimeoutMs, 150}, {kFieldTimeoutBrakeMa, 3000},
                               {kFieldReversalErpm, 300}, {kFieldReversalDwell, 200}};
    uint8_t filled = 0;
    for (const B& b : kGlobal)
        if (!isSet(p, b.id, 0) && setField(p, b.id, 0, b.value) == FieldResult::Ok) ++filled;
    for (uint8_t w = 0; w < 2; ++w) {
        for (const B& b : kWheel)
            if (!isSet(p, b.id, w) && setField(p, b.id, w, b.value) == FieldResult::Ok) ++filled;
        if (fw[w].valid) {
            if (!isSet(p, kFieldFwMajor, w) && setField(p, kFieldFwMajor, w, fw[w].major) == FieldResult::Ok) ++filled;
            if (!isSet(p, kFieldFwMinor, w) && setField(p, kFieldFwMinor, w, fw[w].minor) == FieldResult::Ok) ++filled;
        }
    }
    return filled;
}

uint32_t fieldDigest(const CommissioningProfile& p, uint8_t wheel, const uint8_t* ids, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        const uint32_t v = static_cast<uint32_t>(rawGet(p, ids[i], wheel));
        const uint8_t bytes[6] = {ids[i], static_cast<uint8_t>(isSet(p, ids[i], wheel)),
                                  static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                                  static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)};
        for (uint8_t b : bytes) h = (h ^ b) * 16777619u;
    }
    return h == 0 ? 1 : h;
}

bool sameProfile(const CommissioningProfile& a, const CommissioningProfile& b) {
    if (a.acceptance != b.acceptance || a.allow_remote_drive != b.allow_remote_drive) return false;
    for (size_t i = 0; i < kSpecCount; ++i) {
        const Spec& s = kSpecs[i];
        for (uint8_t w = 0; w < (s.wheel ? 2 : 1); ++w) {
            if (isSet(a, s.id, w) != isSet(b, s.id, w)) return false;
            if (isSet(a, s.id, w) && rawGet(a, s.id, w) != rawGet(b, s.id, w)) return false;
        }
    }
    return true;
}

uint8_t faultBits(ConfigResult r) {
    switch (r) {
        case ConfigResult::Ready: return 0;
        case ConfigResult::Uncommissioned: return 1;
        default: return 2;   // corrupt or unreadable profile; boot storage is separate (faultMask)
    }
}

// ---- store ----
ConfigStore::ConfigStore(RawStorage& storage)
    : storage_(storage), gate_ch6_off_(false), gate_inactive_(false), boot_cached_(false),
      boot_session_(0), generation_(0), last_save_(SaveResult::Ok), boot_result_(BootResult::NotAttempted) {}

uint8_t ConfigStore::faultMask(ConfigResult profile) const {
    const bool boot_fault = boot_result_ == BootResult::IoError || boot_result_ == BootResult::Corrupt ||
                            boot_result_ == BootResult::CounterExhausted;
    return static_cast<uint8_t>(faultBits(profile) | (boot_fault ? 4 : 0));
}

void ConfigStore::encodeBootSlot(uint32_t generation, uint32_t counter, uint8_t out[kBootSlotSize]) {
    memset(out, 0, kBootSlotSize);
    r2link::Writer w(out, kBootSlotSize - 1);
    w.u32(kBootMagic); w.u8(kRecordVersion); w.u32(generation); w.u32(counter);
    w.u16(r2link::crc16(out, w.size()));
    out[kBootSlotSize - 1] = kCommitted;
}

ConfigResult ConfigStore::load(CommissioningProfile& profile) {
    profile = CommissioningProfile();
    const Scan sc = scanSlots(storage_, true);
    if (sc.io_error) return ConfigResult::IoError;
    if (sc.newest == 2) return sc.any_invalid ? ConfigResult::Corrupt : ConfigResult::Uncommissioned;
    uint8_t buf[kProfileSlotSize];
    if (storage_.read(profileSlotAddress(sc.newest), buf, kProfileSlotSize) != StorageResult::Ok)
        return ConfigResult::IoError;
    CommissioningProfile loaded;
    decodeProfile(buf, loaded);
    if (!validateProfile(loaded)) return ConfigResult::Corrupt;
    profile = loaded;
    generation_ = loaded.generation;
    return ConfigResult::Ready;
}

bool ConfigStore::nextBootSession(uint32_t& session) {
    if (boot_cached_) { session = boot_session_; return true; }
    const Scan sc = scanSlots(storage_, false);
    if (sc.io_error) { boot_result_ = BootResult::IoError; return false; }
    if (sc.newest == 2 && sc.any_invalid) {
        // Only a torn first-ever write (one uncommitted slot beside a blank one) is recoverable:
        // no session was ever issued. Any committed-but-bad record means the counter is unknown.
        const bool torn_first = (sc.state[0] == SlotState::Torn && sc.state[1] == SlotState::Blank) ||
                                (sc.state[1] == SlotState::Torn && sc.state[0] == SlotState::Blank);
        if (!torn_first) { boot_result_ = BootResult::Corrupt; return false; }
    }
    uint32_t generation = 0, counter = 0;
    if (sc.newest != 2) {
        uint8_t buf[kBootSlotSize];
        if (storage_.read(bootSlotAddress(sc.newest), buf, kBootSlotSize) != StorageResult::Ok) {
            boot_result_ = BootResult::IoError;
            return false;
        }
        generation = sc.generation[sc.newest];
        counter = u32At(buf, 9);
    }
    if (generation == 0xFFFFFFFFu) { boot_result_ = BootResult::CounterExhausted; return false; }
    uint32_t next = counter + 1;
    if (next == 0) next = 1;
    uint8_t img[kBootSlotSize];
    encodeBootSlot(generation + 1, next, img);
    const Commit commit = commitSlot(storage_, bootSlotAddress(sc.target), img, kBootSlotSize,
                                     sc.state[sc.target] != SlotState::Blank);
    if (commit != Commit::Ok) { boot_result_ = BootResult::IoError; return false; }
    boot_session_ = next;
    boot_cached_ = true;
    boot_result_ = BootResult::Ok;
    session = next;
    return true;
}

void ConfigStore::setProvisioningGate(bool ch6_off, bool actuators_inactive) {
    gate_ch6_off_ = ch6_off;
    gate_inactive_ = actuators_inactive;
}

bool ConfigStore::save(const CommissioningProfile& profile) {
    return trySave(profile, gate_ch6_off_, gate_inactive_) == SaveResult::Ok;
}

SaveResult ConfigStore::trySave(const CommissioningProfile& profile, bool ch6_off, bool actuators_inactive) {
    SaveResult r = SaveResult::Ok;
    if (!ch6_off || !actuators_inactive) r = SaveResult::GateClosed;
    else if (!validateProfile(profile)) r = SaveResult::InvalidProfile;
    else {
        const Scan sc = scanSlots(storage_, true);
        if (sc.io_error) r = SaveResult::IoError;
        else if (sc.newest != 2 && sc.generation[sc.newest] == 0xFFFFFFFFu) r = SaveResult::GenerationExhausted;
        else {
            const uint32_t generation = sc.newest == 2 ? 1 : sc.generation[sc.newest] + 1;
            uint8_t img[kProfileSlotSize];
            encodeProfile(profile, generation, img);
            const Commit c = commitSlot(storage_, profileSlotAddress(sc.target), img, kProfileSlotSize,
                                        sc.state[sc.target] != SlotState::Blank);
            if (c == Commit::Ok) generation_ = generation;
            else r = c == Commit::IoError ? SaveResult::IoError : SaveResult::VerifyFailed;
        }
    }
    last_save_ = r;
    return r;
}

}  // namespace body
