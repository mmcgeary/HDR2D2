#pragma once

#include <cstdint>
#include "BodyClient.h"
#include "Messages.h"

class RemoteAudio {
public:
    explicit RemoteAudio(BodyClient& client);

    // now_ms is required: the link's connected() check is time based, so a
    // defaulted 0 would make every request look like it was sent with no link.
    RequestHandle play(uint16_t track, r2link::AudioPriority priority, uint32_t now_ms);
    RequestHandle playFolder(uint8_t folder, uint16_t track, r2link::AudioPriority priority, uint32_t now_ms);
    RequestHandle stop(uint32_t now_ms);
    RequestHandle pause(uint32_t now_ms);
    RequestHandle resume(uint32_t now_ms);
    RequestHandle setVolume(uint8_t volume, uint32_t now_ms);

    r2link::AudioStatus status(uint32_t now_ms) const;
    ClientError lastError() const;

private:
    BodyClient& client_;
};
