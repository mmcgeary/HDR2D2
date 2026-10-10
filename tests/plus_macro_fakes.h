#pragma once
#include <cstdint>
#include <vector>
#include <cstring>
#include <cassert>

#define F(x) x
uint32_t now = 0;
uint32_t millis() { return now; }
long random(long low, long) { return low; }

struct Console {
    void println(const char*) {}
    void print(const char*) {}
    void println(int) {}
    void print(int) {}
    template <class... A> void printf(const char*, A...) {}
} Serial;

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

MacroState macro{};
MaintenanceInfo g_maintenance{};
R2Macro& active_macro = macro.kind;

uint32_t macro_started_ms = 0;
uint32_t macro_duration_ms = 0;
uint32_t last_macro_step = UINT32_MAX;
uint32_t next_holo_twitch_ms = 0;

bool rc_connected = true, dome_motion_inhibited = false, otaInProgress = false;
unsigned long last_rc_packet_ms = 0;
static uint32_t g_commission_run_id = 0;
static uint32_t g_last_commission_keepalive_ms = 0;
enum { HOMING_INACTIVE, HOMING_SEEKING, HOMING_ALIGNED };
int homing_state = HOMING_INACTIVE;
R2Macro pending_macro_after_home = R2_NONE;
bool outputs_off = false;
int centers = 0;
std::vector<int> tracks;

#define RC_CH_DRIVE_STEER 0
#define RC_CH_DRIVE_THROTTLE 1
#define RC_CH_DOME_STEER 3
#define RC_CH_AUTO_DOME 8

uint16_t rc_channels[10] = {1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 2000, 1500};

bool rcNeutral() {
    return rc_channels[RC_CH_DOME_STEER] >= 1460 && rc_channels[RC_CH_DOME_STEER] <= 1540;
}

bool driveNeutral() {
    return rc_channels[RC_CH_DRIVE_STEER] >= 1460 && rc_channels[RC_CH_DRIVE_STEER] <= 1540 &&
           rc_channels[RC_CH_DRIVE_THROTTLE] >= 1460 && rc_channels[RC_CH_DRIVE_THROTTLE] <= 1540;
}

// Real protocol types and validators, so a request the sketch builds is only
// "sent" when the real codec would encode it.
#include "Messages.h"
#include "Codec.h"
#include "Endpoint.h"
#include "BodyClient.h"

struct FakeBodyClient {
    uint16_t next_seq{1};
    bool link_up{true};
    bool status_fresh{true};
    r2link::BodyStatus status{};
    size_t encode_failures{0};
    std::vector<r2link::DomeRequest> dome_requests;
    std::vector<r2link::ControlRequest> control_requests;
    std::vector<r2link::CommissionRequest> commission_requests;
    std::vector<r2link::Completion> completions;
    std::vector<r2link::Event> events;

    FakeBodyClient() {
        status.control_epoch = 7;
        status.dome_authority_generation = 5;
    }

    bool linkUp(uint32_t) const { return link_up; }

    CommissionStatusSnapshot commission_status{};
    CommissionStatusSnapshot commissionStatus(uint32_t) const { return commission_status; }

    BodyRcState rc_state{};
    BodyRcState rcSnapshot(uint32_t) const { return rc_state; }

    BodyStatusSnapshot bodyStatus(uint32_t) const {
        BodyStatusSnapshot s{};
        s.value = status;
        s.fresh = link_up && status_fresh;
        return s;
    }

    template <class T>
    RequestHandle record(const T& req, std::vector<T>& log) {
        r2link::Frame f{};
        r2link::ErrorCounters e{};
        if (!link_up) return RequestHandle{0, false};
        if (r2link::encode(req, f, e) != r2link::Status::Ok) { ++encode_failures; return RequestHandle{0, false}; }
        log.push_back(req);
        return RequestHandle{next_seq++, true};
    }
    RequestHandle requestDome(const r2link::DomeRequest& req, uint32_t) { return record(req, dome_requests); }
    RequestHandle requestControl(const r2link::ControlRequest& req, uint32_t) { return record(req, control_requests); }
    RequestHandle requestCommission(const r2link::CommissionRequest& req, uint32_t) { return record(req, commission_requests); }

    size_t stopAllCount() const {
        size_t n = 0;
        for (const auto& c : control_requests) n += c.operation == 0;
        return n;
    }

    bool takeCompletion(r2link::Completion& out) {
        if (completions.empty()) return false;
        out = completions.front();
        completions.erase(completions.begin());
        return true;
    }

