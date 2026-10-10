import unittest
from pathlib import Path

from cpp_test_support import run_cpp


ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "ASTROPIXELS_PLUS_UNIFIED/ASTROPIXELS_PLUS_UNIFIED.ino"
ASTRO = ROOT / "ASTROPIXELS_PLUS_UNIFIED"
SHARED = ROOT / "shared/R2BodyLink"


def run_sketch(program):
    """Compile sketch functions against plus_macro_fakes.h and the real protocol headers."""
    return run_cpp(program, include_dirs=[ASTRO, SHARED])


def function(source, name, return_type=None):
    if return_type:
        prefix = f"{return_type} {name}("
    else:
        for rt in ("void", "bool", "int", "uint8_t", "uint32_t", "RequestHandle"):
            if f"{rt} {name}(" in source:
                prefix = f"{rt} {name}("
                break
        else:
            raise ValueError(f"Function {name} not found in source")
    start = source.index(prefix)
    while source.find(";", start) < source.find("{", start):
        start = source.index(prefix, start + 1)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class PlusBehaviorTests(unittest.TestCase):
    def test_primary_sketch_has_no_retired_hardware_writers(self):
        source = SKETCH.read_text()
        for token in ("DfPlayerSerial", "sendVescDuty", "COMM_FORWARD_CAN",
                      "WRITE_DOME_SERVO", "ledcAttachPin", "dfPlayer.begin"):
            with self.subTest(token=token):
                self.assertNotIn(token, source)

    def test_manual_tilt_is_retired_and_random_holos_run_autonomously(self):
        source = SKETCH.read_text()
        for token in ("RC_CH_HOLO_TILT", "RC_CH_HOLO_ENABLE", "manual_holo_active",
                      "processManualHoloTilt"):
            self.assertNotIn(token, source)
        random_fn = function(source, "processRandomHolos")
        result = run_cpp("""
            #include <cstdint>
            #define F(x) x
            uint32_t now=1, next_holo_twitch_ms=0;
            uint32_t millis() { return now; }
            bool rc_connected=true, otaInProgress=false;
            enum { R2_NONE };
            int active_macro=R2_NONE;
            enum { HOMING_INACTIVE };
            int homing_state=HOMING_INACTIVE;
            long random(long low, long) { return low; }
            struct Servos {
                int pulse=0;
                void moveToPulse(int, int, int value) { pulse=value; }
            } servoDispatch;
            struct Holo {
                Servos* owner=nullptr;
                void assignServos(Servos* value, int, int) { owner=value; }
            } frontHolo, rearHolo, topHolo;
            struct Commands { static void process(const char*) {} };
            using CommandEvent=Commands;
            void setHoloServoOwnership(bool) {}
        """ + random_fn + """
            int main() {
                processRandomHolos();
                if (frontHolo.owner != &servoDispatch ||
                    rearHolo.owner != &servoDispatch ||
                    topHolo.owner != &servoDispatch) return 1;
                if (servoDispatch.pulse != 1390) return 2;
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, "Autonomous random holos regressed")

    def test_folder_audio_packets_and_command_spacing(self):
        source = SKETCH.read_text()
        fn = function(source, "playDFPlayerTrack")
        result = run_cpp("""
            #include <cstdint>
            #include <vector>
            #define F(x) x
            uint32_t now=100;
            uint32_t millis() { return now; }
            struct Console { void println(const char*) {} } Serial;
            namespace r2link { enum class AudioPriority { Foreground }; }
            struct FakeAudio {
                std::vector<uint16_t> played;
                void play(uint16_t track, r2link::AudioPriority, uint32_t) { played.push_back(track); }
            } g_remote_audio;
        """ + fn + """
            int main() {
                playDFPlayerTrack(0);
                playDFPlayerTrack(256);
                if (!g_remote_audio.played.empty()) return 1;
                playDFPlayerTrack(109);
                playDFPlayerTrack(110);
                if (g_remote_audio.played.size() != 2 ||
                    g_remote_audio.played[0] != 109 ||
                    g_remote_audio.played[1] != 110) return 2;
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, "Remote audio delegation regressed")

    def test_invalid_channel_frame_cannot_refresh_radio_liveness(self):
        source = SKETCH.read_text()
        fn = function(source, "processBodyRcSnapshot")
        result = run_cpp("""
            #include <cstdint>
            #define F(x) x
            uint32_t now=10, last_rc_packet_ms=0;
            uint32_t millis() { return now; }
            bool rc_connected=false, dome_motion_inhibited=true;
            uint16_t rc_channels[10]={};
            struct BodyRcState {
                bool valid{false};
                uint16_t channels[10]{};
            };
            struct {
                BodyRcState state;
                BodyRcState rcSnapshot(uint32_t) const { return state; }
            } g_body_client;
            struct Console { void println(const char*) {} } Serial;
            struct {
                void stop() {}
                void setOutputAll(bool) {}
            } servoDispatch;
            struct { void assignServos(void*,int,int) {} } frontHolo,rearHolo,topHolo;
            int stops=0;
            void stopDomeMotion() { ++stops; }
            void cancelR2Macro() {}
            #define RC_CH_DRIVE_STEER 0
            #define RC_CH_DRIVE_THROTTLE 1
            #define RC_CH_AUTO_DOME 8
            bool rcNeutral() { return true; }
            bool driveNeutral() { return true; }
            enum class MacroPhase { Idle, WaitingHome, WaitingAudio, Running, Finishing };
            struct MacroState {
                MacroPhase phase{MacroPhase::Idle};
                uint16_t home_sequence{0};
                uint16_t audio_sequence{0};
                bool dome_cancelled{false};
                uint32_t dome_generation{0};
                uint32_t guard_deadline_ms{0};
            } macro;
            enum { HOMING_INACTIVE, HOMING_SEEKING, HOMING_ALIGNED };
            int homing_state = HOMING_INACTIVE;
        """ + fn + """
            int main() {
                g_body_client.state.valid = true;
                g_body_client.state.channels[0] = 1500;
                processBodyRcSnapshot(now);
                if (!rc_connected || rc_channels[0] != 1500) return 1;

                now = 260;
                g_body_client.state.valid = false;
                processBodyRcSnapshot(now);
                if (rc_connected || stops != 1) return 2;
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, "RC snapshot failsafe regressed")

    def test_stale_radio_command_cannot_restart_dome(self):
        self.run_sketch_body(self.DOME_FUNCTIONS, r'''
    now = 100; rc_connected = false; dome_motion_inhibited = true;
    // Radio disconnected -> startDomeHoming rejected
    startDomeHoming(R2_NONE);
    assert(homing_state == HOMING_INACTIVE && g_body_client.dome_requests.empty());
    // Radio connected but motion inhibited -> rejected
    rc_connected = true;
    startDomeHoming(R2_NONE);
    assert(homing_state == HOMING_INACTIVE && g_body_client.dome_requests.empty());
    // Rearm neutral -> startDomeHoming accepted: SeekReference to the front
    dome_motion_inhibited = false;
    startDomeHoming(R2_LEIA);
    assert(homing_state == HOMING_SEEKING && g_body_client.dome_requests.size() == 1);
    assert(g_body_client.dome_requests[0].operation == 2 && g_body_client.dome_requests[0].reference == 0);
    // stopDomeMotion sends Cancel and re-inhibits
    stopDomeMotion();
    assert(homing_state == HOMING_INACTIVE && dome_motion_inhibited);
    assert(g_body_client.dome_requests.size() == 2 && g_body_client.dome_requests[1].operation == 0);
''')

    def test_homing_timeout_and_radio_loss_cancel_deferred_leia(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + ("dispatchBodyEvents",), r'''
    now = 1000; rc_connected = true; dome_motion_inhibited = false;
    rc_channels[RC_CH_AUTO_DOME] = 1000;
    const uint8_t dome = uint8_t(r2link::MessageType::DomeRequest);
    // A failed home drops the deferred macro
    homing_state = HOMING_SEEKING; pending_macro_after_home = R2_LEIA;
    g_body_client.events.push_back(r2link::Event{uint8_t(r2link::EventKind::HardwareError), dome, 9, 0});
    dispatchBodyEvents();
    assert(homing_state == HOMING_INACTIVE && pending_macro_after_home == R2_NONE);
    assert(macro.phase == MacroPhase::Idle && g_dome_behaviour.events.size() == 1);
    assert(g_audio_check.events.size() == 1 && g_audio_check.events[0].request_seq == 9);
    // A completed home starts the deferred Leia macro
    homing_state = HOMING_SEEKING; pending_macro_after_home = R2_LEIA;
    const int centred = centers;
    g_body_client.events.push_back(r2link::Event{uint8_t(r2link::EventKind::Completed), dome, 10, 0});
    dispatchBodyEvents();
    assert(homing_state == HOMING_INACTIVE && pending_macro_after_home == R2_NONE);
    assert(macro.kind == R2_LEIA && centers > centred);
''')

    def test_stop_random_prevents_future_audio_events(self):
        source = (ROOT / "ASTROPIXELS_PLUS_UNIFIED/MarcduinoSound.h").read_text()
        stop = function(source, "stopRandom")
        result = run_cpp("""
            #include <cstdint>
            bool fRandomEnabled = true;
            uint32_t fNextRandomEvent = 1000;
        """ + stop + """
            int main() { stopRandom(); return fRandomEnabled ? 1 : 0; }
        """)
        self.assertEqual(result.returncode, 0, "Random audio remains enabled")

    def test_random_chatter_does_not_select_reserved_macro_tracks(self):
        source = (ROOT / "ASTROPIXELS_PLUS_UNIFIED/MarcduinoSound.h").read_text()
        definitions = "\n".join(line for line in source.splitlines()
                                if line.startswith("#define MP3_BANK"))
        result = run_cpp("""
            #include <cstdint>
            #define MP3_MAX_SOUNDS_PER_BANK 25
            int fModule=0, kHCR=1;
            int selection=1, track=0;
            long random(long low,long high) { return low+(selection-1)%(high-low); }
            void sendHCR(const char*) {}
            void playSound(int bank,int sound) { track=(bank-1)*25+sound; }
        """ + definitions + "\n" + function(source, "playRandom") + """
            int main() {
                for (selection=1; selection<=51; ++selection) {
                    playRandom();
                    if (track==102 || track==106 || track==107 || track==109 ||
                        track==110 || track==255) return 1;
                }
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, "Chatter selected a macro track")

    def test_macro_audio_and_choreography_have_one_timed_owner(self):
        source = SKETCH.read_text()
        self.assertIn("void startR2Macro(R2Macro macro) {", source,
                      "Plus needs one shared macro owner for Wi-Fi and RC")
        functions = "\n".join(function(source, name) for name in
                              ("sendDomeRequest", "cancelR2Macro", "finishR2Macro", "startMacroChoreography",
                               "processMacroCompletion", "processMacroEvent",
                               "startR2Macro", "processR2Macro"))
        prelude = (ROOT / "tests/plus_macro_fakes.h").read_text()
        result = run_sketch(prelude + functions + """
            int main() {
                now = 100;
                startR2Macro(R2_CANTINA);
                if (tracks.size()!=1 || tracks[0]!=106) return 1;
                deliverReply(macro.audio_sequence, r2link::Result::Accepted);
                deliverEvent(r2link::EventKind::PlaybackStarted, macro.audio_sequence);
                if (macro.phase != MacroPhase::Running) return 10;
                now = 30100;
                processR2Macro();
                if (active_macro!=R2_NONE) return 2;
                dome_motion_inhibited = false;
                startR2Macro(R2_FAINT);
                deliverReply(macro.audio_sequence, r2link::Result::Accepted);
                deliverEvent(r2link::EventKind::PlaybackStarted, macro.audio_sequence);
                if (!outputs_off) return 3;
                now += 600;
                processR2Macro();
                if (FLD.sequence!=14 || RLD.sequence!=14 ||
                    frontPSI.sequence!=14 || rearPSI.sequence!=14) return 4;
                if (!frontHolo.dark || !rearHolo.dark || !topHolo.dark) return 5;
                now += 4400;
                processR2Macro();
                if (active_macro!=R2_NONE || centers==0) return 6;
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_macro_ordering_and_acknowledgement(self):
        source = SKETCH.read_text()
        functions = "\n".join(function(source, name) for name in
                              ("sendDomeRequest", "cancelR2Macro", "finishR2Macro", "startMacroChoreography",
                               "processMacroCompletion", "processMacroEvent",
                               "startR2Macro", "processR2Macro"))
        prelude = (ROOT / "tests/plus_macro_fakes.h").read_text()
        result = run_sketch(prelude + functions + """
            int main() {
                now = 1000;
                rc_channels[RC_CH_AUTO_DOME] = 2000;
                rc_channels[RC_CH_DOME_STEER] = 1500;
                rc_channels[RC_CH_DRIVE_STEER] = 1500;
                rc_channels[RC_CH_DRIVE_THROTTLE] = 1500;

                startR2Macro(R2_LEIA);
                assert(macro.phase == MacroPhase::WaitingHome);
                const uint16_t homeSequence = macro.home_sequence;
                assert(homeSequence > 0);

                // Home ACCEPTED alone must not start Leia
                deliverReply(homeSequence, r2link::Result::Accepted);
                assert(macro.phase == MacroPhase::WaitingHome);
                assert(tracks.empty());

                // Home COMPLETED -> play request
                deliverEvent(r2link::EventKind::Completed, homeSequence);
                assert(macro.phase == MacroPhase::WaitingAudio);
                const uint16_t audioSequence = macro.audio_sequence;
                assert(audioSequence > 0);
                assert(tracks.size() == 1 && tracks[0] == 109);

                // Audio ACCEPTED alone must not start choreography
                deliverReply(audioSequence, r2link::Result::Accepted);
                assert(macro.phase == MacroPhase::WaitingAudio);

                // PLAYBACK_STARTED -> Running
                deliverEvent(r2link::EventKind::PlaybackStarted, audioSequence);
                assert(macro.phase == MacroPhase::Running);

                // Completion ends routine
                deliverEvent(r2link::EventKind::Completed, audioSequence);
                assert(macro.phase == MacroPhase::Idle);
                assert(active_macro == R2_NONE);
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_macro_entry_points_takeover_and_guards(self):
        source = SKETCH.read_text()
        functions = "\n".join(function(source, name) for name in
                              ("sendDomeRequest", "stopDomeMotion", "emergencyStop", "cancelR2Macro", "finishR2Macro", "startMacroChoreography",
                               "processMacroCompletion", "processMacroEvent",
                               "startR2Macro", "processR2Macro"))
        prelude = (ROOT / "tests/plus_macro_fakes.h").read_text()
        result = run_sketch(prelude + functions + """
            int main() {
                now = 1000;

                // 1. Auto Dome OFF at trigger goes directly WaitingAudio, no seek
                rc_channels[RC_CH_AUTO_DOME] = 1000;
                startR2Macro(R2_LEIA);
                assert(macro.phase == MacroPhase::WaitingAudio);
                assert(macro.home_sequence == 0);
                assert(macro.dome_cancelled == false);
                assert(tracks.back() == 109);
                cancelR2Macro();

                // 2. Auto Dome ON but manual stick deflecting at trigger -> alignment rejected, goes to WaitingAudio
                rc_channels[RC_CH_AUTO_DOME] = 2000;
                rc_channels[RC_CH_DOME_STEER] = 1800;
                startR2Macro(R2_LEIA);
                assert(macro.phase == MacroPhase::WaitingAudio);
                assert(macro.home_sequence == 0);
                assert(macro.dome_cancelled == true);
                cancelR2Macro();

                // 3. Manual takeover during WaitingHome cancels seek, invalidates sequence
                rc_channels[RC_CH_DOME_STEER] = 1500;
                startR2Macro(R2_LEIA);
                assert(macro.phase == MacroPhase::WaitingHome);
                const uint16_t oldHomeSeq = macro.home_sequence;
                macro.dome_cancelled = true;
                homing_state = HOMING_INACTIVE;
                macro.home_sequence = 0;
                cancelR2Macro();
                assert(macro.phase == MacroPhase::Idle);
                deliverEvent(r2link::EventKind::Completed, oldHomeSeq);
                assert(macro.phase == MacroPhase::Idle);

                // 4. Manual takeover during Running sets dome_cancelled, keeps audio/lights running
                startR2Macro(R2_SCREAM);
                assert(macro.phase == MacroPhase::WaitingAudio);
                deliverReply(macro.audio_sequence, r2link::Result::Accepted);
                deliverEvent(r2link::EventKind::PlaybackStarted, macro.audio_sequence);
                assert(macro.phase == MacroPhase::Running);
                assert(macro.dome_cancelled == false);
                macro.dome_cancelled = true;
                assert(macro.phase == MacroPhase::Running);

                // 5. Operator STOP cancels everything and issues ControlRequest operation 0
                emergencyStop();
                assert(macro.phase == MacroPhase::Idle);
                assert(active_macro == R2_NONE);
                assert(!g_body_client.control_requests.empty());
                assert(g_body_client.control_requests.back().operation == 0);

                // 6. Faint macro runs without issuing LOCK/UNLOCK ControlRequests
                size_t ctrl_before = g_body_client.control_requests.size();
                dome_motion_inhibited = false;
                startR2Macro(R2_FAINT);
                assert(g_body_client.control_requests.size() == ctrl_before);
                deliverReply(macro.audio_sequence, r2link::Result::Accepted);
                deliverEvent(r2link::EventKind::PlaybackStarted, macro.audio_sequence);
                assert(g_body_client.control_requests.size() == ctrl_before);
                cancelR2Macro();

                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_maintenance_lock_gates_ota_and_reboot(self):
        source = SKETCH.read_text()
        functions = "\n".join(function(source, name) for name in
                              ("sendDomeRequest", "stopDomeMotion", "cancelR2Macro", "restartNow", "prepareMaintenance",
                               "maintenanceReady", "releaseMaintenance", "recoverBodyLocks",
                               "processMaintenanceCompletion", "reboot"))
        prelude = (ROOT / "tests/plus_macro_fakes.h").read_text()
        result = run_sketch(prelude + functions + """
            int main() {
                now = 1000;
                assert(!maintenanceReady());

                // prepareMaintenance issues ControlRequest LOCK with token 0xBEEF
                prepareMaintenance(false, false);
                assert(g_maintenance.state == MaintenanceState::Requested);
                assert(g_maintenance.token == 0xBEEF);
                assert(!g_body_client.control_requests.empty());
                assert(g_body_client.control_requests.back().operation == 2);
                assert(g_body_client.control_requests.back().token == 0xBEEF);

                // When body confirms lock:
                r2link::Completion comp{};
                comp.type = r2link::MessageType::ControlRequest;
                comp.sequence = g_maintenance.sequence;
                comp.result = static_cast<uint8_t>(r2link::Result::Accepted);
                processMaintenanceCompletion(comp);
                assert(maintenanceReady());
                assert(g_maintenance.state == MaintenanceState::Locked);

                // releaseMaintenance issues UNLOCK and is Idle once the body confirms it
                releaseMaintenance();
                assert(g_body_client.control_requests.back().operation == 3);
                assert(g_body_client.control_requests.back().token == 0xBEEF);
                comp.sequence = g_maintenance.sequence;
                processMaintenanceCompletion(comp);
                assert(g_maintenance.state == MaintenanceState::Idle);
                assert(!maintenanceReady());

                // recoverBodyLocks issues RECOVER_LOCKS (operation 4, reason 0, token 0)
                recoverBodyLocks();
                assert(g_body_client.control_requests.back().operation == 4);
                assert(g_body_client.control_requests.back().reason == 0);
                assert(g_body_client.control_requests.back().token == 0);

                // reboot initiates prepareMaintenance asynchronously
                reboot();
                assert(g_maintenance.state == MaintenanceState::Requested);
                assert(g_maintenance.pending_reboot == true);

                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)

    MACRO_FUNCTIONS = ("cancelR2Macro", "finishR2Macro", "startMacroChoreography",
                       "processMacroCompletion", "processMacroEvent", "startR2Macro", "processR2Macro")
    DOME_FUNCTIONS = ("sendDomeRequest", "stopDomeMotion", "startDomeHoming") + MACRO_FUNCTIONS

    def sketch_program(self, names, body, extra=""):
        source = SKETCH.read_text()
        functions = "\n".join(function(source, name) for name in names)
        prelude = (ROOT / "tests/plus_macro_fakes.h").read_text()
        # Forward declarations so extracted functions may call each other in any order.
        return prelude + "\nvoid emergencyStop();\nvoid stopDomeMotion();\n" + extra + functions + \
            "\nint main() {\n" + body + "\nreturn 0;\n}\n"

    def run_sketch_body(self, names, body, extra="", sources=()):
        result = run_cpp(self.sketch_program(names, body, extra), extra_sources=list(sources),
                         include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_wheel_test_keepalive_follows_the_drive_page_heartbeat(self):
        self.run_sketch_body(("processCommissioningKeepalive",), r'''
    CommissionStatusSnapshot& cs = g_body_client.commission_status;
    cs.fresh = true; cs.value.state = 1; cs.value.test = 6; cs.value.run_id = 55;
    processCommissioningKeepalive(1000);
    assert(g_body_client.commission_requests.empty());          // no /drive heartbeat: no keepalive
    g_drive_heartbeat.beat(1000);
    processCommissioningKeepalive(1100);
    assert(g_body_client.commission_requests.size() == 1);
    const r2link::CommissionRequest& k = g_body_client.commission_requests.back();
    assert(k.operation == 2 && k.run_id == 55 && k.control_epoch == 7);
    processCommissioningKeepalive(1500);                         // heartbeat 500 ms old: still forwarded
    assert(g_body_client.commission_requests.size() == 2);
    processCommissioningKeepalive(1601);                         // page closed: the body times the test out
    processCommissioningKeepalive(1800);
    assert(g_body_client.commission_requests.size() == 2);
    cs.value.test = 8;
    processCommissioningKeepalive(1900);
    assert(g_body_client.commission_requests.size() == 2);
    cs.value.test = 2;                                           // dome tests keep the dome-driven keepalive
    processCommissioningKeepalive(2000);
    assert(g_body_client.commission_requests.size() == 3);
    cs.value.state = 2;                                          // not running: nothing sent
    processCommissioningKeepalive(2200);
    assert(g_body_client.commission_requests.size() == 3);
''', extra='#include "CommissionKeepalive.h"\nBrowserHeartbeat g_drive_heartbeat;\n')

    def test_transmitter_macros_are_suppressed_during_the_radio_check(self):
        extra = r'''
#include <string>
#include "RadioCheck.h"
RadioCheck g_radio_check;
#define RC_CH_MOOD_SELECT 6
#define RC_CH_MACRO_TRIGGER 7
std::vector<std::string> marc_commands;
int dome_homes = 0;
struct Player {} player;
namespace Marcduino { void processCommand(Player&, const char* c) { marc_commands.push_back(c); } }
void startDomeHoming(R2Macro = R2_NONE) { ++dome_homes; }
'''
        self.run_sketch_body(("dialPosition", "processTransmitterInputs"), r'''
    rc_connected = true;
    rc_channels[6] = 1150;                                       // VrA position 2
    rc_channels[7] = 1000;
    g_radio_check.start(0);
    rc_channels[7] = 2000; processTransmitterInputs();          // SwB DOWN prompt
    assert(marc_commands.empty() && sMarcSound.plays == 0);
    BodyRcState none{};
    g_radio_check.tick(none, 20000);                             // check over (timed out)
    assert(g_radio_check.state() != RadioCheck::State::Prompting);
    processTransmitterInputs();                                  // SwB still DOWN: no macro
    assert(marc_commands.empty() && sMarcSound.plays == 0);
    rc_channels[7] = 1000; processTransmitterInputs();
    rc_channels[7] = 2000; processTransmitterInputs();          // a fresh flip fires
    assert(marc_commands.size() == 1 && sMarcSound.plays == 1);
''', extra=extra, sources=[ASTRO / "RadioCheck.cpp"])

    def test_dome_requests_carry_current_epoch_and_wire_owner(self):
        self.run_sketch_body(self.DOME_FUNCTIONS, r'''
    now = 1000; rc_connected = true; dome_motion_inhibited = false;
    rc_channels[RC_CH_AUTO_DOME] = 2000;
    startDomeHoming(R2_NONE);
    assert(g_body_client.dome_requests.size() == 1);
    const r2link::DomeRequest& home = g_body_client.dome_requests[0];
    assert(home.operation == 2 && home.owner == 1);
    assert(home.control_epoch == 7 && home.dome_authority_generation == 5);
    startR2Macro(R2_LEIA);
    assert(macro.phase == MacroPhase::WaitingHome && macro.home_sequence != 0);
    assert(g_body_client.dome_requests.back().control_epoch == 7);
    stopDomeMotion();
    assert(g_body_client.dome_requests.back().operation == 0);
    assert(g_body_client.dome_requests.back().control_epoch == 7);
    assert(g_body_client.encode_failures == 0);
''')

    def test_routine_dome_paths_do_not_latch_a_body_stop(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + (
            "restartNow", "prepareMaintenance", "prepareCommissionMotion", "startCommissionTest",
            "processBodyRcSnapshot"), r'''
    now = 1000; rc_connected = true; dome_motion_inhibited = false;
    prepareMaintenance(false, false);
    assert(g_body_client.control_requests.back().operation == 2);
    assert(g_body_client.control_requests.back().control_epoch == 7);
    dome_motion_inhibited = false;
    startCommissionTest(1, 0);
    assert(!g_body_client.commission_requests.empty());
    assert(g_body_client.commission_requests.back().operation == 1);
    assert(g_body_client.commission_requests.back().control_epoch == 7);
    g_body_client.rc_state.valid = true;
    for (auto& c : g_body_client.rc_state.channels) c = 1500;
    processBodyRcSnapshot(now);
    g_body_client.rc_state.valid = false;
    processBodyRcSnapshot(now + 300);   // body RC lost: the body owns that failsafe
    stopDomeMotion();
    assert(g_body_client.stopAllCount() == 0);
''')

    def test_commission_motion_gate_needs_live_radio_and_rearm(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + ("prepareCommissionMotion",), r'''
    now = 1000;
    rc_connected = false; dome_motion_inhibited = false;
    assert(!prepareCommissionMotion() && g_body_client.dome_requests.empty());
    rc_connected = true; dome_motion_inhibited = true;
    assert(!prepareCommissionMotion());
    dome_motion_inhibited = false; otaInProgress = true;
    assert(!prepareCommissionMotion());
    otaInProgress = false;
    homing_state = HOMING_SEEKING;
    assert(prepareCommissionMotion());
    assert(homing_state == HOMING_INACTIVE && dome_motion_inhibited);   // dome's own motion ended
    assert(g_body_client.dome_requests.back().operation == 0);
    assert(g_body_client.stopAllCount() == 0);
''')

    def test_operator_stop_is_the_only_stop_all(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + ("emergencyStop",), r'''
    now = 1000;
    emergencyStop();
    assert(g_body_client.stopAllCount() == 1);
    assert(g_body_client.dome_requests.back().operation == 0);
''')

    def test_release_maintenance_keeps_lock_until_body_confirms(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + (
            "restartNow", "prepareMaintenance", "maintenanceReady", "releaseMaintenance", "processMaintenanceCompletion"), r'''
    now = 1000;
    prepareMaintenance(false, false);
    r2link::Completion c{};
    c.type = r2link::MessageType::ControlRequest; c.outcome = r2link::Outcome::Replied;
    c.sequence = g_maintenance.sequence; c.result = uint8_t(r2link::Result::Accepted);
    processMaintenanceCompletion(c);
    assert(maintenanceReady());
    releaseMaintenance();
    assert(g_body_client.control_requests.back().operation == 3);
    assert(g_body_client.control_requests.back().control_epoch == 7);
    c.sequence = g_maintenance.sequence; c.result = uint8_t(r2link::Result::NotReady);
    processMaintenanceCompletion(c);   // body refused: CH6 ON or sticks off-centre
    assert(g_maintenance.state == MaintenanceState::Locked);
    releaseMaintenance();
    c.sequence = g_maintenance.sequence; c.result = uint8_t(r2link::Result::Accepted);
    processMaintenanceCompletion(c);
    assert(g_maintenance.state == MaintenanceState::Idle);
''')

    def test_reboot_restarts_after_body_lock_or_when_link_is_down(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + (
            "restartNow", "prepareMaintenance", "maintenanceReady", "processMaintenanceCompletion", "reboot"), r'''
    now = 1000;
    reboot();
    assert(g_body_client.control_requests.back().operation == 2);
    assert(ESP.restarts == 0);
    r2link::Completion c{};
    c.type = r2link::MessageType::ControlRequest; c.outcome = r2link::Outcome::Replied;
    c.sequence = g_maintenance.sequence; c.result = uint8_t(r2link::Result::Accepted);
    processMaintenanceCompletion(c);
    assert(ESP.restarts == 1);
    assert(g_body_client.stopAllCount() == 0);
    g_maintenance = MaintenanceInfo{};
    g_body_client.link_up = false;      // no body to lock: restarting cannot affect motion
    reboot();
    assert(ESP.restarts == 2);
''')

    def test_wireless_commissioning_requests_encode_with_current_epoch(self):
        self.run_sketch_body(("setCommissionField", "readCommissionField", "acceptCommissionBit"), r'''
    now = 1000;
    setCommissionField(12, 1, 1500);           // brake_ma, right wheel
    const r2link::CommissionRequest& set = g_body_client.commission_requests.back();
    assert(set.operation == 4 && set.field == 12 && set.wheel == 1 && set.value == 1500);
    assert(set.control_epoch == 7);
    readCommissionField(12, 1);
    const r2link::CommissionRequest& read = g_body_client.commission_requests.back();
    assert(read.operation == 0 && read.field == 1 && read.value == 12 && read.wheel == 1);
    acceptCommissionBit(4);                     // vesc_config_left
    const r2link::CommissionRequest& acc = g_body_client.commission_requests.back();
    assert(acc.operation == 6 && acc.value == 4 && acc.control_epoch == 7);
    assert(g_body_client.commission_requests.size() == 3 && g_body_client.encode_failures == 0);
''')

    def test_marc_sound_requests_reach_the_body_on_a_live_link(self):
        program = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <deque>
uint32_t now = 5000;
uint32_t millis() { return now; }
long random(long low, long) { return low; }
struct Stream { void write(uint8_t) {} void print(const char*) {} };
#define DEBUG_PRINTLN(x) do {} while (0)
#define MARCDUINO_ACTION(name, cmd, body)
#define SizeOfArray(a) (sizeof(a) / sizeof((a)[0]))
void playDFPlayerTrack(uint16_t) {}
#include "MarcduinoSound.h"
#include "BodyClient.h"

struct Pipe : r2link::BytePort {
    std::deque<uint8_t> rx; Pipe* peer = nullptr;
    int read() override { if (rx.empty()) return -1; int b = rx.front(); rx.pop_front(); return b; }
    size_t writable() const override { return 1024; }
    size_t write(const uint8_t* d, size_t n) override { peer->rx.insert(peer->rx.end(), d, d + n); return n; }
};

int main() {
    Pipe bp, dp; bp.peer = &dp; dp.peer = &bp;
    r2link::Endpoint body(bp, r2link::kRoleBody, 7);
    BodyClient client; client.begin(dp, 9);
    RemoteAudio audio(client);
    MarcSound sound;
    sound.beginRemote(audio);
    for (int i = 0; i < 2000 && !(body.connected(now) && client.linkUp(now)); ++i) { ++now; body.tick(now); client.tick(now); }
    assert(client.linkUp(now));

    sound.playSound(3, 2);      // happy bank, sound 2 -> track 52
    sound.stop();
    sound.setVolume(0.5f);
    for (int i = 0; i < 20; ++i) { ++now; client.tick(now); body.tick(now); }

    int plays = 0, stops = 0, volumes = 0;
    r2link::Frame f{};
    while (body.takeReceived(f)) {
        r2link::AudioRequest a{}; r2link::ErrorCounters e{};
        if (f.type != r2link::MessageType::AudioRequest || r2link::decode(f, a, e) != r2link::Status::Ok) continue;
        if (a.operation == 0 && a.track == 52) ++plays;
        if (a.operation == 1) ++stops;
        if (a.operation == 4 && a.volume == 15) ++volumes;
    }
    assert(plays == 1 && stops == 1 && volumes == 1);
    return 0;
}
'''
        result = run_cpp(program, extra_sources=[ASTRO / "BodyClient.cpp", ASTRO / "ProfileMirror.cpp", ASTRO / "RemoteAudio.cpp",
                                                SHARED / "src/Endpoint.cpp", SHARED / "src/Codec.cpp"],
                         include_dirs=[ASTRO, SHARED])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_startup_sound_waits_for_the_body_link(self):
        self.run_sketch_body(("processStartupSound",), r'''
    g_body_client.link_up = false;
    processStartupSound(1000);
    assert(sMarcSound.start_sounds == 0);   // a request now would be dropped
    g_body_client.link_up = true;
    processStartupSound(1100);
    processStartupSound(1200);
    assert(sMarcSound.start_sounds == 1);
''')

    def test_dome_request_replies_reach_the_idle_scheduler(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + (
            "restartNow", "prepareMaintenance", "processMaintenanceCompletion", "dispatchBodyCompletions"), r'''
    r2link::Completion c{};
    c.type = r2link::MessageType::DomeRequest; c.sequence = 41;
    c.outcome = r2link::Outcome::Replied; c.result = uint8_t(r2link::Result::Inhibited);
    g_body_client.completions.push_back(c);
    c.sequence = 42; c.outcome = r2link::Outcome::TimedOut; c.result = 0;
    g_body_client.completions.push_back(c);
    c.type = r2link::MessageType::AudioRequest; c.sequence = 43; c.outcome = r2link::Outcome::Replied;
    g_body_client.completions.push_back(c);
    dispatchBodyCompletions();
    assert(g_dome_behaviour.replies.size() == 2);
    assert(g_dome_behaviour.replies[0].first == 41 && g_dome_behaviour.replies[0].second == r2link::Result::Inhibited);
    assert(g_dome_behaviour.replies[1].first == 42 && g_dome_behaviour.replies[1].second == r2link::Result::NotReady);
    // The commissioning wizard and the audio check see every completion (each filters its own).
    assert(g_wizard.completions.size() == 3 && g_audio_check.completions.size() == 3);
    assert(g_audio_check.completions[2].sequence == 43);
''')

    def test_ota_aborts_unless_the_body_maintenance_lock_is_held(self):
        self.run_sketch_body(self.DOME_FUNCTIONS + ("maintenanceReady", "onOtaStart"), r'''
    now = 1000;
    onOtaStart();                                   // no maintenance lock
    assert(Update.aborts == 1 && !otaInProgress);
    g_maintenance.state = MaintenanceState::Locked;
    onOtaStart();
    assert(Update.aborts == 1 && otaInProgress);
    assert(g_body_client.dome_requests.back().operation == 0);   // dome motion cancelled
''')

    def test_dial_positions_are_evenly_spaced_and_reach_thirteen(self):
        self.run_sketch_body(("dialPosition",), r'''
    // 13 bins of 77us: 1000-1076 -> 1, 1077-1153 -> 2, ..., 1924-2000 -> 13.
    const uint16_t us[] = {900, 1000, 1076, 1077, 1500, 1923, 1924, 2000, 2100};
    const uint8_t want[] = {1, 1, 1, 2, 7, 12, 13, 13, 13};
    for (size_t i = 0; i < sizeof us / sizeof us[0]; ++i) assert(dialPosition(us[i]) == want[i]);
''')

    def test_vesc_drive_mixing_and_can_forwarding(self):
        source = SKETCH.read_text()
        # Primary sketch delegates drive authority completely; no local VESC writers
        for token in ("sendVescDuty", "processVescDrive", "COMM_FORWARD_CAN", "VESC_SERIAL"):
            self.assertNotIn(token, source)

    def test_vesc_telemetry_request_and_payload_decoding(self):
        source = SKETCH.read_text()
        # Primary sketch delegates telemetry polling/decoding to Teensy and ingests via BodyClient
        for token in ("requestVescTelemetry", "parseVescValuesPayload", "processVescTelemetry"):
            self.assertNotIn(token, source)


if __name__ == "__main__":
    unittest.main()
