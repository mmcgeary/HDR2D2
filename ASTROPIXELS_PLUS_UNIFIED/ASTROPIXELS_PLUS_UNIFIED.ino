/*
 * ═══════════════════════════════════════════════════════════════════════════════
 *                 ASTROPIXELS PLUS - UNIFIED R2-D2 DOME BRAIN
 *     (ReelTwo OS + FlySky i-Bus + Wi-Fi Web GUI + OTA + 35kg Dome Drive + Homing)
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Target Board:   ESP32 Dev Module (30-pin, AstroPixels Motherboard)
 * Architecture:   Cooperative control loop on the Arduino task
 *
 * Core Allocations:
 *   • Networking (callbacks serviced by loop):
 *       1. Wi-Fi SoftAP ("AstroPixels" / "Astromech") & Station Client
 *       2. Web Server (Port 80) serving interactive GUI at http://192.168.4.1
 *       3. ArduinoOTA Wireless Firmware Flashing (No USB disassembly needed!)
 *       4. Wi-Fi MarcDuino UDP Receiver
 *
 *   • Control (same task; no fixed loop-rate guarantee):
 *       1. Hardware UART2 (115,200 baud / GPIO 16): Non-blocking FlySky i-Bus decoder
 *       2. LEDC Hardware PWM (GPIO 4): 35kg 360° continuous dome servo with zero-creep sleep
 *       3. KY-003 Hall Effect Sensor (GPIO 19): Active 0° Home auto-alignment interrupt
 *       4. PCA9685 I2C 16-Ch Controller (Addr 0x40 / GPIO 21, 22): 6 HoloProjector servos (Ch 0-5)
 *       5. Hardware UART1 (9,600 baud / GPIO 17): Dedicated DFPlayer serial audio transmitter
 *       6. ReelTwo LogicEngine: Movie-accurate FLD1/2, RLD, FPSI, RPSI & text scrolling
 *       7. ReelTwo HoloLights: WS2812 Holo LEDs with dim pulses, rainbow & Leia flicker
 *       8. Procedural Shaders: Plasma, MetaBalls, Fractal, and Bitmap animations
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

// Serial Communication Pins
#define PIN_IBUS_RX          16    // Hardware Serial2 RX (115,200 baud): FlySky i-Bus Stream (Receiver in Dome)
#define PIN_SOUND_TX         17    // Hardware Serial1 TX (9,600 baud): Body DFPlayer RX via Slip Ring Ch 4
#define PIN_VESC_UART_TX     18    // Hardware Serial2 TX (115,200 baud): Dual VESC COMM RX via Slip Ring Ch 6
#define SERIAL2_RX_PIN       16
#define SERIAL2_TX_PIN       18
#define COMMAND_SERIAL       Serial2

// Dome Drive & Homing Pins
#define PIN_DOME_SERVO_PWM   4     // LEDC PWM: 35kg 360° Dome Continuous Servo (via LLC LV3->HV3, Ch 5)
#define PIN_DOME_HALL_SENS   19    // Digital Input: KY-003 Hall Sensor (AUX 5 via LLC LV2)

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
#define PIN_AUX2             4     // Used for PIN_DOME_SERVO_PWM
#define PIN_AUX3             5     // Unused
#define PIN_AUX4             18    // Used for PIN_VESC_UART_TX (Slip Ring Ch 6)
#define PIN_AUX5             19    // Used for PIN_DOME_HALL_SENS

/////////////////////////////////////////////////////////////////////////
// 2. AUDIO SERIAL CONFIGURATION (DFPLAYER MINI)
/////////////////////////////////////////////////////////////////////////

#define SOUND_SERIAL         Serial1
#define SOUND_RX_PIN         -1    // Simplex 1-wire connection down slip ring (No RX needed)
#define SOUND_TX_PIN         PIN_SOUND_TX
#define SOUND_BAUD           9600

#define MARC_SOUND_PLAYER    MarcSound::kDFMini
#define MARC_SOUND_VOLUME    333   // 0 - 1000; ceil(333 / 1000 * 30) = 10
#define MARC_SOUND_STARTUP   255   // Track 255 (startup chime)
#define MARC_SOUND_RANDOM    true  // Ambient background chatter enabled
#define MARC_SOUND_RANDOM_MIN 5000
#define MARC_SOUND_RANDOM_MAX 25000

/////////////////////////////////////////////////////////////////////////
// 3. LEDC PWM CONFIGURATION (35KG CONTINUOUS ROTATION DOME SERVO)
/////////////////////////////////////////////////////////////////////////

#define DOME_LEDC_CHANNEL    0
#define DOME_LEDC_FREQ       50    // 50Hz standard servo frequency
#define DOME_LEDC_RES        16    // 16-bit resolution (0-65535)
#define DOME_DUTY_STOP       4915  // ~1.50ms Center Neutral Stop
#define DOME_DUTY_MAX_CW     6553  // ~2.00ms Full CW Velocity
#define DOME_DUTY_MAX_CCW    3276  // ~1.00ms Full CCW Velocity
#define DOME_CENTER_TRIM     0     // Fine-tune offset in duty ticks if servo drifts

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define WRITE_DOME_SERVO(duty) ledcWrite(PIN_DOME_SERVO_PWM, duty)
#else
  #define WRITE_DOME_SERVO(duty) ledcWrite(DOME_LEDC_CHANNEL, duty)
#endif

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
void startR2Macro(R2Macro macro);
void stopDomeMotion();
void cancelR2Macro();
void disableHoloServos();
bool r2MacroActive();
void startDomeHoming(R2Macro macro = R2_NONE);
void playDFPlayerTrack(uint16_t track_num);

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
    stopDomeMotion();
    WRITE_DOME_SERVO(0);
    unmountFileSystems();
    preferences.end();
    delay(1000);
    ESP.restart();
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
#define RC_CH_HOLO_TILT        2  // Left Stick Y (CH3): Manual Front Holo Tilt
#define RC_CH_DOME_STEER       3  // Left Stick X (CH4): Manual Dome Rotation
#define RC_CH_SPEED_MODE       4  // Switch SwB   (CH5): Transmitter Dual Rates
#define RC_CH_MOOD_SELECT      6  // Knob VrA     (CH7): Persistent Mood & Macro Selector (1-13)
#define RC_CH_MACRO_TRIGGER    7  // Switch SwC   (CH8): Macro Fire Trigger (Flip DOWN)
#define RC_CH_HOLO_ENABLE      8  // Switch SwD   (CH9): Holo Random Motion Toggle

// Dome Homing State Machine
enum HomingState : uint8_t {
    HOMING_INACTIVE = 0,
    HOMING_SEEKING,
    HOMING_ALIGNED
};

HomingState homing_state = HOMING_INACTIVE;
unsigned long homing_start_time_ms = 0;
R2Macro pending_macro_after_home = R2_NONE;
bool dome_motion_inhibited = true;
R2Macro active_macro = R2_NONE;
uint32_t macro_started_ms = 0;
uint32_t macro_duration_ms = 0;
uint32_t last_macro_step = UINT32_MAX;
uint32_t next_holo_twitch_ms = 0;
bool random_holo_enabled = false;

// Dome Servo Creep Prevention Timer
unsigned long dome_stop_command_ms = 0;
bool dome_pulses_active = true;

// Dome Homing Sensor Interrupt Flag
volatile bool is_dome_at_home = false;

// Manual Holo Tilt State
unsigned long last_manual_holo_ms = 0;
bool manual_holo_active = false;

// Audio Deduplication & Direct Packet Driver
uint8_t last_played_track = 0;
unsigned long last_audio_cmd_ms = 0;
uint16_t pending_audio_track = 0;

// Forward Declarations
void setDomeServoSpeed(int speed_percent);
void processDomeHoming();
void processDomeRotation();
void processManualHoloTilt();
void processIBusFrames();
void processTransmitterInputs();
void playDFPlayerTrack(uint16_t track_num);

// KY-003 Hall Sensor Interrupt Handler
void IRAM_ATTR onHallSensorChange() {
    is_dome_at_home = (digitalRead(PIN_DOME_HALL_SENS) == LOW);
}

/////////////////////////////////////////////////////////////////////////
// 11. DOME SERVO MOTION & ACTIVE HOMING FUNCTIONS
/////////////////////////////////////////////////////////////////////////

void setDomeServoSpeed(int speed_percent) {
    speed_percent = constrain(speed_percent, -100, 100);

    if (speed_percent == 0) {
        if (dome_pulses_active) {
            WRITE_DOME_SERVO(DOME_DUTY_STOP + DOME_CENTER_TRIM);
            if (dome_stop_command_ms == 0) {
                dome_stop_command_ms = millis();
            } else if (millis() - dome_stop_command_ms > 250) {
                // Zero-Creep Sleep: Completely shut off PWM duty ticks after 250ms of stopped stick
                WRITE_DOME_SERVO(0);
                dome_pulses_active = false;
            }
        }
        return;
    }

    // Active speed commanded -> Wake up PWM generator immediately
    dome_stop_command_ms = 0;
    dome_pulses_active = true;

    uint32_t duty;
    if (speed_percent > 0) {
        duty = map(speed_percent, 1, 100, DOME_DUTY_STOP + 80, DOME_DUTY_MAX_CW);
    } else {
        duty = map(speed_percent, -1, -100, DOME_DUTY_STOP - 80, DOME_DUTY_MAX_CCW);
    }
    WRITE_DOME_SERVO(duty);
}

bool rcNeutral() {
    return rc_channels[RC_CH_DOME_STEER] >= 1460 &&
           rc_channels[RC_CH_DOME_STEER] <= 1540;
}

void stopDomeMotion() {
    homing_state = HOMING_INACTIVE;
    pending_macro_after_home = R2_NONE;
    dome_motion_inhibited = true;
    setDomeServoSpeed(0);
}

void startDomeHoming(R2Macro macro) {
    if (!rc_connected || dome_motion_inhibited || otaInProgress) {
        Serial.println(F("[HOMING] Rejected: live radio and neutral rearm required."));
        return;
    }
    cancelR2Macro();
    pending_macro_after_home = macro;
    homing_state = HOMING_SEEKING;
    homing_start_time_ms = millis();
    Serial.println(F("[HOMING] Active 0° Dome Homing Routine Initiated..."));
}

void processDomeHoming() {
    if (!rc_connected || dome_motion_inhibited || otaInProgress) {
        stopDomeMotion();
        return;
    }
    if (homing_state == HOMING_INACTIVE) return;

    if (homing_state == HOMING_SEEKING) {
        // Check for homing timeout (e.g. 10 seconds max seek)
        if (millis() - homing_start_time_ms > 10000) {
            Serial.println(F("[HOMING] WARNING: Homing seek timeout. Stopping dome."));
            stopDomeMotion();
            return;
        }

        // Active Magnet Detection: KY-003 Hall output is LOW when magnet is present
        if (is_dome_at_home || digitalRead(PIN_DOME_HALL_SENS) == LOW) {
            setDomeServoSpeed(0);
            homing_state = HOMING_ALIGNED;
            Serial.println(F("[HOMING] Dome LOCKED onto 0° Forward Magnet!"));

            // Center all 6 Holo servos
            for (uint8_t ch = 0; ch < 6; ch++) {
                servoDispatch.moveToPulse(ch, 1500);
            }

            // If a chained macro was pending (such as Princess Leia), trigger it now
            if (pending_macro_after_home != R2_NONE) {
                R2Macro macro = pending_macro_after_home;
                pending_macro_after_home = R2_NONE;
                startR2Macro(macro);
            }
            dome_motion_inhibited = true;
            return;
        }

        // Smooth search speed (25% CW)
        setDomeServoSpeed(25);
    } else if (homing_state == HOMING_ALIGNED) {
        setDomeServoSpeed(0);
        homing_state = HOMING_INACTIVE;
    }
}

void processDomeRotation() {
    if (!rc_connected || otaInProgress || active_macro != R2_NONE) {
        setDomeServoSpeed(0);
        return;
    }
    if (dome_motion_inhibited) {
        setDomeServoSpeed(0);
        if (rcNeutral()) dome_motion_inhibited = false;
        return;
    }
    // If active homing is in progress, manual stick is overridden
    if (homing_state != HOMING_INACTIVE) return;

    uint16_t stick_us = rc_channels[RC_CH_DOME_STEER];

    // Stick deadband between 1460us and 1540us
    if (stick_us >= 1460 && stick_us <= 1540) {
        setDomeServoSpeed(0);
        return;
    }

    int speed = 0;
    if (stick_us > 1540) {
        speed = map(stick_us, 1540, 2000, 5, 100);
    } else if (stick_us < 1460) {
        speed = map(stick_us, 1460, 1000, -5, -100);
    }
    setDomeServoSpeed(speed);
}

void processManualHoloTilt() {
    if (!rc_connected || otaInProgress || active_macro != R2_NONE ||
        homing_state != HOMING_INACTIVE) return;
    uint16_t stick_tilt_us = rc_channels[RC_CH_HOLO_TILT];

    // Tilt deadband: 1460us to 1540us
    if (stick_tilt_us < 1460 || stick_tilt_us > 1540) {
        manual_holo_active = true;
        last_manual_holo_ms = millis();
        frontHolo.assignServos(nullptr, 0, 1);
        servoDispatch.disable(1);

        // Move Front Holo Tilt (Ch 1 in servoSettings) directly
        uint16_t pulse = map(constrain(stick_tilt_us, 1000, 2000), 1000, 2000, 1200, 1800);
        servoDispatch.moveToPulse(1, pulse);
        return;
    }

    if (manual_holo_active) {
        // Hold manual angle for 3 seconds of stick inactivity before releasing
        if (millis() - last_manual_holo_ms > 3000) {
            manual_holo_active = false;
            frontHolo.assignServos(&servoDispatch, 0, 1);
        }
    }
}

/////////////////////////////////////////////////////////////////////////
// 12. DIRECT HARDWARE DFPLAYER TRANSMITTER (9,600 BAUD)
/////////////////////////////////////////////////////////////////////////

void playDFPlayerTrack(uint16_t track_num) {
    if (track_num == 0 || track_num > 255) {
        Serial.println(F("[AUDIO] Invalid track; expected 1..255 in /01."));
        return;
    }
    if (pending_audio_track != 0) {
        Serial.println(F("[AUDIO] Pending track replaced by newer command."));
    }
    pending_audio_track = track_num;
}

void processAudioQueue() {
    if (!pending_audio_track || millis() - last_audio_cmd_ms < 100) return;
    uint16_t track_num = pending_audio_track;
    pending_audio_track = 0;

    // Standard DFPlayer 10-Byte Packet: 7E FF 06 0F 00 [Folder] [Track] [CRC_H] [CRC_L] EF
    // Uses Folder Play (0x0F) for reliable filename playback (/01/xxx.mp3)
    uint8_t cmd[10] = {0x7E, 0xFF, 0x06, 0x0F, 0x00, 0x01, (uint8_t)(track_num & 0xFF), 0x00, 0x00, 0xEF};
    uint16_t sum = 0;
    for (uint8_t i = 1; i < 7; i++) sum += cmd[i];
    sum = -sum;
    cmd[7] = (uint8_t)(sum >> 8);
    cmd[8] = (uint8_t)(sum & 0xFF);

    Serial1.write(cmd, 10);
    last_played_track = (uint8_t)track_num;
    last_audio_cmd_ms = millis();
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

void finishR2Macro() {
    active_macro = R2_NONE;
    dome_motion_inhibited = true;
    centerHoloServos();
    resetSequence();
    if (preferences.getBool(PREFERENCE_MARCSOUND_RANDOM, MARC_SOUND_RANDOM))
        sMarcSound.startRandomInSeconds(12);
    else
        sMarcSound.stopRandom();
}

bool r2MacroActive() {
    return active_macro != R2_NONE;
}

void cancelR2Macro() {
    if (active_macro != R2_NONE) {
        active_macro = R2_NONE;
        pending_audio_track = 0;
        sMarcSound.stop();
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
    setDomeServoSpeed(0);
    sMarcSound.suspendRandom();
    active_macro = macro;
    macro_started_ms = millis();
    last_macro_step = UINT32_MAX;
    setHoloServoOwnership(false);
    centerHoloServos();
    uint16_t track = 0;
    uint8_t effect = LogicEngineDefaults::NORMAL;
    switch (macro) {
        case R2_SCREAM:
            track = 102; macro_duration_ms = 4500;
            effect = LogicEngineDefaults::ALARM;
            break;
        case R2_CANTINA:
            track = 106; macro_duration_ms = 30000; // Provisional audio length.
            effect = LogicEngineDefaults::MARCH;
            break;
        case R2_LEIA:
            track = 109; macro_duration_ms = 14000;
            effect = LogicEngineDefaults::LEIA;
            servoDispatch.moveToPulse(1, 250, 1300);
            break;
        case R2_DISCO:
            track = 110; macro_duration_ms = 20000;
            effect = LogicEngineDefaults::RAINBOW;
            break;
        case R2_FAINT:
            track = 107; macro_duration_ms = 5000;
            effect = LogicEngineDefaults::FAILURE;
            disableHoloServos();
            break;
        default: return;
    }
    LogicEngineRenderer::ColorVal color = macro == R2_SCREAM
        ? LogicEngineRenderer::kRed : LogicEngineRenderer::kDefault;
    FLD.selectSequence(effect, color);
    RLD.selectSequence(effect, color);
    frontPSI.selectSequence(effect, color);
    rearPSI.selectSequence(effect, color);
    if (macro == R2_LEIA) {
        frontHolo.selectSequence(1, 14);
    } else if (macro == R2_DISCO) {
        CommandEvent::process(F("HPA006|20"));
    } else if (macro == R2_CANTINA) {
        CommandEvent::process(F("HPA00331|30"));
    } else if (macro == R2_SCREAM) {
        CommandEvent::process(F("HPA00315|5"));
    } else {
        CommandEvent::process(F("HPA002|1"));
    }
    playDFPlayerTrack(track);
}

void processR2Macro() {
    if (active_macro == R2_NONE) return;
    uint32_t elapsed = millis() - macro_started_ms;
    if (elapsed >= macro_duration_ms) {
        finishR2Macro();
        return;
    }
    if (active_macro == R2_FAINT && elapsed >= 600) {
        FLD.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        RLD.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        frontPSI.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        rearPSI.selectSequence(LogicEngineDefaults::LIGHTSOUT);
        frontHolo.selectSequence(7, 0);
        rearHolo.selectSequence(7, 0);
        topHolo.selectSequence(7, 0);
    } else if (active_macro == R2_CANTINA || active_macro == R2_SCREAM) {
        uint32_t step = elapsed / (active_macro == R2_CANTINA ? 250 : 150);
        if (step != last_macro_step) {
            last_macro_step = step;
            uint8_t ch = step % 6;
            uint16_t pulse = active_macro == R2_CANTINA
                ? (((step / 6) % 2 == 0) ? 1670 : 1330)
                : random(1330, 1671);
            servoDispatch.moveToPulse(ch, 100, pulse);
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
    frontHolo.assignServos(manual_holo_active ? nullptr : &servoDispatch, 0, 1);
    if (!random_holo_enabled || manual_holo_active) return;
    uint32_t now = millis();
    if (int32_t(now - next_holo_twitch_ms) < 0) return;
    servoDispatch.moveToPulse(random(0, 6), 300, random(1390, 1611));
    next_holo_twitch_ms = now + random(1500, 4501);
}

/////////////////////////////////////////////////////////////////////////
// 13. FLYSKY i-BUS 32-BYTE NON-BLOCKING PACKET DECODER
/////////////////////////////////////////////////////////////////////////

void processIBusFrames() {
    static uint8_t ibus_buffer[32];
    static uint8_t ibus_idx = 0;

    while (Serial2.available() > 0) {
        uint8_t byte_in = Serial2.read();

        if (ibus_idx == 0) {
            if (byte_in == 0x20) { // Header Byte 1 (Length: 32 bytes)
                ibus_buffer[0] = byte_in;
                ibus_idx = 1;
            }
        } else if (ibus_idx == 1) {
            if (byte_in == 0x40) { // Header Byte 2 (Command: 0x40)
                ibus_buffer[1] = byte_in;
                ibus_idx = 2;
            } else {
                ibus_idx = 0; // Desync recovery
            }
        } else {
            ibus_buffer[ibus_idx++] = byte_in;

            if (ibus_idx == 32) {
                ibus_idx = 0;

                // Verify 16-bit i-Bus Checksum
                uint16_t checksum = 0xFFFF;
                for (uint8_t i = 0; i < 30; i++) {
                    checksum -= ibus_buffer[i];
                }
                uint16_t packet_checksum = ibus_buffer[30] | (ibus_buffer[31] << 8);

                if (checksum == packet_checksum) {
                    uint16_t channels[14];
                    bool frame_valid = true;
                    for (uint8_t ch = 0; ch < 14; ch++) {
                        channels[ch] = ibus_buffer[2 + (ch * 2)] | (ibus_buffer[3 + (ch * 2)] << 8);
                        if (channels[ch] < 900 || channels[ch] > 2100)
                            frame_valid = false;
                    }
                    if (!frame_valid) {
                        Serial.println(F("[i-BUS] Rejected out-of-range channel frame."));
                        continue;
                    }
                    for (uint8_t ch = 0; ch < 10; ch++)
                        rc_channels[ch] = channels[ch];
                    rc_connected = true;
                    last_rc_packet_ms = millis();
                }
            }
        }
    }

    // 250ms RC Failsafe Detection
    if (rc_connected && (millis() - last_rc_packet_ms > 250)) {
        rc_connected = false;
        stopDomeMotion();
        cancelR2Macro();
        frontHolo.assignServos(nullptr, 0, 1);
        rearHolo.assignServos(nullptr, 2, 3);
        topHolo.assignServos(nullptr, 4, 5);
        servoDispatch.stop();
        servoDispatch.setOutputAll(false);
        manual_holo_active = false;
        Serial.println(F("[SAFETY] i-Bus Connection Lost! Failsafe Stop Activated."));
    }
}

void processTransmitterInputs() {
    if (!rc_connected) return;

    // 1. Holo Random Movement Toggle (Switch SwD / CH9)
    static bool last_swd_state = false;
    bool swd_active = (rc_channels[RC_CH_HOLO_ENABLE] > 1600);
    if (swd_active != last_swd_state) {
        last_swd_state = swd_active;
        random_holo_enabled = swd_active;
        Serial.print(F("[HOLO] Random Posture Movement: "));
        Serial.println(swd_active ? F("ENABLED") : F("DISABLED"));
    }

    // 2. Rotary Knob VrA (CH7: 1000us - 2000us mapped to 13 discrete positions)
    uint8_t dial_pos = map(constrain(rc_channels[RC_CH_MOOD_SELECT], 1000, 2000), 1000, 2000, 1, 13);

    // 3. Macro Fire Trigger (Switch SwC / CH8: Flip DOWN > 1750us)
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
    preferences.clear();
    reboot();
}))

MARCDUINO_ACTION(Restart, #APRESTART, ({
    reboot();
}))

/////////////////////////////////////////////////////////////////////////
// 15. DUAL VESC 4.20 UART & TANK MIXING CONTROLLER
/////////////////////////////////////////////////////////////////////////

#define VESC_COMM_SET_DUTY      5
#define VESC_COMM_FORWARD_CAN   34
#define VESC_CAN_ID_SLAVE       2

static uint16_t vesc_crc16(const uint8_t *buf, uint32_t len) {
    uint16_t crc = 0;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

void sendVescDuty(uint8_t can_id, float duty) {
    duty = constrain(duty, -0.95f, 0.95f);
    int32_t duty_int = (int32_t)(duty * 100000.0f);

    uint8_t payload[8];
    uint8_t p_len = 0;

    if (can_id <= 1) {
        // Direct command to Master (Controller ID 1)
        payload[p_len++] = VESC_COMM_SET_DUTY;
        payload[p_len++] = (duty_int >> 24) & 0xFF;
        payload[p_len++] = (duty_int >> 16) & 0xFF;
        payload[p_len++] = (duty_int >> 8) & 0xFF;
        payload[p_len++] = duty_int & 0xFF;
    } else {
        // Forwarded over internal CAN bus to Slave (Controller ID can_id)
        payload[p_len++] = VESC_COMM_FORWARD_CAN;
        payload[p_len++] = can_id;
        payload[p_len++] = VESC_COMM_SET_DUTY;
        payload[p_len++] = (duty_int >> 24) & 0xFF;
        payload[p_len++] = (duty_int >> 16) & 0xFF;
        payload[p_len++] = (duty_int >> 8) & 0xFF;
        payload[p_len++] = duty_int & 0xFF;
    }

    uint16_t crc = vesc_crc16(payload, p_len);

    uint8_t frame[16];
    uint8_t f_idx = 0;
    frame[f_idx++] = 0x02;            // Packet start
    frame[f_idx++] = p_len;           // Payload length
    for (uint8_t i = 0; i < p_len; i++) {
        frame[f_idx++] = payload[i];
    }
    frame[f_idx++] = (crc >> 8) & 0xFF;
    frame[f_idx++] = crc & 0xFF;
    frame[f_idx++] = 0x03;            // Packet end

    COMMAND_SERIAL.write(frame, f_idx);
}

void stopVescMotors() {
    sendVescDuty(1, 0.0f);
    sendVescDuty(VESC_CAN_ID_SLAVE, 0.0f);
}

void processVescDrive() {
    static uint32_t last_vesc_update_ms = 0;
    uint32_t now = millis();
    if (now - last_vesc_update_ms < 20) return; // 50Hz update rate
    last_vesc_update_ms = now;

    if (!rc_connected || otaInProgress || active_macro == R2_FAINT) {
        stopVescMotors();
        return;
    }

    uint16_t raw_throttle = rc_channels[RC_CH_THROTTLE];
    uint16_t raw_steer    = rc_channels[RC_CH_STEER];

    // Deadband between 1460us and 1540us
    float throttle = 0.0f;
    float steer = 0.0f;

    if (raw_throttle < 1460) {
        throttle = (float)(raw_throttle - 1460) / 460.0f; // -1.0 to 0.0
    } else if (raw_throttle > 1540) {
        throttle = (float)(raw_throttle - 1540) / 460.0f; // 0.0 to +1.0
    }

    if (raw_steer < 1460) {
        steer = (float)(raw_steer - 1460) / 460.0f; // -1.0 to 0.0
    } else if (raw_steer > 1540) {
        steer = (float)(raw_steer - 1540) / 460.0f; // 0.0 to +1.0
    }

    // Dual Rates Speed Switch (Switch SwB / CH5)
    // Low: 35%, Mid: 70%, High: 100%
    float max_rate = 0.35f;
    if (rc_channels[RC_CH_SPEED_MODE] > 1750) {
        max_rate = 1.0f;
    } else if (rc_channels[RC_CH_SPEED_MODE] > 1250) {
        max_rate = 0.70f;
    }

    // Tank Differential Mixing
    float left_duty  = constrain((throttle + steer) * max_rate, -1.0f, 1.0f);
    float right_duty = constrain((throttle - steer) * max_rate, -1.0f, 1.0f);

    sendVescDuty(1, left_duty);
    sendVescDuty(VESC_CAN_ID_SLAVE, right_duty);
}

/////////////////////////////////////////////////////////////////////////
// 16. SYSTEM INITIALIZATION (SETUP)
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

    // 1. Initialize Hardware UART2 for FlySky i-Bus (115,200 baud on GPIO 16)
    COMMAND_SERIAL.begin(115200, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);

    // 2. Initialize Hardware UART1 for Body DFPlayer Mini (9,600 baud on GPIO 17)
    SOUND_SERIAL.begin(SOUND_BAUD, SERIAL_8N1, SOUND_RX_PIN, SOUND_TX_PIN);

    // 3. Mount SPIFFS Filesystem for Web Images & Data
    if (!mountReadOnlyFileSystem()) {
        DEBUG_PRINTLN("[SYSTEM] Warning: Read-only filesystem not mounted");
    }

    // 4. Initialize I2C Bus for PCA9685 16-Channel Servo Controller
    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setTimeOut(10); // Prevent bus lockup from stalling main loop
    SetupEvent::ready();

    // 5. Initialize DFPlayer Audio Engine
    delay(1000); // Allow DFPlayer module to power up
    if (!sMarcSound.begin(MarcSound::kDFMini, SOUND_SERIAL, MARC_SOUND_STARTUP)) {
        DEBUG_PRINTLN("[AUDIO] Warning: DFPlayer init in simplex mode");
    }
    sMarcSound.setVolume(preferences.getInt(PREFERENCE_MARCSOUND_VOLUME, MARC_SOUND_VOLUME) / 1000.0);

    // 6. Assign PCA9685 Servos to 3 HoloProjectors
    frontHolo.assignServos(&servoDispatch, 0, 1);
    rearHolo.assignServos(&servoDispatch, 2, 3);
    topHolo.assignServos(&servoDispatch, 4, 5);

    // 7. Initialize LEDC Hardware Timer for 35kg Continuous Dome Servo
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(PIN_DOME_SERVO_PWM, DOME_LEDC_FREQ, DOME_LEDC_RES);
#else
    ledcSetup(DOME_LEDC_CHANNEL, DOME_LEDC_FREQ, DOME_LEDC_RES);
    ledcAttachPin(PIN_DOME_SERVO_PWM, DOME_LEDC_CHANNEL);
#endif
    setDomeServoSpeed(0); // Neutral zero-speed stop

    // 8. Initialize KY-003 Hall Effect Homing Sensor (GPIO 19 / AUX 5)
    pinMode(PIN_DOME_HALL_SENS, INPUT_PULLUP);
    is_dome_at_home = (digitalRead(PIN_DOME_HALL_SENS) == LOW);
    attachInterrupt(digitalPinToInterrupt(PIN_DOME_HALL_SENS), onHallSensorChange, CHANGE);

    // 9. Attach Procedural Shaders to Displays
    RLD.setLogicEffectSelector(CustomLogicEffectSelector);
    FLD.setLogicEffectSelector(CustomLogicEffectSelector);
    frontPSI.setLogicEffectSelector(CustomLogicEffectSelector);
    rearPSI.setLogicEffectSelector(CustomLogicEffectSelector);

    // 10. Initial Movie Scroll Message
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
        ArduinoOTA.onStart([]() {
            otaInProgress = true;
            stopDomeMotion();
            cancelR2Macro();
            disableHoloServos();
            WRITE_DOME_SERVO(0);
            DEBUG_PRINTLN("[OTA] Update Started...");
        }).onEnd([]() { DEBUG_PRINTLN("[OTA] Update Finished!"); })
          .onError([](ota_error_t error) {
              otaInProgress = false;
              Serial.printf("[OTA] Error[%u]\n", error);
          });
        ArduinoOTA.begin();
#endif
    }
#endif

    // 11. Play Startup Sound & Enable Random Chatter
    sMarcSound.playStartSound();
    sMarcSound.setRandomMin(preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MIN, MARC_SOUND_RANDOM_MIN));
    sMarcSound.setRandomMax(preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MAX, MARC_SOUND_RANDOM_MAX));
    if (preferences.getBool(PREFERENCE_MARCSOUND_RANDOM, MARC_SOUND_RANDOM)) {
        sMarcSound.startRandomInSeconds(12);
    }

    Serial.println(F("[SYSTEM] AstroPixels Plus Unified Brain Online & Ready!"));
}

/////////////////////////////////////////////////////////////////////////
// 17. COOPERATIVE CONTROL AND NETWORK LOOP
/////////////////////////////////////////////////////////////////////////

void loop() {
    // All control-changing callbacks run on this task, not on a second core.
    processIBusFrames();
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
        stopDomeMotion();
        stopVescMotors();
        WRITE_DOME_SERVO(0);
        return;
    }
    processTransmitterInputs();
    processDomeHoming();
    processDomeRotation();
    processManualHoloTilt();
    processRandomHolos();
    AnimatedEvent::process();
    processR2Macro();
    processVescDrive();
    sMarcSound.idle();
    processAudioQueue();
}
