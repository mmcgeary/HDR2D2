#pragma once

#include <cstdint>

struct BodyRcState {
    bool valid{false};
    uint16_t source_age_ms{0};
    uint32_t sample_counter{0};
    uint16_t flags{0};
    uint16_t channels[10]{};
    uint8_t drive_state{0};
    uint8_t dome_state{0};
    uint16_t control_epoch{0};
};
