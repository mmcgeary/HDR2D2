"""Host tests for AstroPixelsPlus/R2Logic.h. Run: python3 -m unittest discover tests"""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r"""
#include <cstdio>
#include <cstdlib>
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

static DomeInputs dome(uint32_t now, uint16_t stick, bool randomOn, bool atFront, bool driving = false)
{
    DomeInputs in;
    in.now = now; in.radio = true; in.stick = stick;
    in.randomOn = randomOn; in.driving = driving; in.atFront = atFront;
    return in;
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

    def test_mood_sounds(self):
        run(r"""
            const uint8_t sizes[5] = {19, 18, 7, 4, 3};
            uint8_t bank, sound;
            pickMoodSound(3, sizes, highest, bank, sound);
            CHECK(bank == 3 && sound == 7);
            pickMoodSound(5, sizes, lowest, bank, sound);
            CHECK(bank == 5 && sound == 1);
            pickMoodSound(0, sizes, lowest, bank, sound);
            CHECK(bank == 1 && sound == 1);
            pickMoodSound(0, sizes, highest, bank, sound);
            CHECK(bank == 5 && sound == 3);
        """)


class DomeTest(unittest.TestCase):
    def test_no_pulses_before_radio(self):
        run(r"""
            DomeBrain d(lowest);
            DomeInputs in = dome(0, 1500, true, false); in.radio = false;
            CHECK(d.update(in) == 0);
        """)

    def test_stick_passes_through_and_neutral_includes_subtrim(self):
        run(r"""
            DomeBrain d(lowest);
            CHECK(d.update(dome(0, 1800, false, false)) == 1800);
            CHECK(d.state() == DomeBrain::kManual);
            CHECK(d.update(dome(10, 1530, false, false)) == 1530);
            CHECK(d.neutral() == 1530);
            DomeInputs lost = dome(20, 1500, false, false); lost.radio = false;
            CHECK(d.update(lost) == 1530);   // keeps the learned neutral
        """)

    def test_random_move_cycle_from_front(self):
        run(r"""
            DomeBrain d(lowest);   // direction -1 (rnd 0), shortest pauses and turns
            uint32_t t = 0;
            CHECK(d.update(dome(t, 1500, true, true)) == 1500);
            t = 3000; d.update(dome(t, 1500, true, true));
            CHECK(d.state() == DomeBrain::kAtFront);
            t = 7000;                        // 4s pause
            CHECK(d.update(dome(t, 1500, true, true)) == 1380);
            CHECK(d.state() == DomeBrain::kTurningAway);
            t = 7400;                        // 400ms turn
            CHECK(d.update(dome(t, 1500, true, false)) == 1500);
            CHECK(d.state() == DomeBrain::kLooking);
            t = 8400;                        // 1s look
            CHECK(d.update(dome(t, 1500, true, false)) == 1580);
            CHECK(d.state() == DomeBrain::kReturning);
            t = 9000;
            CHECK(d.update(dome(t, 1500, true, true)) == 1500);
            CHECK(d.state() == DomeBrain::kAtFront);
        """)

    def test_seeks_front_then_faults_on_timeout(self):
        run(r"""
            DomeBrain d(lowest);
            d.update(dome(0, 1500, true, false));
            CHECK(d.update(dome(3000, 1500, true, false)) == 1580);
            CHECK(d.state() == DomeBrain::kSeeking);
            CHECK(d.update(dome(23000, 1500, true, false)) == 1500);
            CHECK(d.state() == DomeBrain::kFault);
            CHECK(d.update(dome(30000, 1500, true, false)) == 1500);   // stays stopped
            d.update(dome(31000, 1500, false, false));                  // switch off
            CHECK(d.state() == DomeBrain::kIdle);
            CHECK(d.update(dome(32000, 1500, true, false)) == 1580);   // seeks again
        """)

    def test_seek_stops_at_magnet(self):
        run(r"""
            DomeBrain d(lowest);
            d.update(dome(0, 1500, true, false));
            d.update(dome(3000, 1500, true, false));
            CHECK(d.update(dome(5000, 1500, true, true)) == 1500);
            CHECK(d.state() == DomeBrain::kAtFront);
        """)

    def test_driving_sends_dome_home_and_blocks_new_moves(self):
        run(r"""
            DomeBrain d(lowest);
            d.update(dome(0, 1500, true, true));
            d.update(dome(3000, 1500, true, true));
            d.update(dome(7000, 1500, true, true));
            CHECK(d.state() == DomeBrain::kTurningAway);
            CHECK(d.update(dome(7100, 1500, true, false, true)) == 1580);
            CHECK(d.state() == DomeBrain::kReturning);
            d.update(dome(7500, 1500, true, true, true));
            CHECK(d.state() == DomeBrain::kAtFront);
            CHECK(d.update(dome(20000, 1500, true, true, true)) == 1500);   // no move while driving
            CHECK(d.state() == DomeBrain::kAtFront);
            CHECK(d.update(dome(22000, 1500, true, true)) == 1500);          // 3s quiet needed
            CHECK(d.update(dome(23500, 1500, true, true)) == 1380);
        """)

    def test_switch_off_mid_move_still_returns_home(self):
        run(r"""
            DomeBrain d(lowest);
            d.update(dome(0, 1500, true, true));
            d.update(dome(3000, 1500, true, true));
            d.update(dome(7000, 1500, true, true));
            CHECK(d.update(dome(7200, 1500, false, false)) == 1580);
            CHECK(d.state() == DomeBrain::kReturning);
            d.update(dome(7600, 1500, false, true));
            CHECK(d.update(dome(7700, 1500, false, true)) == 1500);
            CHECK(d.state() == DomeBrain::kIdle);
        """)

    def test_stick_overrides_random_and_position_is_relearned(self):
        run(r"""
            DomeBrain d(lowest);
            d.update(dome(0, 1500, true, true));
            d.update(dome(3000, 1500, true, true));
            d.update(dome(7000, 1500, true, true));
            CHECK(d.update(dome(7100, 1900, true, false)) == 1900);
            CHECK(d.state() == DomeBrain::kManual);
            CHECK(d.update(dome(7200, 1500, true, false)) == 1500);
            CHECK(d.state() == DomeBrain::kIdle);
            CHECK(d.update(dome(10200, 1500, true, false)) == 1580);    // seeks front
        """)

    def test_return_times_out_into_fault(self):
        run(r"""
            DomeBrain d(lowest);
            d.update(dome(0, 1500, true, true));
            d.update(dome(3000, 1500, true, true));
            d.update(dome(7000, 1500, true, true));      // 400ms turn: budget 6.2s
            d.update(dome(7400, 1500, true, false));
            d.update(dome(8400, 1500, true, false));
            CHECK(d.state() == DomeBrain::kReturning);
            CHECK(d.update(dome(14600, 1500, true, false)) == 1500);
            CHECK(d.state() == DomeBrain::kFault);
        """)


if __name__ == "__main__":
    unittest.main()
