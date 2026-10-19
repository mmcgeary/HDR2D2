#pragma once

// FlySky remote features for the simplified droid
// (docs/superpowers/specs/2026-10-11-lifelike-behaviour-design.md).
// The receiver's iBUS line comes up the slip ring to Serial2. From it this file
// runs the behaviour level, mood, volume, sounds, the dome and the holo eyes.
// Driving never passes through here: the VESCs read the receiver directly.
//
// Include after MarcduinoSound.h, servoDispatch and preferences are declared.

#include "R2Logic.h"
#include "R2Mood.h"
#include "R2Eyes.h"
#include "R2DomeTracker.h"
#include "R2DomeMotion.h"
#include "R2Attention.h"

static_assert(r2::kClipBankSize == MP3_MAX_SOUNDS_PER_BANK && r2::kPhraseBase == MP3_PHRASE_BASE &&
                  r2::kPhraseBlock == MP3_PHRASE_BLOCK,
              "R2Logic.h's SD card layout must match MarcduinoSound.h");

#define R2_IBUS_SERIAL Serial2
#define R2_IBUS_RX_PIN 16
#define R2_DOME_SERVO_PIN PIN_AUX2 // GPIO4, through the dome level shifter to ring CH6
#define R2_DOME_HALL_PIN PIN_AUX3  // GPIO5, dome Hall sensor: low at each body magnet
#define R2_DOME_LEDC_CHANNEL 15

// Transmitter channels as numbered on the FS-i6X
#define R2_CH_DRIVE_LEFT 1
#define R2_CH_DRIVE_RIGHT 2
#define R2_CH_DOME 4
#define R2_CH_LEVEL 5      // SwC: up = Still, middle = Aware, down = Alive. Failsafe: up.
#define R2_CH_PLAY_SOUND 6 // SwB: flip down = play a mood sound
#define R2_CH_MOOD 7       // VrA
#define R2_CH_VOLUME 8     // VrB

#define R2_RADIO_TIMEOUT_MS 300
// Below the VESC's 10% deadband, so the wheels never move while this reads "not driving".
#define R2_DRIVE_BAND_US 40
#define R2_VOLUME_SAVE_DELAY_MS 3000
#define R2_SPEED_SAVE_INTERVAL_MS 300000
// Longest the front holo lamp stays lit for one look.
#define R2_FRONT_LAMP_MAX_MS 8000
#define PREFERENCE_R2_DOME_SPEEDS "r2speeds"

static uint32_t r2Random(uint32_t lo, uint32_t hi)
{
    return hi > lo ? lo + esp_random() % (hi - lo) : lo;
}

// Magnet blips: the interrupt stores the time of each, the loop reads them in
// order with r2::drainBlips().
static volatile uint32_t sBlipUs[8];
static volatile uint8_t sBlipHead = 0;

