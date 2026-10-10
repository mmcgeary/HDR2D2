#ifdef USE_WIFI_WEB

#include "web-images.h"

////////////////////////////////
// List of available sequences by name and matching id
enum
{
    kMAX_FADE = 15,
    kMAX_DELAY = 500,
    kMIN_DELAY = 10,
    kMIN_BRI = 10,

    kMAX_ADJLOOP = 90000,
    kMIN_ADJLOOP = 500,
};

WMenuData mainMenu[] = {
    { "Logics", "/logics" },
    { "Dome & Macros", "/dome" },
    { "Body Status", "/body" },
    { "Commissioning", "/commissioning" },
    { "Setup", "/setup" }
};

WMenuData setupMenu[] = {
    { "Home", "/" },
    { "Serial", "/serial" },
    { "Sound", "/sound" },
    { "WiFi", "/wifi" },
    { "Firmware", "/firmware" },
    { "Back", "/" }
};

WElement mainContents[] = {
    WVerticalMenu("menu", mainMenu, SizeOfArray(mainMenu)),
    rseriesSVG
};

WElement setupContents[] = {
    WVerticalMenu("setup", setupMenu, SizeOfArray(setupMenu)),
    rseriesSVG
};

WElement domeContents[] = {
    WLabel("Motion requires a live radio. Center the dome stick to rearm after STOP.", "safety"),
    WButton("STOP", "stop", []() { emergencyStop(); }),
    WButton("Release STOP", "releaseStop", []() { releaseBodyStop(); }),
    WButton("Home dome", "homeDome", []() { startDomeHoming(); }),
    WButton("Scream", "scream", []() { startR2Macro(R2_SCREAM); }),
    WButton("Cantina", "cantina", []() { startR2Macro(R2_CANTINA); }),
    WButton("Leia", "leia", []() { startR2Macro(R2_LEIA); }),
    WButton("Disco", "disco", []() { startR2Macro(R2_DISCO); }),
    WButton("Faint", "faint", []() { startR2Macro(R2_FAINT); }),
    WButton("Back", "back", "/")
};

String logicsSeq[] = {
#define LOGICENGINE_SEQ(nam, val) \
    BUILTIN_SEQ(nam, LogicEngineDefaults::val)
#define BUILTIN_SEQ(nam, val) \
    nam,

#include "logic-sequences.h"

#undef BUILTIN_SEQ
#undef LOGICENGINE_SEQ
};

unsigned logicsSeqNumber[] = {
#define LOGICENGINE_SEQ(nam, val) \
    BUILTIN_SEQ(nam, LogicEngineDefaults::val)
#define BUILTIN_SEQ(nam, val) \
    val,

#include "logic-sequences.h"

#undef BUILTIN_SEQ
#undef LOGICENGINE_SEQ
};

String logicsColors[] = {
    "Default",
    "Red",
    "Orange",
    "Yellow",
    "Green",
    "Cyan",
    "Blue",
    "Purple",
    "Magenta",
    "Pink"
};

bool sFLDChanged = true;
bool sRLDChanged = true;

int sFLDSequence;
int sRLDSequence;

String sFLDText = "";
String sRLDText = "";
String sFLDDisplayText;
String sRLDDisplayText;

int sFLDColor = LogicEngineRenderer::kDefault;
int sRLDColor = LogicEngineRenderer::kDefault;

int sFLDSpeedScale;
int sRLDSpeedScale;

int sFLDNumSeconds;
int sRLDNumSeconds;

/////////////////////////////////////////////////////////////////////////
// Web Interface for logic engine animation sequences
WElement logicsContents[] = {
    WSelect("Front Logic Sequence", "frontseq",
        logicsSeq, SizeOfArray(logicsSeq),
        []() { return sFLDSequence; },
        [](int val) { sFLDSequence = val; sFLDChanged = true; } ),
    WSelect("Front Color", "frontcolor",
        logicsColors, SizeOfArray(logicsColors),
        []() { return sFLDColor; },
        [](int val) { sFLDColor = val; sFLDChanged = true; } ),
    WSlider("Animation Speed", "fldspeed", 0, 9,
        []()->int { return sFLDSpeedScale; },
        [](int val) { sFLDSpeedScale = val; sFLDChanged = true; } ),
    WSlider("Number of seconds", "fldseconds", 0, 99,
        []()->int { return sFLDNumSeconds; },
        [](int val) { sFLDNumSeconds = val; sFLDChanged = true; } ),
    WTextField("Front Text:", "fronttext",
        []()->String { return sFLDText; },
        [](String val) { sFLDText = val; sFLDChanged = true; } ),
    WSelect("Rear Logic Sequence", "rearseq",
        logicsSeq, SizeOfArray(logicsSeq),
        []() { return sRLDSequence; },
        [](int val) { sRLDSequence = val; sRLDChanged = true; } ),
    WSelect("Rear Color", "rearcolor",
        logicsColors, SizeOfArray(logicsColors),
        []() { return sRLDColor; },
        [](int val) { sRLDColor = val; sRLDChanged = true; } ),
    WSlider("Animation Speed", "rldspeed", 0, 9,
        []()->int { return sRLDSpeedScale; },
        [](int val) { sRLDSpeedScale = val; sRLDChanged = true; } ),
    WSlider("Number of seconds", "rldseconds", 0, 99,
        []()->int { return sRLDNumSeconds; },
        [](int val) { sRLDNumSeconds = val; sRLDChanged = true; } ),
    WTextField("Rear Text:", "reartext",
        []()->String { return sRLDText; },
        [](String val) { sRLDText = val; sRLDChanged = true; } ),
    WButton("Run", "run", []() {
        if (sFLDChanged)
        {
            sFLDDisplayText = sFLDText;
            sFLDDisplayText.replace("\\n", "\n");
            FLD.selectSequence(logicsSeqNumber[sFLDSequence], (LogicEngineRenderer::ColorVal)sFLDColor, sFLDSpeedScale, sFLDNumSeconds);
            FLD.setTextMessage(sFLDDisplayText.c_str());
            sFLDChanged = false;
        }
        if (sRLDChanged)
        {
            sRLDDisplayText = sRLDText;
            sRLDDisplayText.replace("\\n", "\n");
            RLD.selectSequence(logicsSeqNumber[sRLDSequence], (LogicEngineRenderer::ColorVal)sRLDColor, sRLDSpeedScale, sRLDNumSeconds);
            RLD.setTextMessage(sRLDDisplayText.c_str());
            sRLDChanged = false;
        }
    }),
    WHorizontalAlign(),
    WButton("Back", "back", "/"),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    rseriesSVG
};

