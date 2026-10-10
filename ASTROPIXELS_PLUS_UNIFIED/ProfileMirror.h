#pragma once
#include <cstdint>
#include "Messages.h"

class IFieldReader {
public:
    virtual ~IFieldReader() = default;
    virtual bool requestRead(uint8_t field, uint8_t wheel, uint32_t now_ms) = 0;
};

class ICommissionSink {
public:
    virtual ~ICommissionSink() = default;
    virtual bool sendCommission(const r2link::CommissionRequest& req, uint32_t now_ms, uint16_t& seq) = 0;
};

// Keeps a cached copy of every commissioning profile field on the body by
// issuing one field Read at a time.
class ProfileMirror {
public:
    static const uint8_t kSlots = 35;           // 7 global + 2 x 14 wheel fields
    static const uint32_t kReadTimeoutMs = 250, kReadSpacingMs = 50;

    explicit ProfileMirror(IFieldReader& reader);
    void tick(uint32_t now_ms, bool link_up, const r2link::Diagnostics& latest, uint32_t latest_rx_ms);
    bool value(uint8_t field, uint8_t wheel, int32_t& out) const;   // false: unknown or unset
    bool known(uint8_t field, uint8_t wheel) const;                 // a read reply arrived
    void invalidate(uint8_t field, uint8_t wheel);                  // re-read this one next

private:
    struct Slot { uint8_t field, wheel; bool known, set; int32_t value; };
    int find(uint8_t field, uint8_t wheel) const;
    IFieldReader& reader_;
    Slot slots_[kSlots];
    uint8_t next_{0};
    int outstanding_{-1};
    int priority_{-1};
    uint32_t sent_ms_{0};
};
