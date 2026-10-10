#include "CommissionChecklist.h"
#include <cstdio>

namespace {
struct Item { const char* name; int bit; };   // bit < 0: non-acceptance item
const Item kItems[] = {
    {"Baseline filled", -1}, {"VESC config L", 4}, {"VESC config R", 5},
    {"Timeout brake L", 6}, {"Timeout brake R", 7}, {"Direction L", 8}, {"Direction R", 9},
    {"Reversal L", 10}, {"Reversal R", 11}, {"Dome neutral", 0}, {"Front reference", 1},
    {"Rear reference", 2}, {"Dome timing", 3}, {"Radio check", -2}, {"Failsafe check", -3},
    {"Audio check", -4}, {"Save profile", -5}};

bool done(const ChecklistInput& in, const Item& it) {
    switch (it.bit) {
    case -1: return in.baseline_filled;
    case -2: return in.radio_passed;
    case -3: return in.failsafe_passed;
    case -4: return in.audio_passed;
    case -5: return in.status_fresh && !in.unsaved;
    default: return (in.saved_acceptance >> it.bit) & 1u;
    }
}
}  // namespace

size_t formatChecklist(const ChecklistInput& in, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    size_t n = 0;
    out[0] = '\0';
    for (const Item& it : kItems) {
        const int w = snprintf(out + n, cap - n, "[%c] %s\n", done(in, it) ? 'x' : ' ', it.name);
        if (w < 0 || size_t(w) >= cap - n) { out[cap - 1] = '\0'; return cap - 1; }
        n += size_t(w);
    }
    return n;
}

const char* nextChecklistStep(const ChecklistInput& in) {
    for (const Item& it : kItems)
        if (!done(in, it)) return it.name;
    return "All done";
}
