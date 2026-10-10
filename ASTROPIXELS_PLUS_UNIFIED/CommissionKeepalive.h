#pragma once

#include <cstdint>

// Browser keepalive for the wheel tests. The open /drive page calls a heartbeat
// element every kPeriodMs; the dome forwards the commissioning Keepalive for a
// wheel test only while a heartbeat arrived within kWindowMs, so closing the tab
// (or losing Wi-Fi) lets the body's 300 ms keepalive timeout brake the wheel.
class BrowserHeartbeat {
public:
    static const uint32_t kPeriodMs = 150, kWindowMs = 500;
    void beat(uint32_t now_ms) { seen_ = true; last_ms_ = now_ms; }
    bool fresh(uint32_t now_ms) const { return seen_ && uint32_t(now_ms - last_ms_) <= kWindowMs; }

private:
    bool seen_{false};
    uint32_t last_ms_{0};
};

// Should the dome forward a Keepalive for a running test? Wheel tests (6-8) need a
// fresh browser heartbeat; dome tests (1-5) keep the dome-driven keepalive.
inline bool commissionKeepaliveAllowed(uint8_t test, bool heartbeat_fresh) {
    return test < 6 || test > 8 || heartbeat_fresh;
}
