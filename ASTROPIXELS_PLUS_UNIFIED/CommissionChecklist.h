#pragma once

#include <cstddef>
#include <cstdint>

// Inputs for the guided-commissioning progress checklist shown on the web page.
struct ChecklistInput {
    uint16_t saved_acceptance;   // CommissionStatus.saved_acceptance
    bool unsaved, baseline_filled, radio_passed, failsafe_passed, audio_passed, status_fresh;
};

// Writes one line per item ("[x] VESC config L" / "[ ] ...") separated by '\n'; returns
// bytes written. Always NUL-terminates when capacity > 0; truncates safely.
size_t formatChecklist(const ChecklistInput& in, char* out, size_t capacity);

// The next incomplete item's name, or "All done" when complete.
const char* nextChecklistStep(const ChecklistInput& in);
