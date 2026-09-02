/*
 * ═══════════════════════════════════════════════════════════════════════════════
 *                    R2-D2 UNIFIED ASTROPIXELS DOME BRAIN
 *          (ESP32 + PCA9685 + i-Bus + DFPlayer + 360° Dome Servo)
 * ═══════════════════════════════════════════════════════════════════════════════
 *
 * Board:       ESP32 Dev Module (30-pin)
 * Author:      R2 Droid Builder
 * Architecture: All-in-One Dome Master Controller
 *
 * Subsystems Handled on this Single ESP32:
 *   1. FlySky i-Bus Decoder: Reads all 10 transmitter channels over Slip Ring Ch 3
 *   2. AstroPixels WS2812 Lights: FLD1/2, RLD, FPSI, RPSI, and 3x HP LED rings
 *   3. 6x HoloProjector Servos: Controlled via PCA9685 I2C Driver (GPIO 21/22)
 *   4. 35kg 360° Dome Servo: PWM speed/direction down Slip Ring Ch 5 (GPIO 4)
 *   5. Dome Homing: KY-003 Hall sensor on dome base (GPIO 5 interrupt)
 *   6. Body Sound Engine: Serial commands down Slip Ring Ch 4 to DFPlayer in body
 *   7. Personality Engine: Coordinated sound, light, holo, and dome macros
 *
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <FastLED.h>

// ═══════════════════════════════════════════════════════════════════════════════
// 1. PIN DEFINITIONS (AstroPixels Breakout Board)
// ═══════════════════════════════════════════════════════════════════════════════

// Serial & RC Input Pins
#define PIN_IBUS_RX         16    // Hardware Serial2 RX: From Slip Ring Ch 3 (FlySky iBus)
#define PIN_SOUND_TX        17    // Hardware Serial2 TX: To Slip Ring Ch 4 (Body DFPlayer RX)

// Dome Drive & Homing Pins
#define PIN_DOME_SERVO_PWM  4     // LEDC PWM Out: To Slip Ring Ch 5 (35kg 360° Dome Servo)
#define PIN_DOME_HALL_SENS  5     // GPIO Input (Pullup): From KY-003 Hall Sensor in Dome

// I2C Pins (PCA9685 6-Servo Driver)
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22

// AstroPixels WS2812 Light Data Pins
#define PIN_LED_RLD         33    // Rear Logic Display
#define PIN_LED_FLD         15    // Front Logic Displays (daisy-chained FLD1 -> FLD2)
#define PIN_LED_FPSI        32    // Front Process State Indicator
#define PIN_LED_RPSI        23    // Rear Process State Indicator
#define PIN_LED_FHP         25    // Front HoloProjector LED Board
#define PIN_LED_RHP         26    // Rear HoloProjector LED Board
#define PIN_LED_THP         27    // Top HoloProjector LED Board

// LED Pixel Counts
#define NUM_LEDS_RLD        80
#define NUM_LEDS_FLD        80
#define NUM_LEDS_FPSI       24
#define NUM_LEDS_RPSI       24
#define NUM_LEDS_HP         7     // 7 LEDs per HoloProjector core

CRGB leds_rld[NUM_LEDS_RLD];
CRGB leds_fld[NUM_LEDS_FLD];
CRGB leds_fpsi[NUM_LEDS_FPSI];
CRGB leds_rpsi[NUM_LEDS_RPSI];
CRGB leds_fhp[NUM_LEDS_HP];
CRGB leds_rhp[NUM_LEDS_HP];
CRGB leds_thp[NUM_LEDS_HP];

// PCA9685 Servo Driver Instance (Default Address 0x40)
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);

// Servo Pulse Range for PCA9685 (at 50Hz, 12-bit = 4096 counts)
#define SERVO_MIN_PULSE     150   // ~0.75ms
#define SERVO_MAX_PULSE     600   // ~2.25ms
#define SERVO_MID_PULSE     375   // ~1.50ms (Center)

// 360° Continuous Dome Servo PWM Constants (ESP32 LEDC Channel 0)
#define DOME_PWM_CHANNEL    0
#define DOME_PWM_FREQ       50    // 50Hz standard servo frequency
#define DOME_PWM_RES        16    // 16-bit resolution (0-65535)
#define DOME_STOP_DUTY      4915  // ~1.5ms pulse at 50Hz 16-bit
#define DOME_MAX_CW         6553  // ~2.0ms
#define DOME_MAX_CCW        3276  // ~1.0ms

// ═══════════════════════════════════════════════════════════════════════════════
// 2. RC CHANNELS & STATE VARIABLES
// ═══════════════════════════════════════════════════════════════════════════════

uint16_t rc_channels[10];         // Channels 1-10 (1000us - 2000us)
bool rc_connected = false;
unsigned long last_rc_packet = 0;

// FlySky Channel Mappings
#define CH_DOME_SPEED       2     // Left Stick Y (CH3): Autodome speed / frequency
#define CH_DOME_STEER       3     // Left Stick X (CH4): Manual Dome Rotation
#define CH_SPEED_MODE       4     // SwB (CH5): Drive Speed Rate
#define CH_DRIVE_ENABLE     5     // SwA (CH6): Safety Lockout
#define CH_ROUTINE_SELECT   6     // VrA Knob (CH7): MarcDuino Routine Dial (1-13)
#define CH_ROUTINE_TRIGGER  7     // SwC Switch (CH8): Trigger Selected Routine
#define CH_HOLO_TWITCH      8     // SwD (CH9): Enable/Disable Holo Random Twitch

// Autodome State
bool autodome_enabled = true;
unsigned long next_autodome_time = 0;
unsigned long autodome_stop_time = 0;
int autodome_current_speed = 0;

// Dome Homing State
volatile bool is_dome_at_home = false;

// Active Moods & Macros
enum DroidMood {
  MOOD_NORMAL,
  MOOD_HAPPY,
  MOOD_SAD,
  MOOD_ALARM,
  MOOD_LEIA,
  MOOD_CANTINA,
  MOOD_DISCO,
  MOOD_FAINT
};
DroidMood current_mood = MOOD_NORMAL;
unsigned long macro_end_time = 0;

// ═══════════════════════════════════════════════════════════════════════════════
// 3. KY-003 HALL EFFECT HOMING INTERRUPT
// ═══════════════════════════════════════════════════════════════════════════════
void IRAM_ATTR onHallSensorTrigger() {
  is_dome_at_home = (digitalRead(PIN_DOME_HALL_SENS) == LOW);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4. SETUP
// ═══════════════════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println(F("Initializing R2-D2 Unified Dome Brain..."));

  // 1. Initialize Hardware Serial2 for i-Bus RX and Sound TX
  Serial2.begin(115200, SERIAL_8N1, PIN_IBUS_RX, PIN_SOUND_TX);

  // 2. Initialize FastLED Lighting
  FastLED.addLeds<WS2812B, PIN_LED_RLD, GRB>(leds_rld, NUM_LEDS_RLD);
  FastLED.addLeds<WS2812B, PIN_LED_FLD, GRB>(leds_fld, NUM_LEDS_FLD);
  FastLED.addLeds<WS2812B, PIN_LED_FPSI, GRB>(leds_fpsi, NUM_LEDS_FPSI);
  FastLED.addLeds<WS2812B, PIN_LED_RPSI, GRB>(leds_rpsi, NUM_LEDS_RPSI);
  FastLED.addLeds<WS2812B, PIN_LED_FHP, GRB>(leds_fhp, NUM_LEDS_HP);
  FastLED.addLeds<WS2812B, PIN_LED_RHP, GRB>(leds_rhp, NUM_LEDS_HP);
  FastLED.addLeds<WS2812B, PIN_LED_THP, GRB>(leds_thp, NUM_LEDS_HP);
  FastLED.setBrightness(160);

  // 3. Initialize PCA9685 6-Servo Driver
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  pwm.begin();
  pwm.setPWMFreq(50);
  centerAllHoloServos();

  // 4. Initialize 360° Continuous Dome Rotation PWM (LEDC)
  ledcSetup(DOME_PWM_CHANNEL, DOME_PWM_FREQ, DOME_PWM_RES);
  ledcAttachPin(PIN_DOME_SERVO_PWM, DOME_PWM_CHANNEL);
  setDomeServoSpeed(0); // Stop

  // 5. Initialize Hall Effect Sensor
  pinMode(PIN_DOME_HALL_SENS, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_DOME_HALL_SENS), onHallSensorTrigger, CHANGE);

  // Startup Chime & Lights
  playTrack(255); // 255_startup.mp3
  setMood(MOOD_NORMAL);
  Serial.println(F("[OK] Unified Dome Brain Ready."));
}

// ═══════════════════════════════════════════════════════════════════════════════
// 5. MAIN LOOP
// ═══════════════════════════════════════════════════════════════════════════════
void loop() {
  readIBusStream();
  processDomeRotation();
  processHoloTwitches();
  processMacrosAndRoutines();
  updateAstroPixelsLights();
  FastLED.show();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 6. FLYSKY i-BUS DECODER
// ═══════════════════════════════════════════════════════════════════════════════
void readIBusStream() {
  while (Serial2.available() >= 32) {
    if (Serial2.read() == 0x20 && Serial2.peek() == 0x40) {
      Serial2.read(); // Consume 0x40
      uint8_t buf[28];
      Serial2.readBytes(buf, 28);
      
      for (int i = 0; i < 10; i++) {
        rc_channels[i] = buf[i * 2] | (buf[i * 2 + 1] << 8);
      }
      rc_connected = true;
      last_rc_packet = millis();
    }
  }

  // Failsafe timeout after 500ms signal loss
  if (millis() - last_rc_packet > 500) {
    rc_connected = false;
    setDomeServoSpeed(0);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 7. DOME ROTATION & AUTODOME ENGINE
// ═══════════════════════════════════════════════════════════════════════════════
void processDomeRotation() {
  if (!rc_connected) {
    setDomeServoSpeed(0);
    return;
  }

  int stick_x = rc_channels[CH_DOME_STEER]; // 1000 - 2000us

  // Check manual stick movement (outside 1460 - 1540 deadband)
  if (stick_x < 1460 || stick_x > 1540) {
    autodome_enabled = false;
    int speed = map(stick_x, 1000, 2000, -100, 100);
    setDomeServoSpeed(speed);
  } else {
    // Stick is centered -> Run autonomous autodome twitches
    autodome_enabled = true;
    runAutodomeLogic();
  }
}

void runAutodomeLogic() {
  unsigned long now = millis();

  // If currently moving during an autodome twitch
  if (autodome_current_speed != 0) {
    if (now >= autodome_stop_time) {
      setDomeServoSpeed(0);
      autodome_current_speed = 0;
      next_autodome_time = now + random(3000, 8000); // Wait 3-8 seconds
    }
  } else {
    // Ready for next autonomous move
    if (now >= next_autodome_time) {
      autodome_current_speed = random(20, 50) * (random(2) == 0 ? 1 : -1);
      setDomeServoSpeed(autodome_current_speed);
      autodome_stop_time = now + random(400, 1200); // Twitch for 0.4 - 1.2s
      
      // Random chatty sound
      if (random(10) > 4) {
        playRandomChatter();
      }
    }
  }
}

void setDomeServoSpeed(int speed_percent) {
  // speed_percent: -100 (Full CCW) to +100 (Full CW), 0 = Stopped
  speed_percent = constrain(speed_percent, -100, 100);
  uint32_t duty = map(speed_percent, -100, 100, DOME_MAX_CCW, DOME_MAX_CW);
  ledcWrite(DOME_PWM_CHANNEL, duty);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 8. 6-CHANNEL HOLOPROJECTOR SERVO CONTROLLER (PCA9685)
// ═══════════════════════════════════════════════════════════════════════════════
void centerAllHoloServos() {
  for (int ch = 0; ch < 6; ch++) {
    pwm.setPWM(ch, 0, SERVO_MID_PULSE);
  }
}

void processHoloTwitches() {
  static unsigned long next_holo_twitch = 0;
  if (millis() < next_holo_twitch) return;

  // S1 switch or random twitch mode
  if (current_mood == MOOD_NORMAL || current_mood == MOOD_HAPPY) {
    int target_servo = random(6);
    int target_angle = random(SERVO_MIN_PULSE + 50, SERVO_MAX_PULSE - 50);
    pwm.setPWM(target_servo, 0, target_angle);
    next_holo_twitch = millis() + random(1000, 3500);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 9. SOUND TRIGGER (DFPlayer Mini via Serial down to Body)
// ═══════════════════════════════════════════════════════════════════════════════
void playTrack(uint16_t track_num) {
  // DFPlayer 7E FF 06 03 00 [HIGH] [LOW] [CHECKSUM] EF
  uint8_t cmd[10] = {0x7E, 0xFF, 0x06, 0x03, 0x00, (uint8_t)(track_num >> 8), (uint8_t)(track_num & 0xFF), 0x00, 0x00, 0xEF};
  uint16_t sum = 0;
  for (int i = 1; i < 7; i++) sum += cmd[i];
  sum = -sum;
  cmd[7] = (uint8_t)(sum >> 8);
  cmd[8] = (uint8_t)(sum & 0xFF);
  Serial2.write(cmd, 10);
  
  Serial.print(F("[AUDIO] Fired Track: "));
  Serial.println(track_num);
}

void playRandomChatter() {
  playTrack(random(1, 6)); // Tracks 001 - 005
}

// ═══════════════════════════════════════════════════════════════════════════════
// 10. MACROS & PERSONALITY ROUTINES
// ═══════════════════════════════════════════════════════════════════════════════
void processMacrosAndRoutines() {
  static int prev_trigger_state = 1000;
  int trigger_switch = rc_channels[CH_ROUTINE_TRIGGER]; // SwC: ~1000 (UP), 1500 (MID), 2000 (DOWN)
  int knob_val = rc_channels[CH_ROUTINE_SELECT];        // VrA: 1000 - 2000us

  // Detect switch flip from UP -> DOWN
  if (trigger_switch > 1700 && prev_trigger_state <= 1700) {
    int routine_index = map(knob_val, 1000, 2000, 1, 13);
    executeRoutine(routine_index);
  }
  prev_trigger_state = trigger_switch;

  // Auto-reset mood after macro finishes
  if (macro_end_time > 0 && millis() >= macro_end_time) {
    setMood(MOOD_NORMAL);
    macro_end_time = 0;
  }
}

void executeRoutine(int index) {
  Serial.print(F("[MACRO] Triggered Routine #"));
  Serial.println(index);

  switch (index) {
    case 1:  // Quiet Reset
      setMood(MOOD_NORMAL);
      playTrack(11);
      centerAllHoloServos();
      break;

    case 2:  // Full Awake
      setMood(MOOD_HAPPY);
      playTrack(12);
      break;

    case 4:  // Scream / Alarm
      setMood(MOOD_ALARM);
      playTrack(2); // 002_scream.mp3
      macro_end_time = millis() + 4500;
      break;

    case 5:  // Cantina
      setMood(MOOD_CANTINA);
      playTrack(6); // 006_cantina.mp3
      macro_end_time = millis() + 18000;
      break;

    case 6:  // Princess Leia Message
      setMood(MOOD_LEIA);
      playTrack(9); // 009_leia.mp3
      // Aim Front HP downward for hologram projection
      pwm.setPWM(0, 0, SERVO_MID_PULSE); // Center pan
      pwm.setPWM(1, 0, SERVO_MIN_PULSE + 80); // Tilt down
      macro_end_time = millis() + 14000;
      break;

    case 7:  // Disco
      setMood(MOOD_DISCO);
      playTrack(10); // 010_disco.mp3
      macro_end_time = millis() + 20000;
      break;

    case 8:  // Short Circuit / Faint
      setMood(MOOD_FAINT);
      playTrack(7); // 007_short_circuit.mp3
      macro_end_time = millis() + 5000;
      break;

    default:
      playRandomChatter();
      break;
  }
}

void setMood(DroidMood mood) {
  current_mood = mood;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 11. ASTROPIXELS LIGHT ANIMATION ENGINE
// ═══════════════════════════════════════════════════════════════════════════════
void updateAstroPixelsLights() {
  static uint8_t hue = 0;
  hue++;

  switch (current_mood) {
    case MOOD_ALARM:
      // Flashing Red Alert on Logics, PSIs, and HPs
      fill_solid(leds_rld, NUM_LEDS_RLD, (millis() % 300 < 150) ? CRGB::Red : CRGB::Black);
      fill_solid(leds_fld, NUM_LEDS_FLD, (millis() % 300 < 150) ? CRGB::Red : CRGB::Black);
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB::Red);
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB::Red);
      fill_solid(leds_fhp, NUM_LEDS_HP, CRGB::Red);
      break;

    case MOOD_LEIA:
      // Pale Green Logics + Hologram Blue Front HP Flicker
      fill_solid(leds_rld, NUM_LEDS_RLD, CRGB(30, 180, 80));
      fill_solid(leds_fld, NUM_LEDS_FLD, CRGB(30, 180, 80));
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB(20, 200, 100));
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB(20, 200, 100));
      // Hologram Flicker
      fill_solid(leds_fhp, NUM_LEDS_HP, (random(10) > 3) ? CRGB(20, 80, 255) : CRGB(5, 20, 80));
      break;

    case MOOD_DISCO:
      // Rainbow Party on all displays
      fill_rainbow(leds_rld, NUM_LEDS_RLD, hue, 7);
      fill_rainbow(leds_fld, NUM_LEDS_FLD, hue, 7);
      fill_rainbow(leds_fpsi, NUM_LEDS_FPSI, hue + 64, 10);
      fill_rainbow(leds_rpsi, NUM_LEDS_RPSI, hue + 128, 10);
      fill_rainbow(leds_fhp, NUM_LEDS_HP, hue, 15);
      fill_rainbow(leds_rhp, NUM_LEDS_HP, hue + 80, 15);
      fill_rainbow(leds_thp, NUM_LEDS_HP, hue + 160, 15);
      break;

    case MOOD_NORMAL:
    default:
      // Classic Movie R2 Idle Animation
      // RLD: Red/Blue randomized marching logic
      for (int i = 0; i < NUM_LEDS_RLD; i++) {
        if (random(20) == 0) leds_rld[i] = (random(2) == 0) ? CRGB::Red : CRGB::Blue;
      }
      // FLD: Blue/White randomized marching logic
      for (int i = 0; i < NUM_LEDS_FLD; i++) {
        if (random(20) == 0) leds_fld[i] = (random(2) == 0) ? CRGB::Blue : CRGB::White;
      }
      // PSIs: Classic continuous gradient cycle
      fill_solid(leds_fpsi, NUM_LEDS_FPSI, CRGB::Blue);
      fill_solid(leds_rpsi, NUM_LEDS_RPSI, CRGB::Red);
      break;
  }
}
