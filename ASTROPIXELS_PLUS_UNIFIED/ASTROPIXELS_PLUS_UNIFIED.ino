/*
 * ═══════════════════════════════════════════════════════════════════════════════
 *                 ASTROPIXELS PLUS - UNIFIED R2-D2 DOME BRAIN
 *     (ReelTwo OS + Bi-Directional Body Link + Wi-Fi Web GUI + OTA + Choreography)
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Target Board:   ESP32 Dev Module (30-pin, AstroPixels Motherboard)
 * Architecture:   Cooperative control loop interfacing with Teensy 4.1 Body Controller
 *
 * Core Allocations:
 *   • Networking & Web Interface:
 *       1. Wi-Fi SoftAP ("AstroPixels" / "Astromech") & Station Client
 *       2. Web Server (Port 80) serving interactive GUI at http://192.168.4.1
 *       3. Telemetry page (/diagnostics) and Wireless Commissioning (/commissioning)
 *       4. ArduinoOTA Wireless Firmware Flashing (gated by Teensy maintenance lock)
 *       5. Wi-Fi MarcDuino UDP Receiver
 *
 *   • Peripherals & Interfaces:
 *       1. Hardware UART2 (115,200 baud / GPIO 16 RX, GPIO 17 TX): Bi-directional
 *          SLIP protocol link to Teensy 4.1 Body Controller via Slip Ring CH3/CH6
 *       2. Dual KY-003 Hall Sensors (GPIO 19 Front, GPIO 18 Rear via LLC):
 *          Published Hall telemetry for body-controlled homing and calibration
 *       3. PCA9685 I2C 16-Ch Controller (Addr 0x40 / GPIO 21, 22): 6 HoloProjector servos (Ch 0-5)
 *       4. ReelTwo LogicEngine: Movie-accurate FLD1/2, RLD, FPSI, RPSI & text scrolling
 *       5. ReelTwo HoloLights: WS2812 Holo LEDs with dim pulses, rainbow & Leia flicker
 *       6. Procedural Shaders: Plasma, MetaBalls, Fractal, and Bitmap animations
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#define USE_DEBUG
#define USE_WIFI
#define USE_SPIFFS

#ifdef USE_WIFI
#define REMOTE_ENABLED false // Default off; enable in prefs if pairing Droid Remote
#define WIFI_ENABLED   true  // Wi-Fi Access Point enabled by default
#define WIFI_AP_NAME   "AstroPixels"
#define WIFI_AP_PASSPHRASE "Astromech"
#define WIFI_ACCESS_POINT  true /* true = soft AP; false = join existing Wi-Fi */

#define USE_MDNS
#define USE_OTA
#define USE_WIFI_WEB
#define USE_WIFI_MARCDUINO
#endif

#define SMQ_HOSTNAME "Astro"
#define SMQ_SECRET   "Astromech"

/////////////////////////////////////////////////////////////////////////

#include "ReelTwo.h"
#include "dome/Logics.h"
#include "dome/LogicEngineController.h"
#include "dome/HoloLights.h"
#include "dome/NeoPSI.h"
#include "dome/FireStrip.h"
#include "dome/BadMotivator.h"
#include "ServoDispatchPCA9685.h"
#include "ServoSequencer.h"
#include "core/Marcduino.h"

#include <Preferences.h>
#include <Wire.h>

/////////////////////////////////////////////////////////////////////////
// 1. PIN DEFINITIONS & HARDWARE CONSTANTS
/////////////////////////////////////////////////////////////////////////

// Serial Communication to Teensy 4.1 Body Controller via Slip Ring
#define PIN_BODY_UART_RX     16    // Hardware Serial2 RX (115,200 baud): Body Controller via Slip Ring CH3
#define PIN_BODY_UART_TX     17    // Hardware Serial2 TX (115,200 baud): Body Controller via Slip Ring CH6
#define BODY_SERIAL          Serial2

// Dome Dual Hall Sensor Inputs (KY-003 via LLC)
#define PIN_DOME_HALL_FRONT  19    // Digital Input: Front Hall Sensor 0° (AUX 5 via LLC LV2)
#define PIN_DOME_HALL_REAR   18    // Digital Input: Rear Hall Sensor 180° (AUX 4 via LLC LV3)

// I2C Pins (PCA9685 Servo Controller)
#define PIN_SDA              21
#define PIN_SCL              22
#define PCA9685_I2C_ADDR     0x40

// FastLED / ReelTwo WS2812 Pin Assignments (Matches AstroPixels Factory Hardware)
#define PIN_FRONT_LOGIC      15    // Front Logic Displays (daisy-chained FLD1 -> FLD2)
#define PIN_REAR_LOGIC       33    // Rear Logic Display (RLD)
#define PIN_FRONT_PSI        32    // Front Process State Indicator (FPSI)
#define PIN_REAR_PSI         23    // Rear Process State Indicator (RPSI)
#define PIN_FRONT_HOLO       25    // Front HoloProjector LED Core (7 LEDs)
#define PIN_REAR_HOLO        26    // Rear HoloProjector LED Core (7 LEDs)
#define PIN_TOP_HOLO         27    // Top HoloProjector LED Core (7 LEDs)

// Spare AUX Pins
#define PIN_AUX1             2
#define PIN_AUX2             4
#define PIN_AUX3             5

/////////////////////////////////////////////////////////////////////////
// 2. BODY LINK CLIENT & REMOTE HARDWARE ADAPTERS
/////////////////////////////////////////////////////////////////////////

#include "BodyClient.h"
#include "RemoteAudio.h"
#include "DomeBehaviour.h"

class Esp32SerialPort : public r2link::BytePort {
public:
    explicit Esp32SerialPort(HardwareSerial& serial) : serial_(serial) {}
    int read() override { return serial_.read(); }
    size_t writable() const override {
        const int n = serial_.availableForWrite();
        return n > 0 ? size_t(n) : 0;
    }
    size_t write(const uint8_t* data, size_t length) override {
        const size_t capacity = writable();
        if (length > capacity) length = capacity;
        return length ? serial_.write(data, length) : 0;
    }
private:
    HardwareSerial& serial_;
};

class Esp32DomeRandom : public IDomeRandom {
public:
    int32_t pick(int32_t min_inclusive, int32_t max_exclusive) override {
        if (min_inclusive >= max_exclusive) return min_inclusive;
#if defined(ESP32)
        return min_inclusive + (int32_t)(esp_random() % (uint32_t)(max_exclusive - min_inclusive));
#else
        return min_inclusive + (int32_t)(rand() % (max_exclusive - min_inclusive));
#endif
    }
};

static Esp32SerialPort g_body_port(BODY_SERIAL);
static BodyClient g_body_client;
static RemoteAudio g_remote_audio(g_body_client);
static Esp32DomeRandom g_dome_random;
static DomeBehaviour g_dome_behaviour(g_body_client, g_dome_random, 25);

#define MARC_SOUND_PLAYER    MarcSound::kDFMini
#define MARC_SOUND_VOLUME    333   // 0 - 1000; ceil(333 / 1000 * 30) = 10
#define MARC_SOUND_STARTUP   255   // Track 255 (startup chime)
#define MARC_SOUND_RANDOM    true  // Ambient background chatter enabled
#define MARC_SOUND_RANDOM_MIN 5000
#define MARC_SOUND_RANDOM_MAX 25000

/////////////////////////////////////////////////////////////////////////
// 4. PREFERENCES (NVS FLASH STORAGE)
/////////////////////////////////////////////////////////////////////////

