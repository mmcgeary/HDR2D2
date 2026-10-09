import subprocess
import tempfile
import unittest
from pathlib import Path


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


def run_cpp(program):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        source.write_text(program)
        compilation = subprocess.run(
            ["c++", "-std=c++11", str(source), "-o", str(binary)],
            capture_output=True, text=True)
        if compilation.returncode:
            raise AssertionError(compilation.stderr)
        return subprocess.run([str(binary)], capture_output=True, text=True)


class PlusBehaviorTests(unittest.TestCase):
    def test_manual_tilt_is_clamped_and_holds_library_override(self):
        source = SKETCH.read_text()
        functions = "\n".join(function(source, name) for name in
                              ("processManualHoloTilt", "processRandomHolos"))
        result = run_cpp("""
            #include <cstdint>
            #define F(x) x
            #define RC_CH_HOLO_TILT 2
            uint32_t now=1, last_manual_holo_ms=0, next_holo_twitch_ms=0;
            uint32_t millis() { return now; }
            bool rc_connected=true, otaInProgress=false, manual_holo_active=false;
            bool random_holo_enabled=false;
            enum { R2_NONE };
            int active_macro=R2_NONE;
            enum { HOMING_INACTIVE };
            int homing_state=HOMING_INACTIVE;
            uint16_t rc_channels[10]={1500,1500,2200};
            long map(long x,long a,long b,long c,long d) {
                return (x-a)*(d-c)/(b-a)+c;
            }
            long constrain(long x,long low,long high) {
                return x<low ? low : x>high ? high : x;
            }
            long random(long low,long) { return low; }
            struct Servos {
                int pulse=0;
                void disable(int) {}
                void moveToPulse(int,int value) { pulse=value; }
                void moveToPulse(int,int,int value) { pulse=value; }
            } servoDispatch;
            struct Holo {
                Servos* owner=&servoDispatch;
                void assignServos(Servos* value,int,int) { owner=value; }
            } frontHolo,rearHolo,topHolo;
            struct Commands { static void process(const char*) {} };
            using CommandEvent=Commands;
            void setHoloServoOwnership(bool) {}
        """ + functions + """
            int main() {
                processManualHoloTilt();
                processRandomHolos();
                if (servoDispatch.pulse!=1800 || !manual_holo_active ||
                    frontHolo.owner!=nullptr) return 1;
                rc_channels[2]=950;
                processManualHoloTilt();
                if (servoDispatch.pulse!=1200) return 2;
                rc_channels[2]=1500;
                now=3001;
                processManualHoloTilt();
                processRandomHolos();
                if (!manual_holo_active || frontHolo.owner!=nullptr) return 3;
                now=3002;
                processManualHoloTilt();
                if (manual_holo_active || frontHolo.owner!=&servoDispatch) return 4;
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, "Manual range or hold regressed")

    def test_folder_audio_packets_and_command_spacing(self):
        source = SKETCH.read_text()
        functions = "\n".join(function(source, name) for name in
                              ("playDFPlayerTrack", "processAudioQueue"))
        result = run_cpp("""
            #include <cstdint>
            #include <vector>
            #define F(x) x
            uint32_t now=99, last_audio_cmd_ms=0;
            uint32_t millis() { return now; }
            uint16_t pending_audio_track=0;
            uint8_t last_played_track=0;
            struct { void println(const char*) {} } Serial;
            struct {
                std::vector<uint8_t> bytes;
                void write(uint8_t* data,int size) {
                    bytes.insert(bytes.end(),data,data+size);
                }
            } Serial1;
        """ + functions + """
            int main() {
                playDFPlayerTrack(109);
                playDFPlayerTrack(110);
                processAudioQueue();
                if (!Serial1.bytes.empty()) return 1;
                now=100;
                processAudioQueue();
                std::vector<uint8_t> expected={0x7E,0xFF,0x06,0x0F,0x00,
                                               0x01,0x6E,0xFE,0x7D,0xEF};
                if (Serial1.bytes!=expected || last_played_track!=110) return 2;
                playDFPlayerTrack(0);
                playDFPlayerTrack(256);
                if (pending_audio_track) return 3;
                playDFPlayerTrack(102);
                now=199;
                processAudioQueue();
                if (Serial1.bytes.size()!=10) return 4;
                now=200;
                processAudioQueue();
                if (Serial1.bytes.size()!=20 || last_played_track!=102) return 5;
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, "DFPlayer packet or spacing regressed")

    def test_invalid_channel_frame_cannot_refresh_radio_liveness(self):
        source = SKETCH.read_text()
        result = run_cpp("""
            #include <cstdint>
            #include <deque>
            #define F(x) x
            uint32_t now=10, last_rc_packet_ms=0;
            uint32_t millis() { return now; }
            bool rc_connected=false, manual_holo_active=false;
            uint16_t rc_channels[10]={};
            struct {
                std::deque<uint8_t> bytes;
                int available() { return bytes.size(); }
                uint8_t read() {
                    uint8_t value=bytes.front(); bytes.pop_front(); return value;
                }
            } Serial2;
            struct { void println(const char*) {} } Serial;
            struct {
                void stop() {}
                void setOutputAll(bool) {}
            } servoDispatch;
            struct { void assignServos(void*,int,int) {} } frontHolo,rearHolo,topHolo;
            int stops=0;
            void stopDomeMotion() { ++stops; }
            void cancelR2Macro() {}
            void feed(int invalid_channel) {
                uint8_t frame[32]={0x20,0x40};
                for (int ch=0; ch<14; ++ch) {
                    uint16_t pulse=ch==invalid_channel ? 500 :
                                   invalid_channel>=0 ? 2000 : 1500;
                    frame[2+ch*2]=pulse&255; frame[3+ch*2]=pulse>>8;
                }
                uint16_t sum=0xffff;
                for (int i=0; i<30; ++i) sum-=frame[i];
                frame[30]=sum&255; frame[31]=sum>>8;
                for (uint8_t value:frame) Serial2.bytes.push_back(value);
            }
        """ + function(source, "processIBusFrames") + """
            int main() {
                feed(-1); processIBusFrames();
                if (!rc_connected || rc_channels[0]!=1500) return 1;
                now=260;
                feed(0); processIBusFrames();
                if (last_rc_packet_ms!=10 || rc_channels[1]!=1500) return 2;
                feed(13); processIBusFrames();
                if (last_rc_packet_ms!=10 || rc_channels[1]!=1500) return 4;
                now=261; processIBusFrames();
                if (rc_connected || stops!=1) return 3;
                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, "Invalid frame retained stale live input")

    def test_stale_radio_command_cannot_restart_dome(self):
        source = SKETCH.read_text()
        rotation = function(source, "processDomeRotation")
        result = run_cpp("""
            #include <cstdint>
            #include <cstdio>
            enum { HOMING_INACTIVE, HOMING_SEEKING, HOMING_ALIGNED };
            int homing_state = HOMING_INACTIVE;
            bool rc_connected = false;
            uint16_t rc_channels[10] = {};
            #define RC_CH_DOME_STEER 3
            int speed = 0;
            void setDomeServoSpeed(int value) { speed = value; }
            bool dome_motion_inhibited = true;
            bool otaInProgress = false;
            enum { R2_NONE, R2_SCREAM };
            int active_macro = R2_NONE;
            long map(long x,long a,long b,long c,long d) {
                return (x-a)*(d-c)/(b-a)+c;
            }
        """ + source[source.index("bool rcNeutral() {"):
                     source.index("void stopDomeMotion() {")] + rotation + """
            int main() {
                rc_channels[3] = 2000;
                processDomeRotation();
                if (speed != 0) {
                    std::puts("Disconnected radio restarted dome motion");
                    return 1;
                }
                rc_connected = true;
                processDomeRotation();
                if (speed != 0 || !dome_motion_inhibited) return 2;
                rc_channels[3] = 1500;
                processDomeRotation();
                if (speed != 0 || dome_motion_inhibited) return 3;
                rc_channels[3] = 2000;
                processDomeRotation();
                if (speed == 0) return 4;
                active_macro = R2_SCREAM;
                processDomeRotation();
                if (speed != 0) return 6;
                active_macro = R2_NONE;
                otaInProgress = true;
                processDomeRotation();
                if (speed != 0) return 5;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_homing_timeout_and_radio_loss_cancel_deferred_leia(self):
        source = SKETCH.read_text()
        functions = "\n".join(function(source, name) for name in
                              ("stopDomeMotion", "processDomeHoming"))
        result = run_cpp("""
            #include <cstdint>
            #include <cstdio>
            #define F(x) x
            #define LOW 0
            #define PIN_DOME_HALL_SENS 19
            uint32_t now=20000;
            uint32_t millis() { return now; }
            bool rc_connected=true, dome_motion_inhibited=false, otaInProgress=false;
            enum R2Macro { R2_NONE, R2_LEIA };
            enum { HOMING_INACTIVE, HOMING_SEEKING, HOMING_ALIGNED };
            int homing_state=HOMING_SEEKING;
            uint32_t homing_start_time_ms=0;
            R2Macro pending_macro_after_home=R2_LEIA;
            bool is_dome_at_home=false;
            int digitalRead(int) { return is_dome_at_home ? 0 : 1; }
            int speed=25, macro_count=0;
            void setDomeServoSpeed(int value) { speed=value; }
            void startR2Macro(R2Macro) { ++macro_count; }
            struct { void println(const char*) {} } Serial;
            struct { void moveToPulse(int,int) {} } servoDispatch;
        """ + functions + """
            int main() {
                processDomeHoming();
                if (speed!=0 || homing_state!=HOMING_INACTIVE ||
                    pending_macro_after_home!=R2_NONE || macro_count!=0) return 1;
                dome_motion_inhibited=false;
                homing_state=HOMING_SEEKING;
                homing_start_time_ms=now;
                pending_macro_after_home=R2_LEIA;
                is_dome_at_home=true;
                processDomeHoming();
                if (macro_count!=1 || !dome_motion_inhibited) return 2;
                homing_state=HOMING_SEEKING;
                pending_macro_after_home=R2_LEIA;
                rc_connected=false;
                processDomeHoming();
                if (homing_state!=HOMING_INACTIVE || pending_macro_after_home!=R2_NONE ||
                    macro_count!=1) return 3;
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
        functions = "\n".join(function(source, name) for name in
                              ("sendVescDuty", "stopVescMotors", "processVescDrive"))
        definitions = "\n".join(line for line in source.splitlines()
                                if line.startswith("#define VESC_") or
                                   line.startswith("#define RC_CH_"))
        crc_fn = source[source.index("static uint16_t vesc_crc16"):
                        source.index("void sendVescDuty(")]
        result = run_cpp("""
            #include <cstdint>
            #include <vector>
            #include <cmath>
            #define F(x) x
            """ + definitions + """
            uint32_t now = 100;
            uint32_t millis() { return now; }
            bool rc_connected = true;
            bool otaInProgress = false;
            enum { R2_NONE, R2_FAINT };
            int active_macro = R2_NONE;
            uint16_t rc_channels[10] = {1500, 1500, 1500, 1500, 2000, 1500, 1500, 1500, 1500, 1500};
            float constrain(float val, float low, float high) {
                return val < low ? low : val > high ? high : val;
            }
            struct MockSerial {
                std::vector<std::vector<uint8_t>> packets;
                void write(const uint8_t* data, uint8_t len) {
                    packets.emplace_back(data, data + len);
                }
            } COMMAND_SERIAL;
        """ + crc_fn + functions + """
            int main() {
                // 1. Deadband test: stick neutral (1500, 1500)
                processVescDrive();
                if (COMMAND_SERIAL.packets.size() != 2) return 1;
                // Master packet (CAN ID 1): start=0x02, len=5, cmd=5, duty=0
                auto p_m = COMMAND_SERIAL.packets[0];
                if (p_m[0] != 0x02 || p_m[1] != 5 || p_m[2] != 5 ||
                    p_m[3] != 0 || p_m[4] != 0 || p_m[5] != 0 || p_m[6] != 0 ||
                    p_m.back() != 0x03) return 2;
                // Slave packet (CAN ID 2): start=0x02, len=7, cmd=34 (FORWARD_CAN), id=2, cmd=5, duty=0
                auto p_s = COMMAND_SERIAL.packets[1];
                if (p_s[0] != 0x02 || p_s[1] != 7 || p_s[2] != 34 || p_s[3] != 2 || p_s[4] != 5 ||
                    p_s[5] != 0 || p_s[6] != 0 || p_s[7] != 0 || p_s[8] != 0 ||
                    p_s.back() != 0x03) return 3;

                // 2. Tank spin in place: Steer full right (2000), Throttle neutral (1500), High speed (2000)
                COMMAND_SERIAL.packets.clear();
                now += 25;
                rc_channels[RC_CH_STEER] = 2000;
                rc_channels[RC_CH_THROTTLE] = 1500;
                processVescDrive();
                if (COMMAND_SERIAL.packets.size() != 2) return 4;
                // Master left wheel should be positive duty (+95000)
                int32_t left_duty = (int32_t(COMMAND_SERIAL.packets[0][3]) << 24) |
                                    (int32_t(COMMAND_SERIAL.packets[0][4]) << 16) |
                                    (int32_t(COMMAND_SERIAL.packets[0][5]) << 8) |
                                    int32_t(COMMAND_SERIAL.packets[0][6]);
                // Slave right wheel should be negative duty (-95000)
                int32_t right_duty = (int32_t(COMMAND_SERIAL.packets[1][5]) << 24) |
                                     (int32_t(COMMAND_SERIAL.packets[1][6]) << 16) |
                                     (int32_t(COMMAND_SERIAL.packets[1][7]) << 8) |
                                     int32_t(COMMAND_SERIAL.packets[1][8]);
                if (left_duty <= 0 || right_duty >= 0) return 5;

                // 3. Radio disconnect failsafe: Should force 0 duty
                COMMAND_SERIAL.packets.clear();
                now += 25;
                rc_connected = false;
                processVescDrive();
                if (COMMAND_SERIAL.packets.size() != 2) return 6;
                left_duty = (int32_t(COMMAND_SERIAL.packets[0][3]) << 24) |
                            (int32_t(COMMAND_SERIAL.packets[0][4]) << 16) |
                            (int32_t(COMMAND_SERIAL.packets[0][5]) << 8) |
                            int32_t(COMMAND_SERIAL.packets[0][6]);
                if (left_duty != 0) return 7;

                return 0;
            }
        """)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