    bool takeEvent(r2link::Event& out) {
        if (events.empty()) return false;
        out = events.front();
        events.erase(events.begin());
        return true;
    }
} g_body_client;

struct FakeDomeBehaviour {
    std::vector<std::pair<uint16_t, r2link::Result>> replies;
    std::vector<r2link::Event> events;
    void onReply(uint16_t seq, r2link::Result r) { replies.push_back({seq, r}); }
    void onEvent(const r2link::Event& ev) { events.push_back(ev); }
} g_dome_behaviour;

struct FakeWizard {
    std::vector<r2link::Completion> completions;
    void onCompletion(const r2link::Completion& c) { completions.push_back(c); }
} g_wizard;

struct FakeAudioCheck {
    std::vector<r2link::Completion> completions;
    std::vector<r2link::Event> events;
    void onCompletion(const r2link::Completion& c) { completions.push_back(c); }
    void onEvent(const r2link::Event& ev) { events.push_back(ev); }
} g_audio_check;

struct FakeRemoteAudio {
    RequestHandle play(uint16_t track, r2link::AudioPriority, uint32_t) {
        tracks.push_back(track);
        return RequestHandle{g_body_client.next_seq++, true};
    }
} g_remote_audio;

struct Sound {
    int start_sounds = 0;
    int plays = 0;
    void playStartSound() { ++start_sounds; }
    void playSound(int, int) { ++plays; }
    void playRandom() { ++plays; }
    void suspendRandom() {}
    void stop() {}
    void startRandomInSeconds(int) {}
    void stopRandom() {}
} sMarcSound;

struct PreferencesFake {
    bool getBool(const char*, bool def) { return def; }
    void clear() {}
    void end() {}
} preferences;
#define PREFERENCE_MARCSOUND_RANDOM "rnd"
#define MARC_SOUND_RANDOM true

struct Servos {
    void moveToPulse(int, int) {}
    void moveToPulse(int, int, int) {}
    void stop() {}
    void setOutputAll(bool) {}
} servoDispatch;

struct Display {
    int sequence = 0;
    void selectSequence(int seq, int = 0, int = 0, int = 0) { sequence = seq; }
} FLD, RLD, frontPSI, rearPSI;

struct Holo {
    bool dark = false;
    void selectSequence(int seq, int = 0) { dark = seq == 7; }
    void assignServos(Servos*, int, int) {}
} frontHolo, rearHolo, topHolo;

namespace LogicEngineDefaults {
    constexpr int NORMAL=0, ALARM=1, FAILURE=2, LEIA=3, MARCH=4, RAINBOW=10, LIGHTSOUT=14;
}
namespace LogicEngineRenderer { using ColorVal=int; constexpr int kDefault=0, kRed=1, kGreen=4; }

struct Commands { static void process(const char*) {} };
using CommandEvent = Commands;

void setHoloServoOwnership(bool) {}
void centerHoloServos() { ++centers; outputs_off=false; }
void disableHoloServos() { outputs_off=true; }
void playDFPlayerTrack(uint16_t track) { tracks.push_back(track); }
void resetSequence() {}
void unmountFileSystems() {}
struct UpdateFake {
    int aborts{0};
    void abort() { ++aborts; }
} Update;
struct EspFake {
    int restarts{0};
    void restart() { ++restarts; }
} ESP;
#define DEBUG_PRINTLN(x) do {} while(0)

void cancelR2Macro();
void finishR2Macro();
RequestHandle sendDomeRequest(r2link::DomeOperation operation, r2link::DomeReference reference);
static void restartNow(bool clear_prefs);
void startMacroChoreography(R2Macro m);

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

void processMacroCompletion(const r2link::Completion& comp);
void processMacroEvent(const r2link::Event& ev);

inline void deliverReply(uint16_t seq, r2link::Result res) {
    r2link::Completion comp{};
    comp.sequence = seq;
    comp.outcome = r2link::Outcome::Replied;
    comp.result = static_cast<uint8_t>(res);
    processMacroCompletion(comp);
}

inline void deliverEvent(r2link::EventKind kind, uint16_t seq) {
    r2link::Event ev{};
    ev.kind = static_cast<uint8_t>(kind);
    ev.request_seq = seq;
    ev.request_type = (macro.phase == MacroPhase::WaitingHome)
        ? static_cast<uint8_t>(r2link::MessageType::DomeRequest)
        : static_cast<uint8_t>(r2link::MessageType::AudioRequest);
    processMacroEvent(ev);
}