////////////////////////////////

WElement serialContents[] = {
    WLabel("Serial2: Bidirectional Body Link on GPIO16 (RX) / GPIO17 (TX) at 115200 baud", "bodylink"),
    WLabel("Radio, dual VESCs and DFPlayer managed remotely via Teensy 4.1", "remote"),
    WHorizontalAlign(),
    WButton("Back", "back", "/setup"),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    WVerticalAlign(),
    rseriesSVG
};

////////////////////////////////

String soundPlayer[] = {
    "Disabled",
    "MP3 Trigger",
    "DFMiniPlayer",
    "HCR"
};

String soundSerial[] = {
    "AUX4/AUX5",
    "Serial2"
};

int marcSoundPlayer;
int marcSoundSerial;
int marcSoundVolume;
int marcSoundStartup;
bool marcSoundRandom;
int marcSoundRandomMin;
int marcSoundRandomMax;

WElement soundContents[] = {
    WLabel("DFPlayer Mini: remote body management via Teensy 4.1", "player"),
    WVerticalAlign(),
    WSlider("Sound Volume", "soundVolume", 0, 1000,
        []() { return (marcSoundVolume = preferences.getInt(PREFERENCE_MARCSOUND_VOLUME, MARC_SOUND_VOLUME)); },
        [](int val) {
            marcSoundVolume = val;
            sMarcSound.setVolume(marcSoundVolume / 1000.0);
        } ),
    WVerticalAlign(),
    WTextFieldInteger("Sound Startup", "soundStartup",
        []()->String { return String(marcSoundStartup = preferences.getInt(PREFERENCE_MARCSOUND_STARTUP, MARC_SOUND_STARTUP)); },
        [](String val) { marcSoundStartup = val.toInt(); }),
    WVerticalAlign(),
    WCheckbox("Random Sound", "soundRandom",
        []() { return (marcSoundRandom = (preferences.getBool(PREFERENCE_MARCSOUND_RANDOM, MARC_SOUND_RANDOM))); },
        [](bool val) { marcSoundRandom = val; } ),
    WVerticalAlign(),
    WTextFieldInteger("Random Min Millis", "soundRandomMin",
        []()->String { return String(marcSoundRandomMin = preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MIN, MARC_SOUND_RANDOM_MIN)); },
        [](String val) { marcSoundRandomMin = val.toInt(); }),
    WVerticalAlign(),
    WTextFieldInteger("Random Max Millis", "soundRandomMax",
        []()->String { return String(marcSoundRandomMax = preferences.getInt(PREFERENCE_MARCSOUND_RANDOM_MAX, MARC_SOUND_RANDOM_MAX)); },
        [](String val) { marcSoundRandomMax = val.toInt(); }),
    WVerticalAlign(),
    WButton("Save", "save", []() {
        preferences.putInt(PREFERENCE_MARCSOUND_VOLUME, marcSoundVolume);
        preferences.putInt(PREFERENCE_MARCSOUND_STARTUP, marcSoundStartup);
        preferences.putBool(PREFERENCE_MARCSOUND_RANDOM, marcSoundRandom);
        preferences.putInt(PREFERENCE_MARCSOUND_RANDOM_MIN, marcSoundRandomMin);
        preferences.putInt(PREFERENCE_MARCSOUND_RANDOM_MAX, marcSoundRandomMax);
        if (marcSoundRandom && !r2MacroActive())
        {
            sMarcSound.startRandom();
        }
        else
        {
            sMarcSound.stopRandom();
        }
        sMarcSound.setVolume(marcSoundVolume / 1000.0);
        // Flip values around if min is greater than max
        if (marcSoundRandomMin > marcSoundRandomMax)
        {
            int t = marcSoundRandomMin;
            marcSoundRandomMin = marcSoundRandomMax;
            marcSoundRandomMax = t;
        }
        sMarcSound.setRandomMin(marcSoundRandomMin);
        sMarcSound.setRandomMax(marcSoundRandomMax);
    }),
    WHorizontalAlign(),
    WButton("Back", "back", "/setup"),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    WVerticalAlign(),
    rseriesSVG
};

////////////////////////////////

String wifiSSID;
String wifiPass;
bool wifiAP;

