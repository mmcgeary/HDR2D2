"""Tests for tools/magnet_template.py."""

import math
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import magnet_template  # noqa: E402


class MagnetTemplateTest(unittest.TestCase):
    def test_positions_match_the_firmware_ring(self):
        firmware = (ROOT / "AstroPixelsPlus/R2DomeTracker.h").read_text()
        ring = re.search(r"kMagnetDeg\[kMagnetCount\] = \{([^}]*)\}", firmware).group(1)
        self.assertEqual([int(v) for v in ring.split(",")], magnet_template.MAGNETS)

    def test_positions_are_on_the_circle_clockwise_from_front(self):
        points = magnet_template.magnet_positions(250)
        self.assertEqual(len(points), 13)
        for deg, x, y in points:
            self.assertAlmostEqual(math.hypot(x, y), 125, places=6)
        deg, x, y = points[0]
        self.assertEqual((deg, round(x, 6), round(y, 6)), (0, 0, -125))      # front is up the page
        deg, x, y = points[1]
        self.assertEqual(deg, 5)
        self.assertGreater(x, 0)                                             # clockwise = to the right

    def test_svg_has_marks_scale_bar_and_true_size(self):
        svg = magnet_template.template_svg(250)
        self.assertEqual(svg.count('class="magnet"'), 13)
        self.assertIn('width="300mm"', svg)
        self.assertIn('class="scale"', svg)
        bar = re.search(r'<line class="scale" x1="([\d.]+)" y1="[\d.]+" x2="([\d.]+)"', svg)
        self.assertAlmostEqual(float(bar.group(2)) - float(bar.group(1)), 100.0)
        self.assertIn("FRONT", svg)

    def test_command_line_writes_a_file(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "ring.svg"
            subprocess.run([sys.executable, str(ROOT / "tools/magnet_template.py"), "260", str(out)], check=True)
            self.assertIn("<svg", out.read_text())


if __name__ == "__main__":
    unittest.main()