static void IRAM_ATTR r2HallIsr()
{
    uint8_t head = sBlipHead;
    sBlipUs[head & 7] = micros();
    sBlipHead = uint8_t(head + 1);
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
        pinMode(R2_DOME_HALL_PIN, INPUT_PULLUP);
        attachInterrupt(digitalPinToInterrupt(R2_DOME_HALL_PIN), r2HallIsr, FALLING);
        ledcSetup(R2_DOME_LEDC_CHANNEL, 50, 16);
        ledcAttachPin(R2_DOME_SERVO_PIN, R2_DOME_LEDC_CHANNEL);
        ledcWrite(R2_DOME_LEDC_CHANNEL, 0);
        fSavedVolume = preferences.getInt(PREFERENCE_MARCSOUND_VOLUME, MARC_SOUND_VOLUME);
        r2::DomeSpeeds speeds;
        if (preferences.getBytes(PREFERENCE_R2_DOME_SPEEDS, &speeds, sizeof(speeds)) == sizeof(speeds))
        {
            fTracker.importSpeeds(speeds);
            fTracker.markSaved();
        }
        for (uint8_t i = 0; i < 6; i++)
            servoDispatch.setServoEasingMethod(i, Easing::SineEaseInOut);
    }

    void loop()
    {
        uint32_t now = millis();
        readRadio(now);
        if (fRadio)
            readControls(now);
        uint8_t mood = fMoods.update(fDialMood, now, r2Random);
        const r2::MoodProfile& profile = r2::moodProfile(mood);

        // One time for the whole pass: blips up to it, then the update and the
        // new command, so the tracker sees events in the order they happened.
        // The sensor level is read right before it.
        bool overMagnet = digitalRead(R2_DOME_HALL_PIN) == LOW;
        uint32_t nowUs = micros();
        fBlipTail = r2::drainBlips(sBlipUs, sBlipHead, fBlipTail, nowUs, fTracker);
        fTracker.update(nowUs);

        r2::AttentionInputs in;
        in.nowMs = now;
        in.radio = fRadio || !fRadioSeen; // before any radio: run at the starting level
        in.level = fLevel;
        in.mood = mood;
        in.stick = fStick;
        in.driving = fDriving;
        in.swb = fSwbPressed;
        in.domeKnown = fTracker.known();
        in.domeAngle = fTracker.angle();
        in.domeArrived = fMotion.arrived();
        fSwbPressed = false;
        r2::AttentionOutputs out = fAttention.update(in);

        uint16_t pulse = fMotion.update(now, fRadio, fIbus.channel(R2_CH_DOME), out.dome, fTracker.known(),
                                        fTracker.angle(), fTracker);
        fTracker.setSensor(overMagnet);
        fTracker.setCommand(nowUs, fMotion.direction(), fMotion.offsetUs());
        if (pulse != fDomePulse)
        {
            fDomePulse = pulse;
            ledcWrite(R2_DOME_LEDC_CHANNEL, uint32_t(pulse) * 65536 / 20000);
        }

        updateEyes(now, out, profile);
        if (out.soundBank >= 0)
            playSound(uint8_t(out.soundBank));
        saveSettings(now);
        report(mood);
    }

    // Holo lamp for an eye: the pixel level to show, and whether the lamp
    // rather than the stock holo flicker owns that holo right now.
    uint8_t holoLampLevel(uint8_t eye) const { return fHoloLamps.level(eye); }
    bool holoLampOwned(uint8_t eye) const { return fHoloLamps.owned(eye); }
    uint8_t activeMood() const { return fMoods.active(); }

