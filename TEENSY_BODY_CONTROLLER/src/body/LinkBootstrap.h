#pragma once
// Hardware-free link bootstrap. It owns a port that never carries bytes and an
// Endpoint, which keeps the shared link library part of the Teensy image
// without configuring any UART or pin. Replace NullPort with the dome-link
// UART adapter at integration time; local_session 0 keeps the link disabled.
#include <stddef.h>
#include <stdint.h>
#include "Endpoint.h"

namespace body {

class NullPort : public r2link::BytePort {
public:
    int read() override { return -1; }
    size_t writable() const override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
};

class LinkBootstrap {
public:
    explicit LinkBootstrap(uint32_t local_session);
    void tick(uint32_t now_ms);
    bool connected(uint32_t now_ms) const { return endpoint_.connected(now_ms); }
    r2link::Endpoint& endpoint() { return endpoint_; }
    const r2link::Endpoint& endpoint() const { return endpoint_; }

private:
    NullPort port_;
    r2link::Endpoint endpoint_;
};

}  // namespace body
