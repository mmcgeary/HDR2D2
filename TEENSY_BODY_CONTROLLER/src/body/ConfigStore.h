#pragma once
// Portable commissioning profile and boot-counter persistence over an
// injectable raw byte store. No Arduino or EEPROM types appear here; a Teensy
// adapter implements RawStorage at integration time.
//
// There are no built-in hardware defaults: a profile starts with every field
// unset and every readiness flag false. Saving to storage is NOT manufacturing
// acceptance; acceptance bits are set only by acceptBit() after the
// corresponding commissioning step. Nothing here configures a VESC.
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
enum class AcceptResult : uint8_t { Ok, UnsupportedBit, Prerequisite };

// Field ids follow the commissioning protocol (spec section 9).
enum FieldId : uint8_t {
    kFieldServoNeutral = 0, kFieldServoMin = 1, kFieldServoMax = 2, kFieldAutoSpeed = 3, kFieldSlew = 4,
    kFieldDirection = 5, kFieldFwMajor = 6, kFieldFwMinor = 7, kFieldLayout = 8,
    kFieldMotorMa = 9, kFieldBatteryMa = 10, kFieldRegenMa = 11, kFieldBrakeMa = 12,
    kFieldUndervoltage = 13, kFieldOvervoltage = 14, kFieldTimeoutMs = 15, kFieldTimeoutBrakeMa = 16,
    kFieldReversalErpm = 17, kFieldReversalDwell = 18, kFieldCwRate = 19, kFieldCcwRate = 20
};

// Acceptance bits. Wheel-specific bits are base + wheel (0 left, 1 right).
enum AcceptanceBit : uint8_t {
    kAcceptServoNeutral = 0, kAcceptFrontRef = 1, kAcceptRearRef = 2, kAcceptAutoTiming = 3,
    kAcceptVescConfig = 4, kAcceptTimeoutBrake = 6, kAcceptDirection = 8, kAcceptReversal = 10,
    kAcceptBitCount = 12
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
AcceptResult acceptBit(CommissioningProfile& p, uint8_t bit);
bool validateProfile(const CommissioningProfile& p);
Readiness readiness(const CommissioningProfile& p);
// Fault bits for status reporting: 0 ready, 1 uncommissioned, 2 corrupt or I/O error.
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
    // session) when the counter cannot be read, written or trusted.
    bool nextBootSession(uint32_t& session);

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
};

}  // namespace body
