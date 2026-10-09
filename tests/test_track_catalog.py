"""Tests for tools/generate_track_catalog.py."""
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from cpp_test_support import run_cpp
from generate_track_catalog import generate


class TrackCatalogTests(unittest.TestCase):
    def test_valid_csv_generates_compilable_header(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            csv_file = Path(tmpdir) / "tracks.csv"
            header_file = Path(tmpdir) / "TrackCatalog.h"

            csv_file.write_text(
                "track,duration_ms,completion_guard_ms\n"
                "102,4500,6500\n"
                "110,0,22000\n"
            )

            generate(csv_file, header_file)
            self.assertTrue(header_file.exists())

            # Verify C++ compilation and lookup
            program = f"""
#include <cassert>
#include "{header_file}"

int main() {{
    assert(kTrackCatalogCount == 2);
    TrackInfo t102 = lookupTrack(1, 102);
    assert(t102.track == 102);
    assert(t102.duration_ms == 4500);
    assert(t102.completion_guard_ms == 6500);

    TrackInfo t110 = lookupTrack(1, 110);
    assert(t110.track == 110);
    assert(t110.duration_ms == 0);
    assert(t110.completion_guard_ms == 22000);

    // Unknown track returns 600000ms guard and 0 duration
    TrackInfo t99 = lookupTrack(1, 99);
    assert(t99.track == 99);
    assert(t99.duration_ms == 0);
    assert(t99.completion_guard_ms == 600000);
    return 0;
}}
"""
            result = run_cpp(program)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_duplicate_track(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            csv_file = Path(tmpdir) / "tracks.csv"
            header_file = Path(tmpdir) / "TrackCatalog.h"
            csv_file.write_text(
                "track,duration_ms,completion_guard_ms\n"
                "102,0,6500\n"
                "102,0,6500\n"
            )
            with self.assertRaises(ValueError) as ctx:
                generate(csv_file, header_file)
            self.assertIn("duplicate track 102", str(ctx.exception))

    def test_rejects_out_of_range_track(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            csv_file = Path(tmpdir) / "tracks.csv"
            header_file = Path(tmpdir) / "TrackCatalog.h"
            csv_file.write_text(
                "track,duration_ms,completion_guard_ms\n"
                "256,0,6500\n"
            )
            with self.assertRaises(ValueError) as ctx:
                generate(csv_file, header_file)
            self.assertIn("out of range", str(ctx.exception))

    def test_rejects_missing_column(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            csv_file = Path(tmpdir) / "tracks.csv"
            header_file = Path(tmpdir) / "TrackCatalog.h"
            csv_file.write_text(
                "track,duration_ms\n"
                "102,0\n"
            )
            with self.assertRaises(ValueError) as ctx:
                generate(csv_file, header_file)
            self.assertIn("missing required columns", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
