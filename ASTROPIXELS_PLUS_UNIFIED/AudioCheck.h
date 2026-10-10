#pragma once

#include <cstdint>
#include "Messages.h"
#include "Endpoint.h"

// Confirms the DFPlayer reports playback started for a requested track.
class AudioCheck {
public:
    enum class State : uint8_t { Idle, Waiting, Passed, Failed };
    static const uint32_t kTimeoutMs = 3000;

    void begin(uint16_t sequence, bool queued, uint32_t now_ms);   // after RemoteAudio::play(255, ...)
    void onEvent(const r2link::Event& ev);
    void onCompletion(const r2link::Completion& c);
    void tick(uint32_t now_ms);
    State state() const;

private:
    State state_{State::Idle};
    uint16_t seq_{0};
    uint32_t start_ms_{0};
};