WElement wifiContents[] = {
    W1("WiFi Setup"),
    WCheckbox("WiFi Enabled", "enabled",
        []() { return wifiEnabled; },
        [](bool val) { wifiEnabled = val; } ),
    WHR(),
    WCheckbox("Access Point", "apmode",
        []() { return (wifiAP = preferences.getBool(PREFERENCE_WIFI_AP, WIFI_ACCESS_POINT)); },
        [](bool val) { wifiAP = val; } ),
    WTextField("WiFi:", "wifi",
        []()->String { return (wifiSSID = preferences.getString(PREFERENCE_WIFI_SSID, WIFI_AP_NAME)); },
        [](String val) { wifiSSID = val; } ),
    WPassword("Password:", "password",
        []()->String { return (wifiPass = preferences.getString(PREFERENCE_WIFI_PASS, WIFI_AP_PASSPHRASE)); },
        [](String val) { wifiPass = val; } ),
    WHR(),
    WButton("Save", "save", []() {
        DEBUG_PRINTLN("WiFi Changed");
        preferences.putBool(PREFERENCE_WIFI_ENABLED, wifiEnabled);
        preferences.putBool(PREFERENCE_WIFI_AP, wifiAP);
        preferences.putString(PREFERENCE_WIFI_SSID, wifiSSID);
        preferences.putString(PREFERENCE_WIFI_PASS, wifiPass);
        reboot();
    }),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    WVerticalAlign(),
    rseriesSVG
};

////////////////////////////////

////////////////////////////////

WElement firmwareContents[] = {
    W1("Firmware Setup"),
    WButton("Prepare update", "prepareUpdate", []() { prepareMaintenance(); }),
    WFirmwareFile("Firmware:", "firmware"),
    WFirmwareUpload("Reflash", "firmware"),
    WLabel("Current Firmware Build Date:", "label"),
    WLabel(__DATE__, "date"),
#ifdef BUILD_VERSION
    WHRef(BUILD_VERSION, "Sources"),
#endif
    WButton("Clear Prefs", "clear", []() {
        DEBUG_PRINTLN("Clear all preference settings");
        clearPrefsAndReboot();
    }),
    WHorizontalAlign(),
    WButton("Reboot", "reboot", []() {
        reboot();
    }),
    WHorizontalAlign(),
    WButton("Back", "back", "/setup"),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    WVerticalAlign(),
    rseriesSVG
};

//////////////////////////////////////////////////////////////////

inline String formatBodyVesc(uint8_t wheel) {
    auto v = g_body_client.vescStatus(wheel, millis());
    char buf[128];
    if (!v.valid) {
        // Firmware-only frames (no valid sample yet) carry zeroed measurements: never show them.
        if (!v.present) return String("Unavailable");
        snprintf(buf, sizeof(buf), "FW %u.%u, no live data", v.fw_major, v.fw_minor);
        return String(buf);
    }
    snprintf(buf, sizeof(buf), "%u.%02uV %ldmA %ld eRPM %d.%uC",
             v.pack_cV / 100, v.pack_cV % 100,
             (long)v.motor_mA, (long)v.erpm,
             v.mosfet_dC / 10, abs(v.mosfet_dC % 10));
    return String(buf);
}

inline String formatBodyState() {
    auto s = g_body_client.bodyStatus(millis());
    if (!s.fresh) {
        return String("Unavailable");
    }
    char buf[128];
    const char* dstate = (s.value.drive_state == 3) ? "Active" :
                         (s.value.drive_state == 2) ? "Ready" :
                         (s.value.drive_state == 1) ? "Failsafe" : "Disabled";
    snprintf(buf, sizeof(buf), "Drive: %s | Epoch: %u | Age: %ums",
             dstate, s.value.control_epoch, (unsigned)s.effective_age_ms);
    return String(buf);
}

inline String formatBodyLocks() {
    auto s = g_body_client.bodyStatus(millis());
    if (!s.fresh) {
        return String("Unavailable");
    }
    if (s.value.lock_reasons == 0) {
        return String("Unlocked");
    }
    String out = "Locked [";
    if (s.value.lock_reasons & 1) out += "Operator ";
    if (s.value.lock_reasons & (1 << 1)) out += "Reserved ";
    if (s.value.lock_reasons & (1 << 2)) out += "Maintenance ";
    if (s.value.lock_reasons & (1 << 3)) out += "Commissioning ";
    out.trim();
    out += "]";
    return out;
}

inline String formatBodyAudio() {
    auto a = g_body_client.audioStatus(millis());
    if (a.state == 0) {
        return String("Idle");
    }
    if (a.state == 1) {
        return String("Starting");
    }
    if (a.state == 2) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Playing track %u", a.track);
        return String(buf);
    }
    if (a.state == 3) {
        return String("Finished");
    }
    return String("Unavailable");
}

inline String formatBodyLastError() {
    auto err = g_body_client.lastError();
    if (err.result == 0 && err.code == 0) {
        return String("None");
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "Result %u (Code %u, Detail %u)",
             err.result, err.code, err.detail);
    return String(buf);
}