#define PREFERENCE_WIFI_ENABLED      "wifi"
#define PREFERENCE_WIFI_SSID         "ssid"
#define PREFERENCE_WIFI_PASS         "pass"
#define PREFERENCE_WIFI_AP           "ap"
#define PREFERENCE_MARCSOUND         "msound"
#define PREFERENCE_MARCSOUND_VOLUME  "mvolume"
#define PREFERENCE_MARCSOUND_STARTUP "msoundstart"
#define PREFERENCE_MARCSOUND_RANDOM  "mrandom"
#define PREFERENCE_MARCSOUND_RANDOM_MIN "mrandommin"
#define PREFERENCE_MARCSOUND_RANDOM_MAX "mrandommax"

#define CONSOLE_BUFFER_SIZE          300

/////////////////////////////////////////////////////////////////////////
// 5. REELTWO WI-FI, WEB SERVER & OTA HEADERS
/////////////////////////////////////////////////////////////////////////

#include "wifi/WifiAccess.h"

#ifdef USE_MDNS
#include <ESPmDNS.h>
#endif
#ifdef USE_WIFI_WEB
#include "wifi/WifiWebServer.h"
#endif
#ifdef USE_WIFI_MARCDUINO
#include "wifi/WifiMarcduinoReceiver.h"
#endif
#ifdef USE_OTA
#include <ArduinoOTA.h>
#endif
#ifdef USE_SPIFFS
#include "SPIFFS.h"
#define USE_FS SPIFFS
#endif
#include "FS.h"

/////////////////////////////////////////////////////////////////////////
// 6. REELTWO DISPLAY & HOLO INSTANCES
/////////////////////////////////////////////////////////////////////////

AstroPixelRLD<PIN_REAR_LOGIC> RLD(LogicEngineRLDDefault, 3);
AstroPixelFLD<PIN_FRONT_LOGIC> FLD(LogicEngineFLDDefault, 1);
AstroPixelFrontPSI<PIN_FRONT_PSI> frontPSI(LogicEngineFrontPSIDefault, 4);
AstroPixelRearPSI<PIN_REAR_PSI> rearPSI(LogicEngineRearPSIDefault, 5);

HoloLights frontHolo(PIN_FRONT_HOLO, HoloLights::kRGB, 1);
HoloLights rearHolo(PIN_REAR_HOLO, HoloLights::kRGB, 2);
HoloLights topHolo(PIN_TOP_HOLO, HoloLights::kRGB, 3);

/////////////////////////////////////////////////////////////////////////
// 7. PCA9685 16-CH SERVO MAPPING (SINGLE CONTROLLER: 6 HOLO SERVOS)
/////////////////////////////////////////////////////////////////////////

#define HOLO_HSERVO 0x1000
#define HOLO_VSERVO 0x2000
#define ALL_DOME_PANELS_MASK 0 // No physical dome door panels on this droid

const ServoSettings servoSettings[] PROGMEM = {
    { 0, 1000, 2000, HOLO_HSERVO }, /* Ch 0: Front Holo Horizontal (Pan) */
    { 1, 1000, 2000, HOLO_VSERVO }, /* Ch 1: Front Holo Vertical (Tilt) */
    { 2, 1000, 2000, HOLO_HSERVO }, /* Ch 2: Rear Holo Horizontal (Pan) */
    { 3, 1000, 2000, HOLO_VSERVO }, /* Ch 3: Rear Holo Vertical (Tilt) */
    { 4, 1000, 2000, HOLO_HSERVO }, /* Ch 4: Top Holo Horizontal (Pan) */
    { 5, 1000, 2000, HOLO_VSERVO }  /* Ch 5: Top Holo Vertical (Tilt) */
};

enum R2Macro : uint8_t { R2_NONE, R2_SCREAM, R2_CANTINA, R2_LEIA, R2_DISCO, R2_FAINT };

enum class MacroPhase : uint8_t {
    Idle = 0,
    WaitingHome,
    WaitingAudio,
    Running,
    Finishing
};

struct MacroState {
    MacroPhase phase{MacroPhase::Idle};
    R2Macro kind{R2_NONE};
    uint16_t home_sequence{0};
    uint16_t audio_sequence{0};
    bool dome_cancelled{false};
    uint32_t dome_generation{0};
    uint32_t guard_deadline_ms{0};
    uint32_t started_ms{0};
    uint32_t duration_ms{0};
    uint32_t last_step{UINT32_MAX};
};

enum class MaintenanceState : uint8_t {
    Idle = 0,
    Requested,
    Locked,
    Updating,
    Failed,
    Releasing
};

struct MaintenanceInfo {
    MaintenanceState state{MaintenanceState::Idle};
    uint16_t token{0};
    uint16_t sequence{0};
    uint32_t deadline_ms{0};
    bool pending_reboot{false};
    bool clear_prefs_on_reboot{false};
};

extern MacroState macro;
extern MaintenanceInfo g_maintenance;
extern R2Macro& active_macro;

void startR2Macro(R2Macro macro);
void stopDomeMotion();
void emergencyStop();
void releaseBodyStop();
void cancelR2Macro();
void finishR2Macro();
void disableHoloServos();
bool r2MacroActive();
void startDomeHoming(R2Macro macro = R2_NONE);
void playDFPlayerTrack(uint16_t track_num);
bool maintenanceReady();
void prepareMaintenance(bool for_reboot = false, bool clear_prefs = false);
void releaseMaintenance();
void recoverBodyLocks();
void clearPrefsAndReboot();
void startCommissionTest(uint8_t test, int32_t val = 0);
void cancelCommissionTest();
void saveCommissionProfile();
void acceptCommissionBit(uint8_t bit);
void setCommissionField(uint8_t field, uint8_t wheel, int32_t value);
void onOtaStart();
void readCommissionField(uint8_t field, uint8_t wheel);

ServoDispatchPCA9685<SizeOfArray(servoSettings)> servoDispatch(servoSettings);
ServoSequencer servoSequencer(servoDispatch);
AnimationPlayer player(servoSequencer);
MarcduinoSerial<> marcduinoSerial(player);

/////////////////////////////////////////////////////////////////////////
// 8. PROCEDURAL SHADERS & SOUND SYSTEM
/////////////////////////////////////////////////////////////////////////

#include "MarcduinoSound.h"
MarcSound::Module sSoundPlayer;

#define NUM_LEDS 28 * 4
CRGB leds[NUM_LEDS];

enum {
    SDBITMAP = 100,
    PLASMA,
    METABALLS,
    FRACTAL,
    FADEANDSCROLL
};

#include "effects/BitmapEffect.h"
#include "effects/FadeAndScrollEffect.h"
#include "effects/FractalEffect.h"
#include "effects/MeatBallsEffect.h"
#include "effects/PlasmaEffect.h"

LogicEffect CustomLogicEffectSelector(unsigned selectSequence) {
    static const LogicEffect sCustomLogicEffects[] = {
        LogicEffectBitmap,
        LogicEffectPlasma,
        LogicEffectMetaBalls,
        LogicEffectFractal,
        LogicEffectFadeAndScroll
    };
    if (selectSequence >= 100 && selectSequence - 100 < SizeOfArray(sCustomLogicEffects)) {
        return LogicEffect(sCustomLogicEffects[selectSequence - 100]);
    }
    return LogicEffectDefaultSelector(selectSequence);
}

Preferences preferences;

bool mountReadOnlyFileSystem() {
#ifdef USE_SPIFFS
    return (SPIFFS.begin(true));
#endif
    return false;
}

