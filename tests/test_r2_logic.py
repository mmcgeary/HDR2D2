"""Host tests for AstroPixelsPlus/R2Logic.h. Run: python3 -m unittest discover tests"""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r"""
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "R2Logic.h"
using namespace r2;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

// Deterministic random: always the lowest value.
static uint32_t lowest(uint32_t lo, uint32_t) { return lo; }
// Always the highest value.
static uint32_t highest(uint32_t, uint32_t hi) { return hi - 1; }

static void frame(IbusParser& p, const uint16_t ch[14], bool corrupt, bool& ok)
{
    uint8_t b[32] = {0x20, 0x40};
    for (int i = 0; i < 14; i++) { b[2 + i * 2] = ch[i] & 0xFF; b[3 + i * 2] = ch[i] >> 8; }
    uint16_t sum = 0xFFFF;
    for (int i = 0; i < 30; i++) sum -= b[i];
    if (corrupt) sum ^= 1;
    b[30] = sum & 0xFF; b[31] = sum >> 8;
    ok = false;
    for (int i = 0; i < 32; i++) ok = p.feed(b[i]) || ok;
}

int main() {
"""

POSTLUDE = r"""
    return failures ? 1 : 0;
}
"""


def run(body):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "test.cpp"
        binary = Path(directory) / "test"
        source.write_text(PRELUDE + body + POSTLUDE)
        compiled = subprocess.run(
            ["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-I", str(ROOT / "AstroPixelsPlus"),
             str(source), "-o", str(binary)],
            capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout)


class IbusTest(unittest.TestCase):
    def test_valid_frame_sets_channels(self):
        run(r"""
            IbusParser p; bool ok;
            uint16_t ch[14]; for (int i = 0; i < 14; i++) ch[i] = 1000 + i * 50;
            frame(p, ch, false, ok);
            CHECK(ok);
            CHECK(p.channel(1) == 1000);
            CHECK(p.channel(4) == 1150);
            CHECK(p.channel(10) == 1450);
            CHECK(p.channel(0) == 1500);
            CHECK(p.channel(15) == 1500);
        """)

    def test_bad_checksum_is_ignored(self):
        run(r"""
            IbusParser p; bool ok;
            uint16_t ch[14]; for (int i = 0; i < 14; i++) ch[i] = 1900;
            frame(p, ch, true, ok);
            CHECK(!ok);
            CHECK(p.channel(1) == 1500);
        """)

    def test_resyncs_after_garbage(self):
        run(r"""
            IbusParser p; bool ok;
            const uint8_t junk[] = {0x00, 0x20, 0x20, 0x13, 0x40, 0xFF};
            for (uint8_t b : junk) p.feed(b);
            uint16_t ch[14]; for (int i = 0; i < 14; i++) ch[i] = 1234;
            frame(p, ch, false, ok);
            CHECK(ok);
            CHECK(p.channel(3) == 1234);
        """)


class ControlsTest(unittest.TestCase):
    def test_dial_positions(self):
        run(r"""
            CHECK(dialPosition(1000, 6) == 0);
            CHECK(dialPosition(900, 6) == 0);
            CHECK(dialPosition(1500, 6) == 2);
            CHECK(dialPosition(2000, 6) == 5);
            CHECK(dialPosition(2100, 6) == 5);
        """)

    def test_switches(self):
        run(r"""
            CHECK(!switchOn(1000)); CHECK(switchOn(2000));
            CHECK(!switchNotUp(1000)); CHECK(switchNotUp(1500)); CHECK(switchNotUp(2000));
        """)

    def test_switch_level(self):
        run(r"""
            CHECK(switchLevel(1000) == 0);
            CHECK(switchLevel(1249) == 0);
            CHECK(switchLevel(1500) == 1);
            CHECK(switchLevel(1750) == 1);
            CHECK(switchLevel(1751) == 2);
            CHECK(switchLevel(2000) == 2);
        """)

    def test_edge_trigger_ignores_switch_down_at_start(self):
        run(r"""
            EdgeTrigger t;
            CHECK(!t.update(true));
            CHECK(!t.update(false));
            CHECK(t.update(true));
            CHECK(!t.update(true));
        """)

    def test_volume_dial(self):
        run(r"""
            VolumeDial v;
            CHECK(v.update(1500) == 15);
            CHECK(v.update(1510) == -1);    // jitter ignored
            CHECK(v.update(2000) == 30);
            CHECK(v.update(1000) == 0);
            CHECK(v.update(1000) == -1);
        """)

    def test_random_timer(self):
        run(r"""
            RandomTimer t(100, 200);
            CHECK(!t.update(0, true, lowest));     // arms
            CHECK(!t.update(99, true, lowest));
            CHECK(t.update(100, true, lowest));
            CHECK(!t.update(150, false, lowest));  // disabled
            CHECK(!t.update(150, true, lowest));   // re-arms
            CHECK(t.update(250, true, lowest));
        """)

    def test_random_timer_range_from_settings(self):
        run(r"""
            RandomTimer t(100, 200);
            t.setRange(1000, 3000);
            CHECK(!t.update(0, true, highest));
            CHECK(!t.update(2998, true, highest));
            CHECK(t.update(2999, true, highest));
            t.setRange(500, 400);                 // bad range: max forced above min
            CHECK(!t.update(3000, false, highest));
            CHECK(!t.update(3000, true, highest));
            CHECK(t.update(3500, true, highest));
        """)

    def test_mood_sounds(self):
        run(r"""
            const uint8_t clips[5] = {19, 18, 7, 4, 3};
            const uint8_t phrases[5] = {120, 120, 120, 120, 120};
            CHECK(pickMoodSound(3, clips, phrases, lowest) == 51);
            CHECK(pickMoodSound(3, clips, phrases, highest) == 720);
            CHECK(pickMoodSound(0, clips, phrases, lowest) == 1);
            CHECK(pickMoodSound(0, clips, phrases, highest) == 1020);
            const uint8_t no_phrases[5] = {0, 0, 0, 0, 0};
            CHECK(pickMoodSound(3, clips, no_phrases, lowest) == 51);
            CHECK(pickMoodSound(3, clips, no_phrases, highest) == 57);
            const uint8_t no_clips[5] = {0, 0, 0, 0, 0};
            CHECK(pickMoodSound(3, no_clips, phrases, lowest) == 601);
            CHECK(pickMoodSound(3, no_clips, phrases, highest) == 720);
            CHECK(pickMoodSound(0, no_clips, phrases, lowest) == 301);
            CHECK(pickMoodSound(0, no_clips, phrases, highest) == 1020);
        """)


