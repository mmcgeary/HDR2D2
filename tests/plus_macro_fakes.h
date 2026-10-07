#include <cstdint>
#include <vector>
#define F(x) x
uint32_t now = 0;
uint32_t millis() { return now; }
long random(long low, long high) { return low; }
struct Console {
    void println(const char*) {}
    void print(const char*) {}
} Serial;
enum R2Macro : uint8_t { R2_NONE, R2_SCREAM, R2_CANTINA, R2_LEIA, R2_DISCO, R2_FAINT };
R2Macro active_macro = R2_NONE;
uint32_t macro_started_ms, macro_duration_ms, last_macro_step, next_holo_twitch_ms;
bool rc_connected = true, dome_motion_inhibited = false, otaInProgress = false;
enum { HOMING_INACTIVE, HOMING_SEEKING, HOMING_ALIGNED };
int homing_state = HOMING_INACTIVE;
R2Macro pending_macro_after_home = R2_NONE;
bool outputs_off = false;
int centers = 0;
std::vector<int> tracks;
struct Sound { void suspendRandom() {} } sMarcSound;
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
void setDomeServoSpeed(int) {}
void finishR2Macro() { active_macro=R2_NONE; centerHoloServos(); }
void cancelR2Macro() { active_macro=R2_NONE; }