void unmountFileSystems() {
#ifdef USE_SPIFFS
    SPIFFS.end();
#endif
}

void reboot() {
    DEBUG_PRINTLN("[SYSTEM] Restarting...");
    prepareMaintenance(true, false);
}

void clearPrefsAndReboot() {
    DEBUG_PRINTLN("[SYSTEM] Clearing preferences and restarting...");
    prepareMaintenance(true, true);
}

void resetSequence() {
    CommandEvent::process(F(
        "LE000000|0\n" // LogicEngine devices to normal
        "HPA000|0\n"
    ));
}

int32_t strtol(const char *cmd, const char **endptr) {
    bool sign = false;
    int32_t result = 0;
    if (*cmd == '-') { cmd++; sign = true; }
    while (isdigit(*cmd)) {
        result = result * 10L + (*cmd - '0');
        cmd++;
    }
    *endptr = cmd;
    return (sign) ? -result : result;
}

bool numberparams(const char *cmd, uint8_t &argcount, int32_t *args, uint8_t maxcount) {
    for (argcount = 0; argcount < maxcount; argcount++) {
        args[argcount] = strtol(cmd, &cmd);
        if (*cmd == '\0') { argcount++; return true; }
        else if (*cmd != ',') return false;
        cmd++;
    }
    return true;
}

/////////////////////////////////////////////////////////////////////////
// 9. MARCDUINO COMPATIBILITY HEADERS
/////////////////////////////////////////////////////////////////////////

#include "MarcduinoHolo.h"
#include "MarcduinoLogics.h"
#include "MarcduinoSequence.h"
#include "MarcduinoPSI.h"

#ifdef USE_WIFI
WifiAccess wifiAccess;
bool wifiEnabled;
bool wifiActive;
bool otaInProgress;
#endif

#ifdef USE_WIFI_MARCDUINO
WifiMarcduinoReceiver wifiMarcduinoReceiver(wifiAccess);
#endif

#ifdef USE_WIFI_WEB
#include "WebPages.h"
#endif

/////////////////////////////////////////////////////////////////////////
// 10. FLYSKY i-BUS & DOME HOMING STATE MACHINE
/////////////////////////////////////////////////////////////////////////

// RC Channel Buffer (Channels 1 to 10 in microseconds: 1000us - 2000us)
uint16_t rc_channels[10] = {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500};
bool rc_connected = false;
unsigned long last_rc_packet_ms = 0;

// FlySky FS-i6X Channel Mapping
#define RC_CH_STEER            0  // Right Stick X (CH1): Tank Differential Steering
#define RC_CH_THROTTLE         1  // Right Stick Y (CH2): Forward / Reverse Throttle
#define RC_CH_DRIVE_STEER      0
#define RC_CH_DRIVE_THROTTLE   1
#define RC_CH_DOME_STEER       3  // Left Stick X (CH4): Manual Dome Rotation
#define RC_CH_SPEED_MODE       4  // Switch SwC   (CH5, 3-pos): Duty rate 35/70/100%
#define RC_CH_MOOD_SELECT      6  // Knob VrA     (CH7): Persistent Mood & Macro Selector (1-13)
#define RC_CH_MACRO_TRIGGER    7  // Switch SwB   (CH8): Macro Fire Trigger (Flip DOWN)
#define RC_CH_AUTO_DOME        8  // Switch SwD   (CH9): Auto Dome Enable

// Dome Homing & Macro Tracking
enum HomingState : uint8_t {
    HOMING_INACTIVE = 0,
    HOMING_SEEKING,
    HOMING_ALIGNED
};

HomingState homing_state = HOMING_INACTIVE;
R2Macro pending_macro_after_home = R2_NONE;
bool dome_motion_inhibited = true;
MacroState macro{};
MaintenanceInfo g_maintenance{};
R2Macro& active_macro = macro.kind;
uint32_t macro_started_ms = 0;
uint32_t macro_duration_ms = 0;
uint32_t last_macro_step = UINT32_MAX;
uint32_t next_holo_twitch_ms = 0;

// Forward Declarations
void processHallSensors(uint32_t now_ms);
void processBodyRcSnapshot(uint32_t now_ms);
void processTransmitterInputs();
void playDFPlayerTrack(uint16_t track_num);
void stopDomeMotion();
void startDomeHoming(R2Macro macro);
void processMacroCompletion(const r2link::Completion& comp);
void processMacroEvent(const r2link::Event& ev);
void processMaintenanceCompletion(const r2link::Completion& comp);
void processMaintenance(uint32_t now);

bool rcNeutral() {
    return rc_channels[RC_CH_DOME_STEER] >= 1460 &&
           rc_channels[RC_CH_DOME_STEER] <= 1540;
}

bool driveNeutral() {
    return rc_channels[RC_CH_DRIVE_STEER] >= 1460 &&
           rc_channels[RC_CH_DRIVE_STEER] <= 1540 &&
           rc_channels[RC_CH_DRIVE_THROTTLE] >= 1460 &&
           rc_channels[RC_CH_DRIVE_THROTTLE] <= 1540;
}

// Every event-owned dome request carries the body's current control epoch and
// dome authority generation, and the DomeRequest wire owner (not DomeOwner).
RequestHandle sendDomeRequest(r2link::DomeOperation operation, r2link::DomeReference reference) {
    const auto status = g_body_client.bodyStatus(millis());
    r2link::DomeRequest req{};
    req.operation = static_cast<uint8_t>(operation);
    req.reference = (operation == r2link::DomeOperation::SeekReference) ? static_cast<uint8_t>(reference) : 0;
    req.owner = r2link::kDomeRequestOwnerEvent;
    req.control_epoch = status.value.control_epoch;
    req.dome_authority_generation = status.value.dome_authority_generation;
    return g_body_client.requestDome(req, millis());
}

// Dome-local stop: ends macros, homing and holo motion and cancels the dome's own
// remote dome actions. It never latches a body STOP; the body owns RC failsafe.
void stopDomeMotion() {
    homing_state = HOMING_INACTIVE;
    pending_macro_after_home = R2_NONE;
    dome_motion_inhibited = true;
    cancelR2Macro();
    disableHoloServos();
    sendDomeRequest(r2link::DomeOperation::Cancel, r2link::DomeReference::Front);
}

// Operator emergency stop: also latches STOP on the body until it is released.
void emergencyStop() {
    stopDomeMotion();
    const auto status = g_body_client.bodyStatus(millis());
    r2link::ControlRequest creq{};
    creq.operation = 0; // STOP_ALL
    creq.reason = r2link::kReasonOperator;
    creq.token = 0;
    creq.control_epoch = status.value.control_epoch;
    g_body_client.requestControl(creq, millis());
}

void releaseBodyStop() {
    const auto status = g_body_client.bodyStatus(millis());
    r2link::ControlRequest req{};
    req.operation = 1; // RELEASE_STOP (body requires CH6 OFF, sticks centred 500ms)
    req.reason = r2link::kReasonOperator;
    req.token = 0;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestControl(req, millis());
}

void startDomeHoming(R2Macro macro) {
    if (!rc_connected || dome_motion_inhibited || otaInProgress) {
        Serial.println(F("[HOMING] Rejected: live radio and neutral rearm required."));
        return;
    }
    cancelR2Macro();
    pending_macro_after_home = macro;
    homing_state = HOMING_SEEKING;
    if (!sendDomeRequest(r2link::DomeOperation::SeekReference, r2link::DomeReference::Front).queued) {
        homing_state = HOMING_INACTIVE;
        pending_macro_after_home = R2_NONE;
        Serial.println(F("[HOMING] Rejected: body link not ready."));
        return;
    }
    Serial.println(F("[HOMING] Active 0° Dome Homing Routine Initiated via Body..."));
}

