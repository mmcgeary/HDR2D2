#include "ProfileMirror.h"

ProfileMirror::ProfileMirror(IFieldReader& reader) : reader_(reader), slots_{} {
    static const uint8_t kGlobal[] = {0, 1, 2, 3, 4, 19, 20};
    uint8_t i = 0;
    for (uint8_t f : kGlobal) slots_[i++] = Slot{f, 0, false, false, 0};
    for (uint8_t w = 0; w < 2; ++w)
        for (uint8_t f = 5; f <= 18; ++f) slots_[i++] = Slot{f, w, false, false, 0};
}

int ProfileMirror::find(uint8_t field, uint8_t wheel) const {
    for (uint8_t i = 0; i < kSlots; ++i)
        if (slots_[i].field == field && slots_[i].wheel == wheel) return i;
    return -1;
}

void ProfileMirror::tick(uint32_t now, bool link_up, const r2link::Diagnostics& d, uint32_t rx_ms) {
    if (!link_up) {                     // the body may restart or change: forget every cached field
        outstanding_ = -1;
        for (Slot& slot : slots_) slot.known = false;
        return;
    }
    if (outstanding_ >= 0) {
        Slot& s = slots_[outstanding_];
        if (d.subtype == 1 && d.field == s.field && d.wheel == s.wheel && rx_ms >= sent_ms_ && rx_ms != 0) {
            s.known = true; s.set = d.known != 0; s.value = d.value;
            outstanding_ = -1;
        } else if (now - sent_ms_ >= kReadTimeoutMs) {
            outstanding_ = -1;
        } else {
            return;
        }
    }
    if (now - sent_ms_ < kReadSpacingMs && sent_ms_ != 0) return;
    const int slot = priority_ >= 0 ? priority_ : next_;
    if (!reader_.requestRead(slots_[slot].field, slots_[slot].wheel, now)) return;
    outstanding_ = slot; sent_ms_ = now;
    if (priority_ >= 0) priority_ = -1;
    else next_ = static_cast<uint8_t>((next_ + 1) % kSlots);
}

bool ProfileMirror::value(uint8_t field, uint8_t wheel, int32_t& out) const {
    const int i = find(field, wheel);
    if (i < 0 || !slots_[i].known || !slots_[i].set) return false;
    out = slots_[i].value; return true;
}

bool ProfileMirror::known(uint8_t field, uint8_t wheel) const {
    const int i = find(field, wheel); return i >= 0 && slots_[i].known;
}

void ProfileMirror::invalidate(uint8_t field, uint8_t wheel) {
    const int i = find(field, wheel);
    if (i >= 0) { slots_[i].known = false; priority_ = i; }
}