private:
    r2::IbusParser fIbus;
    r2::EdgeTrigger fPlaySwitch;
    r2::DialSelector fMoodDial;
    r2::VolumeDial fVolumeDial;
    r2::MoodSelector fMoods;
    r2::DomeTracker fTracker;
    r2::DomeMotion fMotion;
    r2::Attention fAttention{r2Random};
    r2::LeadingEye fFrontEye;
    r2::ChameleonEye fRearEye;
    r2::ChameleonEye fTopEye;
    r2::HoloLampFader fHoloLamps;
    uint8_t fBlipTail = 0;

    bool fRadioSeen = false;
    bool fRadio = false;
    uint32_t fLastFrameMs = 0;

    // Before the receiver is heard: Aware, General mood, dome silent.
    uint8_t fLevel = r2::kAware;
    uint8_t fDialMood = r2::kGeneral;
    bool fStick = false;
    bool fDriving = false;
    bool fSwbPressed = false;

    int fVolumeLevel = -1;
    uint32_t fVolumeChangedMs = 0;
    int fSavedVolume = 0;
    uint32_t fSpeedsSavedMs = 0;
    uint16_t fDomePulse = 0;

    uint8_t fReportedLevel = 255;
    uint8_t fReportedMood = 255;
    const char* fReportedPhase = "";
    bool fReportedKnown = false;
    bool fReportedDisabled = false;

    void readRadio(uint32_t now)
    {
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
    }

    void readControls(uint32_t now)
    {
        fDriving = r2::deflected(fIbus.channel(R2_CH_DRIVE_LEFT), R2_DRIVE_BAND_US) ||
                   r2::deflected(fIbus.channel(R2_CH_DRIVE_RIGHT), R2_DRIVE_BAND_US);
        // The planner's "stick in use" matches when DomeMotion passes the stick through.
        fStick = r2::deflected(fIbus.channel(R2_CH_DOME), r2::DomeMotion::defaults().stickDeadbandUs);
        fLevel = r2::switchLevel(fIbus.channel(R2_CH_LEVEL));
        if (fPlaySwitch.update(r2::switchOn(fIbus.channel(R2_CH_PLAY_SOUND))))
            fSwbPressed = true;
        fDialMood = fMoodDial.update(fIbus.channel(R2_CH_MOOD), r2::kMoodCount);

        int level = fVolumeDial.update(fIbus.channel(R2_CH_VOLUME));
        if (level >= 0)
        {
            sMarcSound.setVolume(float(level) / r2::VolumeDial::kMaxLevel);
            fVolumeLevel = level;
            fVolumeChangedMs = now;
        }
    }

    void updateEyes(uint32_t now, const r2::AttentionOutputs& out, const r2::MoodProfile& profile)
    {
        float range = out.calm ? 0.7f : 1.0f;
        float hold = out.calm ? 1.5f : 1.0f;
        r2::EyeMove moveFront =
            fFrontEye.update(now, out.eyesActive, profile, out.eyeCue, out.eyeSide, out.calm, r2Random);
        r2::EyeMove moveRear = fRearEye.update(now, out.eyesActive, profile, range, hold, r2Random);
        r2::EyeMove moveTop = fTopEye.update(now, out.eyesActive, profile, range, hold, r2Random);

        applyEyeMove(r2::kFrontEye, moveFront);
        applyEyeMove(r2::kRearEye, moveRear);
        applyEyeMove(r2::kTopEye, moveTop);

        // Lamps: the front holo lights for a look once its eye gets there and
        // stays lit until the eye leaves the look. The rear and top holos light
        // now and then while their eyes hold still.
        if (!out.eyesActive)
        {
            for (uint8_t eye = 0; eye < 3; eye++)
                fHoloLamps.release(eye, now);
        }
        else
        {
            if (out.eyeCue == r2::kCueLook && moveFront.pending)
                fHoloLamps.triggerHold(r2::kFrontEye, now, moveFront.durationMs, R2_FRONT_LAMP_MAX_MS,
                                       profile.holoHoldChancePct, r2Random);
            else if (!fFrontEye.looking())
                fHoloLamps.release(r2::kFrontEye, now);
            uint8_t wanderChance = profile.holoHoldChancePct / 4;
            if (moveRear.pending)
                fHoloLamps.triggerHold(r2::kRearEye, now, moveRear.durationMs, fRearEye.lastHoldMs(), wanderChance,
                                       r2Random);
            if (moveTop.pending)
                fHoloLamps.triggerHold(r2::kTopEye, now, moveTop.durationMs, fTopEye.lastHoldMs(), wanderChance,
                                       r2Random);
        }
        fHoloLamps.update(now);
    }

    // Both servos of an eye start together and take the same time.
    static void applyEyeMove(uint8_t eye, const r2::EyeMove& move)
    {
        if (!move.pending)
            return;
        servoDispatch.moveTo(r2::kEyeServoH[eye], 0, move.durationMs, r2::eyeServoPos(move.to.h, r2::kEyeFlipH[eye]));
        servoDispatch.moveTo(r2::kEyeServoV[eye], 0, move.durationMs, r2::eyeServoPos(move.to.v, r2::kEyeFlipV[eye]));
    }

    void playSound(uint8_t bank)
    {
#if MOOD_SOUND_SOURCE == MOOD_SOUNDS_GENERATED_ONLY
        static const uint8_t kBankSizes[5] = {0, 0, 0, 0, 0};
        static const uint8_t kPhraseCounts[5] = {
            MP3_BANK1_PHRASES, MP3_BANK2_PHRASES, MP3_BANK3_PHRASES, MP3_BANK4_PHRASES, MP3_BANK5_PHRASES};
#elif MOOD_SOUND_SOURCE == MOOD_SOUNDS_CLIPS_ONLY
        static const uint8_t kBankSizes[5] = {
            MP3_BANK1_SOUNDS, MP3_BANK2_SOUNDS, MP3_BANK3_SOUNDS, MP3_BANK4_SOUNDS, MP3_BANK5_SOUNDS};
        static const uint8_t kPhraseCounts[5] = {0, 0, 0, 0, 0};
#else // MOOD_SOUNDS_EXTENDED
        static const uint8_t kBankSizes[5] = {
            MP3_BANK1_SOUNDS, MP3_BANK2_SOUNDS, MP3_BANK3_SOUNDS, MP3_BANK4_SOUNDS, MP3_BANK5_SOUNDS};
        static const uint8_t kPhraseCounts[5] = {
            MP3_BANK1_PHRASES, MP3_BANK2_PHRASES, MP3_BANK3_PHRASES, MP3_BANK4_PHRASES, MP3_BANK5_PHRASES};
#endif
        uint16_t file = r2::pickMoodSound(bank, kBankSizes, kPhraseCounts, r2Random);
        sMarcSound.playFile(file);
    }

    // Saves the dial's volume once it has settled, and the learned dome
    // speeds at most every 5 minutes.
    void saveSettings(uint32_t now)
    {
        if (fVolumeLevel >= 0 && now - fVolumeChangedMs >= R2_VOLUME_SAVE_DELAY_MS)
        {
            int value = fVolumeLevel * 1000 / r2::VolumeDial::kMaxLevel;
            if (value != fSavedVolume)
            {
                preferences.putInt(PREFERENCE_MARCSOUND_VOLUME, value);
                fSavedVolume = value;
            }
        }
        if (fTracker.dirty() && now - fSpeedsSavedMs >= R2_SPEED_SAVE_INTERVAL_MS)
        {
            r2::DomeSpeeds speeds;
            fTracker.exportSpeeds(speeds);
            preferences.putBytes(PREFERENCE_R2_DOME_SPEEDS, &speeds, sizeof(speeds));
            fTracker.markSaved();
            fSpeedsSavedMs = now;
        }
    }

    void report(uint8_t mood)
    {
        static const char* const kLevels[] = {"still", "aware", "alive"};
        if (fLevel != fReportedLevel)
        {
            fReportedLevel = fLevel;
            Serial.printf("[R2] level %s\n", kLevels[fLevel < 3 ? fLevel : 0]);
        }
        if (mood != fReportedMood)
        {
            fReportedMood = mood;
            Serial.printf("[R2] mood %s%s\n", r2::moodProfile(mood).name, fDialMood == r2::kMixed ? " (mixed)" : "");
        }
        if (fAttention.phase() != fReportedPhase)
        {
            fReportedPhase = fAttention.phase();
            Serial.printf("[R2] behaviour %s\n", fReportedPhase);
        }
        if (fTracker.known() != fReportedKnown)
        {
            fReportedKnown = fTracker.known();
            Serial.println(fReportedKnown ? "[R2] dome position found" : "[R2] dome position lost");
        }
        if (fAttention.domeDisabled() != fReportedDisabled)
        {
            fReportedDisabled = fAttention.domeDisabled();
            if (fReportedDisabled)
                Serial.println("[R2] dome looks off: front magnets not found twice (SwC to Still resets)");
        }
    }
};

R2Remote r2Remote;
