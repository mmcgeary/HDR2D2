import re
import unittest
from pathlib import Path


class FirmwareContractTests(unittest.TestCase):
    def test_timeout_cancels_queued_macro(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        function = source.split("void processDomeHoming() {", 1)[1].split(
            "// ═══════════════════════════════════════════════════════════════════════════════",
            1,
        )[0]
        match = re.search(
            r"if \(millis\(\) - homing_start_time_ms > 6000\) \{(.*?)\n  \}",
            function,
            re.DOTALL,
        )
        self.assertIsNotNone(match, "homing timeout branch should exist")
        timeout_branch = match.group(1)
        self.assertIn("pending_macro_after_home = MACRO_NONE;", timeout_branch)
        self.assertNotIn("startMacro(", timeout_branch)

    def test_mg90s_pwm_counts_use_conservative_1_to_2_ms_range(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        expected = {
            "SERVO_MIN_PULSE": 205,
            "SERVO_MID_PULSE": 307,
            "SERVO_MAX_PULSE": 410,
        }
        for name, count in expected.items():
            with self.subTest(name=name):
                match = re.search(rf"#define {name}\s+(\d+)\b", source)
                self.assertIsNotNone(match, f"{name} should be defined")
                self.assertEqual(int(match.group(1)), count)

    def test_holo_switch_gates_autonomous_but_not_manual_tilt(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        function = source.split("void processHoloServos() {", 1)[1].split(
            "// ═══════════════════════════════════════════════════════════════════════════════",
            1,
        )[0]
        manual_tilt = function.index("pwm.setPWM(1, 0, tilt_pulse);")
        switch_gate = function.find("rc_channels[RC_CH_HOLO_ENABLE]")
        autonomous_motion = function.index("static unsigned long next_twitch_ms")
        self.assertNotEqual(switch_gate, -1, "SwD should gate autonomous motion")
        self.assertLess(manual_tilt, switch_gate)
        self.assertLess(switch_gate, autonomous_motion)
        self.assertIn("if (rc_channels[RC_CH_HOLO_ENABLE] < 1500) return;", function)

    def test_cantina_macro_uses_provisional_30_second_track_duration(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        self.assertTrue(
            re.search(r"\{\s*106,\s*30000\s*\}.*MACRO_CANTINA", source),
            "Cantina track should use the provisional 30-second duration",
        )

    def test_cantina_macro_moves_servos_in_staggered_steps(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        function = source.split("void processHoloServos() {", 1)[1].split(
            "// ═══════════════════════════════════════════════════════════════════════════════",
            1,
        )[0]
        self.assertTrue("if (current_macro == MACRO_CANTINA)" in function)
        self.assertTrue("pwm.setPWM(servo, 0, pulse);" in function)
        self.assertTrue("(now - macro_start_time_ms) / 250" in function)

    def test_scream_macro_uses_low_amplitude_staggered_holo_twitches(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        function = source.split("void processHoloServos() {", 1)[1].split(
            "// ═══════════════════════════════════════════════════════════════════════════════",
            1,
        )[0]
        self.assertIn("if (current_macro == MACRO_SCREAM)", function)
        scream = function.split("if (current_macro == MACRO_SCREAM)", 1)[1].split(
            "if (current_macro == MACRO_CANTINA)", 1
        )[0]
        self.assertIn("pwm.setPWM(target_servo, 0, pulse);", scream)
        self.assertIn("SERVO_MID_PULSE - 35", scream)
        self.assertIn("SERVO_MID_PULSE + 36", scream)
        self.assertIn("random(100, 251)", scream)

    def test_marching_pattern_helper_is_defined_at_file_scope(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        definitions = re.findall(r"(?m)^void fillMarchingPattern\(.*\) \{", source)
        self.assertEqual(len(definitions), 1)
        self.assertIn(
            "void fillMarchingPattern(CRGB* leds, uint16_t count, uint8_t phase);",
            source,
        )

    def test_faint_macro_disables_servos_and_blacks_out_all_led_groups(self):
        source = Path("ASTROPIXELS_UNIFIED_BRAIN.ino").read_text()
        self.assertTrue("pwm.setPWM(ch, 0, 4096);" in source)
        start_macro = source.split("void startMacro(ActiveMacro macro) {", 1)[1].split(
            "void processMacroTimers()", 1
        )[0]
        self.assertTrue("if (macro == MACRO_FAINT)" in start_macro)
        self.assertTrue("disableAllHoloServos();" in start_macro)

        lighting = source.split("void processAstroPixelsLighting() {", 1)[1]
        faint = lighting.split("case MACRO_FAINT:", 1)[1].split("default:", 1)[0]
        for leds in ("leds_rld", "leds_fld", "leds_fpsi", "leds_rpsi",
                     "leds_fhp", "leds_rhp", "leds_thp"):
            with self.subTest(leds=leds):
                self.assertTrue(f"fill_solid({leds}," in faint, f"{leds} should be included in Faint")


if __name__ == "__main__":
    unittest.main()