WElement bodyContents[] = {
    W1("Body Status"),
    WLabel("Requires CH6 OFF and sticks neutral for lock recovery.", "safety"),
    WTextField("Left VESC:", "b_vescl", []()->String { return formatBodyVesc(0); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Right VESC:", "b_vescr", []()->String { return formatBodyVesc(1); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Body State:", "b_state", []()->String { return formatBodyState(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Motion Locks:", "b_locks", []()->String { return formatBodyLocks(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Audio Status:", "b_audio", []()->String { return formatBodyAudio(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Last Error:", "b_err", []()->String { return formatBodyLastError(); }, [](String) {}),
    WVerticalAlign(),
    WButton("Recover body locks", "recover", []() { recoverBodyLocks(); }),
    WHorizontalAlign(),
    WButton("Back", "back", "/"),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    WVerticalAlign(),
    rseriesSVG
};

inline String formatCommissionHall() {
    bool front = (digitalRead(PIN_DOME_HALL_FRONT) == LOW);
    bool rear = (digitalRead(PIN_DOME_HALL_REAR) == LOW);
    char buf[64];
    snprintf(buf, sizeof(buf), "Front: %s, Rear: %s",
             front ? "ACTIVE (0 deg)" : "Inactive",
             rear ? "ACTIVE (180 deg)" : "Inactive");
    return String(buf);
}

inline String formatCommissionRc() {
    auto rc = g_body_client.rcSnapshot(millis());
    if (!rc.valid) return String("Disconnected / Stale");
    char buf[64];
    snprintf(buf, sizeof(buf), "CH6: %u (%s), CH9: %u (%s)",
             rc.channels[5],
             (rc.channels[5] < 1250) ? "OFF" : "ARMED",
             rc.channels[8],
             (rc.channels[8] >= 1750) ? "ON" : "OFF");
    return String(buf);
}

// CommissionStatus.state and .test names (tests 6-8 are the wheel tests).
static const char* const kCommissionStates[] = { "Idle", "Running", "Completed", "Cancelled", "Failed", "TimedOut" };

inline const char* commissionTestName(uint8_t test) {
    static const char* const tests[] = { "None", "Neutral", "FrontRef", "RearRef", "TimingCw", "TimingCcw",
                                         "TimeoutBrake", "Direction", "Reversal" };
    return (test < SizeOfArray(tests)) ? tests[test] : "Unknown";
}

inline String formatCommissionTestState() {
    auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh) return String("Offline / Stale");
    const char* st = (cs.value.state <= 5) ? kCommissionStates[cs.value.state] : "Unknown";
    const char* te = commissionTestName(cs.value.test);
    char buf[80];
    snprintf(buf, sizeof(buf), "%s (Test: %s, Run: %lu, Err: %u)",
             st, te, (unsigned long)cs.value.run_id, cs.value.error);
    return String(buf);
}

inline String formatCommissionRevolutions() {
    auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh) return String("N/A");
    char buf[80];
    snprintf(buf, sizeof(buf), "Rev 1: %lu ms, Rev 2: %lu ms, Rev 3: %lu ms",
             (unsigned long)cs.value.revolution_ms[0],
             (unsigned long)cs.value.revolution_ms[1],
             (unsigned long)cs.value.revolution_ms[2]);
    return String(buf);
}

inline String formatCommissionRates() {
    auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh) return String("N/A");
    char buf[96];
    snprintf(buf, sizeof(buf), "Neutral: %u us, CW: %u ddeg/s, CCW: %u ddeg/s, Saved: %s",
             cs.value.trial_neutral_us,
             cs.value.proposed_cw_ddeg_s,
             cs.value.proposed_ccw_ddeg_s,
             cs.value.saved ? "YES" : "NO");
    return String(buf);
}

inline String formatCommissionAcceptance() {
    auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh) return String("N/A");
    const uint32_t f = cs.value.flags;
    char buf[96];
    snprintf(buf, sizeof(buf), "CH6 %s, CH9 %s, sticks %s, Hall %s | saved gen %lu",
             (f & 1) ? "OFF" : "ON", (f & 2) ? "ON" : "OFF", (f & 4) ? "centred" : "moved",
             (f & 8) ? "fresh" : "stale", (unsigned long)cs.value.config_generation);
    return String(buf);
}

// Guided commissioning. Values are rendered when the page loads: reload the page to
// see progress after pressing a button.

inline const char* commissionResultName(uint8_t r) {
    static const char* const names[] = { "Accepted", "InvalidArgument", "NotReady", "ManualOverride",
                                         "Inhibited", "Unsupported", "Busy", "WrongEpoch", "HardwareError" };
    return (r < SizeOfArray(names)) ? names[r] : "Unknown";
}

// Set when the operator pressed Cancel but the request could not be sent (link down
// or busy); the wizard is then still running and Cancel should be pressed again.
bool sWizardCancelRefused = false;

inline String formatWizard() {
    static const char* const states[] = { "Idle", "Running", "Done", "Failed" };
    const CommissionWizard::State st = g_wizard.state();
    char buf[128];
    if (st == CommissionWizard::State::Failed) {
        snprintf(buf, sizeof(buf), "Failed (test %s, error %u, reply %s)",
                 commissionTestName(g_wizard.currentTest()), g_wizard.lastError(),
                 commissionResultName(g_wizard.lastResult()));
    } else {
        snprintf(buf, sizeof(buf), "%s (test %s)", states[uint8_t(st)],
                 commissionTestName(g_wizard.currentTest()));
    }
    String out(buf);
    if (sWizardCancelRefused && st == CommissionWizard::State::Running) {
        out += " - Cancel not sent, press Cancel again";
    }
    return out;
}

inline String formatRadioCheck() {
    static const char* const states[] = { "Not started", "", "PASSED", "FAILED" };
    static const char* const fs[] = { "", " (failsafe frames OK)",
                                      " (receiver stops output; drive disarms on staleness)",
                                      " (failsafe values wrong: set CH1/2/4 centre, CH6/8/9 low)" };
    const RadioCheck::State st = g_radio_check.state();
    if (st == RadioCheck::State::Prompting) return String(g_radio_check.prompt());
    if (st == RadioCheck::State::Failed && g_radio_check.failsafe() == RadioCheck::Failsafe::Unknown) {
        return String("FAILED (timed out at: ") + g_radio_check.prompt() + ")";
    }
    return String(states[uint8_t(st)]) + fs[uint8_t(g_radio_check.failsafe())];
}

inline String formatAudioCheck() {
    static const char* const states[] = { "Not started", "Waiting for playback...", "PASSED", "FAILED (no playback)" };
    return String(states[uint8_t(g_audio_check.state())]);
}

// A field's input holds only its number (empty while unknown or unset) so the digits-only
// filter never fights placeholder text; FieldStateView shows the state beside it.
inline String formatFieldValue(uint8_t field, uint8_t wheel) {
    int32_t v = 0;
    return g_profile_mirror.value(field, wheel, v) ? String(v) : String("");
}

class FieldStateView : public WDynamic {
public:
    FieldStateView(uint8_t field, uint8_t wheel) : field_(field), wheel_(wheel) {}
    void emitBody(Print& out) const override {
        int32_t v = 0;
        if (!g_profile_mirror.known(field_, wheel_)) out.println("<small>reading... (reload)</small>");
        else if (!g_profile_mirror.value(field_, wheel_, v)) out.println("<small>(unset)</small>");
    }
private:
    uint8_t field_, wheel_;
};

template <uint8_t Field, uint8_t Wheel>
const WDynamic& fieldStateView() {
    static const FieldStateView view(Field, Wheel);
    return view;
}

inline String formatProfileSaved() {
    const auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh) return String("unknown (no commissioning status)");
    return String(cs.value.unsaved ? "unsaved changes" : "saved");
}

// Only a whole (optionally negative) number is staged; an emptied or garbled field is ignored.
inline bool parseFieldValue(const String& text, int32_t& out) {
    String t = text;
    t.trim();
    if (t.length() == 0 || t.length() > 10) return false;
    unsigned i = (t[0] == '-') ? 1 : 0;
    if (i == t.length()) return false;
    for (; i < t.length(); ++i) {
        if (t[i] < '0' || t[i] > '9') return false;
    }
    out = t.toInt();
    return true;
}

inline void stageCommissionField(uint8_t field, uint8_t wheel, const String& text) {
    int32_t v = 0;
    if (!parseFieldValue(text, v)) return;
    setCommissionField(field, wheel, v);
    g_profile_mirror.invalidate(field, wheel);
}

// Every wizard start button clears the "Cancel not sent" note. Starts that move the dome or
// wheels are checked against the wizard first, so a refused start never stops dome motion.
inline bool wizardIdle() { return g_wizard.state() != CommissionWizard::State::Running; }

inline void wizardAcceptAndSave(const uint8_t* bits, uint8_t count) {
    sWizardCancelRefused = false;
    g_wizard.acceptAndSave(bits, count, millis());
}

inline void wizardApplyBaseline() {
    sWizardCancelRefused = false;
    g_wizard.applyBaseline(millis());
}

inline void wizardNeutral() {
    sWizardCancelRefused = false;
    if (wizardIdle() && prepareCommissionMotion()) g_wizard.startNeutral(millis());
}

inline void wizardCalibrateDome() {
    sWizardCancelRefused = false;
    if (wizardIdle() && prepareCommissionMotion()) g_wizard.startDomeCalibration(millis());
}

// Nudge the dome servo neutral from the mirrored value; refused until that value is
// known (Apply baseline first on a fresh profile) so a guess never overwrites it.
inline void nudgeDomeNeutral(int16_t delta_us) {
    sWizardCancelRefused = false;
    int32_t v = 0;
    if (!g_profile_mirror.value(0, 0, v) || !g_wizard.canNudge(delta_us, v) || !prepareCommissionMotion()) return;
    if (g_wizard.nudgeNeutral(delta_us, v, millis())) g_profile_mirror.invalidate(0, 0);
}

// "Wheels are raised" covers one run: it is cleared once a wheel test Begin is sent.
inline void runWheelTest(uint8_t test, uint8_t wheel) {
    sWizardCancelRefused = false;
    if (!g_wheels_raised || !wizardIdle() || !prepareCommissionMotion()) return;
    if (g_wizard.startWheelTest(test, wheel, g_wheels_raised, millis())) g_wheels_raised = false;
}

inline void setWheelDirection(uint8_t wheel, int32_t direction) {
    setCommissionField(5, wheel, direction);
    g_profile_mirror.invalidate(5, wheel);
}

inline String formatVesc(uint8_t wheel) {
    const auto v = g_body_client.vescStatus(wheel, millis());
    if (!v.present) return String("not detected");
    char buf[96];
    if (!v.valid) {
        snprintf(buf, sizeof(buf), "FW %u.%u, no live data", v.fw_major, v.fw_minor);
    } else {
        snprintf(buf, sizeof(buf), "FW %u.%u, %u.%02u V, %ld erpm, fault %u", v.fw_major, v.fw_minor,
                 v.pack_cV / 100, v.pack_cV % 100, (long)v.erpm, v.fault);
    }
    return String(buf);
}

inline String formatWheelResult() {
    const auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh || cs.value.test < 6 || cs.value.test > 8) return String("-");
    const char* st = (cs.value.state == 2) ? "PASSED" : (cs.value.state == 1) ? "running" :
                     (cs.value.state <= 5) ? kCommissionStates[cs.value.state] : "Unknown";
    const int peak = cs.value.peak_current_cA;
    const unsigned mag = (unsigned)(peak < 0 ? -peak : peak);
    int32_t timeout_ms = 0;
    char timeout[16] = "?";
    if (g_profile_mirror.value(15, cs.value.wheel ? 1 : 0, timeout_ms)) snprintf(timeout, sizeof(timeout), "%ld", (long)timeout_ms);
    char buf[144];
    snprintf(buf, sizeof(buf), "%s %s wheel %c: stop %u ms (VESC timeout %s ms), peak %s%u.%02u A, peak %ld erpm, fault %u",
             commissionTestName(cs.value.test), st, cs.value.wheel ? 'R' : 'L', cs.value.stop_ms, timeout,
             peak < 0 ? "-" : "", mag / 100, mag % 100, (long)cs.value.peak_erpm, cs.value.vesc_fault);
    return String(buf);
}

