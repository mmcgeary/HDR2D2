#pragma once

// FlySky remote features for the simplified droid. The receiver's iBUS line
// comes up the slip ring to Serial2. From it this file runs:
//   - the dome servo (stick, plus random moves homed on the front Hall sensor)
//   - mood sounds (a dial picks the mood, a switch plays one, random chatter)
//   - volume (a dial)
//   - holoprojector eyes: smooth moves, front eye roams gently, top and rear wander independently
// Driving never passes through here: the VESCs read the receiver directly.
//
// Include after MarcduinoSound.h, servoDispatch and preferences are declared.

#include "R2Logic.h"
#include "R2Eyes.h"

#define R2_IBUS_SERIAL Serial2
#define R2_IBUS_RX_PIN 16
#define R2_DOME_SERVO_PIN PIN_AUX2 // GPIO4, through the dome level shifter to ring CH6
#define R2_FRONT_HALL_PIN PIN_AUX3 // GPIO5, low when the magnet is in front
#define R2_DOME_LEDC_CHANNEL 15

// Transmitter channels as numbered on the FS-i6X
#define R2_CH_DRIVE_LEFT 1
#define R2_CH_DRIVE_RIGHT 2
#define R2_CH_DOME 4
// Random dome sits on CH5 so the receiver failsafe (CH1-CH6) can switch it off.
#define R2_CH_RANDOM_DOME 5 // SwA: down = random dome on
#define R2_CH_PLAY_SOUND 6  // SwB: flip down = play a mood sound
#define R2_CH_MOOD 7        // VrA
#define R2_CH_VOLUME 8      // VrB
#define R2_CH_RANDOM 9      // SwC: up = quiet; middle or down = random sounds and holos

#define R2_RADIO_TIMEOUT_MS 300
// Below the VESC's 10% deadband, so the wheels never move while this reads "not driving".
#define R2_DRIVE_BAND_US 40
#define R2_VOLUME_SAVE_DELAY_MS 3000

static uint32_t r2Random(uint32_t lo, uint32_t hi)
{
    return hi > lo ? lo + esp_random() % (hi - lo) : lo;
}

// Set when the magnet arrives, so a front pass is seen even if the loop was busy.
static volatile bool sFrontHallEdge = false;

static void IRAM_ATTR r2FrontHallIsr()
{
    sFrontHallEdge = true;
}

class R2Remote
{
public:
    // Called first in setup(): the level shifter pulls a floating pin high,
    // which some servos read as a command.
    static void holdDomeServoLow()
    {
        pinMode(R2_DOME_SERVO_PIN, OUTPUT);
        digitalWrite(R2_DOME_SERVO_PIN, LOW);
    }

    void setup()
    {
        R2_IBUS_SERIAL.begin(115200, SERIAL_8N1, R2_IBUS_RX_PIN, -1);
        pinMode(R2_FRONT_HALL_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(R2_FRONT_HALL_PIN), r2FrontHallIsr, FALLING);
        ledcSetup(R2_DOME_LEDC_CHANNEL, 50, 16);
        ledcAttachPin(R2_DOME_SERVO_PIN, R2_DOME_LEDC_CHANNEL);
        ledcWrite(R2_DOME_LEDC_CHANNEL, 0);
        fSavedVolume = preferences.getInt(PREFERENCE_MARCSOUND_VOLUME, MARC_SOUND_VOLUME);
        // The stock web page's random-sound interval settings still apply.
        fChatter.setRange(preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MIN, MARC_SOUND_RANDOM_MIN),
                          preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MAX, MARC_SOUND_RANDOM_MAX));
        for (uint8_t i = 0; i < 6; i++)
            servoDispatch.setServoEasingMethod(i, Easing::SineEaseInOut);
    }

    void loop()
    {
        uint32_t now = millis();
        while (R2_IBUS_SERIAL.available())
        {
            if (fIbus.feed(R2_IBUS_SERIAL.read()))
            {
                fLastFrameMs = now;
                fRadioSeen = true;
            }
        }
        bool radio = fRadioSeen && now - fLastFrameMs < R2_RADIO_TIMEOUT_MS;
        if (radio != fRadio)
        {
            fRadio = radio;
            Serial.println(radio ? "[R2] radio OK" : "[R2] radio lost");
        }
        if (radio)
            readControls(now);
        updateDome(now, radio);
        updateSounds(now);
        updateHolos(now);
        saveVolume(now);
    }

