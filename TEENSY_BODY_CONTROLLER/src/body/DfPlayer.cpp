#include "body/DfPlayer.h"
#include <cstring>

namespace body {

DfPlayer::DfPlayer(r2link::BytePort& port, const TrackInfo* catalog, size_t count)
    : port_(port), catalog_(catalog), catalog_count_(count) {}

void DfPlayer::initSimulated(uint32_t now_ms) {
    (void)now_ms;
    init_step_ = InitStep::Ready;
    state_ = r2link::AudioState::Idle;
    q_head_ = q_tail_ = q_count_ = 0;
    ev_head_ = ev_tail_ = ev_count_ = 0;
    rx_len_ = 0;
    last_tx_ms_ = 0;
    last_poll_ms_ = 0;
}

uint16_t DfPlayer::calculateChecksum(const uint8_t* p) {
    uint16_t sum = 0;
    for (int i = 1; i < 7; ++i) sum += p[i];
    return static_cast<uint16_t>(-static_cast<int16_t>(sum));
}

void DfPlayer::serializePacket(uint8_t cmd, uint8_t feedback, uint8_t param_h, uint8_t param_l, uint8_t out[10]) {
    out[0] = 0x7E;
    out[1] = 0xFF;
    out[2] = 0x06;
    out[3] = cmd;
    out[4] = feedback;
    out[5] = param_h;
    out[6] = param_l;
    const uint16_t sum = calculateChecksum(out);
    out[7] = static_cast<uint8_t>(sum >> 8);
    out[8] = static_cast<uint8_t>(sum & 0xFF);
    out[9] = 0xEF;
}

const TrackInfo* DfPlayer::findTrack(uint16_t track) const {
    if (!catalog_) return nullptr;
    for (size_t i = 0; i < catalog_count_; ++i) {
        if (catalog_[i].track == track) {
            return &catalog_[i];
        }
    }
    return nullptr;
}

void DfPlayer::pushEvent(r2link::EventKind kind, uint16_t seq, r2link::Detail detail) {
    if (ev_count_ >= kMaxEvents) return;
    events_[ev_tail_].kind = static_cast<uint8_t>(kind);
    events_[ev_tail_].request_type = static_cast<uint8_t>(r2link::MessageType::AudioRequest);
    events_[ev_tail_].request_seq = seq;
    events_[ev_tail_].detail = static_cast<uint16_t>(detail);
    ev_tail_ = (ev_tail_ + 1) % kMaxEvents;
    ++ev_count_;
}

bool DfPlayer::takeEvent(r2link::Event& ev) {
    if (ev_count_ == 0) return false;
    ev = events_[ev_head_];
    ev_head_ = (ev_head_ + 1) % kMaxEvents;
    --ev_count_;
    return true;
}

bool DfPlayer::enqueueCommand(uint8_t cmd, uint8_t param_h, uint8_t param_l, bool high_priority) {
    if (q_count_ >= kMaxQueue) return false;
    if (high_priority && q_count_ > 0) {
        q_head_ = (q_head_ + kMaxQueue - 1) % kMaxQueue;
        queue_[q_head_] = OutCommand{cmd, 0, param_h, param_l};
        ++q_count_;
        return true;
    }
    queue_[q_tail_] = OutCommand{cmd, 0, param_h, param_l};
    q_tail_ = (q_tail_ + 1) % kMaxQueue;
    ++q_count_;
    return true;
}

void DfPlayer::clearQueuedPlays() {
    OutCommand temp[kMaxQueue];
    size_t new_count = 0;
    while (q_count_ > 0) {
        OutCommand c = queue_[q_head_];
        q_head_ = (q_head_ + 1) % kMaxQueue;
        --q_count_;
        if (c.cmd != 0x0F) {
            temp[new_count++] = c;
        }
    }
    for (size_t i = 0; i < new_count; ++i) {
        queue_[i] = temp[i];
    }
    q_head_ = 0;
    q_tail_ = new_count;
    q_count_ = new_count;
}

r2link::Result DfPlayer::request(const r2link::AudioRequest& req, uint16_t owner_seq, uint32_t now_ms) {
    if (state_ == r2link::AudioState::Offline) {
        return r2link::Result::NotReady;
    }

    if (req.operation == uint8_t(r2link::AudioOperation::Play)) {
        if (req.folder == 0 || req.track == 0) {
            return r2link::Result::InvalidArgument;
        }

        if ((state_ == r2link::AudioState::Starting || state_ == r2link::AudioState::Playing) &&
            priority_ == 1 && req.priority == 0) {
            return r2link::Result::Inhibited;
        }

        if ((state_ == r2link::AudioState::Starting || state_ == r2link::AudioState::Playing || state_ == r2link::AudioState::Paused) &&
            owner_seq_ != 0) {
            pushEvent(r2link::EventKind::Cancelled, owner_seq_, r2link::Detail::None);
        }

        folder_ = req.folder;
        track_ = req.track;
        priority_ = req.priority;
        owner_seq_ = owner_seq;
        state_ = r2link::AudioState::Starting;
        playback_start_ms_ = 0;
        start_request_ms_ = now_ms;
        elapsed_ms_ = 0;

        const TrackInfo* info = findTrack(track_);
        if (info) {
            duration_ms_ = info->duration_ms;
            guard_ms_ = info->completion_guard_ms;
            duration_known_ = (info->duration_ms > 0);
        } else {
            duration_ms_ = 0;
            guard_ms_ = 600000;
            duration_known_ = false;
        }

        clearQueuedPlays();
        enqueueCommand(0x0F, folder_, static_cast<uint8_t>(track_ & 0xFF), true);
        return r2link::Result::Accepted;
    }

    if (req.operation == uint8_t(r2link::AudioOperation::Stop)) {
        clearQueuedPlays();
        if (state_ == r2link::AudioState::Starting || state_ == r2link::AudioState::Playing || state_ == r2link::AudioState::Paused) {
            if (owner_seq_ != 0) {
                pushEvent(r2link::EventKind::Cancelled, owner_seq_, r2link::Detail::None);
            }
            state_ = r2link::AudioState::Idle;
        }
        enqueueCommand(0x16, 0, 0, true);
        return r2link::Result::Accepted;
    }

    if (req.operation == uint8_t(r2link::AudioOperation::Pause)) {
        if (state_ == r2link::AudioState::Playing) {
            state_ = r2link::AudioState::Paused;
            pause_start_ms_ = now_ms;
            if (playback_start_ms_ > 0 && now_ms >= playback_start_ms_) {
                elapsed_ms_ = now_ms - playback_start_ms_;
            }
            enqueueCommand(0x0E, 0, 0, true);
        }
        return r2link::Result::Accepted;
    }

    if (req.operation == uint8_t(r2link::AudioOperation::Resume)) {
        if (state_ == r2link::AudioState::Paused) {
            state_ = r2link::AudioState::Playing;
            if (pause_start_ms_ > 0) {
                playback_start_ms_ += (now_ms - pause_start_ms_);
                pause_start_ms_ = 0;
            }
            enqueueCommand(0x0D, 0, 0, true);
        }
        return r2link::Result::Accepted;
    }

    if (req.operation == uint8_t(r2link::AudioOperation::SetVolume)) {
        volume_ = (req.volume > 30) ? 30 : req.volume;
        enqueueCommand(0x06, 0, volume_);
        return r2link::Result::Accepted;
    }

    return r2link::Result::InvalidArgument;
}

r2link::AudioStatus DfPlayer::status(uint32_t now_ms) const {
    r2link::AudioStatus s{};
    s.state = static_cast<uint8_t>(state_);
    s.folder = folder_;
    s.track = track_;
    s.volume = volume_;
    s.owner_request_seq = owner_seq_;
    s.duration_ms = duration_ms_;

    uint8_t val = 0;
    if (duration_known_) val |= 0x01;
    if (state_ == r2link::AudioState::Playing || state_ == r2link::AudioState::Paused) {
        val |= 0x02;
    }
    s.validity = val;

    if (state_ == r2link::AudioState::Playing && playback_start_ms_ > 0) {
        s.elapsed_ms = (now_ms >= playback_start_ms_) ? (now_ms - playback_start_ms_) : 0;
    } else {
        s.elapsed_ms = elapsed_ms_;
    }
    return s;
}

void DfPlayer::peerLost(uint32_t now_ms) {
    (void)now_ms;
    if (state_ == r2link::AudioState::Starting || state_ == r2link::AudioState::Playing || state_ == r2link::AudioState::Paused) {
        clearQueuedPlays();
        if (owner_seq_ != 0) {
            pushEvent(r2link::EventKind::Cancelled, owner_seq_, r2link::Detail::None);
        }
        state_ = r2link::AudioState::Idle;
        enqueueCommand(0x16, 0, 0, true);
    }
}

void DfPlayer::handlePacket(const uint8_t* p, uint32_t now_ms) {
    const uint8_t cmd = p[3];
    const uint8_t param_l = p[6];

    if (init_step_ != InitStep::Ready) {
        init_step_ = InitStep::Ready;
        state_ = r2link::AudioState::Idle;
        enqueueCommand(0x06, 0, volume_);
        enqueueCommand(0x07, 0, 0);
    }

    if (cmd == 0x42) {
        // Query status response: param_l == 1 playing, 2 paused, 0 stopped
        if (param_l == 1) {
            if (state_ == r2link::AudioState::Starting) {
                state_ = r2link::AudioState::Playing;
                playback_start_ms_ = now_ms;
                elapsed_ms_ = 0;
                pushEvent(r2link::EventKind::PlaybackStarted, owner_seq_, r2link::Detail::None);
            }
        } else if (param_l == 2) {
            if (state_ == r2link::AudioState::Playing) {
                state_ = r2link::AudioState::Paused;
                pause_start_ms_ = now_ms;
                if (playback_start_ms_ > 0 && now_ms >= playback_start_ms_) {
                    elapsed_ms_ = now_ms - playback_start_ms_;
                }
            }
        } else if (param_l == 0) {
            // While Starting the player may still be opening the file: "stopped" is
            // not a completion then. A play that never starts is ended by the timeout.
            if (state_ == r2link::AudioState::Playing) {
                state_ = r2link::AudioState::Idle;
                pushEvent(r2link::EventKind::Completed, owner_seq_, r2link::Detail::None);
            }
        }
    } else if (cmd == 0x3D) {
        // Track finished
        if (state_ == r2link::AudioState::Playing || state_ == r2link::AudioState::Starting) {
            state_ = r2link::AudioState::Idle;
            pushEvent(r2link::EventKind::Completed, owner_seq_, r2link::Detail::None);
        }
    } else if (cmd == 0x40) {
        // Device error
        if (state_ == r2link::AudioState::Starting || state_ == r2link::AudioState::Playing) {
            state_ = r2link::AudioState::Error;
            pushEvent(r2link::EventKind::HardwareError, owner_seq_, r2link::Detail::DeviceError);
        }
    }
}

void DfPlayer::processIncoming(uint32_t now_ms) {
    int b = 0;
    while ((b = port_.read()) >= 0) {
        const uint8_t byte = static_cast<uint8_t>(b);
        if (rx_len_ == 0) {
            if (byte == 0x7E) {
                rx_buf_[0] = byte;
                rx_len_ = 1;
            }
        } else if (rx_len_ < 10) {
            rx_buf_[rx_len_++] = byte;
            if (rx_len_ == 10) {
                if (rx_buf_[9] == 0xEF) {
                    const uint16_t expected_sum = calculateChecksum(rx_buf_);
                    const uint16_t actual_sum = (static_cast<uint16_t>(rx_buf_[7]) << 8) | rx_buf_[8];
                    if (expected_sum == actual_sum) {
                        handlePacket(rx_buf_, now_ms);
                    }
                }
                rx_len_ = 0;
            }
        } else {
            rx_len_ = 0;
        }
    }
}

void DfPlayer::tick(uint32_t now_ms) {
    // 1. Non-blocking initialization
    if (init_step_ == InitStep::Unstarted) {
        init_step_ = InitStep::ResetSent;
        init_start_ms_ = now_ms;
        enqueueCommand(0x0C, 0, 0); // Reset
    } else if (init_step_ != InitStep::Ready) {
        if (now_ms - init_start_ms_ >= 3000) {
            state_ = r2link::AudioState::Offline;
        }
        if (now_ms - init_start_ms_ >= kResetRetryMs && q_count_ == 0) {
            init_start_ms_ = now_ms;
            enqueueCommand(0x0C, 0, 0); // retry reset until the player answers
        }
    }

    // 2. Process incoming packets
    processIncoming(now_ms);

    // 3. A play that never reports playback is abandoned rather than left Starting.
    if (state_ == r2link::AudioState::Starting && now_ms - start_request_ms_ >= kStartTimeoutMs) {
        pushEvent(r2link::EventKind::Timeout, owner_seq_, r2link::Detail::AudioUnavailable);
        state_ = r2link::AudioState::Idle;
        clearQueuedPlays();
        enqueueCommand(0x16, 0, 0, true); // Stop, so a late start cannot play unowned
    }

    // 3b. Completion guard timeout check
    if (state_ == r2link::AudioState::Playing && playback_start_ms_ > 0) {
        elapsed_ms_ = now_ms - playback_start_ms_;
        if (elapsed_ms_ >= guard_ms_) {
            pushEvent(r2link::EventKind::Timeout, owner_seq_, r2link::Detail::AudioGuardExpired);
            state_ = r2link::AudioState::Idle;
            enqueueCommand(0x16, 0, 0, true); // Stop
        }
    }

    // 4. Status poll every 500ms
    if ((state_ == r2link::AudioState::Idle || state_ == r2link::AudioState::Starting || state_ == r2link::AudioState::Playing) &&
        q_count_ == 0 && (now_ms - last_poll_ms_ >= 500)) {
        enqueueCommand(0x42, 0, 0);
        last_poll_ms_ = now_ms;
    }

    // 5. Transmit queued commands with >= 100ms spacing
    if (q_count_ > 0) {
        if (last_tx_ms_ == 0 || (now_ms - last_tx_ms_ >= 100)) {
            OutCommand c = queue_[q_head_];
            q_head_ = (q_head_ + 1) % kMaxQueue;
            --q_count_;

            uint8_t packet[10];
            serializePacket(c.cmd, c.feedback, c.param_h, c.param_l, packet);
            port_.write(packet, 10);
            last_tx_ms_ = now_ms;
        }
    }
}

} // namespace body
