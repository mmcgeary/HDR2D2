#include <Arduino.h>
#include <stdio.h>
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
#include <Servo.h>

// No saved acceptance is injected yet: boot profile keeps motion disabled.
// Pin 2 attached to continuous dome servo; body/dome link remains NullPort.
static body::CommissioningProfile g_profile;
static body::DriveController g_drive;
static body::DomeController g_dome(g_profile);
static Servo g_dome_servo;
static body::LinkBootstrap g_link(0);
static body::IbusInput g_input;
static body::IbusTelemetry g_telemetry;
static body::ReceiverPort g_receiver(Serial5);
static body::TelemetryPort g_sensor(Serial6);
static body::VescPort g_left_vesc(Serial1, 0);
static body::VescPort g_right_vesc(Serial2, 1);
static body::VescLink g_left(g_left_vesc, 0);
static body::VescLink g_right(g_right_vesc, 1);
// Shared immediate body-status view published over the link.
static r2link::BodyStatus g_body_status{};

static void publishDriveStatus(uint32_t now_ms) {
    const body::Readiness ready = body::readiness(g_profile);
    const uint8_t state = uint8_t(g_drive.driveState());
    const uint8_t intent = uint8_t(g_drive.intent());
    const uint8_t locks = g_drive.motionLocks();
    const uint8_t profile_ready = (ready.drive ? 1 : 0) |
        (ready.manual_dome ? 2 : 0) | (ready.auto_dome ? 4 : 0);
    const body::RcSnapshot rc = g_input.snapshot(now_ms);
    const body::VescSample left = g_left.sample(now_ms), right = g_right.sample(now_ms);
    const uint32_t faults = (!body::validateProfile(g_profile) ? 2u : !ready.drive ? 1u : 0u) |
        (!rc.valid ? (1u << 3) : 0u) |
        (left.stale ? (1u << 4) : 0u) | (right.stale ? (1u << 5) : 0u) |
        (left.fault ? (1u << 6) : 0u) | (right.fault ? (1u << 7) : 0u) |
        (left.unsupported || right.unsupported ? (1u << 8) : 0u) |
        (g_drive.deadlineMisses() ? (1u << 9) : 0u);
    const uint8_t dome_state = uint8_t(g_dome.state());
    const uint8_t dome_owner = uint8_t(g_dome.owner());
    const uint32_t dome_gen = g_dome.authorityGeneration();
    const uint8_t angle_valid = g_dome.position().valid() ? 1 : 0;
    const int16_t angle_ddeg = g_dome.position().angleDdeg();
    const bool changed = g_body_status.drive_state != state ||
        g_body_status.drive_intent != intent || g_body_status.lock_reasons != locks ||
        g_body_status.profile_ready != profile_ready ||
        g_body_status.control_epoch != g_drive.controlEpoch() || g_body_status.faults != faults ||
        g_body_status.dome_state != dome_state ||
        g_body_status.dome_owner != dome_owner ||
        g_body_status.dome_authority_generation != dome_gen ||
        g_body_status.angle_valid != angle_valid ||
        g_body_status.estimated_angle_ddeg != angle_ddeg;
    g_body_status.drive_state = state;
    g_body_status.drive_intent = intent;
    g_body_status.lock_reasons = locks;
    g_body_status.profile_ready = profile_ready;
    g_body_status.control_epoch = g_drive.controlEpoch();
    g_body_status.faults = faults;
    g_body_status.dome_state = dome_state;
    g_body_status.dome_owner = dome_owner;
    g_body_status.dome_authority_generation = dome_gen;
    g_body_status.angle_valid = angle_valid;
    g_body_status.estimated_angle_ddeg = angle_ddeg;
    static bool published = false;
    static uint32_t published_ms = 0;
    if (changed || !published || uint32_t(now_ms - published_ms) >= 200) {
        r2link::Frame frame{};
        r2link::ErrorCounters errors;
        if (r2link::encode(g_body_status, frame, errors) == r2link::Status::Ok &&
            g_link.endpoint().publishLatest(frame, now_ms)) {
            published = true; published_ms = now_ms;
        }
    }
}

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
    g_left.setProfile(body::VescProfile::fromSaved(g_profile, 0));
    g_right.setProfile(body::VescProfile::fromSaved(g_profile, 1));
    g_dome_servo.attach(body_pins::kDomeServo);
    g_dome_servo.writeMicroseconds(g_profile.servo_neutral ? g_profile.servo_neutral : 1500);
}

void loop() {
    const uint32_t now_ms = millis();
    g_receiver.pump(g_input, now_ms);
    g_left.tick(now_ms);
    g_right.tick(now_ms);
    g_drive.update(g_input.snapshot(now_ms), g_left.sample(now_ms),
                   g_right.sample(now_ms), g_profile, now_ms);
    // Nonblocking injection only. Safety brakes apply on the same loop;
    // drive renewals follow the owner's 20ms cadence.
    static bool sent = false;
    static uint32_t command_revision = 0;
    const body::WheelCommands& commands = g_drive.commands();
    if (!sent || command_revision != g_drive.commandRevision()) {
        body::applyWheelCommands(commands, g_left, g_right);
        command_revision = g_drive.commandRevision(); sent = true;
    }
    g_dome.updateDrive(g_drive.intent(), now_ms);
    g_dome.updateRc(g_input.snapshot(now_ms), now_ms);
    g_dome.setControlEpoch(g_drive.controlEpoch());
    g_dome.tick(now_ms);
    const body::ServoCommand dome_cmd = g_dome.output();
    if (dome_cmd.pulses) {
        g_dome_servo.writeMicroseconds(dome_cmd.pulse_us);
    }
    g_telemetry.setMeasurements(body::vescMeasurements(g_left.sample(now_ms), g_right.sample(now_ms)));
    g_sensor.pump(g_telemetry, micros());
    g_telemetry.tick(micros(), g_sensor);
    g_link.tick(now_ms);
    r2link::Frame rx_frame{};
    uint32_t rx_ms = 0;
    while (g_link.endpoint().takeReceived(rx_frame, rx_ms)) {
        if (rx_frame.type == r2link::MessageType::DomeRequest) {
            r2link::DomeRequest req{};
            r2link::ErrorCounters err{};
            if (r2link::decode(rx_frame, req, err) == r2link::Status::Ok) {
                r2link::Result res = g_dome.request(req, rx_frame.sequence, now_ms);
                g_link.endpoint().reply(rx_frame, res, 0);
            }
        } else if (rx_frame.type == r2link::MessageType::HallState) {
            r2link::HallState hall{};
            r2link::ErrorCounters err{};
            if (r2link::decode(rx_frame, hall, err) == r2link::Status::Ok) {
                g_dome.updateHall(hall, rx_ms);
            }
        }
    }
    if (!g_link.endpoint().connected(now_ms)) {
        g_dome.peerLost(now_ms);
    }
    r2link::Event ev{};
    while (g_dome.takeEvent(ev)) {
        r2link::Frame ev_frame{};
        r2link::ErrorCounters err{};
        if (r2link::encode(ev, ev_frame, err) == r2link::Status::Ok) {
            uint16_t seq = 0;
            g_link.endpoint().request(ev_frame, now_ms, seq);
        }
    }
    publishDriveStatus(now_ms);
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
