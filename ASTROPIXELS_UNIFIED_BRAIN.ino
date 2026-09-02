/*
 * ═══════════════════════════════════════════════════════════════════════════════
 *                    R2-D2 UNIFIED ASTROPIXELS DOME BRAIN
 *         (ESP32 Master Brain: Persistent Moods, Macros, Servos & Audio)
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Board:          ESP32 Dev Module (30-pin, AstroPixels Motherboard)
 * Architecture:   Deterministic, Non-Blocking, Zero-Heap Allocation State Machine
 *
 * Implemented Subsystems & Engineering Protections:
 *   1. Hardware UART2 (115,200 baud / GPIO 16): Non-blocking i-Bus frame parser with CRC
 *   2. Hardware UART1 (9,600 baud / GPIO 17): Dedicated DFPlayer serial audio transmitter
 *   3. LEDC Hardware PWM (GPIO 4): 35kg 360° continuous dome servo with zero-creep sleep
 *   4. KY-003 Hall Effect Sensor (GPIO 5): Hardware interrupt for 0° dome homing
 *   5. PCA9685 I2C 16-Channel Controller: 6-servo HoloProjector posture & twitch engine
 *   6. FastLED WS2812 Controller: FLD1/2, RLD, FPSI, RPSI, and 3x HP LED cores
 *   7. Dual-Layer Personality Engine: 5 Persistent Moods + 5 Interruptible Macros
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <FastLED.h>

// ═══════════════════════════════════════════════════════════════════════════════
// 1. PIN DEFINITIONS & HARDWARE CONSTANTS
// ═══════════════════════════════════════════════════════════════════════════════

// Serial Communication Pins (Decoupled UARTs for distinct baud rates)
#define PIN_IBUS_RX         16    // Hardware Serial2 RX (115,200 baud): FlySky i-Bus
#define PIN_SOUND_TX        17    // Hardware Serial1 TX (9,600 baud): Body DFPlayer RX

// Dome Drive & Homing Pins
#define PIN_DOME_SERVO_PWM  4     // LEDC PWM: 35kg 360° Dome Continuous Servo
#define PIN_DOME_HALL_SENS  5     // Digital Input (Pull-up): KY-003 Hall Sensor in Dome

// I2C Pins (PCA9685 Servo Controller)
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22
#define PCA9685_I2C_ADDR    0x40

// FastLED WS2812 Pin Assignments
#define PIN_LED_FLD         15    // Front Logic Displays (daisy-chained FLD1 -> FLD2)
#define PIN_LED_RPSI        23    // Rear Process State Indicator
#define PIN_LED_FHP         25    // Front HoloProjector LED Core
#define PIN_LED_RHP         26    // Rear HoloProjector LED Core
#define PIN_LED_THP         27    // Top HoloProjector LED Core
#define PIN_LED_FPSI        32    // Front Process State Indicator
#define PIN_LED_RLD         33    // Rear Logic Display

// LED Pixel Counts
#define NUM_LEDS_FLD        80
#define NUM_LEDS_RLD        80
#define NUM_LEDS_FPSI       24
#define NUM_LEDS_RPSI       24
#define NUM_LEDS_HP         7     // 7 LEDs per HoloProjector core

// Static LED Buffers (Zero Dynamic Heap Allocation)
CRGB leds_fld[NUM_LEDS_FLD];
CRGB leds_rld[NUM_LEDS_RLD];
CRGB leds_fpsi[NUM_LEDS_FPSI];
CRGB leds_rpsi[NUM_LEDS_RPSI];
CRGB leds_fhp[NUM_LEDS_HP];
CRGB leds_rhp[NUM_LEDS_HP];
CRGB leds_thp[NUM_LEDS_HP];

// PCA9685 12-bit PWM Counts (50Hz = 20ms period, 4096 steps = 4.88us/step)
#define SERVO_MIN_PULSE     150   // ~0.73ms
#define SERVO_MID_PULSE     375   // ~1.83ms (Center)
#define SERVO_MAX_PULSE     600   // ~2.93ms

// 35kg Continuous Rotation Dome Servo (LEDC Channel 0, 16-bit resolution)
#define DOME_LEDC_CHANNEL   0
#define DOME_LEDC_FREQ      50    // 50Hz standard servo frequency
#define DOME_LEDC_RES       16    // 16-bit resolution (0-65535)
#define DOME_DUTY_STOP      4915  // ~1.50ms (Center Neutral)
#define DOME_DUTY_MAX_CW    6553  // ~2.00ms (Full CW)
#define DOME_DUTY_MAX_CCW   3276  // ~1.00ms (Full CCW)
#define DOME_CENTER_TRIM    0     // Fine-tune offset in duty ticks if servo drifts

// ═══════════════════════════════════════════════════════════════════════════════
// 2. STATE ENUMS & PROGMEM DATA STRUCTURES
// ═══════════════════════════════════════════════════════════════════════════════

// Layer 1: Persistent Ambient Moods (Governs continuous idle behavior)
enum PersistentMood : uint8_t {
  MOOD_NORMAL = 0,    // Iconic movie look, balanced chirps, periodic gentle wandering
  MOOD_HAPPY,         // Fast lively logics, frequent high-pitch whistling, rapid twitches
  MOOD_SASSY,         // Grumbling razzes, impatient dome shakes, orange/purple logics
  MOOD_SAD,           // Slow dim blue logics, dropped holos, mournful low whines
  MOOD_ALERT,         // Rapid red strobe, aggressive head sweeps, alarm klaxon pulses
  MOOD_COUNT
};

// Layer 2: Interruptible Interactive Macros (One-shot timed sequences)
enum ActiveMacro : uint8_t {
  MACRO_NONE = 0,
  MACRO_LEIA,         // Princess Leia speech + Pale green logics + Front HP blue flicker & down-tilt
  MACRO_CANTINA,      // Cantina Band song + Synchronized rhythmic marching lights & dance steps
  MACRO_SCREAM,       // Loud distress scream + Red flashing panic + Rapid erratic holo twitches
  MACRO_DISCO,        // Star Wars Disco theme + Full rainbow wave across all displays
  MACRO_FAINT,        // Electrical short-circuit + Spark audio + Dim flicker & total blackout
  MACRO_COUNT
};

// Flash-Stored Sound Range Definition
struct SoundRange {
  uint8_t startTrack;
  uint8_t endTrack;
};

// Mood Sound Pool Table (Stored in Flash Memory - Zero RAM Consumption)
const SoundRange PROGMEM MOOD_SOUND_POOLS[MOOD_COUNT] = {
  { 1, 20 },   // MOOD_NORMAL: Standard movie chirps (001 - 020)
  { 1, 20 },   // MOOD_HAPPY:  Upbeat whistling & melodies (001 - 020)
  { 21, 40 },  // MOOD_SASSY:  Annoyed razzes, grunts & scoffs (021 - 040)
  { 41, 60 },  // MOOD_SAD:    Mournful whines & low beeps (041 - 060)
  { 61, 80 }   // MOOD_ALERT:  Emergency alert chirps & beeps (061 - 080)
};

// Macro Track & Duration Configuration
struct MacroConfig {
  uint16_t trackNumber;
  uint32_t durationMs;
};

const MacroConfig PROGMEM MACRO_CONFIGS[MACRO_COUNT] = {
  { 0,   0     }, // MACRO_NONE
  { 109, 14000 }, // MACRO_LEIA:    Track 109, 14 seconds
  { 106, 18000 }, // MACRO_CANTINA: Track 106, 18 seconds
  { 102, 4500  }, // MACRO_SCREAM:  Track 102, 4.5 seconds
  { 110, 20000 }, // MACRO_DISCO:   Track 110, 20 seconds
  { 107, 5000  }  // MACRO_FAINT:   Track 107, 5 seconds
};

// ═══════════════════════════════════════════════════════════════════════════════
// 3. GLOBAL INSTANCES & RUNTIME VARIABLES
// ═══════════════════════════════════════════════════════════════════════════════

Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(PCA9685_I2C_ADDR);

// RC Channel Buffer (Channels 1 to 10 in microseconds: 1000us - 2000us)
uint16_t rc_channels[10] = {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500};
bool rc_connected = false;
unsigned long last_rc_packet_ms = 0;

// Channel Mapping Definitions (FlySky FS-i6X)
#define RC_CH_DOME_SPEED       2  // Left Stick Y (CH3): Autodome Speed & Chatter Frequency
#define RC_CH_DOME_STEER       3  // Left Stick X (CH4): Manual Dome Rotation Override
#define RC_CH_SPEED_MODE       4  // Switch SwB   (CH5): Transmitter Dual Rates
#define RC_CH_DRIVE_ENABLE     5  // Switch SwA   (CH6): Drive Safety Lockout
#define RC_CH_MOOD_SELECT      6  // Knob VrA     (CH7): Persistent Mood & Macro Selector (1-13)
#define RC_CH_MACRO_TRIGGER    7  // Switch SwC   (CH8): Macro Execution Trigger
#define RC_CH_HOLO_ENABLE      8  // Switch SwD   (CH9): Holo Random Motion Toggle

// System State Machine
PersistentMood current_mood = MOOD_NORMAL;
ActiveMacro current_macro = MACRO_NONE;
unsigned long macro_expire_time_ms = 0;

// Autodome Motion State
unsigned long next_autodome_start_ms = 0;
unsigned long autodome_stop_time_ms = 0;
int autodome_active_speed = 0;

// Dome Servo Creep Prevention Timer
unsigned long dome_stop_command_ms = 0;
bool dome_pulses_active = true;

// Dome Homing Sensor State
volatile bool is_dome_at_home = false;

// Audio Deduplication & Command Throttling
uint8_t last_played_track = 0;
unsigned long last_audio_cmd_ms = 0;

// ═══════════════════════════════════════════════════════════════════════════════
// 4. INTERRUPT SERVICE ROUTINES
// ═══════════════════════════════════════════════════════════════════════════════
void IRAM_ATTR onHallSensorChange() {
  is_dome_at_home = (digitalRead(PIN_DOME_HALL_SENS) == LOW);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 5. SYSTEM INITIALIZATION (SETUP)
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println(F("[SYSTEM] Booting AstroPixels Unified Dome Brain..."));

  // 1. Hardware UART2 for FlySky i-Bus RX (115,200 baud on GPIO 16)
  Serial2.begin(115200, SERIAL_8N1, PIN_IBUS_RX, -1);

  // 2. Hardware UART1 for Body DFPlayer TX (9,600 baud on GPIO 17)
  Serial1.begin(9600, SERIAL_8N1, -1, PIN_SOUND_TX);

  // 3. Initialize FastLED Lighting
  FastLED.addLeds<WS2812B, PIN_LED_FLD, GRB>(leds_fld, NUM_LEDS_FLD);
  FastLED.addLeds<WS2812B, PIN_LED_RLD, GRB>(leds_rld, NUM_LEDS_RLD);
  FastLED.addLeds<WS2812B, PIN_LED_FPSI, GRB>(leds_fpsi, NUM_LEDS_FPSI);
  FastLED.addLeds<WS2812B, PIN_LED_RPSI, GRB>(leds_rpsi, NUM_LEDS_RPSI);
  FastLED.addLeds<WS2812B, PIN_LED_FHP, GRB>(leds_fhp, NUM_LEDS_HP);
  FastLED.addLeds<WS2812B, PIN_LED_RHP, GRB>(leds_rhp, NUM_LEDS_HP);
  FastLED.addLeds<WS2812B, PIN_LED_THP, GRB>(leds_thp, NUM_LEDS_HP);
  FastLED.setBrightness(160);

  // 4. Initialize PCA9685 I2C 6-Servo Driver
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  pwm.begin();
  pwm.setPWMFreq(50);
  centerAllHoloServos();

  // 5. Initialize LEDC Hardware Timer for 35kg Continuous Dome Servo
  ledcSetup(DOME_LEDC_CHANNEL, DOME_LEDC_FREQ, DOME_LEDC_RES);
  ledcAttachPin(PIN_DOME_SERVO_PWM, DOME_LEDC_CHANNEL);
  setDomeServoSpeed(0); // Zero velocity stop

  // 6. Initialize KY-003 Hall Homing Sensor
  pinMode(PIN_DOME_HALL_SENS, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_DOME_HALL_SENS), onHallSensorChange, CHANGE);

  // 7. Play System Startup Sound & Set Default Normal Mood
  delay(300);
  playTrack(255); // 255_startup.mp3
  setPersistentMood(MOOD_NORMAL);

  Serial.println(F("[SYSTEM] AstroPixels Dome Brain Online & Ready."));
}

// ═══════════════════════════════════════════════════════════════════════════════
// 6. MAIN DETERMINISTIC EXECUTION LOOP (~200Hz)
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
  processIBusFrames();
  processTransmitterInputs();
  processMacroTimers();
  processDomeRotation();
  processHoloServos();
  processAstroPixelsLighting();

  FastLED.show();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 7. ROBUST NON-BLOCKING FLYSKY i-BUS DECODER & FAILSAFE
// ═══════════════════════════════════════════════════════════════════════════════
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
        ibus_idx = 0; // Invalid header, resync
      }
    } else {
      ibus_buffer[ibus_idx++] = byte_in;

      if (ibus_idx >= 32) {
        // Full 32-byte frame received -> Verify 16-bit Checksum
        uint16_t checksum_calc = 0xFFFF;
        for (uint8_t i = 0; i < 30; i++) {
          checksum_calc -= ibus_buffer[i];
        }

        uint16_t checksum_frame = ibus_buffer[30] | (ibus_buffer[31] << 8);

        if (checksum_calc == checksum_frame) {
          // Valid frame -> Extract 10 RC Channels (little-endian microseconds)
          for (uint8_t ch = 0; ch < 10; ch++) {
            uint16_t raw_val = ibus_buffer[2 + ch * 2] | (ibus_buffer[3 + ch * 2] << 8);
            if (raw_val >= 900 && raw_val <= 2100) {
              rc_channels[ch] = raw_val;
            }
          }
          rc_connected = true;
          last_rc_packet_ms = millis();
        }
        ibus_idx = 0; // Ready for next frame
      }
    }
  }

  // Failsafe: Stop all motion if transmitter signal lost for >500ms
  if (millis() - last_rc_packet_ms > 500) {
    rc_connected = false;
    setDomeServoSpeed(0);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 8. TRANSMITTER INPUT PROCESSING & MACRO TRIGGERING
// ═══════════════════════════════════════════════════════════════════════════════
void processTransmitterInputs() {
  if (!rc_connected) return;

  // 1. Evaluate Persistent Mood from Rotary Knob VrA (CH7: 1000us - 2000us)
  uint16_t knob_us = rc_channels[RC_CH_MOOD_SELECT];
  PersistentMood selected_mood = MOOD_NORMAL;

  if (knob_us < 1200) {
    selected_mood = MOOD_NORMAL;
  } else if (knob_us < 1400) {
    selected_mood = MOOD_HAPPY;
  } else if (knob_us < 1600) {
    selected_mood = MOOD_SASSY;
  } else if (knob_us < 1800) {
    selected_mood = MOOD_SAD;
  } else {
    selected_mood = MOOD_ALERT;
  }

  if (selected_mood != current_mood && current_macro == MACRO_NONE) {
    setPersistentMood(selected_mood);
  }

  // 2. Detect Edge-Trigger on Switch SwC (CH8) to fire Interactive Macros
  static uint16_t prev_trigger_us = 1000;
  uint16_t current_trigger_us = rc_channels[RC_CH_MACRO_TRIGGER];

  // Trigger on switch flip from UP (<1400us) to DOWN (>1700us)
  if (current_trigger_us > 1700 && prev_trigger_us <= 1400) {
    uint8_t dial_index = map(knob_us, 1000, 2000, 1, 13);
    triggerMacroFromDial(dial_index);
  }
  prev_trigger_us = current_trigger_us;
}

void triggerMacroFromDial(uint8_t dial_index) {
  switch (dial_index) {
    case 1:  // Force quiet reset
      current_macro = MACRO_NONE;
      setPersistentMood(MOOD_NORMAL);
      playTrack(11); // 011_quiet.mp3
      centerAllHoloServos();
      break;

    case 4:  // Alarm / Scream Macro
      startMacro(MACRO_SCREAM);
      break;

    case 5:  // Cantina Band Macro
      startMacro(MACRO_CANTINA);
      break;

    case 6:  // Princess Leia Hologram Macro
      startMacro(MACRO_LEIA);
      break;

    case 7:  // Star Wars Disco Macro
      startMacro(MACRO_DISCO);
      break;

    case 8:  // Short Circuit / Faint Macro
      startMacro(MACRO_FAINT);
      break;

    default: // Manual random chatter trigger
      playThemedChatter();
      break;
  }
}

void startMacro(ActiveMacro macro) {
  if (macro == MACRO_NONE || macro >= MACRO_COUNT) return;

  current_macro = macro;
  MacroConfig config;
  memcpy_P(&config, &MACRO_CONFIGS[macro], sizeof(MacroConfig));

  macro_expire_time_ms = millis() + config.durationMs;
  playTrack(config.trackNumber);

  // Macro-specific servo staging
  if (macro == MACRO_LEIA) {
    pwm.setPWM(0, 0, SERVO_MID_PULSE);       // Front HP Pan Center
    pwm.setPWM(1, 0, SERVO_MIN_PULSE + 80);  // Front HP Tilt Downward 35°
  }

  Serial.print(F("[MACRO] Started Macro ID: "));
  Serial.println(macro);
}

void processMacroTimers() {
  if (current_macro != MACRO_NONE && millis() >= macro_expire_time_ms) {
    Serial.print(F("[MACRO] Finished Macro ID: "));
    Serial.println(current_macro);

    current_macro = MACRO_NONE;
    centerAllHoloServos();
  }
}

void setPersistentMood(PersistentMood mood) {
  current_mood = mood;
  Serial.print(F("[MOOD] Active Mood Changed To: "));
  Serial.println(mood);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 9. DOME ROTATION & DYNAMIC AUTODOME ENGINE (With Anti-Creep Sleep)
// ═══════════════════════════════════════════════════════════════════════════════
void processDomeRotation() {
  if (!rc_connected) {
    setDomeServoSpeed(0);
    return;
  }

  int16_t stick_steer = rc_channels[RC_CH_DOME_STEER]; // 1000us - 2000us

  // Manual Override Deadband (1460us to 1540us)
  if (stick_steer < 1460 || stick_steer > 1540) {
    // Manual stick drive overrides autodome
    autodome_active_speed = 0;
    int speed_percent = map(stick_steer, 1000, 2000, -100, 100);
    setDomeServoSpeed(speed_percent);
  } else {
    // Stick is centered -> Run autonomous autodome state machine
    runAutodomeStateMachine();
  }
}

void runAutodomeStateMachine() {
  unsigned long now = millis();

  // If currently executing an autonomous twitch move
  if (autodome_active_speed != 0) {
    if (now >= autodome_stop_time_ms) {
      setDomeServoSpeed(0);
      autodome_active_speed = 0;

      // Schedule next move based on active mood dynamics
      uint32_t min_wait = 3000;
      uint32_t max_wait = 8000;
      if (current_mood == MOOD_ALERT) { min_wait = 1000; max_wait = 2500; }
      if (current_mood == MOOD_SAD)   { min_wait = 8000; max_wait = 16000; }
      if (current_mood == MOOD_HAPPY) { min_wait = 2000; max_wait = 5000; }

      next_autodome_start_ms = now + random(min_wait, max_wait);
    }
  } else {
    // Ready for next autonomous twitch
    if (now >= next_autodome_start_ms) {
      int base_speed = 35;
      uint16_t min_dur = 400;
      uint16_t max_dur = 1100;

      if (current_mood == MOOD_ALERT) { base_speed = 60; min_dur = 250; max_dur = 600; }
      if (current_mood == MOOD_SAD)   { base_speed = 20; min_dur = 600; max_dur = 1500; }
      if (current_mood == MOOD_HAPPY) { base_speed = 45; min_dur = 350; max_dur = 900; }

      autodome_active_speed = (random(base_speed - 10, base_speed + 15)) * (random(2) == 0 ? 1 : -1);
      setDomeServoSpeed(autodome_active_speed);
      autodome_stop_time_ms = now + random(min_dur, max_dur);

      // Trigger mood-matched background chatter
      if (random(10) > 4) {
        playThemedChatter();
      }
    }
  }
}

void setDomeServoSpeed(int speed_percent) {
  // speed_percent: -100 (Full CCW) to +100 (Full CW), 0 = Stop
  speed_percent = constrain(speed_percent, -100, 100);

  if (speed_percent == 0) {
    if (dome_pulses_active) {
      if (dome_stop_command_ms == 0) {
        dome_stop_command_ms = millis();
        ledcWrite(DOME_LEDC_CHANNEL, DOME_DUTY_STOP + DOME_CENTER_TRIM);
      } else if (millis() - dome_stop_command_ms > 150) {
        // Zero-Creep Sleep: Disable pulses after 150ms settling window
        ledcWrite(DOME_LEDC_CHANNEL, 0);
        dome_pulses_active = false;
      }
    }
  } else {
    dome_pulses_active = true;
    dome_stop_command_ms = 0;
    uint32_t duty = map(speed_percent, -100, 100, DOME_DUTY_MAX_CCW, DOME_DUTY_MAX_CW);
    ledcWrite(DOME_LEDC_CHANNEL, duty + DOME_CENTER_TRIM);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 10. 6-CHANNEL HOLOPROJECTOR SERVO POSTURE CONTROLLER (PCA9685)
// ═══════════════════════════════════════════════════════════════════════════════
void centerAllHoloServos() {
  for (uint8_t ch = 0; ch < 6; ch++) {
    pwm.setPWM(ch, 0, SERVO_MID_PULSE);
  }
}

void processHoloServos() {
  if (current_macro != MACRO_NONE) return; // Servos managed by macro

  static unsigned long next_twitch_ms = 0;
  if (millis() < next_twitch_ms) return;

  switch (current_mood) {
    case MOOD_SAD:
      // Drop all 3 Holos downward (dejected posture)
      pwm.setPWM(1, 0, SERVO_MIN_PULSE + 70); // Front Tilt Down
      pwm.setPWM(3, 0, SERVO_MIN_PULSE + 70); // Rear Tilt Down
      pwm.setPWM(5, 0, SERVO_MIN_PULSE + 70); // Top Tilt Down
      next_twitch_ms = millis() + 4000;
      break;

    case MOOD_ALERT: {
      // Rapid erratic twitches across random servos
      uint8_t target_servo = random(6);
      uint16_t pulse = random(SERVO_MIN_PULSE + 40, SERVO_MAX_PULSE - 40);
      pwm.setPWM(target_servo, 0, pulse);
      next_twitch_ms = millis() + random(300, 900);
      break;
    }

    case MOOD_HAPPY:
    case MOOD_NORMAL:
    default: {
      // Organic ambient wandering
      uint8_t target_servo = random(6);
      uint16_t pulse = random(SERVO_MIN_PULSE + 80, SERVO_MAX_PULSE - 80);
      pwm.setPWM(target_servo, 0, pulse);
      next_twitch_ms = millis() + random(1500, 4500);
      break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 11. DFPLAYER AUDIO TRANSMITTER (Dedicated Serial1 @ 9600 Baud)
// ═══════════════════════════════════════════════════════════════════════════════
void playTrack(uint16_t track_num) {
  // Enforce 100ms hardware pacing between consecutive DFPlayer commands
  if (millis() - last_audio_cmd_ms < 100) {
    delay(50);
  }

  // DFPlayer Standard Command Packet: 7E FF 06 03 00 [HIGH] [LOW] [CHECKSUM_H] [CHECKSUM_L] EF
  uint8_t cmd[10] = {0x7E, 0xFF, 0x06, 0x03, 0x00, (uint8_t)(track_num >> 8), (uint8_t)(track_num & 0xFF), 0x00, 0x00, 0xEF};
  uint16_t sum = 0;
  for (uint8_t i = 1; i < 7; i++) sum += cmd[i];
  sum = -sum;
  cmd[7] = (uint8_t)(sum >> 8);
  cmd[8] = (uint8_t)(sum & 0xFF);

  Serial1.write(cmd, 10);
  last_played_track = (uint8_t)track_num;
  last_audio_cmd_ms = millis();

  Serial.print(F("[AUDIO] Transmitted DFPlayer Track (9600 baud): "));
  Serial.println(track_num);
}

void playThemedChatter() {
  SoundRange pool;
  memcpy_P(&pool, &MOOD_SOUND_POOLS[current_mood], sizeof(SoundRange));

  uint8_t track = random(pool.startTrack, pool.endTrack + 1);

  // Smart deduplication: Avoid playing the identical track twice consecutively
  if (track == last_played_track && pool.endTrack > pool.startTrack) {
    track = (track >= pool.endTrack) ? pool.startTrack : track + 1;
  }

  playTrack(track);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 12. ASTROPIXELS LIGHTING ENGINE (FastLED)
// ═══════════════════════════════════════════════════════════════════════════════
void processAstroPixelsLighting() {
  static uint8_t hue = 0;
  hue++;

  // 1. Handle Active Macro Lighting Overrides
  if (current_macro != MACRO_NONE) {
    switch (current_macro) {
      case MACRO_LEIA:
        fill_solid(leds_rld, NUM_LEDS_RLD, CRGB(30, 180, 80));   // Pale Green
        fill_solid(leds_fld, NUM_LEDS_FLD, CRGB(30, 180, 80));
        fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB(20, 200, 100));
        fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB(20, 200, 100));
        fill_solid(leds_fhp, NUM_LEDS_HP, (random(10) > 3) ? CRGB(20, 80, 255) : CRGB(5, 20, 80)); // Hologram Flicker
        return;

      case MACRO_SCREAM:
        fill_solid(leds_rld, NUM_LEDS_RLD, (millis() % 250 < 125) ? CRGB::Red : CRGB::Black);
        fill_solid(leds_fld, NUM_LEDS_FLD, (millis() % 250 < 125) ? CRGB::Red : CRGB::Black);
        fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB::Red);
        fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB::Red);
        fill_solid(leds_fhp, NUM_LEDS_HP, CRGB::Red);
        return;

      case MACRO_DISCO:
        fill_rainbow(leds_rld, NUM_LEDS_RLD, hue, 7);
        fill_rainbow(leds_fld, NUM_LEDS_FLD, hue, 7);
        fill_rainbow(leds_fpsi, NUM_LEDS_FPSI, hue + 64, 10);
        fill_rainbow(leds_rpsi, NUM_LEDS_RPSI, hue + 128, 10);
        fill_rainbow(leds_fhp, NUM_LEDS_HP, hue, 15);
        fill_rainbow(leds_rhp, NUM_LEDS_HP, hue + 80, 15);
        fill_rainbow(leds_thp, NUM_LEDS_HP, hue + 160, 15);
        return;

      case MACRO_FAINT:
        fill_solid(leds_rld, NUM_LEDS_RLD, (random(10) > 7) ? CRGB(10, 20, 50) : CRGB::Black);
        fill_solid(leds_fld, NUM_LEDS_FLD, (random(10) > 7) ? CRGB(10, 20, 50) : CRGB::Black);
        fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB::Black);
        fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB::Black);
        return;

      default:
        break;
    }
  }

  // 2. Handle Persistent Mood Lighting
  switch (current_mood) {
    case MOOD_ALERT:
      fill_solid(leds_rld, NUM_LEDS_RLD, (millis() % 400 < 200) ? CRGB(200, 0, 0) : CRGB(40, 0, 0));
      fill_solid(leds_fld, NUM_LEDS_FLD, (millis() % 400 < 200) ? CRGB(200, 0, 0) : CRGB(40, 0, 0));
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB::Red);
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB::Red);
      fill_solid(leds_fhp, NUM_LEDS_HP, CRGB(180, 0, 0));
      break;

    case MOOD_SAD:
      // Dim, slow-moving deep blue
      for (uint8_t i = 0; i < NUM_LEDS_RLD; i++) {
        if (random(40) == 0) leds_rld[i] = CRGB(0, 10, 60);
      }
      for (uint8_t i = 0; i < NUM_LEDS_FLD; i++) {
        if (random(40) == 0) leds_fld[i] = CRGB(0, 10, 60);
      }
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB(0, 20, 80));
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB(0, 20, 80));
      break;

    case MOOD_SASSY:
      // Orange & Purple marching logics
      for (uint8_t i = 0; i < NUM_LEDS_RLD; i++) {
        if (random(20) == 0) leds_rld[i] = (random(2) == 0) ? CRGB(255, 80, 0) : CRGB(160, 0, 200);
      }
      for (uint8_t i = 0; i < NUM_LEDS_FLD; i++) {
        if (random(20) == 0) leds_fld[i] = (random(2) == 0) ? CRGB(255, 80, 0) : CRGB(160, 0, 200);
      }
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB(255, 80, 0));
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB(160, 0, 200));
      break;

    case MOOD_HAPPY:
      // High-speed energetic multi-color march
      for (uint8_t i = 0; i < NUM_LEDS_RLD; i++) {
        if (random(12) == 0) leds_rld[i] = (random(2) == 0) ? CRGB::Blue : CRGB::Green;
      }
      for (uint8_t i = 0; i < NUM_LEDS_FLD; i++) {
        if (random(12) == 0) leds_fld[i] = (random(2) == 0) ? CRGB::Blue : CRGB::Cyan;
      }
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB::Green);
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB::Blue);
      break;

    case MOOD_NORMAL:
    default:
      // Standard Film Look: RLD Red/Blue random march, FLD Blue/White march
      for (uint8_t i = 0; i < NUM_LEDS_RLD; i++) {
        if (random(25) == 0) leds_rld[i] = (random(2) == 0) ? CRGB::Red : CRGB::Blue;
      }
      for (uint8_t i = 0; i < NUM_LEDS_FLD; i++) {
        if (random(25) == 0) leds_fld[i] = (random(2) == 0) ? CRGB::Blue : CRGB::White;
      }
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB::Blue);
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB::Red);
      break;
  }
}
