#pragma once

#include <cstdint>
#include "BodyClient.h"
#include "Messages.h"

class RemoteAudio {
public:
    explicit RemoteAudio(BodyClient& client);

    RequestHandle play(uint16_t track, r2link::AudioPriority priority = r2link::AudioPriority::Foreground, uint32_t now_ms = 0);
    RequestHandle playFolder(uint8_t folder, uint16_t track, r2link::AudioPriority priority = r2link::AudioPriority::Foreground, uint32_t now_ms = 0);
    RequestHandle stop(uint32_t now_ms = 0);
    RequestHandle pause(uint32_t now_ms = 0);
    RequestHandle resume(uint32_t now_ms = 0);
    RequestHandle setVolume(uint8_t volume, uint32_t now_ms = 0);

    r2link::AudioStatus status(uint32_t now_ms) const;
    ClientError lastError() const;

private:
    BodyClient& client_;
};
