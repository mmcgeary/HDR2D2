#include "RemoteAudio.h"

RemoteAudio::RemoteAudio(BodyClient& client) : client_(client) {}

RequestHandle RemoteAudio::play(uint16_t track, r2link::AudioPriority priority, uint32_t now_ms) {
    return playFolder(1, track, priority, now_ms);
}

RequestHandle RemoteAudio::playFolder(uint8_t folder, uint16_t track, r2link::AudioPriority priority, uint32_t now_ms) {
    r2link::AudioRequest req{};
    req.operation = static_cast<uint8_t>(r2link::AudioOperation::Play);
    req.folder = folder;
    req.track = track;
    req.volume = 0;
    req.priority = static_cast<uint8_t>(priority);
    return client_.requestAudio(req, now_ms);
}

RequestHandle RemoteAudio::stop(uint32_t now_ms) {
    r2link::AudioRequest req{};
    req.operation = static_cast<uint8_t>(r2link::AudioOperation::Stop);
    return client_.requestAudio(req, now_ms);
}

RequestHandle RemoteAudio::pause(uint32_t now_ms) {
    r2link::AudioRequest req{};
    req.operation = static_cast<uint8_t>(r2link::AudioOperation::Pause);
    return client_.requestAudio(req, now_ms);
}

RequestHandle RemoteAudio::resume(uint32_t now_ms) {
    r2link::AudioRequest req{};
    req.operation = static_cast<uint8_t>(r2link::AudioOperation::Resume);
    return client_.requestAudio(req, now_ms);
}

RequestHandle RemoteAudio::setVolume(uint8_t volume, uint32_t now_ms) {
    r2link::AudioRequest req{};
    req.operation = static_cast<uint8_t>(r2link::AudioOperation::SetVolume);
    req.volume = volume > r2link::kMaxVolume ? r2link::kMaxVolume : volume;
    return client_.requestAudio(req, now_ms);
}

r2link::AudioStatus RemoteAudio::status(uint32_t now_ms) const {
    return client_.audioStatus(now_ms);
}

ClientError RemoteAudio::lastError() const {
    return client_.lastError();
}