// The checklist is multi-line, so it is rendered as HTML at page load instead of through
// a text field (field values are emitted inside single-quoted JavaScript strings).
class ChecklistView : public WDynamic {
public:
    void emitBody(Print& out) const override {
        char buf[512];
        formatChecklist(checklistInput(), buf, sizeof(buf));
        out.print("<p><pre style='display:inline-block;text-align:left'>");
        out.print(buf);
        out.println("</pre></p>");
    }
} sChecklistView;

// Live status for the open commissioning pages: one "<field id>=<text>" line per read-only
// field, so a running test can be watched without reloading (a reload of /drive would stop
// the heartbeat and brake the wheel). A poll from /drive is the wheel-test browser heartbeat.
inline void emitLiveStatus(Print& out, String query) {
    const bool drive = liveStatusFromDrive(query.c_str());
    if (drive) g_drive_heartbeat.beat(millis());
    out.println("HTTP/1.0 200 OK");
    out.println("Content-type:text/plain");
    out.println("Cache-Control: no-store");
    out.println("Connection: close");
    out.println();
    auto line = [&out](const char* id, const String& text) { out.print(id); out.print('='); out.println(text); };
    if (drive) {
        line("d_saved", formatProfileSaved());
        line("d_vl", formatVesc(0));
        line("d_vr", formatVesc(1));
        line("d_res", formatWheelResult());
        line("d_wiz", formatWizard());
    } else {
        line("c_hall", formatCommissionHall());
        line("c_rc", formatCommissionRc());
        line("c_status", formatCommissionTestState());
        line("c_revs", formatCommissionRevolutions());
        line("c_rates", formatCommissionRates());
        line("c_accept", formatCommissionAcceptance());
        line("c_next", String(nextChecklistStep(checklistInput())));
        line("c_wiz", formatWizard());
        line("c_rprompt", formatRadioCheck());
        line("c_aresult", formatAudioCheck());
    }
}

