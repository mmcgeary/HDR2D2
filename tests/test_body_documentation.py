import re
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GUIDES = (
    "BODY_CONTROLLER_WIRING.md", "BODY_CONTROLLER_COMMISSIONING.md",
    "SYSTEM_ARCHITECTURE.md", "DOME_WIRING_DIAGRAM.md",
    "POWER_HARNESS_GUIDE.md", "VESC_DRIVE_INTEGRATION.md",
    "ASTROPIXELS_PLUS_UNIFIED/README.md",
)
RING_GUIDES = (
    "BODY_CONTROLLER_WIRING.md", "SYSTEM_ARCHITECTURE.md",
    "DOME_WIRING_DIAGRAM.md", "POWER_HARNESS_GUIDE.md",
    "ASTROPIXELS_PLUS_UNIFIED/README.md",
)
FUSE_ROWS = (
    "| B-SERVO | Body dome-rotation servo | 5A |",
    "| B-LOGIC | Teensy, receiver, DFPlayer, body shifter | 2A |",
    "| D-SERVO | PCA9685 V+ / six holo servos | 5A |",
    "| D-LOGIC | AstroPixels, Hall sensor, dome shifter | 3A |",
)


class BodyDocumentationTests(unittest.TestCase):
    def read(self, name):
        path = ROOT / name
        self.assertTrue(path.exists(), f"Missing build guide: {name}")
        return path.read_text()

    def test_transition_notice_in_every_active_guide(self):
        for name in GUIDES:
            with self.subTest(file=name):
                self.assertIn("Do not connect this wiring to the old ESP32-only firmware",
                              self.read(name))

    def test_carrier_terminal_map(self):
        text = self.read("BODY_CONTROLLER_WIRING.md")
        for row in (
            "| 1 | Serial1 TX | Left VESC COMM RX |",
            "| 0 | Serial1 RX | Left VESC COMM TX |",
            "| 8 | Serial2 TX | Right VESC COMM RX |",
            "| 7 | Serial2 RX | Right VESC COMM TX |",
            "| 14 | Serial3 TX | 1k ohm resistor, then DFPlayer RX |",
            "| 15 | Serial3 RX | DFPlayer TX |",
            "| 17 | Serial4 TX | Slip ring BODY CH3 |",
            "| 16 | Serial4 RX | Slip ring BODY CH6 |",
            "| 21 | Serial5 RX | Body shifter LV1 |",
            "| 24 | Serial6 single-wire TX/RX | Body shifter LV2 |",
            "| 2 | Dome servo pulses | Body shifter LV3 |",
        ):
            with self.subTest(row=row):
                self.assertIn(row, text)
        for selection in ("Treedix", "B09NXYWYK7", "B0FFMLDYNY",
                          "14 Teensy terminal connections", "RX25 is unused"):
            self.assertIn(selection, text)

    def test_shared_slip_ring_map(self):
        for name in RING_GUIDES:
            text = self.read(name)
            for row in (
                "| CH3 | Teensy TX17 | ESP32 GPIO16 | Body to dome serial |",
                "| CH4 | Unconnected | Unconnected | Spare |",
                "| CH5 | Unconnected | Unconnected | Spare |",
                "| CH6 | Teensy RX16 | ESP32 GPIO17 | Dome to body serial |",
            ):
                with self.subTest(file=name, row=row):
                    self.assertIn(row, text)

    def test_approved_output_fuses_and_no_extra_fuse_boxes(self):
        for name in ("BODY_CONTROLLER_WIRING.md", "POWER_HARNESS_GUIDE.md"):
            text = self.read(name)
            for row in FUSE_ROWS:
                with self.subTest(file=name, row=row):
                    self.assertIn(row, text)
            self.assertIn("B0FDJYRGB7", text)
            self.assertIn("No separate 5V fuse boxes", text)
        text = self.read("POWER_HARNESS_GUIDE.md")
        self.assertIn("| F6 | Empty spare |", text)

    def test_pca_logic_and_servo_supplies_are_distinct(self):
        text = self.read("DOME_WIRING_DIAGRAM.md")
        for row in (
            "| I2C D / GPIO21 | SDA |",
            "| I2C C / GPIO22 | SCL |",
            "| ESP32 3.3V | VCC |",
            "| D-SERVO 5A fused 5V | V+ screw terminal |",
        ):
            self.assertIn(row, text)
        self.assertIn("Leave I2C V disconnected", text)
        self.assertIn("0x40", text)

    def test_commissioning_has_concrete_failure_and_usb_checks(self):
        text = self.read("BODY_CONTROLLER_COMMISSIONING.md")
        for heading in (
            "Bare Teensy USB flash", "Carrier continuity", "VUSB/VIN isolation",
            "Logic-shifter communication check", "Transmitter switched off",
            "Receiver iBUS unplugged", "Left VESC UART unplugged",
            "Right VESC UART unplugged", "Dome link unplugged",
            "Web STOP acknowledgement", "Maintenance lock before OTA",
            "Hall update lost during homing", "Full-pack braking",
        ):
            self.assertIn(heading, text)
        self.assertIn("No oscilloscope or logic analyzer", text)
        self.assertIn("USB alone will not power Teensy", text)
        self.assertIn("teensy41_card11b_rev4.png", text)
        self.assertNotIn("instant", text.lower())

    def test_bom_selected_hardware_and_fuse_ratings(self):
        ns = {"s": "urn:schemas-microsoft-com:office:spreadsheet"}
        rows = [
            " | ".join(d.text or "" for d in row.findall("s:Cell/s:Data", ns))
            for row in ET.parse(ROOT / "Master_R2D2_BOM.xls").findall(".//s:Row", ns)
        ]
        for name, status in (("Teensy 4.1", "Ordered"),
                             ("Treedix", "Ordered"),
                             ("Lonely Binary", "Selected"),
                             ("FS-CVT01", "Unused"),
                             ("B0FDJYRGB7", "Owned")):
            self.assertTrue(any(name in row and status in row for row in rows),
                            f"BOM missing {name} / {status}")
        self.assertTrue(any("B-SERVO=5A" in row and "B-LOGIC=2A" in row
                            and "D-SERVO=5A" in row and "D-LOGIC=3A" in row
                            for row in rows))
        ids = [r.split(" | ")[0] for r in rows[1:]]
        self.assertEqual(len(ids), len(set(ids)))

    def test_dual_hall_radio_channels_and_faint_lock_contract(self):
        dome_wiring = self.read("DOME_WIRING_DIAGRAM.md")
        self.assertIn("Two KY-003 Hall sensors (Front 0° and Rear 180°) detect the single dome ring magnet.", dome_wiring)
        self.assertIn("| HV2 / B2 | KY-003 Front Hall signal (0°) |", dome_wiring)
        self.assertIn("| LV2 / A2 | GPIO19 / AUX5 |", dome_wiring)
        self.assertIn("| HV3 / B3 | KY-003 Rear Hall signal (180°) |", dome_wiring)
        self.assertIn("| LV3 / A3 | GPIO18 / AUX4 |", dome_wiring)
        self.assertIn("| GPIO18 | Rear Hall through dome shifter channel 3 / AUX4 |", dome_wiring)
        self.assertIn("| GPIO19 | Front Hall through dome shifter channel 2 / AUX5 |", dome_wiring)

        body_wiring = self.read("BODY_CONTROLLER_WIRING.md")
        self.assertIn("Front Hall signal goes to HV2/B2 (LV2/A2 to GPIO19); Rear Hall signal goes to HV3/B3 (LV3/A3 to GPIO18)", body_wiring)

        arch = self.read("SYSTEM_ARCHITECTURE.md")
        self.assertIn("Two Hall sensors (Front/Rear)", arch)
        self.assertIn("Transmitter CH3 is unused; CH9 gates Auto Dome.", arch)
        self.assertIn("Faint macro does not engage maintenance lock", arch)

        esp_readme = self.read("ASTROPIXELS_PLUS_UNIFIED/README.md")
        self.assertIn("| CH3 / left vertical | Unused |", esp_readme)
        self.assertIn("| CH9 / SwD | Auto Dome enable |", esp_readme)
        self.assertIn("| GPIO18 | Rear Hall signal through dome Lonely Binary channel 3 |", esp_readme)
        self.assertIn("| GPIO19 | Front Hall signal through dome Lonely Binary channel 2 |", esp_readme)
        self.assertIn("Faint macro does not engage maintenance lock", esp_readme)

        ns = {"s": "urn:schemas-microsoft-com:office:spreadsheet"}
        rows = [
            [d.text or "" for d in row.findall("s:Cell/s:Data", ns)]
            for row in ET.parse(ROOT / "Master_R2D2_BOM.xls").findall(".//s:Row", ns)
        ]
        hall_row = next(r for r in rows if len(r) > 2 and r[2] == "Dome Homing Sensors")
        self.assertEqual(hall_row[4], "2")
        self.assertIn("GPIO19", hall_row[6])
        self.assertIn("GPIO18", hall_row[6])

        mag_row = next(r for r in rows if len(r) > 2 and r[2] == "Dome Homing Magnet")
        self.assertEqual(mag_row[4], "1")

    def test_active_guide_local_links_resolve(self):
        for name in GUIDES + ("README.md",):
            path = ROOT / name
            for target in re.findall(r"\[[^\]]*\]\(([^)]+)\)", self.read(name)):
                if "://" in target or target.startswith("#"):
                    continue
                with self.subTest(file=name, target=target):
                    self.assertTrue((path.parent / target.split("#", 1)[0]).exists())


if __name__ == "__main__":
    unittest.main()