void playDFPlayerTrack(uint16_t track_num) {
    if (track_num == 0 || track_num > 255) {
        Serial.println(F("[AUDIO] Invalid track; expected 1..255 in /01."));
        return;
    }
    g_remote_audio.play(track_num, r2link::AudioPriority::Foreground, millis());
}

void processHallSensors(uint32_t now_ms) {
    static uint32_t last_hall_publish_ms = 0;
    static uint8_t last_active_mask = 0xFF;
    static uint32_t hall_sample_counter = 0;

    // Both KY-003 sensors are active LOW (magnet detected = LOW)
    uint8_t front_active = (digitalRead(PIN_DOME_HALL_FRONT) == LOW) ? 1 : 0;
    uint8_t rear_active = (digitalRead(PIN_DOME_HALL_REAR) == LOW) ? 2 : 0;
    uint8_t active_mask = front_active | rear_active;
    uint8_t valid_mask = 0x03;

    if (active_mask != last_active_mask || (now_ms - last_hall_publish_ms >= 20)) {
        last_active_mask = active_mask;
        last_hall_publish_ms = now_ms;
        ++hall_sample_counter;
        g_body_client.publishHall(valid_mask, active_mask, hall_sample_counter, now_ms);
    }
}

void processBodyRcSnapshot(uint32_t now_ms) {
    BodyRcState rc = g_body_client.rcSnapshot(now_ms);
    if (rc.valid) {
        for (uint8_t ch = 0; ch < 10; ++ch) {
            rc_channels[ch] = rc.channels[ch];
        }
        rc_connected = true;
        last_rc_packet_ms = now_ms;
        if (dome_motion_inhibited && rcNeutral()) {
            dome_motion_inhibited = false;
        }

        const bool manual_active = !rcNeutral();
        const bool drive_active = !driveNeutral();
        const bool ch9_off = (rc_channels[RC_CH_AUTO_DOME] < 1750);

        if (manual_active || drive_active || ch9_off) {
            if (macro.phase == MacroPhase::WaitingHome) {
                macro.dome_cancelled = true;
                homing_state = HOMING_INACTIVE;
                macro.home_sequence = 0;
                cancelR2Macro();
            } else if (macro.phase == MacroPhase::WaitingAudio || macro.phase == MacroPhase::Running) {
                macro.dome_cancelled = true;
            }
        }
    } else {
        if (rc_connected) {
            rc_connected = false;
            stopDomeMotion();
            cancelR2Macro();
            frontHolo.assignServos(nullptr, 0, 1);
            rearHolo.assignServos(nullptr, 2, 3);
            topHolo.assignServos(nullptr, 4, 5);
            servoDispatch.stop();
            servoDispatch.setOutputAll(false);
            Serial.println(F("[SAFETY] Body RC Lost! Failsafe Stop Activated."));
        }
    }
}

void setHoloServoOwnership(bool reelTwoOwnsServos) {
    frontHolo.assignServos(reelTwoOwnsServos ? &servoDispatch : nullptr, 0, 1);
    rearHolo.assignServos(reelTwoOwnsServos ? &servoDispatch : nullptr, 2, 3);
    topHolo.assignServos(reelTwoOwnsServos ? &servoDispatch : nullptr, 4, 5);
}

void centerHoloServos() {
    for (uint8_t ch = 0; ch < 6; ch++) {
        servoDispatch.moveToPulse(ch, 250, 1500);
    }
}

void disableHoloServos() {
    servoDispatch.stop();
    servoDispatch.setOutputAll(false);
}

inline uint16_t getMacroTrack(R2Macro m) {
    switch (m) {
        case R2_SCREAM: return 102;
        case R2_CANTINA: return 106;
        case R2_LEIA: return 109;
        case R2_DISCO: return 110;
        case R2_FAINT: return 107;
        default: return 0;
    }
}

inline uint32_t getMacroDuration(R2Macro m) {
    switch (m) {
        case R2_SCREAM: return 4500;
        case R2_CANTINA: return 30000;
        case R2_LEIA: return 14000;
        case R2_DISCO: return 20000;
        case R2_FAINT: return 5000;
        default: return 0;
    }
}

inline uint32_t getMacroGuard(R2Macro m) {
    return getMacroDuration(m) + 2000;
}

void startMacroChoreography(R2Macro macro_arg) {
    setHoloServoOwnership(false);
    centerHoloServos();
    uint8_t effect = LogicEngineDefaults::NORMAL;
    switch (macro_arg) {
        case R2_SCREAM:
            effect = LogicEngineDefaults::ALARM;
            break;
        case R2_CANTINA:
            effect = LogicEngineDefaults::MARCH;
            break;
        case R2_LEIA:
            effect = LogicEngineDefaults::LEIA;
            servoDispatch.moveToPulse(1, 250, 1300);
            break;
        case R2_DISCO:
            effect = LogicEngineDefaults::RAINBOW;
            break;
        case R2_FAINT:
            effect = LogicEngineDefaults::FAILURE;
            disableHoloServos();
            break;
        default: return;
    }
    LogicEngineRenderer::ColorVal color = (macro_arg == R2_SCREAM)
        ? LogicEngineRenderer::kRed : LogicEngineRenderer::kDefault;
    FLD.selectSequence(effect, color);
    RLD.selectSequence(effect, color);
    frontPSI.selectSequence(effect, color);
    rearPSI.selectSequence(effect, color);
    if (macro_arg == R2_LEIA) {
        frontHolo.selectSequence(1, 14);
    } else if (macro_arg == R2_DISCO) {
        CommandEvent::process(F("HPA006|20"));
    } else if (macro_arg == R2_CANTINA) {
        CommandEvent::process(F("HPA00331|30"));
    } else if (macro_arg == R2_SCREAM) {
        CommandEvent::process(F("HPA00315|5"));
    } else {
        CommandEvent::process(F("HPA002|1"));
    }
}

void finishR2Macro() {
    macro.phase = MacroPhase::Idle;
    macro.kind = R2_NONE;
    macro.home_sequence = 0;
    macro.audio_sequence = 0;
    dome_motion_inhibited = true;
    centerHoloServos();
    resetSequence();
    if (preferences.getBool(PREFERENCE_MARCSOUND_RANDOM, MARC_SOUND_RANDOM))
        sMarcSound.startRandomInSeconds(12);
    else
        sMarcSound.stopRandom();
}

bool r2MacroActive() {
    return macro.phase != MacroPhase::Idle;
}

void cancelR2Macro() {
    if (macro.phase != MacroPhase::Idle) {
        if (macro.phase == MacroPhase::WaitingHome) {
            homing_state = HOMING_INACTIVE;
            sendDomeRequest(r2link::DomeOperation::Cancel, r2link::DomeReference::Front);
        }
        if (macro.phase == MacroPhase::WaitingAudio || macro.phase == MacroPhase::Running) {
            sMarcSound.stop();
        }
        macro.phase = MacroPhase::Idle;
        macro.kind = R2_NONE;
        macro.home_sequence = 0;
        macro.audio_sequence = 0;
        if (preferences.getBool(PREFERENCE_MARCSOUND_RANDOM, MARC_SOUND_RANDOM))
            sMarcSound.startRandomInSeconds(12);
        else
            sMarcSound.stopRandom();
        resetSequence();
    }
}

