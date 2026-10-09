#pragma once
#include <stddef.h>
#include <stdint.h>

namespace r2link {

// Non-blocking byte transport used by Endpoint. Implementations never wait:
// read() returns -1 when no byte is available and write() accepts at most
// writable() bytes (a partial write is normal).
class BytePort {
public:
    virtual int read() = 0;
    virtual size_t writable() const = 0;
    virtual size_t write(const uint8_t* data, size_t length) = 0;
protected:
    ~BytePort() {}
};

}  // namespace r2link
