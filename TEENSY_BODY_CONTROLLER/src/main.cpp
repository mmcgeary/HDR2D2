#include <Arduino.h>
#include <stdio.h>
#include "Codec.h"
#include "Endpoint.h"
#include "body/ConfigStore.h"
#include "body/LinkBootstrap.h"
#include "body/Pins.h"
#include "body/HardwareAdapters.h"

// Only radio UARTs are active. No actuator is instantiated, no servo pin is
// configured, and the Task 2 link remains disabled on its NullPort.
static body::LinkBootstrap g_link(0);
static body::IbusInput g_input;
static body::IbusTelemetry g_telemetry; // measurements stay invalid until Task 4
static body::ReceiverPort g_receiver(Serial5);
static body::TelemetryPort g_sensor(Serial6);

void setup() {
    Serial.begin(115200);
    g_receiver.begin();
    g_sensor.begin();
}

void loop() {
    g_sensor.pump(g_telemetry, micros());
    g_telemetry.tick(micros(), g_sensor);
    const uint32_t now_ms = millis();
    g_receiver.pump(g_input, now_ms);
    g_link.tick(now_ms);
    g_telemetry.tick(micros(), g_sensor);

    static uint32_t last = 0;
    if (uint32_t(now_ms - last) >= 5000) {
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