void startR2Macro(R2Macro macro) {
    if (macro == R2_NONE || !rc_connected || dome_motion_inhibited || otaInProgress) {
        Serial.println(F("[MACRO] Rejected: live radio and neutral rearm required."));
        return;
    }
    cancelR2Macro();
    homing_state = HOMING_INACTIVE;
    pending_macro_after_home = R2_NONE;
    sMarcSound.suspendRandom();

    ::macro.kind = macro;
    ::macro.started_ms = millis();
    ::macro.duration_ms = getMacroDuration(macro);
    ::macro.guard_deadline_ms = millis() + getMacroGuard(macro);
    ::macro.dome_cancelled = false;
    ::macro.last_step = UINT32_MAX;
    ::macro.home_sequence = 0;
    ::macro.audio_sequence = 0;

    auto bstatus = g_body_client.bodyStatus(millis());
    ::macro.dome_generation = bstatus.value.dome_authority_generation;

    const bool auto_dome_on = (rc_channels[RC_CH_AUTO_DOME] >= 1750);
    const bool sticks_neutral = rcNeutral() && driveNeutral();

    if (macro == R2_LEIA) {
        if (auto_dome_on && sticks_neutral) {
            RequestHandle h = sendDomeRequest(r2link::DomeOperation::SeekReference, r2link::DomeReference::Front);
            if (h.queued) {
                ::macro.phase = MacroPhase::WaitingHome;
                homing_state = HOMING_SEEKING;
                ::macro.home_sequence = h.sequence;
                Serial.println(F("[MACRO] Princess Leia: Seeking 0° front reference..."));
                return;
            }
            Serial.println(F("[MACRO] Princess Leia: Alignment unavailable; playing in place."));
            ::macro.dome_cancelled = true;
        } else if (auto_dome_on && !sticks_neutral) {
            Serial.println(F("[MACRO] Princess Leia: Alignment rejected (manual/drive active)."));
            ::macro.dome_cancelled = true;
        }
    }

    ::macro.phase = MacroPhase::WaitingAudio;
    uint16_t track = getMacroTrack(macro);
    RequestHandle ah = g_remote_audio.play(track, r2link::AudioPriority::Foreground, millis());
    ::macro.audio_sequence = ah.sequence;
}

void processMacroCompletion(const r2link::Completion& comp) {
    if (macro.phase == MacroPhase::WaitingHome && comp.sequence == macro.home_sequence) {
        if (comp.result != static_cast<uint8_t>(r2link::Result::Accepted)) {
            macro.dome_cancelled = true;
            cancelR2Macro();
        }
    } else if (macro.phase == MacroPhase::WaitingAudio && comp.sequence == macro.audio_sequence) {
        if (comp.result != static_cast<uint8_t>(r2link::Result::Accepted)) {
            cancelR2Macro();
        }
    }
}

void processMacroEvent(const r2link::Event& ev) {
    if (macro.phase == MacroPhase::WaitingHome) {
        if (ev.request_type == static_cast<uint8_t>(r2link::MessageType::DomeRequest) &&
            ev.request_seq == macro.home_sequence) {
            if (ev.kind == static_cast<uint8_t>(r2link::EventKind::Completed)) {
                homing_state = HOMING_INACTIVE;
                centerHoloServos();
                if (macro.dome_cancelled) {
                    cancelR2Macro();
                    return;
                }
                macro.phase = MacroPhase::WaitingAudio;
                uint16_t track = getMacroTrack(macro.kind);
                RequestHandle ah = g_remote_audio.play(track, r2link::AudioPriority::Foreground, millis());
                macro.audio_sequence = ah.sequence;
            } else if (ev.kind == static_cast<uint8_t>(r2link::EventKind::HardwareError) ||
                       ev.kind == static_cast<uint8_t>(r2link::EventKind::Timeout) ||
                       ev.kind == static_cast<uint8_t>(r2link::EventKind::Cancelled)) {
                homing_state = HOMING_INACTIVE;
                cancelR2Macro();
            }
        }
    } else if (macro.phase == MacroPhase::WaitingAudio) {
        if (ev.request_type == static_cast<uint8_t>(r2link::MessageType::AudioRequest) &&
            ev.request_seq == macro.audio_sequence) {
            if (ev.kind == static_cast<uint8_t>(r2link::EventKind::PlaybackStarted)) {
                macro.phase = MacroPhase::Running;
                macro.started_ms = millis();
                startMacroChoreography(macro.kind);
            } else if (ev.kind == static_cast<uint8_t>(r2link::EventKind::HardwareError) ||
                       ev.kind == static_cast<uint8_t>(r2link::EventKind::Timeout) ||
                       ev.kind == static_cast<uint8_t>(r2link::EventKind::Cancelled)) {
                cancelR2Macro();
            }
        }
    } else if (macro.phase == MacroPhase::Running) {
        if (ev.request_type == static_cast<uint8_t>(r2link::MessageType::AudioRequest) &&
            ev.request_seq == macro.audio_sequence) {
            if (ev.kind == static_cast<uint8_t>(r2link::EventKind::Completed)) {
                finishR2Macro();
            } else if (ev.kind == static_cast<uint8_t>(r2link::EventKind::HardwareError) ||
                       ev.kind == static_cast<uint8_t>(r2link::EventKind::Timeout) ||
                       ev.kind == static_cast<uint8_t>(r2link::EventKind::Cancelled)) {
                cancelR2Macro();
            }
        }
    }
}

void processR2Macro() {
    if (macro.phase == MacroPhase::Idle) return;
    uint32_t now = millis();

    // Check generation change during WaitingHome
    if (macro.phase == MacroPhase::WaitingHome) {
        auto status = g_body_client.bodyStatus(now);
        if (status.fresh && status.value.dome_authority_generation != macro.dome_generation) {
            macro.dome_cancelled = true;
            homing_state = HOMING_INACTIVE;
            macro.home_sequence = 0;
            cancelR2Macro();
            return;
        }
    }

    if (now >= macro.guard_deadline_ms) {
        if (macro.phase == MacroPhase::Running) {
            finishR2Macro();
        } else {
            cancelR2Macro();
        }
        return;
    }
    if (macro.phase != MacroPhase::Running) return;

    uint32_t elapsed = now - macro.started_ms;
    if (elapsed >= macro.duration_ms) {
        finishR2Macro();
        return;
    }
    if (macro.kind == R2_FAINT && elapsed >= 600) {
        FLD.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        RLD.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        frontPSI.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        rearPSI.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        frontHolo.selectSequence(7, 0);
        rearHolo.selectSequence(7, 0);
        topHolo.selectSequence(7, 0);
    } else if (macro.kind == R2_CANTINA || macro.kind == R2_SCREAM) {
        uint32_t step = elapsed / (macro.kind == R2_CANTINA ? 250 : 150);
        if (step != macro.last_step) {
            macro.last_step = step;
            uint8_t ch = step % 6;
            uint16_t pulse = (macro.kind == R2_CANTINA)
                ? (((step / 6) % 2 == 0) ? 1670 : 1330)
                : random(1330, 1671);
            servoDispatch.moveToPulse(ch, 100, pulse);
        }
    }
}

bool maintenanceReady() {
    return g_maintenance.state == MaintenanceState::Locked;
}

static void restartNow(bool clear_prefs) {
    if (clear_prefs) {
        preferences.clear();
    }
    preferences.end();
    unmountFileSystems();
    ESP.restart();
}

