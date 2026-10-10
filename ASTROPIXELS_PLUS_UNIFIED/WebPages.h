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
    WButton("STOP", "stop", []() { stopDomeMotion(); }),
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
    if (!v.valid) {
        return String("Unavailable");
    }
    char buf[128];
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
    if (s.value.motion_locked_reasons == 0) {
        return String("Unlocked");
    }
    String out = "Locked [";
    if (s.value.motion_locked_reasons & 1) out += "Operator ";
    if (s.value.motion_locked_reasons & (1 << 1)) out += "Reserved ";
    if (s.value.motion_locked_reasons & (1 << 2)) out += "Maintenance ";
    if (s.value.motion_locked_reasons & (1 << 3)) out += "Commissioning ";
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

inline String formatCommissionTestState() {
    auto cs = g_body_client.commissionStatus(millis());
    if (!cs.fresh) return String("Offline / Stale");
    const char* states[] = { "Idle", "Running", "Completed", "Cancelled", "Failed", "TimedOut" };
    const char* tests[] = { "None", "Neutral", "FrontRef", "RearRef", "TimingCw", "TimingCcw", "VescTimeout" };
    const char* st = (cs.value.state <= 5) ? states[cs.value.state] : "Unknown";
    const char* te = (cs.value.test <= 6) ? tests[cs.value.test] : "Unknown";
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
    char buf[64];
    snprintf(buf, sizeof(buf), "Flags: 0x%04lX, Gen: %lu",
             (unsigned long)cs.value.flags,
             (unsigned long)cs.value.config_generation);
    return String(buf);
}

WElement commissioningContents[] = {
    W1("Dome Calibration & Commissioning"),
    WLabel("Ensure CH6 is OFF and CH9 is ON before initiating motion tests.", "safety"),
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
    WButton("Neutral Test", "c_neutral", []() { startCommissionTest(1); }),
    WHorizontalAlign(),
    WButton("Front Ref Test", "c_front", []() { startCommissionTest(2); }),
    WHorizontalAlign(),
    WButton("Rear Ref Test", "c_rear", []() { startCommissionTest(3); }),
    WVerticalAlign(),
    WButton("Timing CW", "c_cw", []() { startCommissionTest(4); }),
    WHorizontalAlign(),
    WButton("Timing CCW", "c_ccw", []() { startCommissionTest(5); }),
    WHorizontalAlign(),
    WButton("Cancel Test", "c_cancel", []() { cancelCommissionTest(); }),
    WVerticalAlign(),
    WButton("Accept Neutral", "c_acc_neu", []() { acceptCommissionBit(0); }),
    WHorizontalAlign(),
    WButton("Accept Front Ref", "c_acc_fref", []() { acceptCommissionBit(1); }),
    WHorizontalAlign(),
    WButton("Accept Rear Ref", "c_acc_rref", []() { acceptCommissionBit(2); }),
    WVerticalAlign(),
    WButton("Accept Timing", "c_acc_tim", []() { acceptCommissionBit(3); }),
    WHorizontalAlign(),
    WButton("Save Profile", "c_save", []() { saveCommissionProfile(); }),
    WVerticalAlign(),
    WButton("Back", "back", "/"),
    WHorizontalAlign(),
    WButton("Home", "home", "/"),
    WVerticalAlign(),
    rseriesSVG
};

//////////////////////////////////////////////////////////////////

WPage pages[] = {
    WPage("/", mainContents, SizeOfArray(mainContents)),
      WPage("/dome", domeContents, SizeOfArray(domeContents)),
      WPage("/body", bodyContents, SizeOfArray(bodyContents)),
      WPage("/commissioning", commissioningContents, SizeOfArray(commissioningContents)),
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