class HcrCommandTest(unittest.TestCase):
    def test_mood_setup_strings(self):
        run(r"""
            CHECK(std::strcmp(HcrCommand::moodSetup(1), "<OR,M1,MN12,MX45>") == 0);
            CHECK(std::strcmp(HcrCommand::moodSetup(2), "<M0,OR,OH30,OC20>") == 0);
            CHECK(std::strcmp(HcrCommand::moodSetup(3), "<M0,OR,OH80>") == 0);
            CHECK(std::strcmp(HcrCommand::moodSetup(4), "<M0,OR,OS85>") == 0);
            CHECK(std::strcmp(HcrCommand::moodSetup(5), "<M0,OR,OH95>") == 0);
            CHECK(std::strcmp(HcrCommand::moodSetup(0), "<OR,M1,MN12,MX45>") == 0);
            CHECK(std::strcmp(HcrCommand::moodSetup(9), "<OR,M1,MN12,MX45>") == 0);
        """)

    def test_stimulus_strings(self):
        run(r"""
            CHECK(std::strcmp(HcrCommand::stimulus(1, false), "<MM>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(1, true), "<SH0>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(2, false), "<SC0>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(2, true), "<SH0>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(3, false), "<SH0>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(3, true), "<SH1>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(4, false), "<SS0>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(4, true), "<SS1>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(5, false), "<SH1>") == 0);
            CHECK(std::strcmp(HcrCommand::stimulus(5, true), "<CA0901C1020>") == 0);
        """)

    def test_queries(self):
        run(r"""
            CHECK(std::strcmp(HcrCommand::queryVocalizing(), "<QPV>") == 0);
            CHECK(std::strcmp(HcrCommand::queryDuration(), "<QT>") == 0);
        """)


class HcrTelemetryParserTest(unittest.TestCase):
    def test_vocalizing_telemetry(self):
        run(r"""
            HcrTelemetryParser parser;
            const char* s1 = "<QPV,1>";
            bool ok = false;
            for (size_t i = 0; i < std::strlen(s1); i++) ok = parser.feed(s1[i]) || ok;
            CHECK(ok);
            CHECK(parser.isVocalizing());

            const char* s2 = "<QPV,0>";
            ok = false;
            for (size_t i = 0; i < std::strlen(s2); i++) ok = parser.feed(s2[i]) || ok;
            CHECK(ok);
            CHECK(!parser.isVocalizing());
        """)

    def test_duration_telemetry(self):
        run(r"""
            HcrTelemetryParser parser;
            const char* s = "<QT,2.35>";
            bool ok = false;
            for (size_t i = 0; i < std::strlen(s); i++) ok = parser.feed(s[i]) || ok;
            CHECK(ok);
            CHECK(std::fabs(parser.currentDuration() - 2.35f) < 0.01f);
        """)

    def test_noise_and_split_packets(self):
        run(r"""
            HcrTelemetryParser parser;
            const char* stream = "noise123<QPV,1>garbage<QT,1.42>tail";
            bool p1 = false, p2 = false;
            for (size_t i = 0; i < std::strlen(stream); i++) {
                if (parser.feed(stream[i])) {
                    if (parser.isVocalizing()) p1 = true;
                    if (std::fabs(parser.currentDuration() - 1.42f) < 0.01f) p2 = true;
                }
            }
            CHECK(p1);
            CHECK(p2);
        """)

    def test_malformed_and_overflow(self):
        run(r"""
            HcrTelemetryParser parser;
            const char* bad = "<UNKNOWN,123><><broken";
            for (size_t i = 0; i < std::strlen(bad); i++) parser.feed(bad[i]);
            CHECK(!parser.isVocalizing());

            // Long packet overflow protection
            parser.feed('<');
            for (int i = 0; i < 60; i++) parser.feed('A');
            parser.feed('>');
            CHECK(!parser.isVocalizing());

            // Resyncs immediately after bad packet
            const char* good = "<QPV,1>";
            bool ok = false;
            for (size_t i = 0; i < std::strlen(good); i++) ok = parser.feed(good[i]) || ok;
            CHECK(ok);
            CHECK(parser.isVocalizing());
        """)


if __name__ == "__main__":
    unittest.main()