void prepareMaintenance(bool for_reboot, bool clear_prefs) {
    if (g_maintenance.state == MaintenanceState::Locked) {
        if (for_reboot) restartNow(clear_prefs);
        return;
    }
    stopDomeMotion();
    if (!g_body_client.linkUp(millis())) {
        // No body to lock. A restart cannot affect motion; an update still needs the lock.
        if (for_reboot) {
            restartNow(clear_prefs);
            return;
        }
        g_maintenance.state = MaintenanceState::Failed;
        Serial.println(F("[MAINTENANCE] Body link down; cannot take the maintenance lock."));
        return;
    }
    auto status = g_body_client.bodyStatus(millis());
    r2link::ControlRequest req{};
    req.operation = 2; // LOCK
    req.reason = r2link::kReasonMaintenance;
    req.token = 0xBEEF;
    req.control_epoch = status.value.control_epoch;
    RequestHandle h = g_body_client.requestControl(req, millis());
    g_maintenance.state = MaintenanceState::Requested;
    g_maintenance.token = req.token;
    g_maintenance.sequence = h.sequence;
    g_maintenance.deadline_ms = millis() + 3000;
    g_maintenance.pending_reboot = for_reboot;
    g_maintenance.clear_prefs_on_reboot = clear_prefs;
}

void releaseMaintenance() {
    g_maintenance.pending_reboot = false;
    g_maintenance.clear_prefs_on_reboot = false;
    if (g_maintenance.state == MaintenanceState::Locked) {
        // The body may refuse (CH6 ON or sticks off-centre): stay Locked until it confirms.
        auto status = g_body_client.bodyStatus(millis());
        r2link::ControlRequest req{};
        req.operation = 3; // UNLOCK
        req.reason = r2link::kReasonMaintenance;
        req.token = g_maintenance.token;
        req.control_epoch = status.value.control_epoch;
        RequestHandle h = g_body_client.requestControl(req, millis());
        if (h.queued) {
            g_maintenance.state = MaintenanceState::Releasing;
            g_maintenance.sequence = h.sequence;
        }
        return;
    }
    g_maintenance.state = MaintenanceState::Idle;
    g_maintenance.token = 0;
    g_maintenance.sequence = 0;
}

void recoverBodyLocks() {
    auto status = g_body_client.bodyStatus(millis());
    r2link::ControlRequest req{};
    req.operation = 4; // RECOVER_LOCKS
    req.reason = r2link::kReasonOperator;
    req.token = 0;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestControl(req, millis());
}

void processMaintenanceCompletion(const r2link::Completion& comp) {
    if (comp.type != r2link::MessageType::ControlRequest || comp.sequence != g_maintenance.sequence) return;
    const bool accepted = comp.outcome == r2link::Outcome::Replied &&
                          comp.result == static_cast<uint8_t>(r2link::Result::Accepted);
    if (g_maintenance.state == MaintenanceState::Releasing) {
        if (accepted) {
            g_maintenance.state = MaintenanceState::Idle;
            g_maintenance.token = 0;
            g_maintenance.sequence = 0;
        } else {
            g_maintenance.state = MaintenanceState::Locked;
            Serial.println(F("[MAINTENANCE] Unlock refused: set CH6 OFF and centre sticks, then retry."));
        }
        return;
    }
    if (g_maintenance.state == MaintenanceState::Requested) {
        if (accepted) {
            g_maintenance.state = MaintenanceState::Locked;
            if (g_maintenance.pending_reboot) restartNow(g_maintenance.clear_prefs_on_reboot);
        } else {
            g_maintenance.state = MaintenanceState::Failed;
            Serial.println(F("[MAINTENANCE] Lock rejected by body controller."));
        }
    }
}

void processMaintenance(uint32_t now) {
    if (g_maintenance.state == MaintenanceState::Requested) {
        auto bstatus = g_body_client.bodyStatus(now);
        if (bstatus.fresh && (bstatus.value.lock_reasons & (1 << 2))) {
            g_maintenance.state = MaintenanceState::Locked;
            if (g_maintenance.pending_reboot) restartNow(g_maintenance.clear_prefs_on_reboot);
            return;
        }
        if (now >= g_maintenance.deadline_ms) {
            g_maintenance.state = MaintenanceState::Failed;
            Serial.println(F("[MAINTENANCE] Lock request timed out. USB recovery required."));
        }
    }
}

static uint32_t g_commission_run_id = 0;
static uint32_t g_last_commission_keepalive_ms = 0;

void startCommissionTest(uint8_t test, int32_t val) {
    (void)val;
    if (!rc_connected || dome_motion_inhibited || otaInProgress) {
        Serial.println(F("[COMMISSION] Rejected: live radio and safety rearmed required."));
        return;
    }
    stopDomeMotion();
    auto status = g_body_client.bodyStatus(millis());
    g_commission_run_id = millis();
    if (g_commission_run_id == 0) g_commission_run_id = 1;

    r2link::CommissionRequest req{};
    req.operation = 1; // Begin
    req.test = test;
    req.run_id = g_commission_run_id;
    req.field = 0;
    req.wheel = 0;
    req.value = 0;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestCommission(req, millis());
    g_last_commission_keepalive_ms = millis();
}

void cancelCommissionTest() {
    auto status = g_body_client.bodyStatus(millis());
    r2link::CommissionRequest req{};
    req.operation = 3; // Cancel
    req.test = 0;
    req.run_id = g_commission_run_id;
    req.field = 0;
    req.wheel = 0;
    req.value = 0;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestCommission(req, millis());
}

void saveCommissionProfile() {
    auto status = g_body_client.bodyStatus(millis());
    r2link::CommissionRequest req{};
    req.operation = 5; // Save
    req.test = 0;
    req.run_id = g_commission_run_id;
    req.field = 0;
    req.wheel = 0;
    req.value = 0;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestCommission(req, millis());
}

void acceptCommissionBit(uint8_t bit) {
    auto status = g_body_client.bodyStatus(millis());
    r2link::CommissionRequest req{};
    req.operation = 6; // Accept
    req.test = 0;
    req.run_id = g_commission_run_id;
    req.field = 0;
    req.wheel = 0;
    req.value = bit;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestCommission(req, millis());
}

// Stage one profile field on the body (CH6 OFF, CH9 OFF, sticks centred). Field ids
// follow ConfigStore.h; Save is still required before actuators use the value.
void setCommissionField(uint8_t field, uint8_t wheel, int32_t value) {
    auto status = g_body_client.bodyStatus(millis());
    r2link::CommissionRequest req{};
    req.operation = 4; // SetField
    req.field = field;
    req.wheel = wheel;
    req.value = value;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestCommission(req, millis());
}

// Ask the body to publish one staged field as a Diagnostics frame.
void readCommissionField(uint8_t field, uint8_t wheel) {
    auto status = g_body_client.bodyStatus(millis());
    r2link::CommissionRequest req{};
    req.operation = 0; // Read
    req.field = 1;     // subtype 1: profile field
    req.wheel = wheel;
    req.value = field;
    req.control_epoch = status.value.control_epoch;
    g_body_client.requestCommission(req, millis());
}

// ArduinoOTA calls this after Update.begin(). Returning does not cancel an update,
// so a transfer without the body maintenance lock is aborted explicitly: the
// transfer loop then ends at once and Update.end() fails without activating it.
void onOtaStart() {
    if (!maintenanceReady()) {
        Serial.println(F("[OTA] Rejected: Body maintenance lock not held. Prepare update first."));
        Update.abort();
        return;
    }
    otaInProgress = true;
    stopDomeMotion();
    disableHoloServos();
    DEBUG_PRINTLN("[OTA] Update Started...");
}

