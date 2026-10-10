#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "Codec.h"
#include "Endpoint.h"
#include "body/ConfigStore.h"
#include "body/LinkBootstrap.h"
#include "body/Pins.h"
#include "body/HardwareAdapters.h"
#include "body/VescLink.h"
#include "body/DriveController.h"
#include "body/DomePosition.h"
#include "body/DomeController.h"
#include "body/DfPlayer.h"
#include "body/BodyController.h"
#include "TrackCatalog.h"
#include <Servo.h>

#if __has_include(<Watchdog_t4.h>)
#include <Watchdog_t4.h>
static WDT_T4<WDT1> g_wdt;
#endif

static body::EepromStorage g_eeprom;
static body::ConfigStore g_config_store(g_eeprom);
static body::ReceiverPort g_receiver(Serial5);
static body::TelemetryPort g_sensor(Serial6);
static body::VescPort g_left_vesc(Serial1, 0);
static body::VescPort g_right_vesc(Serial2, 1);
static body::AudioPort g_audio_port(Serial3);
static body::DomeLinkPort g_dome_link(Serial4);

static body::BodyController g_controller(g_config_store, g_dome_link, g_left_vesc, g_right_vesc, g_audio_port);
static body::CommissioningProfile& g_profile = g_controller.profile();
static body::DriveController& g_drive = g_controller.drive();
static body::DomeController& g_dome = g_controller.dome();
static body::DfPlayer& g_dfplayer = g_controller.audio();
static body::VescLink& g_left = g_controller.leftVesc();
static body::VescLink& g_right = g_controller.rightVesc();
static r2link::Endpoint& g_link = g_controller.linkEndpoint();
static Servo g_dome_servo;
static body::IbusInput g_input;
static body::IbusTelemetry g_telemetry;
static r2link::BodyStatus g_body_status{};

static bool g_capture_waiting = false;
static bool g_capture_reporting = false;
static size_t g_capture_offset = 0;
static body::VescCapture g_capture{};

static bool usbCaptureTick() {
    if (g_capture_waiting && (g_left.takeCapture(g_capture) || g_right.takeCapture(g_capture))) {
        g_capture_waiting = false;
        g_capture_reporting = true;
        g_capture_offset = 0;
    }
    if (!g_capture_reporting) return g_capture_waiting;

    char line[128];
    int n = snprintf(line, sizeof line, "VESC_RAW wheel=%u offset=%u length=%u ",
        unsigned(g_capture.wheel), unsigned(g_capture_offset), unsigned(g_capture.length));
    const size_t end = g_capture_offset + 16 < g_capture.length ? g_capture_offset + 16 : g_capture.length;
    for (size_t i = g_capture_offset; i < end; ++i)
        n += snprintf(line + n, sizeof line - size_t(n), "%02X", unsigned(g_capture.bytes[i]));
    line[n++] = '\n';
    if (Serial.availableForWrite() >= n) {
        Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(n));
        g_capture_offset = end;
        if (g_capture_offset == g_capture.length) g_capture_reporting = false;
    }
    return true;
}

static void usbCliTick(uint32_t now_ms) {
    static char line_buf[128];
    static size_t line_len = 0;

    int b = 0;
    while ((b = Serial.read()) >= 0) {
        const char c = static_cast<char>(b);

        if (c == 'l' || c == 'L') {
            if (line_len == 0) {
                g_left.requestCapture(c == 'l' ? 4 : 0);
                g_capture_waiting = true;
                continue;
            }
        } else if (c == 'r' || c == 'R') {
            if (line_len == 0) {
                g_right.requestCapture(c == 'r' ? 4 : 0);
                g_capture_waiting = true;
                continue;
            }
        }

        if (c == '\r' || c == '\n') {
            if (line_len > 0) {
                line_buf[line_len] = '\0';
                char response[256];
                if (g_controller.processCli(line_buf, response, sizeof(response), now_ms)) {
                    const size_t resp_len = strlen(response);
                    if (resp_len > 0 && Serial.availableForWrite() >= (int)resp_len) {
                        Serial.write(reinterpret_cast<const uint8_t*>(response), resp_len);
                    }
                }
                line_len = 0;
            }
        } else if (line_len + 1 < sizeof(line_buf)) {
            line_buf[line_len++] = c;
        }
    }
}

void setup() {
    Serial.begin(115200);
    g_receiver.begin();
    g_sensor.begin();
    g_left_vesc.begin();
    g_right_vesc.begin();
    g_audio_port.begin();
    g_dome_link.begin();
    g_controller.init(millis());
    g_dome_servo.attach(body_pins::kDomeServo);
    g_dome_servo.writeMicroseconds(g_profile.servo_neutral ? g_profile.servo_neutral : 1500);

#if __has_include(<Watchdog_t4.h>)
    WDT_timings_t wdt_config{};
    wdt_config.timeout = 1.0f;
    wdt_config.pin = 0;
    wdt_config.callback = nullptr;
    g_wdt.begin(wdt_config);
#endif
}

void loop() {
    const uint32_t now_ms = millis();
    const uint32_t now_us = micros();

    // 1. Receiver sensor processing first
    g_telemetry.tick(now_us, g_sensor);
    g_sensor.pump(g_telemetry, now_us);

    // 2. RC / VESC RX bounded pump
    g_receiver.pump(g_input, now_ms);
    g_controller.updateRc(g_input.snapshot(now_ms), now_ms);

    // 3. Controller tick (VESC links, link endpoint, drive 20ms, dome, audio, events, status)
    g_controller.tick(now_ms, now_us);
    g_body_status = g_controller.status();

    // 4. Actuator servo pulse output
    const body::ServoCommand dome_cmd = g_controller.domeOutput();
    if (dome_cmd.pulses) {
        g_dome_servo.writeMicroseconds(dome_cmd.pulse_us);
    }

    // 5. Telemetry measurements
    g_telemetry.setMeasurements(body::vescMeasurements(g_left.sample(now_ms), g_right.sample(now_ms)));
    g_telemetry.tick(now_us, g_sensor);

    // 6. USB CLI and diagnostic capture
    usbCliTick(now_ms);
    const bool usb_busy = usbCaptureTick();

    // 7. Watchdog feed
#if __has_include(<Watchdog_t4.h>)
    if (g_controller.deadlineHealthy()) {
        g_wdt.feed();
    }
#endif

    // 8. Low-priority periodic USB status banner
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
        if (n > 0 && size_t(n) < sizeof line && Serial.availableForWrite() >= n)
            Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(n));
    }
}
