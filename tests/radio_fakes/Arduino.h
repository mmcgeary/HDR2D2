#pragma once
#include <stdint.h>
#include <stddef.h>
#include <deque>
#include <cassert>
#define SERIAL_8N1 0
#define SERIAL_8N1_HALF_DUPLEX 1
struct HardwareSerial {
    int rx_pin=-1, tx_pin=-1, format=-1, baud=0, space=100;
    bool begun=false, open_drain=false, open_drain_after_begin=false;
    size_t written=0, calls=0, reads=0;
    std::deque<uint8_t> bytes;
    void begin(int b, int f=SERIAL_8N1) { baud=b; format=f; begun=true; open_drain=false; }
    int read() { ++reads; if(bytes.empty()) return -1; int b=bytes.front(); bytes.pop_front(); return b; }
    int available() { return static_cast<int>(bytes.size()); }
    int availableForWrite() { return space; }
    size_t write(const uint8_t*, size_t n) {
        assert(n<=size_t(space)); ++calls; written+=n; space-=n; return n;
    }
};
struct HardwareSerialIMXRT : HardwareSerial {
    void setRX(int p) { rx_pin=p; }
    void setTX(int p, bool od=false) { tx_pin=p; open_drain=od; open_drain_after_begin=begun && od; }
};
struct FakeEeprom {
    uint8_t data[4096];
    FakeEeprom() { for (size_t i = 0; i < 4096; ++i) data[i] = 0xFF; }
    size_t length() const { return 4096; }
    uint8_t read(int addr) const { return (addr >= 0 && addr < 4096) ? data[addr] : 0xFF; }
    void write(int addr, uint8_t val) { if (addr >= 0 && addr < 4096) data[addr] = val; }
    void update(int addr, uint8_t val) { write(addr, val); }
};
extern FakeEeprom EEPROM;
extern HardwareSerialIMXRT Serial1, Serial2, Serial3, Serial4, Serial5, Serial6;
extern HardwareSerial Serial;
extern uint32_t fake_ms, fake_us;
inline uint32_t millis() { return fake_ms; }
inline uint32_t micros() { return fake_us; }