// Body events: idle scheduler, macro sequencer and dome homing (a completed home
// starts any macro deferred behind it; a failed one drops it).
void dispatchBodyEvents() {
    r2link::Event ev;
    while (g_body_client.takeEvent(ev)) {
        g_dome_behaviour.onEvent(ev);
        processMacroEvent(ev);
        if (ev.request_type != static_cast<uint8_t>(r2link::MessageType::DomeRequest)) continue;
        if (ev.kind == static_cast<uint8_t>(r2link::EventKind::Completed)) {
            homing_state = HOMING_INACTIVE;
            centerHoloServos();
            if (pending_macro_after_home != R2_NONE) {
                R2Macro macro = pending_macro_after_home;
                pending_macro_after_home = R2_NONE;
                startR2Macro(macro);
            }
        } else if (ev.kind == static_cast<uint8_t>(r2link::EventKind::HardwareError) ||
                   ev.kind == static_cast<uint8_t>(r2link::EventKind::Timeout) ||
                   ev.kind == static_cast<uint8_t>(r2link::EventKind::Cancelled)) {
            homing_state = HOMING_INACTIVE;
            pending_macro_after_home = R2_NONE;
        }
    }
}

// Terminal outcomes of the dome's own requests: macros, maintenance and the idle
// dome scheduler each consume the ones they own. A lost reply counts as NotReady.
void dispatchBodyCompletions() {
    r2link::Completion comp;
    while (g_body_client.takeCompletion(comp)) {
        processMacroCompletion(comp);
        processMaintenanceCompletion(comp);
        if (comp.type == r2link::MessageType::DomeRequest) {
            g_dome_behaviour.onReply(comp.sequence, comp.outcome == r2link::Outcome::Replied
                ? static_cast<r2link::Result>(comp.result) : r2link::Result::NotReady);
        }
    }
}

// Audio lives on the body, so the startup chime can only be requested once the
// link is up; requesting it from setup() would always be dropped.
void processStartupSound(uint32_t now) {
    static bool played = false;
    if (!played && g_body_client.linkUp(now)) {
        played = true;
        sMarcSound.playStartSound();
    }
}

void processCommissioningKeepalive(uint32_t now) {
    auto cstatus = g_body_client.commissionStatus(now);
    if (cstatus.fresh && cstatus.value.state == 1) { // Running
        if (now - g_last_commission_keepalive_ms >= 100) {
            g_last_commission_keepalive_ms = now;
            auto status = g_body_client.bodyStatus(now);
            r2link::CommissionRequest req{};
            req.operation = 2; // Keepalive
            req.test = 0;
            req.run_id = cstatus.value.run_id;
            req.field = 0;
            req.wheel = 0;
            req.value = 0;
            req.control_epoch = status.value.control_epoch;
            g_body_client.requestCommission(req, now);
        }
    }
}


void processRandomHolos() {
    // This scheduler owns random motion; library LED effects remain independent.
    CommandEvent::process(F("HPA198"));
    if (!rc_connected || otaInProgress || active_macro != R2_NONE ||
        homing_state != HOMING_INACTIVE) {
        setHoloServoOwnership(false);
        return;
    }
    rearHolo.assignServos(&servoDispatch, 2, 3);
    topHolo.assignServos(&servoDispatch, 4, 5);
    frontHolo.assignServos(&servoDispatch, 0, 1);
    uint32_t now = millis();
    if (int32_t(now - next_holo_twitch_ms) < 0) return;
    servoDispatch.moveToPulse(random(0, 6), 300, random(1390, 1611));
    next_holo_twitch_ms = now + random(1500, 4501);
}

// VrA knob: 13 equal ~77us bins over 1000-2000us (map() would give position 13
// only at exactly 2000us).
uint8_t dialPosition(uint16_t us) {
    const uint32_t v = us < 1000 ? 0 : us > 2000 ? 1000 : uint32_t(us - 1000);
    return static_cast<uint8_t>(1 + v * 13 / 1001);
}

void processTransmitterInputs() {
    if (!rc_connected) return;

    // Rotary Knob VrA (CH7: 1000us - 2000us mapped to 13 discrete positions)
    uint8_t dial_pos = dialPosition(rc_channels[RC_CH_MOOD_SELECT]);

    // 3. Macro Fire Trigger (Switch SwB / CH8: Flip DOWN > 1750us)
    static bool macro_trigger_latched = false;
    bool macro_swc_down = (rc_channels[RC_CH_MACRO_TRIGGER] > 1750);

    if (macro_swc_down && !macro_trigger_latched) {
        macro_trigger_latched = true;
        Serial.print(F("[TX] Macro Triggered at Dial Position: "));
        Serial.println(dial_pos);

        switch (dial_pos) {
            case 1: // Position 1: Auto-Center Reset / Home Dome
                startDomeHoming();
                Marcduino::processCommand(player, "@0T1");
                playDFPlayerTrack(11);
                break;

            case 2: // Position 2: Movie Normal
                Marcduino::processCommand(player, "@0T1");
                sMarcSound.playSound(1, 1);
                break;

            case 3: // Position 3: Happy Melodic
                Marcduino::processCommand(player, "@0T1");
                sMarcSound.playSound(3, 1);
                break;

            case 4: // Position 4: Scream Macro
                Marcduino::processCommand(player, ":SE01");
                break;

            case 5: // Position 5: Cantina Band Song & March
                Marcduino::processCommand(player, ":SE05");
                break;

            case 6: // Position 6: Princess Leia (Auto-align head forward 0°, then play message)
                Serial.println(F("[MACRO] Princess Leia: Aligning forward to audience..."));
                Marcduino::processCommand(player, ":SE08");
                break;

            case 7: // Position 7: Star Wars Disco Rainbow
                Marcduino::processCommand(player, ":SE09");
                break;

            case 8: // Position 8: Short Circuit / Faint
                Marcduino::processCommand(player, ":SE06");
                break;

            default:
                sMarcSound.playRandom();
                break;
        }
    } else if (!macro_swc_down) {
        macro_trigger_latched = false; // Arm for next execution
    }
}

/////////////////////////////////////////////////////////////////////////
// 14. MARCDUINO COMMAND EXTENSIONS (CUSTOM COMMANDS)
/////////////////////////////////////////////////////////////////////////

MARCDUINO_ACTION(HomeDomeCommand, :DMH, ({
    startDomeHoming();
}))

MARCDUINO_ACTION(StopDomeCommand, :DMS, ({
    stopDomeMotion();
    cancelR2Macro();
}))

MARCDUINO_ACTION(DirectCommand, ~RT, ({
    CommandEvent::process(Marcduino::getCommand());
}))

MARCDUINO_ACTION(MDDirectCommand, @AP, ({
    CommandEvent::process(Marcduino::getCommand());
}))

MARCDUINO_ACTION(WifiToggle, #APWIFI, ({
#ifdef USE_WIFI
    bool wifiSetting = wifiEnabled;
    switch (*Marcduino::getCommand()) {
        case '0': wifiSetting = false; break;
        case '1': wifiSetting = true; break;
        case '\0': wifiSetting = !wifiSetting; break;
    }
    if (wifiEnabled != wifiSetting) {
        preferences.putBool(PREFERENCE_WIFI_ENABLED, wifiSetting);
        reboot();
    }
#endif
}))

