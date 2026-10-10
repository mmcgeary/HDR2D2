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
    Failed
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

namespace r2link {
    enum class Result : uint8_t {
        Accepted = 0, InvalidArgument, NotReady, ManualOverride, Inhibited,
        Unsupported, Busy, WrongEpoch, HardwareError
    };
    enum class Outcome : uint8_t { Replied, TimedOut, Unsent, PeerLost, SessionChanged, EncodeFailed };
    enum class EventKind : uint8_t {
        Completed = 0, Cancelled = 1, Timeout = 2, HardwareError = 3,
        PlaybackStarted = 4, DomeTakeover = 5,
        Fault = 3
    };
    enum class MessageType : uint8_t {
        DomeRequest = 0x07, AudioRequest = 0x08, ControlRequest = 0x23
    };
    enum class AudioPriority : uint8_t { Ambient = 0, Foreground = 1 };
    enum class DomeOperation : uint8_t { Cancel = 0, Velocity = 1, SeekReference = 2 };
    enum class DomeOwner : uint8_t { Idle = 0, Event = 1 };
    enum class DomeReference : uint8_t { Front = 0, Rear = 1 };

    struct Completion {
        MessageType type{MessageType::ControlRequest};
        uint16_t sequence{0};
        Outcome outcome{Outcome::Replied};
        uint8_t result{0};
        uint16_t detail{0};
    };

    struct Event {
        uint8_t kind{0};
        uint8_t request_type{0};
        uint16_t request_seq{0};
        Event() = default;
        Event(uint8_t k, uint8_t rt, uint16_t rs = 0) : kind(k), request_type(rt), request_seq(rs) {}
    };

    struct DomeRequest {
        uint8_t operation{0};
        uint8_t owner{0};
        uint8_t reference{0};
        int16_t speed_percent{0};
        uint16_t lease_ms{0};
        uint16_t control_epoch{0};
        uint32_t dome_authority_generation{0};
    };

    struct ControlRequest {
        uint8_t operation{0};
        uint8_t reason{0};
        uint16_t token{0};
        uint16_t control_epoch{0};
    };

    struct BodyStatus {
        uint8_t drive_state{3};
        uint8_t dome_state{1};
        uint16_t control_epoch{1};
        uint32_t dome_authority_generation{5};
        uint8_t motion_locked_reasons{0};
    };

    const uint8_t kReasonOperator = 0;
    const uint8_t kReasonMaintenance = 2;
}

struct RequestHandle {
    uint16_t sequence{0};
    bool queued{false};

    RequestHandle() = default;
    RequestHandle(uint16_t seq, bool q) : sequence(seq), queued(q) {}
};

struct BodyStatusSnapshot {
    r2link::BodyStatus value{};
    bool fresh{true};
    uint32_t effective_age_ms{10};
};

struct FakeBodyClient {
    uint16_t next_seq{1};
    std::vector<r2link::DomeRequest> dome_requests;
    std::vector<r2link::ControlRequest> control_requests;
    std::vector<r2link::Completion> completions;
    std::vector<r2link::Event> events;

    BodyStatusSnapshot bodyStatus(uint32_t) const {
        BodyStatusSnapshot s{};
        s.value.dome_authority_generation = 5;
        s.value.control_epoch = 1;
        s.value.motion_locked_reasons = (g_maintenance.state == MaintenanceState::Locked) ? (1 << 2) : 0;
        s.fresh = true;
        return s;
    }

    RequestHandle requestDome(const r2link::DomeRequest& req, uint32_t) {
        dome_requests.push_back(req);
        return RequestHandle{next_seq++, true};
    }

    RequestHandle requestControl(const r2link::ControlRequest& req, uint32_t) {
        control_requests.push_back(req);
        return RequestHandle{next_seq++, true};
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

struct FakeRemoteAudio {
    RequestHandle play(uint16_t track, r2link::AudioPriority, uint32_t) {
        tracks.push_back(track);
        return RequestHandle{g_body_client.next_seq++, true};
    }
} g_remote_audio;

struct Sound {
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
} servoDispatch;

struct Display {
    int sequence = 0;
    void selectSequence(int seq, int = 0, int = 0, int = 0) { sequence = seq; }
} FLD, RLD, frontPSI, rearPSI;

struct Holo {
    bool dark = false;
    void selectSequence(int seq, int = 0) { dark = seq == 7; }
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
struct EspFake {
    int restarts{0};
    void restart() { ++restarts; }
} ESP;
#define DEBUG_PRINTLN(x) do {} while(0)

void cancelR2Macro();
void finishR2Macro();
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
