#include "AudioCheck.h"

void AudioCheck::begin(uint16_t seq, bool queued, uint32_t now) {
    seq_ = seq; start_ms_ = now; state_ = queued ? State::Waiting : State::Failed;
}
void AudioCheck::onEvent(const r2link::Event& ev) {
    if (state_ != State::Waiting || ev.request_type != uint8_t(r2link::MessageType::AudioRequest) ||
        ev.request_seq != seq_) return;
    if (ev.kind == uint8_t(r2link::EventKind::PlaybackStarted)) state_ = State::Passed;
    else if (ev.kind != uint8_t(r2link::EventKind::Completed)) state_ = State::Failed;
}
void AudioCheck::onCompletion(const r2link::Completion& c) {
    if (state_ != State::Waiting || c.type != r2link::MessageType::AudioRequest || c.sequence != seq_) return;
    if (c.outcome != r2link::Outcome::Replied || c.result != uint8_t(r2link::Result::Accepted)) state_ = State::Failed;
}
void AudioCheck::tick(uint32_t now) {
    if (state_ == State::Waiting && now - start_ms_ >= kTimeoutMs) state_ = State::Failed;
}
AudioCheck::State AudioCheck::state() const { return state_; }
