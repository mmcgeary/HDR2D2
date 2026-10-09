#pragma once
// Portable commissioning profile and boot-counter persistence over an
// injectable raw byte store. No Arduino or EEPROM types appear here; a Teensy
// adapter implements RawStorage at integration time.
//
// There are no built-in hardware defaults: a profile starts with every field
// unset and every readiness flag false. Saving to storage is NOT manufacturing
// acceptance; acceptance bits are set only by acceptBit() with explicit
// AcceptanceEvidence. No API here sets a bit from field validity alone, and
// load() never grants one. (A record whose flags were altered together with a
// matching CRC is not a threat this module can detect in physical EEPROM.)
// Nothing here configures a VESC.
//
// All calls are loop-context only and never block.
#include <stddef.h>
#include <stdint.h>

namespace body {

enum class StorageResult : uint8_t { Ok, ReadError, WriteError, OutOfRange };

class RawStorage {
public:
    virtual size_t size() const = 0;
    virtual StorageResult read(size_t address, uint8_t* destination, size_t length) = 0;
    virtual StorageResult write(size_t address, const uint8_t* source, size_t length) = 0;
protected:
    ~RawStorage() {}
};

// Blank (all 0xFF) storage is Uncommissioned, never Corrupt. A read failure is
// IoError and dominates every other result.
enum class ConfigResult : uint8_t { Uncommissioned, Ready, Corrupt, IoError };
enum class SaveResult : uint8_t { Ok, GateClosed, InvalidProfile, IoError, VerifyFailed, GenerationExhausted };
enum class FieldResult : uint8_t { Ok, UnknownField, BadWheel, OutOfRange };
enum class AcceptResult : uint8_t {
    Ok, UnsupportedBit,
    NotStationary,   // CH6 and CH9 not both OFF, sticks not centered, or motion not stopped
    NotConfirmed,    // no explicit operator confirmation
    Prerequisite,    // fields or earlier acceptance bits missing
    TestEvidence,    // required completed run / operator observation missing
    ConfigMismatch   // evidence was gathered against a different staged configuration
};
enum class BootResult : uint8_t { NotAttempted, Ok, IoError, Corrupt, CounterExhausted };

// Field ids follow the commissioning protocol (spec section 9).
enum FieldId : uint8_t {
    kFieldServoNeutral = 0, kFieldServoMin = 1, kFieldServoMax = 2, kFieldAutoSpeed = 3, kFieldSlew = 4,
    kFieldDirection = 5, kFieldFwMajor = 6, kFieldFwMinor = 7, kFieldLayout = 8,
    kFieldMotorMa = 9, kFieldBatteryMa = 10, kFieldRegenMa = 11, kFieldBrakeMa = 12,
    kFieldUndervoltage = 13, kFieldOvervoltage = 14, kFieldTimeoutMs = 15, kFieldTimeoutBrakeMa = 16,
    kFieldReversalErpm = 17, kFieldReversalDwell = 18, kFieldCwRate = 19, kFieldCcwRate = 20
};

// Supported VESC values-reply shape (field id 8). This is a protocol shape, not
// a claim of firmware compatibility: kLayoutLegacyGetValues is the legacy
// COMM_GET_VALUES (command 4) reply. Its byte offsets belong to the Task 4
// decoder (temperature, motor/input current, duty, eRPM, input voltage, ...,
// fault code) and Task 4 must verify them against each installed firmware
// before any drive is permitted. kLayoutUnknown (0) is the unset value: it can
// never be stored as a field, never validates and never makes drive ready.
enum VescValuesLayout : uint8_t { kLayoutUnknown = 0, kLayoutLegacyGetValues = 1 };
bool layoutSupported(uint8_t layout);

// Acceptance bits (single definition; the later UI binds to these names).
// Wheel bits are base + wheel (0 left, 1 right).
//   0 servo_neutral      completed neutral trial run
//   1 front_reference    completed front-reference acquisition run
//   2 rear_reference     completed rear-reference acquisition run
//   3 auto_timing        completed uncancelled run in both directions
//   4/5 vesc_config      operator observed VESC Tool agrees with the record
//   6/7 timeout_brake    operator observed external timeout/brake test
//   8/9 direction        operator observed wheel direction
//   10/11 reversal       operator observed external reversal test
enum AcceptanceBit : uint8_t {
    kAcceptServoNeutral = 0, kAcceptFrontRef = 1, kAcceptRearRef = 2, kAcceptAutoTiming = 3,
    kAcceptVescConfig = 4, kAcceptTimeoutBrake = 6, kAcceptDirection = 8, kAcceptReversal = 10,
    kAcceptBitCount = 12
};
const char* acceptanceBitName(uint8_t bit);   // null for undefined bits

// Observations the caller (commissioning layer) must supply. Every member
// defaults to false/0; nothing is inferred. All bits need the first group.
// Bits 0-3 need a completed dome run whose observed_run_id equals the
// commanded_run_id of the run the body started (nonzero, not cancelled; bit 3
// also both directions). Bits 4-11 need vesc_operator_observed. For every bit,
// config_digest must equal acceptanceDigest() of the CURRENT staged profile,
// so evidence collected for other values is rejected.
struct AcceptanceEvidence {
    bool ch6_off, ch9_off, sticks_centered, stationary, operator_confirmed;
    bool test_completed, test_cancelled, cw_completed, ccw_completed;
    bool vesc_operator_observed;
    uint32_t observed_run_id, commanded_run_id, config_digest;
    AcceptanceEvidence();
};

struct WheelProfile {
    int8_t direction;
    uint8_t fw_major, fw_minor, layout;
    uint32_t motor_ma, battery_ma, regen_ma, brake_ma;
    uint16_t undervoltage_cv, overvoltage_cv, timeout_ms;
    uint32_t timeout_brake_ma;
    uint16_t reversal_erpm_limit, reversal_dwell_ms;
    uint32_t set_mask;              // bit n set when field id n has been provided
};

struct CommissioningProfile {
    uint16_t servo_neutral, servo_min, servo_max;
    uint8_t auto_speed_percent;
    uint16_t duty_slew_permille_per_s, cw_ddeg_per_s, ccw_ddeg_per_s;
    uint32_t set_mask;              // bit n set when global field id n has been provided
    uint32_t acceptance;
    uint8_t allow_remote_drive;     // always 0 in this release
    uint32_t generation;            // filled by load(); ignored by save()
    WheelProfile wheel[2];
    CommissioningProfile();
};

struct Readiness { bool drive, manual_dome, auto_dome; };

FieldResult setField(CommissioningProfile& p, uint8_t id, uint8_t wheel, int32_t value);
bool getField(const CommissioningProfile& p, uint8_t id, uint8_t wheel, int32_t& value);
uint32_t acceptanceDigest(const CommissioningProfile& p, uint8_t bit);   // 0 for undefined bits
AcceptResult acceptBit(CommissioningProfile& p, uint8_t bit, const AcceptanceEvidence& evidence);
bool validateProfile(const CommissioningProfile& p);
Readiness readiness(const CommissioningProfile& p);
// BODY_STATUS fault bits for the profile: bit0 configuration missing, bit1
// configuration corrupt or unreadable. Boot-session storage is reported by
// ConfigStore::faultMask() as bit2, never mixed into the profile result.
uint8_t faultBits(ConfigResult r);

class ConfigStore {
public:
    static const size_t kBootSlotSize = 32, kProfileSlotSize = 128;
    static const size_t kBootBase = 0, kProfileBase = 64, kRequiredBytes = 64 + 2 * 128;