MARCDUINO_ACTION(ClearPrefs, #APZERO, ({
    clearPrefsAndReboot();
}))

MARCDUINO_ACTION(Restart, #APRESTART, ({
    reboot();
}))

/////////////////////////////////////////////////////////////////////////
// 15. SYSTEM INITIALIZATION (SETUP)
/////////////////////////////////////////////////////////////////////////

void setup() {
    REELTWO_READY();

    Serial.begin(115200);
    Serial.println(F("═════════════════════════════════════════════════════════════"));
    Serial.println(F("   ASTROPIXELS PLUS - UNIFIED R2-D2 DOME BRAIN (REELTWO)     "));
    Serial.println(F("═════════════════════════════════════════════════════════════"));

    if (!preferences.begin("astro", false)) {
        DEBUG_PRINTLN("[SYSTEM] Failed to initialize preferences in NVS");
    }

#ifdef USE_WIFI
    wifiEnabled = wifiActive = preferences.getBool(PREFERENCE_WIFI_ENABLED, WIFI_ENABLED);
#endif

    // 1. Initialize Body Controller UART (115,200 baud, RX16, TX17 on Serial2)
    BODY_SERIAL.begin(115200, SERIAL_8N1, PIN_BODY_UART_RX, PIN_BODY_UART_TX);

    // 2. Initialize BodyClient session from NVS boot counter
    Preferences r2link_prefs;
    r2link_prefs.begin("r2link", false);
    uint32_t session = r2link_prefs.getUInt("session", 0) + 1;
    if (session == 0) session = 1;
    size_t written = r2link_prefs.putUInt("session", session);
    r2link_prefs.end();
    if (written == 0) {
        DEBUG_PRINTLN("[SYSTEM] Error: Failed to commit r2link session to NVS; disabling link");
        session = 0;
    }

    g_body_client.begin(g_body_port, session);

    // 3. Mount SPIFFS Filesystem for Web Images & Data
    if (!mountReadOnlyFileSystem()) {
        DEBUG_PRINTLN("[SYSTEM] Warning: Read-only filesystem not mounted");
    }

    // 4. Initialize I2C Bus for PCA9685 16-Channel Servo Controller
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(10); // Prevent bus lockup from stalling main loop
    SetupEvent::ready();

    // 5. Initialize Remote Audio Engine via Body Controller
    sMarcSound.beginRemote(g_remote_audio, MARC_SOUND_STARTUP);
    sMarcSound.setVolume(preferences.getInt(PREFERENCE_MARCSOUND_VOLUME, MARC_SOUND_VOLUME) / 1000.0);

    // 6. Assign PCA9685 Servos to 3 HoloProjectors
    frontHolo.assignServos(&servoDispatch, 0, 1);
    rearHolo.assignServos(&servoDispatch, 2, 3);
    topHolo.assignServos(&servoDispatch, 4, 5);

    // 7. Initialize Dual KY-003 Hall Effect Homing Sensors
    pinMode(PIN_DOME_HALL_FRONT, INPUT_PULLUP);
    pinMode(PIN_DOME_HALL_REAR, INPUT_PULLUP);

    // 8. Attach Procedural Shaders to Displays
    RLD.setLogicEffectSelector(CustomLogicEffectSelector);
    FLD.setLogicEffectSelector(CustomLogicEffectSelector);
    frontPSI.setLogicEffectSelector(CustomLogicEffectSelector);
    rearPSI.setLogicEffectSelector(CustomLogicEffectSelector);

    // 9. Initial Movie Scroll Message
    RLD.selectScrollTextLeft("... AstroPixels ....", LogicEngineRenderer::kBlue, 0, 15);
    FLD.selectScrollTextLeft("... R2-D2 ...", LogicEngineRenderer::kRed, 0, 15);

#ifdef USE_WIFI
    if (wifiEnabled) {
#ifdef USE_WIFI_WEB
        wifiAccess.setNetworkCredentials(
            preferences.getString(PREFERENCE_WIFI_SSID, WIFI_AP_NAME),
            preferences.getString(PREFERENCE_WIFI_PASS, WIFI_AP_PASSPHRASE),
            preferences.getBool(PREFERENCE_WIFI_AP, WIFI_ACCESS_POINT),
            preferences.getBool(PREFERENCE_WIFI_ENABLED, WIFI_ENABLED)
        );

#ifdef USE_WIFI_MARCDUINO
        wifiMarcduinoReceiver.setEnabled(true);
        wifiMarcduinoReceiver.setCommandHandler([](const char *cmd) {
            Marcduino::processCommand(player, cmd);
        });
#endif

        wifiAccess.notifyWifiConnected([](WifiAccess &wifi) {
            Serial.print(F("[WIFI] Connected! Web GUI available at http://"));
            Serial.println(wifi.getIPAddress());
#ifdef USE_MDNS
            if (!wifi.isSoftAP()) {
                MDNS.begin("astropixels");
            }
#endif
        });
#endif

#ifdef USE_OTA
        ArduinoOTA.onStart(onOtaStart).onEnd([]() { DEBUG_PRINTLN("[OTA] Update Finished!"); })
          .onError([](ota_error_t error) {
              otaInProgress = false;
              Serial.printf("[OTA] Error[%u]\n", error);
          });
        ArduinoOTA.begin();
#endif
    }
#endif

    // 10. Random chatter (the startup sound plays once the body link is up)
    sMarcSound.setRandomMin(preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MIN, MARC_SOUND_RANDOM_MIN));
    sMarcSound.setRandomMax(preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MAX, MARC_SOUND_RANDOM_MAX));
    if (preferences.getBool(PREFERENCE_MARCSOUND_RANDOM, MARC_SOUND_RANDOM)) {
        sMarcSound.startRandomInSeconds(12);
    }

    Serial.println(F("[SYSTEM] AstroPixels Plus Unified Brain Online & Ready!"));
}

/////////////////////////////////////////////////////////////////////////
// 16. COOPERATIVE CONTROL AND NETWORK LOOP
/////////////////////////////////////////////////////////////////////////

void loop() {
    uint32_t now = millis();

    // 1. Tick body client and process incoming frames from Teensy
    g_body_client.tick(now);

    // 2. Drain completions and forward to macros, maintenance and the idle scheduler
    dispatchBodyCompletions();

    // 3. Drain events and forward to DomeBehaviour and macro sequencer
    dispatchBodyEvents();

    // 4. Process maintenance timeouts
    processMaintenance(now);

    // 5. Process commissioning keepalive and the deferred startup sound
    processCommissioningKeepalive(now);
    processStartupSound(now);

    // 3. Dual Hall sensors publish
    processHallSensors(now);

    // 4. Ingest RC snapshot from Teensy
    processBodyRcSnapshot(now);

    // 5. Autonomous dome behaviour scheduler
    DomeBehaviourInput dome_input = g_body_client.makeDomeBehaviourInput(now, active_macro != R2_NONE);
    g_dome_behaviour.tick(dome_input, now);

#ifdef USE_WIFI
    if (wifiActive) {
#ifdef USE_OTA
        ArduinoOTA.handle();
#endif
#ifdef USE_WIFI_WEB
        webServer.handle();
#endif
    }
#endif
    if (otaInProgress) {
        return; // motion was stopped once when the update started
    }

    processTransmitterInputs();
    processRandomHolos();
    AnimatedEvent::process();
    processR2Macro();
    sMarcSound.idle();
}
