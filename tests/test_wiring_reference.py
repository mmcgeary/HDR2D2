"""Checks wiring_reference.html: its data is complete, matches the firmware pins, and every wire routes."""

import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAGE = (ROOT / "wiring_reference.html").read_text()
DATA = json.loads(re.search(r'<script id="harness-data" type="application/json">(.*?)</script>', PAGE, re.S).group(1))
ENGINE = re.search(r'<script id="routing-engine">(.*?)</script>', PAGE, re.S).group(1)
COMPONENTS, WIRES = DATA["components"], DATA["wires"]

# Ports that are deliberately left unconnected.
UNUSED_PORTS = {"fuseBlock:F2 SPARE"}


def endpoints():
    for wire in WIRES:
        for end in (wire["from"], wire["to"]):
            component, port = end.split(":", 1)
            yield wire, component, port


class DataTest(unittest.TestCase):
    def test_wire_ids_unique(self):
        ids = [w["id"] for w in WIRES]
        self.assertEqual(len(ids), len(set(ids)))

    def test_endpoints_exist(self):
        for wire, component, port in endpoints():
            self.assertIn(component, COMPONENTS, wire["id"])
            self.assertIn(port, COMPONENTS[component]["ports"], wire["id"])

    def test_right_side_ports_exist(self):
        for cid, component in COMPONENTS.items():
            for port in component["right"]:
                self.assertIn(port, component["ports"], cid)

    def test_every_port_is_wired(self):
        used = {f"{c}:{p}" for _, c, p in endpoints()}
        ports = {f"{cid}:{p}" for cid, c in COMPONENTS.items() for p in c["ports"]}
        self.assertEqual(ports - used, UNUSED_PORTS)

    def test_categories_have_filters(self):
        options = set(re.findall(r'<option value="([a-z0-9]+)"', PAGE)) - {"all"}
        self.assertEqual({w["cat"] for w in WIRES}, options)

    def test_every_wire_has_a_kind(self):
        for wire in WIRES:
            self.assertIn(wire["kind"], {"power", "ground", "signal"}, wire["id"])

    def test_slip_ring_contacts_used_once_per_side(self):
        for n in range(1, 7):
            for side in ("BODY", "DOME"):
                count = sum(1 for _, c, p in endpoints() if c == "ring" and p == f"CH{n} {side}")
                self.assertEqual(count, 1, f"CH{n} {side}")


class FirmwarePinsTest(unittest.TestCase):
    def test_esp_pins_match_firmware(self):
        ino = (ROOT / "AstroPixelsPlus/AstroPixelsPlus.ino").read_text()
        remote = (ROOT / "AstroPixelsPlus/R2Remote.h").read_text()
        aux = dict(re.findall(r"#define (PIN_AUX\d) (\d+)", ino))
        pins = {
            "iBUS": re.search(r"#define R2_IBUS_RX_PIN (\d+)", remote).group(1),
            "DOME SERVO": aux[re.search(r"#define R2_DOME_SERVO_PIN (PIN_AUX\d)", remote).group(1)],
            "HALL": aux[re.search(r"#define R2_FRONT_HALL_PIN (PIN_AUX\d)", remote).group(1)],
            "DF TX": aux[re.search(r"#define SOUND_RX_PIN (PIN_AUX\d)", ino).group(1)],
            "DF RX": aux[re.search(r"#define SOUND_TX_PIN (PIN_AUX\d)", ino).group(1)],
        }
        for function, gpio in pins.items():
            self.assertIn(f"GPIO{gpio} {function}", COMPONENTS["esp"]["ports"], function)


@unittest.skipUnless(shutil.which("node"), "node not installed")
class RoutingTest(unittest.TestCase):
    def test_every_wire_routes(self):
        script = ENGINE + "\nconst data = " + json.dumps(DATA) + ";\n" + \
            "const {routes} = buildLayout(data); console.log(routes.length);"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "route.js"
            path.write_text(script)
            result = subprocess.run(["node", str(path)], capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(int(result.stdout.strip()), len(WIRES))


if __name__ == "__main__":
    unittest.main()
