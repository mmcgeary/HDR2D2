"""Checks the holo servo table in AstroPixelsPlus.ino against WIRING.md."""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class ServoTableTest(unittest.TestCase):
    def test_holo_servos_on_pca9685_channels_0_to_5(self):
        sketch = (ROOT / "AstroPixelsPlus" / "AstroPixelsPlus.ino").read_text()
        table = sketch[sketch.index("const ServoSettings servoSettings[]"):]
        table = table[:table.index("};")]
        pins = [int(p) for p in re.findall(r"\{\s*(\d+)\s*,\s*\d+\s*,\s*\d+\s*,", table)]
        # ReelTwo's ServoDispatchPCA9685 numbers pins from 1: pin 1 is channel 0.
        self.assertEqual(pins, [1, 2, 3, 4, 5, 6])


if __name__ == "__main__":
    unittest.main()
