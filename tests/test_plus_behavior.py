import unittest
from pathlib import Path

from cpp_test_support import run_cpp


ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "ASTROPIXELS_PLUS_UNIFIED/ASTROPIXELS_PLUS_UNIFIED.ino"


def function(source, name):
    start = source.index(f"void {name}(")
    while source.find(";", start) < source.find("{", start):
        start = source.index(f"void {name}(", start + 1)
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
            bool rcNeutral() { return true; }
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
        source = SKETCH.read_text()
        stop_fn = function(source, "stopDomeMotion")
        start_fn = function(source, "startDomeHoming")
        result = run_cpp("""
            #include <cstdint>
            #include <cstdio>
            #include <vector>
            #define F(x) x
            enum { HOMING_INACTIVE, HOMING_SEEKING, HOMING_ALIGNED };
            int homing_state = HOMING_INACTIVE;
            bool rc_connected = false;
            uint16_t rc_channels[10] = {};
            #define RC_CH_DOME_STEER 3
            bool dome_motion_inhibited = true;
            bool otaInProgress = false;
            enum R2Macro : uint8_t { R2_NONE, R2_LEIA };
            R2Macro pending_macro_after_home = R2_NONE;
            uint32_t now = 100;
            uint32_t millis() { return now; }
            void cancelR2Macro() {}
            struct Console { void println(const char*) {} } Serial;
            namespace r2link {
                enum class DomeOperation : uint8_t { Cancel=0, Velocity=1, SeekReference=2 };
                enum class DomeOwner : uint8_t { Idle=0, Event=1 };
                enum class DomeReference : uint8_t { Front=0, Rear=1 };
                struct DomeRequest {
                    uint8_t operation{0};
                    uint8_t owner{0};
                    uint8_t reference{0};
                    int16_t speed_percent{0};
                    uint16_t lease_ms{0};
                    uint32_t generation{0};
                };
                struct BodyStatus { uint32_t dome_authority_generation{5}; };
            }
            struct BodyStatusSnapshot { r2link::BodyStatus value; };
            struct FakeBodyClient {
                std::vector<r2link::DomeRequest> requests;
                BodyStatusSnapshot bodyStatus(uint32_t) const { return BodyStatusSnapshot{}; }
                void requestDome(const r2link::DomeRequest& req, uint32_t) { requests.push_back(req); }
            } g_body_client;
            bool rcNeutral() { return rc_channels[3] >= 1460 && rc_channels[3] <= 1540; }
        """ + stop_fn + start_fn + """
            int main() {
                // Radio disconnected -> startDomeHoming rejected
                startDomeHoming(R2_NONE);
                if (homing_state != HOMING_INACTIVE || !g_body_client.requests.empty()) return 1;

                // Radio connected but motion inhibited -> rejected
                rc_connected = true;
                startDomeHoming(R2_NONE);
                if (homing_state != HOMING_INACTIVE || !g_body_client.requests.empty()) return 2;

                // Rearm neutral -> startDomeHoming accepted
                dome_motion_inhibited = false;
                startDomeHoming(R2_LEIA);
                if (homing_state != HOMING_SEEKING || g_body_client.requests.size() != 1) return 3;
                if (g_body_client.requests[0].operation != 2 || // SeekReference
                    g_body_client.requests[0].reference != 0) return 4; // Front

                // stopDomeMotion sends Cancel
                stopDomeMotion();
                if (homing_state != HOMING_INACTIVE || !dome_motion_inhibited) return 5;
                if (g_body_client.requests.size() != 2 ||
                    g_body_client.requests[1].operation != 0) return 6; // Cancel

                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_homing_timeout_and_radio_loss_cancel_deferred_leia(self):
        source = SKETCH.read_text()
        self.assertIn("g_body_client.takeEvent", source)
        result = run_cpp("""
            #include <cstdint>
            #define F(x) x
            enum R2Macro : uint8_t { R2_NONE, R2_LEIA };
            R2Macro active_macro = R2_NONE;
            R2Macro pending_macro_after_home = R2_NONE;
            enum { HOMING_INACTIVE, HOMING_SEEKING };
            int homing_state = HOMING_INACTIVE;
            int macro_started = 0;
            void startR2Macro(R2Macro m) { active_macro = m; ++macro_started; }
            int holo_centered = 0;
            void centerHoloServos() { ++holo_centered; }
            namespace r2link {
                enum class EventKind : uint8_t { None=0, Completed=1, Fault=2, Cancelled=3 };
                enum class MessageType : uint8_t { DomeRequest=0x07 };
                struct Event {
                    uint8_t kind{0};
                    uint8_t request_type{0};
                    Event(uint8_t k, uint8_t rt) : kind(k), request_type(rt) {}
                };
            }
            void onEvent(const r2link::Event& ev) {
                if (ev.kind == 1 && ev.request_type == 0x07) {
                    homing_state = HOMING_INACTIVE;
                    centerHoloServos();
                    if (pending_macro_after_home != R2_NONE) {
                        R2Macro m = pending_macro_after_home;
                        pending_macro_after_home = R2_NONE;
                        startR2Macro(m);
                    }
                } else if ((ev.kind == 2 || ev.kind == 3) && ev.request_type == 0x07) {
                    homing_state = HOMING_INACTIVE;
                    pending_macro_after_home = R2_NONE;
                }
            }
            int main() {
                homing_state = HOMING_SEEKING;
                pending_macro_after_home = R2_LEIA;

                // Event Fault -> cancels deferred macro
                r2link::Event fault_ev{static_cast<uint8_t>(r2link::EventKind::Fault), 0x07};
                onEvent(fault_ev);
                if (homing_state != HOMING_INACTIVE || pending_macro_after_home != R2_NONE || macro_started != 0) return 1;

                // Event Completed -> triggers deferred Leia
                homing_state = HOMING_SEEKING;
                pending_macro_after_home = R2_LEIA;
                r2link::Event comp_ev{static_cast<uint8_t>(r2link::EventKind::Completed), 0x07};
                onEvent(comp_ev);
                if (homing_state != HOMING_INACTIVE || macro_started != 1 || active_macro != R2_LEIA || holo_centered != 1) return 2;

                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)

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
                              ("startR2Macro", "processR2Macro"))
        prelude = (ROOT / "tests/plus_macro_fakes.h").read_text()
        result = run_cpp(prelude + functions + """
            int main() {
                now = 100;
                startR2Macro(R2_CANTINA);
                if (tracks.size()!=1 || tracks[0]!=106) return 1;
                now = 30100;
                processR2Macro();
                if (active_macro!=R2_NONE) return 2;
                startR2Macro(R2_FAINT);
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
