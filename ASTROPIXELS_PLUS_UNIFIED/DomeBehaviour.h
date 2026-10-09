#pragma once

#include <cstdint>
#include <cstddef>
#include "Messages.h"
#include "Endpoint.h"

class IDomeRequestSink {
public:
    virtual ~IDomeRequestSink() = default;
    virtual bool submit(const r2link::DomeRequest& req, uint32_t now_ms, uint16_t& sequence) = 0;
};

class IDomeRandom {
public:
    virtual ~IDomeRandom() = default;
    virtual int32_t pick(int32_t min_inclusive, int32_t max_exclusive) = 0;
};

struct DomeBehaviourInput {
    r2link::RcStatus rc{};
    r2link::BodyStatus status{};
    bool rc_fresh{false};
    bool status_fresh{false};
    bool event_active{false};
};

class DomeBehaviour {
public:
    enum class State : uint8_t {
        WaitingIdle = 0,
        Referencing = 1,
        Pausing = 2,
        Sweeping = 3,
        Returning = 4
    };

    DomeBehaviour(IDomeRequestSink& sink, IDomeRandom& random, int16_t auto_speed_percent = 25);

    void tick(const DomeBehaviourInput& input, uint32_t now_ms);
    void onReply(uint16_t sequence, r2link::Result result);
    void onEvent(const r2link::Event& ev);
    void onPeerLost(uint32_t now_ms);

    State state() const { return state_; }
    int16_t targetAngleDdeg() const { return target_angle_ddeg_; }
    uint16_t activeSequence() const { return active_seq_; }
    bool faulted() const { return fault_; }

private:
    IDomeRequestSink& sink_;
    IDomeRandom& random_;
    int16_t auto_speed_percent_;

    State state_{State::WaitingIdle};
    uint32_t last_activity_ms_{0};
    uint32_t last_now_ms_{0};
    bool initialized_{false};

    uint16_t active_seq_{0};
    bool req_pending_{false};
    uint32_t last_lease_ms_{0};

    uint32_t pause_start_ms_{0};
    uint32_t pause_duration_ms_{0};
    bool pause_after_sweep_{false};

    int16_t target_angle_ddeg_{0};
    bool fault_{false};
    bool auto_dome_prior_on_{false};
    uint32_t last_generation_{0};

    void resetToIdle(uint32_t now_ms);
};