private:
    r2::IbusParser fIbus;
    r2::DomeBrain fDome{r2Random};
    r2::EdgeTrigger fPlaySwitch;
    r2::VolumeDial fVolumeDial;
    r2::LeadingEye fFrontEye;
    r2::ChameleonEye fRearEye;
    r2::ChameleonEye fTopEye;
    r2::RandomTimer fChatter{MARC_SOUND_RANDOM_MIN, MARC_SOUND_RANDOM_MAX};

    bool fRadioSeen = false;
    bool fRadio = false;
    uint32_t fLastFrameMs = 0;

    // Until the radio is heard the droid behaves like stock: random sounds
    // and holos on, dome still.
    bool fRandomOn = true;
    bool fRandomDome = false;
    bool fDriving = false;
    bool fPlayRequested = false;
    uint8_t fMood = 0;

    int fVolumeLevel = -1;
    uint32_t fVolumeChangedMs = 0;
    int fSavedVolume = 0;

    r2::DomeBrain::State fDomeState = r2::DomeBrain::kIdle;
    uint16_t fDomePulse = 0;

    void readControls(uint32_t now)
    {
        fDriving = r2::deflected(fIbus.channel(R2_CH_DRIVE_LEFT), R2_DRIVE_BAND_US) ||
                   r2::deflected(fIbus.channel(R2_CH_DRIVE_RIGHT), R2_DRIVE_BAND_US);
        fRandomDome = r2::switchOn(fIbus.channel(R2_CH_RANDOM_DOME));
        fRandomOn = r2::switchNotUp(fIbus.channel(R2_CH_RANDOM));
        if (fPlaySwitch.update(r2::switchOn(fIbus.channel(R2_CH_PLAY_SOUND))))
            fPlayRequested = true;

        uint8_t mood = r2::dialPosition(fIbus.channel(R2_CH_MOOD), r2::kMoodCount);
        if (mood != fMood)
        {
            static const char *const kMoods[] = {"mixed", "general", "chat", "happy", "sad", "whistle"};
            fMood = mood;
            Serial.printf("[R2] mood %s\n", kMoods[mood]);
        }

        int level = fVolumeDial.update(fIbus.channel(R2_CH_VOLUME));
        if (level >= 0)
        {
            sMarcSound.setVolume(float(level) / r2::VolumeDial::kMaxLevel);
            fVolumeLevel = level;
            fVolumeChangedMs = now;
        }
    }

    void updateDome(uint32_t now, bool radio)
    {
        r2::DomeInputs in;
        in.now = now;
        in.radio = radio;
        in.stick = fIbus.channel(R2_CH_DOME);
        in.randomOn = fRandomDome;
        in.driving = fDriving;
        bool passedFront = sFrontHallEdge;
        sFrontHallEdge = false;
        in.atFront = passedFront || digitalRead(R2_FRONT_HALL_PIN) == LOW;
        uint16_t pulse = fDome.update(in);
        if (pulse != fDomePulse)
        {
            fDomePulse = pulse;
            ledcWrite(R2_DOME_LEDC_CHANNEL, uint32_t(pulse) * 65536 / 20000);
        }
        if (fDome.state() != fDomeState)
        {
            static const char *const kStates[] = {
                "idle", "manual", "seeking front", "at front", "turning away", "looking", "returning",
                "FAULT: front sensor not found (switch random dome off to reset)"};
            fDomeState = fDome.state();
            Serial.printf("[R2] dome %s\n", kStates[fDomeState]);
        }
    }

    void updateSounds(uint32_t now)
    {
        if (fPlayRequested)
        {
            fPlayRequested = false;
            playMoodSound();
            fChatter.restart(now, r2Random);
        }
        if (fChatter.update(now, fRandomOn, r2Random))
            playMoodSound();
    }

    void playMoodSound()
    {
        static const uint8_t kBankSizes[5] = {
            MP3_BANK1_SOUNDS, MP3_BANK2_SOUNDS, MP3_BANK3_SOUNDS, MP3_BANK4_SOUNDS, MP3_BANK5_SOUNDS};
        uint8_t bank, sound;
        r2::pickMoodSound(fMood, kBankSizes, r2Random, bank, sound);
        sMarcSound.playSound(bank, sound);
    }

    void updateHolos(uint32_t now)
    {
        const r2::MoodProfile& mood = r2::moodProfile(fMood);
        float range = fDriving ? 0.7f : 1.0f;
        float hold = fDriving ? 1.5f : 1.0f;
        applyEyeMove(r2::kFrontEye, fFrontEye.update(now, fRandomOn, mood, r2::kCueNone, 0, fDriving, r2Random));
        applyEyeMove(r2::kRearEye, fRearEye.update(now, fRandomOn, mood, range, hold, r2Random));
        applyEyeMove(r2::kTopEye, fTopEye.update(now, fRandomOn, mood, range, hold, r2Random));
    }

    // Both servos of an eye start together and take the same time.
    static void applyEyeMove(uint8_t eye, const r2::EyeMove& move)
    {
        if (!move.pending)
            return;
        servoDispatch.moveTo(r2::kEyeServoH[eye], 0, move.durationMs, r2::eyeServoPos(move.to.h, r2::kEyeFlipH[eye]));
        servoDispatch.moveTo(r2::kEyeServoV[eye], 0, move.durationMs, r2::eyeServoPos(move.to.v, r2::kEyeFlipV[eye]));
    }

    // Saves the dial's volume for the next power-up once it has settled.
    void saveVolume(uint32_t now)
    {
        if (fVolumeLevel < 0 || now - fVolumeChangedMs < R2_VOLUME_SAVE_DELAY_MS)
            return;
        int value = fVolumeLevel * 1000 / r2::VolumeDial::kMaxLevel;
        if (value != fSavedVolume)
        {
            preferences.putInt(PREFERENCE_MARCSOUND_VOLUME, value);
            fSavedVolume = value;
        }
    }
};

R2Remote r2Remote;