// Hidden page element: polls the live status (the next poll starts when the last one ends,
// so a slow link never piles requests up) and fills each named field, except the one being
// edited.
class WLiveStatus : public WElement {
public:
    WLiveStatus(const char* page, unsigned period_ms) {
        appendScriptf(
            "function liveStatus() {fetch('/cstatus?p=%s&').then(function(r) {return r.text();})"
            ".then(function(t) {t.split('\\n').forEach(function(l) {var i = l.indexOf('=');"
            " if (i < 1) return; var e = document.getElementById(l.substring(0, i) + '_fld');"
            " if (e && e !== document.activeElement) e.value = l.substring(i + 1).trim();});})"
            ".catch(function() {}).then(function() {setTimeout(liveStatus, %u);});}\n"
            "liveStatus();\n", page, period_ms);
    }
};

// Each row is the input plus its state note (two array elements).
#define FIELD_ROW(label, id, field, wheel) \
    WTextFieldInteger(label, id, []()->String { return formatFieldValue(field, wheel); }, \
        [](String val) { stageCommissionField(field, wheel, val); }), \
    WDynamicElement(fieldStateView<field, wheel>())
// Direction is -1/+1: the integer field's digits-only filter would block the sign.
#define FIELD_ROW_SIGNED(label, id, field, wheel) \
    WTextField(label, id, []()->String { return formatFieldValue(field, wheel); }, \
        [](String val) { stageCommissionField(field, wheel, val); }), \
    WDynamicElement(fieldStateView<field, wheel>())