    static size_t bootSlotAddress(uint8_t index) { return kBootBase + index * kBootSlotSize; }
    static size_t profileSlotAddress(uint8_t index) { return kProfileBase + index * kProfileSlotSize; }
    static void encodeBootSlot(uint32_t generation, uint32_t counter, uint8_t out[kBootSlotSize]);

    explicit ConfigStore(RawStorage& storage);

    ConfigResult load(CommissioningProfile& profile);
    // Allocates this boot's session id once and caches it. Returns false (no
    // session) when the counter cannot be read, written or trusted. A torn
    // first-ever write (one torn slot, one blank, never a committed record)
    // recovers with counter 1; any other lack of a valid record is Corrupt.
    bool nextBootSession(uint32_t& session);
    BootResult bootResult() const { return boot_result_; }
    // faultBits(profile) plus bit2 when boot-session storage failed.
    uint8_t faultMask(ConfigResult profile) const;

    // Saving requires the provisioning gate: ch6 off and actuators inactive.
    void setProvisioningGate(bool ch6_off, bool actuators_inactive);
    bool save(const CommissioningProfile& profile);
    SaveResult trySave(const CommissioningProfile& profile, bool ch6_off, bool actuators_inactive);
    SaveResult lastSave() const { return last_save_; }
    uint32_t generation() const { return generation_; }

private:
    RawStorage& storage_;
    bool gate_ch6_off_, gate_inactive_, boot_cached_;
    uint32_t boot_session_, generation_;
    SaveResult last_save_;
    BootResult boot_result_;
};

}  // namespace body
