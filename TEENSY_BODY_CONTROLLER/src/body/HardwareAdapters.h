#pragma once
#include <Arduino.h>
#include "BytePort.h"
#include "IbusInput.h"
#include "IbusTelemetry.h"
#include "Pins.h"

namespace body {

class HardwareSerialPort : public r2link::BytePort {
public:
    explicit HardwareSerialPort(HardwareSerial& serial) : serial_(serial) {}
    int read() override { return serial_.read(); }
    size_t writable() const override {
        const int n = serial_.availableForWrite();
        return n > 0 ? size_t(n) : 0;
    }
    size_t write(const uint8_t* bytes, size_t length) override {
        const size_t capacity = writable();
        if (length > capacity) length = capacity;
        return length ? serial_.write(bytes, length) : 0;
    }
protected:
    HardwareSerial& serial_;
};

class ReceiverPort : public HardwareSerialPort {
public:
    explicit ReceiverPort(HardwareSerialIMXRT& serial) : HardwareSerialPort(serial), uart_(serial) {}
    void begin() {
        uart_.setRX(body_pins::kReceiverRx);
        uart_.begin(115200);
    }
    // TX20 is not wired; the receiver adapter never transmits.
    size_t writable() const override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
    void pump(IbusInput& input, uint32_t now_ms) {
        for (uint8_t i = 0; i < 64; ++i) {
            const int byte = read();
            if (byte < 0) break;
            input.feed(uint8_t(byte), now_ms);
        }
        input.tick(now_ms);
    }
private:
    HardwareSerialIMXRT& uart_;
};

class VescPort : public HardwareSerialPort {
public:
    VescPort(HardwareSerialIMXRT& serial, uint8_t wheel) :
        HardwareSerialPort(serial), uart_(serial), wheel_(wheel) {}
    void begin() {
        uart_.setRX(wheel_ == 0 ? body_pins::kLeftVescRx : body_pins::kRightVescRx);
        uart_.setTX(wheel_ == 0 ? body_pins::kLeftVescTx : body_pins::kRightVescTx);
        uart_.begin(115200, SERIAL_8N1);
    }
private:
    HardwareSerialIMXRT& uart_;
    uint8_t wheel_;
};

class TelemetryPort : public HardwareSerialPort {
public:
    explicit TelemetryPort(HardwareSerialIMXRT& serial) : HardwareSerialPort(serial), uart_(serial) {}
    void begin() {
        uart_.begin(115200, SERIAL_8N1_HALF_DUPLEX);
        uart_.setTX(body_pins::kTelemetrySingleWire, true);
    }
    // Native half-duplex is on TX24 alone, never attach RX25.
    void pump(IbusTelemetry& telemetry, uint32_t now_us) {
        for (uint8_t i = 0; i < 16; ++i) {
            const int byte = read();
            if (byte < 0) break;
            telemetry.feed(uint8_t(byte), now_us);
        }
    }
private:
    HardwareSerialIMXRT& uart_;
};

} // namespace body
