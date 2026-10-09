#include <Arduino.h>
#include <stdio.h>
#include "Codec.h"
#include "Endpoint.h"
#include "body/ConfigStore.h"
#include "body/LinkBootstrap.h"
#include "body/Pins.h"
#include "body/HardwareAdapters.h"
#include "body/VescLink.h"

// UART diagnostics only: profiles stay zero/unaccepted. No actuator commands
// are requested, no servo pin is configured; body/dome link remains NullPort.
static body::LinkBootstrap g_link(0);
static body::IbusInput g_input;
static body::IbusTelemetry g_telemetry;
static body::ReceiverPort g_receiver(Serial5);
static body::TelemetryPort g_sensor(Serial6);
static body::VescPort g_left_vesc(Serial1, 0);
static body::VescPort g_right_vesc(Serial2, 1);
static body::VescLink g_left(g_left_vesc, 0);
static body::VescLink g_right(g_right_vesc, 1);

// At most one explicitly requested packet; print 16 raw bytes per loop only
// when USB has room. No wait-for-host, flush, or continuous raw stream.
static bool usbCaptureTick() {
    static body::VescCapture capture{};
    static bool waiting = false, reporting = false;
    static size_t offset = 0;
    if (!waiting && !reporting) {
        const int command = Serial.read();
        if (command == 'l' || command == 'L') {
            g_left.requestCapture(command == 'l' ? 4 : 0); waiting = true;
        } else if (command == 'r' || command == 'R') {
            g_right.requestCapture(command == 'r' ? 4 : 0); waiting = true;
        }
    }
    if (waiting && (g_left.takeCapture(capture) || g_right.takeCapture(capture))) {
        waiting = false; reporting = true; offset = 0;
    }
    if (!reporting) return waiting;
    char line[128];
    int n = snprintf(line, sizeof line, "VESC_RAW wheel=%u offset=%u length=%u ",
        unsigned(capture.wheel), unsigned(offset), unsigned(capture.length));
    const size_t end = offset + 16 < capture.length ? offset + 16 : capture.length;
    for (size_t i = offset; i < end; ++i)
        n += snprintf(line + n, sizeof line - size_t(n), "%02X", unsigned(capture.bytes[i]));
    line[n++] = '\n';
    if (Serial.availableForWrite() >= n) {
        Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(n));
        offset = end;
        if (offset == capture.length) reporting = false;
    }
    return true;
}

void setup() {
    Serial.begin(115200);
    g_receiver.begin();
    g_sensor.begin();
    g_left_vesc.begin();
    g_right_vesc.begin();
}

void loop() {
    const uint32_t now_ms = millis();
    g_receiver.pump(g_input, now_ms);
    g_left.tick(now_ms);
    g_right.tick(now_ms);
    g_telemetry.setMeasurements(body::vescMeasurements(g_left.sample(now_ms), g_right.sample(now_ms)));
    g_sensor.pump(g_telemetry, micros());
    g_telemetry.tick(micros(), g_sensor);
    g_link.tick(now_ms);
    g_telemetry.tick(micros(), g_sensor);
    const bool usb_busy = usbCaptureTick();

    static uint32_t last = 0;
    if (!usb_busy && uint32_t(now_ms - last) >= 5000) {
        last = now_ms;
        const body::RcSnapshot rc = g_input.snapshot(now_ms);
        const body::IbusInputCounters& r = g_input.counters();
        const body::IbusTelemetryCounters& t = g_telemetry.counters();
        char line[272];
        const int n = snprintf(line, sizeof line,
            "[BODY] UNCOMMISSIONED rc=%u flags=%u age=%lu "
            "CH1/2/4/6/8/9=%u/%u/%u/%u/%u/%u "
            "rx=%lu crc=%lu range=%lu partial=%lu sensor=%lu late=%lu echo=%lu\n",
            unsigned(rc.valid), unsigned(rc.flags), (unsigned long)(now_ms - rc.sample_ms),
            unsigned(rc.channels[0]), unsigned(rc.channels[1]), unsigned(rc.channels[3]),
            unsigned(rc.channels[5]), unsigned(rc.channels[7]), unsigned(rc.channels[8]),
            (unsigned long)r.valid_frames, (unsigned long)r.checksum_errors,
            (unsigned long)r.channel_errors, (unsigned long)r.partial_timeouts,
            (unsigned long)t.responses, (unsigned long)t.deadline_misses,
            (unsigned long)t.echo_bytes);
        // USB output is best effort and never waits for a host or buffer space.
        if (n > 0 && size_t(n) < sizeof line && Serial.availableForWrite() >= n)
            Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(n));
    }
}
