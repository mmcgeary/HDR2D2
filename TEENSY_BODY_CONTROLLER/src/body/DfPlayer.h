#pragma once

#include <cstdint>
#include <cstddef>
#include "BytePort.h"
#include "Messages.h"
#include "Endpoint.h"
#include "TrackCatalog.h"

namespace body {

class DfPlayer {
public:
    DfPlayer(r2link::BytePort& port, const TrackInfo* catalog = nullptr, size_t count = 0);

    void initSimulated(uint32_t now_ms);

    void tick(uint32_t now_ms);
    r2link::Result request(const r2link::AudioRequest& req, uint16_t owner_seq, uint32_t now_ms);
    r2link::AudioStatus status(uint32_t now_ms) const;
    bool takeEvent(r2link::Event& ev);
    void peerLost(uint32_t now_ms);

    static void serializePacket(uint8_t cmd, uint8_t feedback, uint8_t param_h, uint8_t param_l, uint8_t out[10]);
    static uint16_t calculateChecksum(const uint8_t* p);

private:
    struct OutCommand {
        uint8_t cmd{0};
        uint8_t feedback{0};
        uint8_t param_h{0};
        uint8_t param_l{0};
        OutCommand() = default;
        OutCommand(uint8_t c, uint8_t f, uint8_t ph, uint8_t pl)
            : cmd(c), feedback(f), param_h(ph), param_l(pl) {}
    };

    r2link::BytePort& port_;
    const TrackInfo* catalog_;
    size_t catalog_count_;

    r2link::AudioState state_{r2link::AudioState::Offline};
    uint8_t volume_{10}; // default 10/30
    uint8_t folder_{1};
    uint16_t track_{0};
    uint8_t priority_{0}; // 0 = Ambient, 1 = Foreground
    uint16_t owner_seq_{0};

    uint32_t playback_start_ms_{0};
    uint32_t elapsed_ms_{0};
    uint32_t pause_start_ms_{0};
    uint32_t guard_ms_{600000};
    uint32_t duration_ms_{0};
    bool duration_known_{false};

    static const size_t kMaxQueue = 8;
    OutCommand queue_[kMaxQueue]{};
    size_t q_head_{0};
    size_t q_tail_{0};
    size_t q_count_{0};
    uint32_t last_tx_ms_{0};

    enum class InitStep : uint8_t { Unstarted, ResetSent, VolumeSent, EqSent, Ready };
    InitStep init_step_{InitStep::Unstarted};
    uint32_t init_start_ms_{0};
    uint32_t last_poll_ms_{0};

    static const size_t kMaxEvents = 8;
    r2link::Event events_[kMaxEvents]{};
    size_t ev_head_{0};
    size_t ev_tail_{0};
    size_t ev_count_{0};

    uint8_t rx_buf_[10]{};
    size_t rx_len_{0};

    void pushEvent(r2link::EventKind kind, uint16_t seq, r2link::Detail detail);
    bool enqueueCommand(uint8_t cmd, uint8_t param_h, uint8_t param_l, bool high_priority = false);
    void clearQueuedPlays();
    void sendPacket(const OutCommand& c, uint32_t now_ms);
    void processIncoming(uint32_t now_ms);
    void handlePacket(const uint8_t* p, uint32_t now_ms);
    const TrackInfo* findTrack(uint16_t track) const;
};

} // namespace body