WElement commissioningContents[] = {
    W1("Dome Calibration & Commissioning"),
    WLabel("CH6 OFF and sticks centred; SwD may be in either position. Status lines update live.", "safety"),
    WLiveStatus("c", 500),
    WTextField("Hall Sensors:", "c_hall", []()->String { return formatCommissionHall(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Radio Status:", "c_rc", []()->String { return formatCommissionRc(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Test Status:", "c_status", []()->String { return formatCommissionTestState(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Revolutions:", "c_revs", []()->String { return formatCommissionRevolutions(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Rates & State:", "c_rates", []()->String { return formatCommissionRates(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Acceptance:", "c_accept", []()->String { return formatCommissionAcceptance(); }, [](String) {}),
    WVerticalAlign(),
    W1("Checklist"),
    WDynamicElement(sChecklistView),
    WTextField("Next step:", "c_next", []()->String { return String(nextChecklistStep(checklistInput())); }, [](String) {}),
    WVerticalAlign(),
    FIELD_ROW("Servo min us (1000-1499):", "f1", 1, 0),
    FIELD_ROW("Servo max us (1501-2000):", "f2", 2, 0),
    FIELD_ROW("Auto speed % (1-25):", "f3", 3, 0),
    FIELD_ROW("CW rate ddeg/s (timing):", "f19", 19, 0),
    FIELD_ROW("CCW rate ddeg/s (timing):", "f20", 20, 0),
    WVerticalAlign(),
    WButton("Apply baseline", "c_base", []() { wizardApplyBaseline(); }),
    WHorizontalAlign(),
    WButton("Save profile", "c_save", []() { wizardAcceptAndSave(nullptr, 0); }),
    WVerticalAlign(),
    W1("Dome"),
    WButton("Neutral hold", "c_neutral", []() { wizardNeutral(); }),
    WHorizontalAlign(),
    WButton("Nudge -5us", "c_nm", []() { nudgeDomeNeutral(-5); }),
    WHorizontalAlign(),
    WButton("Nudge +5us", "c_np", []() { nudgeDomeNeutral(5); }),
    WHorizontalAlign(),
    WButton("Accept neutral & Save", "c_acc_neu", []() { const uint8_t b[] = {0}; wizardAcceptAndSave(b, 1); }),
    WVerticalAlign(),
    WButton("Calibrate dome (refs + timing)", "c_cal", []() { wizardCalibrateDome(); }),
    WHorizontalAlign(),
    WButton("Accept dome & Save", "c_acc_dome", []() { const uint8_t b[] = {1, 2, 3}; wizardAcceptAndSave(b, 3); }),
    WHorizontalAlign(),
    WButton("Cancel", "c_cancel", []() { sWizardCancelRefused = !g_wizard.cancel(millis()); }),
    WVerticalAlign(),
    WTextField("Wizard:", "c_wiz", []()->String { return formatWizard(); }, [](String) {}),
    WVerticalAlign(),
    W1("Radio & audio"),
    WButton("Start radio check", "c_radio", []() { g_radio_check.start(millis()); }),
    WHorizontalAlign(),
    WButton("Audio check", "c_audio", []() { startAudioCheck(); }),
    WVerticalAlign(),
    WTextField("Radio:", "c_rprompt", []()->String { return formatRadioCheck(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Audio:", "c_aresult", []()->String { return formatAudioCheck(); }, [](String) {}),
    WVerticalAlign(),
    WButton("Drive commissioning", "c_drive", "/drive"),
    WVerticalAlign(),
    WButton("Back", "back", "/"),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    WVerticalAlign(),
    rseriesSVG
};

WElement driveContents[] = {
    W1("Drive Commissioning (CH6 OFF, sticks centred)"),
    WLabel("Status lines update live; keep this page open while a wheel test runs. Field edits are staged; accept and Save to keep them.", "d_note"),
    WTextField("Profile:", "d_saved", []()->String { return formatProfileSaved(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Left VESC:", "d_vl", []()->String { return formatVesc(0); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Right VESC:", "d_vr", []()->String { return formatVesc(1); }, [](String) {}),
    WVerticalAlign(),
    WLabel("Compare these with VESC Tool for each controller, then accept VESC config.", "d_hint"),
    WLiveStatus("d", BrowserHeartbeat::kPeriodMs),   // also the wheel-test heartbeat
    FIELD_ROW("L VESC FW major:", "f6l", 6, 0), FIELD_ROW("R VESC FW major:", "f6r", 6, 1),
    FIELD_ROW("L VESC FW minor:", "f7l", 7, 0), FIELD_ROW("R VESC FW minor:", "f7r", 7, 1),
    FIELD_ROW("L values layout (1):", "f8l", 8, 0), FIELD_ROW("R values layout (1):", "f8r", 8, 1),
    FIELD_ROW("Slew permille/s:", "f4", 4, 0),
    FIELD_ROW("L motor mA:", "f9l", 9, 0), FIELD_ROW("R motor mA:", "f9r", 9, 1),
    FIELD_ROW("L battery mA:", "f10l", 10, 0), FIELD_ROW("R battery mA:", "f10r", 10, 1),
    FIELD_ROW("L regen mA:", "f11l", 11, 0), FIELD_ROW("R regen mA:", "f11r", 11, 1),
    FIELD_ROW("L brake mA:", "f12l", 12, 0), FIELD_ROW("R brake mA:", "f12r", 12, 1),
    FIELD_ROW("L undervolt cV:", "f13l", 13, 0), FIELD_ROW("R undervolt cV:", "f13r", 13, 1),
    FIELD_ROW("L overvolt cV:", "f14l", 14, 0), FIELD_ROW("R overvolt cV:", "f14r", 14, 1),
    FIELD_ROW("L VESC timeout ms (150):", "f15l", 15, 0), FIELD_ROW("R VESC timeout ms (150):", "f15r", 15, 1),
    FIELD_ROW("L timeout brake mA:", "f16l", 16, 0), FIELD_ROW("R timeout brake mA:", "f16r", 16, 1),
    FIELD_ROW("L reversal erpm:", "f17l", 17, 0), FIELD_ROW("R reversal erpm:", "f17r", 17, 1),
    FIELD_ROW("L reversal dwell ms:", "f18l", 18, 0), FIELD_ROW("R reversal dwell ms:", "f18r", 18, 1),
    FIELD_ROW_SIGNED("L direction:", "f5l", 5, 0), FIELD_ROW_SIGNED("R direction:", "f5r", 5, 1),
    WVerticalAlign(),
    WButton("Accept VESC config & Save", "d_acc_cfg", []() { const uint8_t b[] = {4, 5}; wizardAcceptAndSave(b, 2); }),
    WVerticalAlign(),
    WCheckbox("Wheels are raised (clears after each test)", "d_raised", []() { return g_wheels_raised; }, [](bool v) { g_wheels_raised = v; }),
    WVerticalAlign(),
    WButton("Timeout test L", "d_tl", []() { runWheelTest(6, 0); }),
    WHorizontalAlign(),
    WButton("Timeout test R", "d_tr", []() { runWheelTest(6, 1); }),
    WVerticalAlign(),
    WButton("Direction test L", "d_dl", []() { runWheelTest(7, 0); }),
    WHorizontalAlign(),
    WButton("Direction test R", "d_dr", []() { runWheelTest(7, 1); }),
    WVerticalAlign(),
    WButton("L rolled forward", "d_lf", []() { setWheelDirection(0, 1); }),
    WHorizontalAlign(),
    WButton("L rolled backward", "d_lb", []() { setWheelDirection(0, -1); }),
    WHorizontalAlign(),
    WButton("R rolled forward", "d_rf", []() { setWheelDirection(1, 1); }),
    WHorizontalAlign(),
    WButton("R rolled backward", "d_rb", []() { setWheelDirection(1, -1); }),
    WVerticalAlign(),
    WButton("Reversal test L", "d_rvl", []() { runWheelTest(8, 0); }),
    WHorizontalAlign(),
    WButton("Reversal test R", "d_rvr", []() { runWheelTest(8, 1); }),
    WVerticalAlign(),
    WTextField("Last wheel test:", "d_res", []()->String { return formatWheelResult(); }, [](String) {}),
    WVerticalAlign(),
    WTextField("Wizard:", "d_wiz", []()->String { return formatWizard(); }, [](String) {}),
    WVerticalAlign(),
    WButton("Accept wheel tests & Save", "d_acc_wt", []() {
        const uint8_t b[] = {6, 7, 8, 9, 10, 11}; wizardAcceptAndSave(b, 6); }),
    WHorizontalAlign(),
    WButton("Cancel", "d_cancel", []() { sWizardCancelRefused = !g_wizard.cancel(millis()); }),
    WVerticalAlign(),
    WButton("Back", "back", "/commissioning"),
    WVerticalAlign(),
    rseriesSVG
};

#undef FIELD_ROW
#undef FIELD_ROW_SIGNED

//////////////////////////////////////////////////////////////////

WPage pages[] = {
    WPage("/", mainContents, SizeOfArray(mainContents)),
      WPage("/dome", domeContents, SizeOfArray(domeContents)),
      WPage("/body", bodyContents, SizeOfArray(bodyContents)),
      WPage("/commissioning", commissioningContents, SizeOfArray(commissioningContents)),
      WPage("/drive", driveContents, SizeOfArray(driveContents)),
      WAPI("/cstatus", emitLiveStatus),
      WPage("/logics", logicsContents, SizeOfArray(logicsContents)),
    WPage("/setup", setupContents, SizeOfArray(setupContents)),
      WPage("/serial", serialContents, SizeOfArray(serialContents)),
      WPage("/sound", soundContents, SizeOfArray(soundContents)),
      WPage("/wifi", wifiContents, SizeOfArray(wifiContents)),
      WPage("/firmware", firmwareContents, SizeOfArray(firmwareContents)),
        WUpload("/upload/firmware",
            [](Client& client)
            {
                if (!maintenanceReady() || Update.hasError())
                    client.println("HTTP/1.0 200 FAIL");
                else
                    client.println("HTTP/1.0 200 OK");
                client.println("Content-type:text/html");
                client.println("Vary: Accept-Encoding");
                client.println();
                client.println();
                client.stop();
                if (maintenanceReady() && !Update.hasError())
                {
                    delay(1000);
                    preferences.end();
                    ESP.restart();
                }
                FLD.selectSequence(LogicEngineDefaults::FAILURE);
                FLD.setTextMessage("Flash Fail");
                FLD.selectSequence(LogicEngineDefaults::TEXTSCROLLLEFT, LogicEngineRenderer::kRed, 1, 0);
                FLD.setEffectWidthRange(1.0);
                FLD.setEffectWidthRange(1.0);
                otaInProgress = false;
            },
            [](WUploader& upload)
            {
                if (upload.status == UPLOAD_FILE_START)
                {
                    if (!maintenanceReady())
                    {
                        Serial.println(F("[UPLOAD] Rejected: maintenance lock not ready"));
                        return;
                    }
                    otaInProgress = true;
                    stopDomeMotion();
                    cancelR2Macro();
                    disableHoloServos();
                    unmountFileSystems();
                    FLD.selectSequence(LogicEngineDefaults::NORMAL);
                    RLD.selectSequence(LogicEngineDefaults::NORMAL);
                    FLD.setEffectWidthRange(0);
                    RLD.setEffectWidthRange(0);
                    Serial.printf("Update: %s\n", upload.filename.c_str());
                    if (!Update.begin(upload.fileSize))
                    {
                        Update.printError(Serial);
                    }
                }
                else if (upload.status == UPLOAD_FILE_WRITE)
                {
                    if (!maintenanceReady() || Update.hasError()) return;
                    float range = (float)upload.receivedSize / (float)upload.fileSize;
                    DEBUG_PRINTLN("Received: "+String(range*100)+"%");
                    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
                    {
                        Update.printError(Serial);
                    }
                    FLD.setEffectWidthRange(range);
                    RLD.setEffectWidthRange(range);
                }
                else if (upload.status == UPLOAD_FILE_END)
                {
                    if (!maintenanceReady() || Update.hasError()) return;
                    DEBUG_PRINTLN("GAME OVER");
                    if (Update.end(true))
                    {
                        Serial.printf("Update Success: %u\nRebooting...\n", upload.receivedSize);
                    }
                    else
                    {
                        Update.printError(Serial);
                    }
                }
            })
};

WifiWebServer<10,SizeOfArray(pages)> webServer(pages, wifiAccess);
#endif
