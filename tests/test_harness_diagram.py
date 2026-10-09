import json
import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class HarnessDiagramTests(unittest.TestCase):
    def load_data(self):
        path = ROOT / "wiring_visualizer.html"
        self.assertTrue(path.exists(), "Corrected electrical diagram is missing")
        match = re.search(
            r'<script id="harness-data" type="application/json">(.*?)</script>',
            path.read_text(), re.S)
        self.assertIsNotNone(match, "Diagram needs inspectable circuit data")
        return json.loads(match.group(1))

    def test_all_wires_have_valid_endpoints_and_unique_ids(self):
        data = self.load_data()
        ids = [wire["id"] for wire in data["wires"]]
        self.assertEqual(len(ids), len(set(ids)))
        for component in data["components"].values():
            self.assertEqual(len(component["ports"]), len(set(component["ports"])))
            for connection in component.get("connections", []):
                self.assertEqual(len(connection), 2)
                for port in connection:
                    self.assertIn(port, component["ports"])
        for wire in data["wires"]:
            for endpoint in (wire["from"], wire["to"]):
                component, terminal = endpoint.split(":", 1)
                self.assertIn(component, data["components"])
                self.assertIn(terminal, data["components"][component]["ports"])
            self.assertTrue(wire["spec"])
            self.assertTrue(wire["note"])

    def test_main_protection_precedes_cutoff(self):
        data = self.load_data()
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        self.assertIn(("battery:+", "mainFuse:IN"), routes)
        self.assertIn(("mainFuse:OUT", "cutoff:IN"), routes)
        self.assertIn(("cutoff:OUT", "fuseBlock:+ FEED"), routes)
        self.assertNotIn(("battery:+", "cutoff:IN"), routes)
        self.assertEqual(data["components"]["mainFuse"]["amps"], 25)

    def test_fuse_box_contains_both_supply_buses(self):
        data = self.load_data()
        self.assertNotIn("negative", data["components"])
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        self.assertIn(("battery:-", "fuseBlock:- FEED"), routes)
        for wire_id in ("drive-return", "body-input-return",
                        "dome-feed-return", "amp-return", "vesc-comm-ground",
                        "right-vesc-comm-ground"):
            wire = next(w for w in data["wires"] if w["id"] == wire_id)
            self.assertEqual(wire["to"], "fuseBlock:NEGATIVE BUS")

    def test_slip_ring_carries_battery_voltage_not_five_volts(self):
        data = self.load_data()
        wires = {wire["id"]: wire for wire in data["wires"]}
        self.assertEqual(wires["dome-feed"]["from"], "fuseBlock:F4 7.5A")
        self.assertEqual(wires["dome-feed"]["to"], "ringBody:CH1")
        self.assertEqual(wires["dome-input"]["from"], "ringDome:CH1")
        self.assertEqual(wires["dome-input"]["to"], "domeBuck:IN+")
        ring_contacts = [wire for wire in data["wires"] if wire["cat"] == "ring"]
        self.assertEqual(len(ring_contacts), 6)
        self.assertEqual(wires["ring3"]["from"], "ringBody:CH3")
        self.assertEqual(wires["ring3"]["to"], "ringDome:CH3")
        self.assertEqual(wires["body-link-tx"]["from"], "teensy:17 TX4")
        self.assertEqual(wires["body-link-tx"]["to"], "ringBody:CH3")
        self.assertEqual(wires["dome-link-rx"]["from"], "ringDome:CH3")
        self.assertEqual(wires["dome-link-rx"]["to"], "esp:GPIO16 RX")
        self.assertEqual(wires["dome-link-tx"]["from"], "esp:GPIO17 TX")
        self.assertEqual(wires["dome-link-tx"]["to"], "ringDome:CH6")
        self.assertEqual(wires["body-link-rx"]["from"], "ringBody:CH6")
        self.assertEqual(wires["body-link-rx"]["to"], "teensy:16 RX4")

    def test_five_volt_positive_rails_are_separate(self):
        data = self.load_data()
        graph = {}
        for wire in data["wires"]:
            graph.setdefault(wire["from"], set()).add(wire["to"])
            graph.setdefault(wire["to"], set()).add(wire["from"])
        visited, pending = set(), ["bodyBuck:OUT+"]
        while pending:
            node = pending.pop()
            if node in visited:
                continue
            visited.add(node)
            pending.extend(graph.get(node, set()) - visited)
        self.assertNotIn("domeBuck:OUT+", visited)
        self.assertNotIn("ringBody:CH1", visited)

    def test_receiver_does_not_parallel_vesc_bec_outputs(self):
        data = self.load_data()
        endpoints = {endpoint for wire in data["wires"]
                     for endpoint in (wire["from"], wire["to"])}
        for vesc in ("leftVesc", "rightVesc"):
            self.assertNotIn(f"{vesc}:PPM +5V UNUSED", endpoints)
            self.assertNotIn(f"{vesc}:TEMP UNUSED", endpoints)
        hall_wires = [w for w in data["wires"] if w["cat"] == "hall"]
        self.assertEqual(len(hall_wires), 10)
        self.assertTrue(all(w.get("awg") == 22 and w["length_m"] == 2
                            for w in hall_wires))
        phase_wires = [w for w in data["wires"] if w["cat"] == "phase"]
        self.assertEqual(len(phase_wires), 6)
        self.assertTrue(all(w["awg"] == 12 and w["length_m"] == 2
                            for w in phase_wires))

    def test_protected_branch_schedule_matches_approved_values(self):
        data = self.load_data()
        self.assertEqual(data["components"]["fuseBlock"]["fuses"],
                         {"F1": 15, "F3": 7.5,
                          "F4": 7.5, "F5": 5})
        for component in ("body5", "dome5"):
            block = data["components"][component]
            self.assertNotIn("fuses", block)
            self.assertIn("distribution", block["name"])
            self.assertTrue(all(not re.search(r"\d+A", p) for p in block["ports"]))

    def test_dual_vesc_has_one_shared_power_feed(self):
        data = self.load_data()
        wires = {w["id"]: w for w in data["wires"]}
        self.assertEqual(wires["drive-input"]["from"], "fuseBlock:F1 15A")
        self.assertEqual(wires["drive-input"]["to"], "driveSupply:B+")
        self.assertEqual(wires["drive-return"]["from"], "driveSupply:B-")
        for controller in ("leftVesc", "rightVesc"):
            self.assertNotIn("B+", data["components"][controller]["ports"])
            self.assertNotIn("B-", data["components"][controller]["ports"])
        self.assertTrue(all("fuseBlock:F2 UNUSED" not in (w["from"], w["to"])
                            for w in data["wires"]))

    def test_body_controller_owns_direct_uart_links(self):
        data = self.load_data()
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        for route in (
            ("teensy:1 TX1", "leftVesc:COMM RX"),
            ("leftVesc:COMM TX", "teensy:0 RX1"),
            ("teensy:8 TX2", "rightVesc:COMM RX"),
            ("rightVesc:COMM TX", "teensy:7 RX2"),
            ("teensy:14 TX3", "dfplayer:RX"),
            ("dfplayer:TX", "teensy:15 RX3"),
        ):
            self.assertIn(route, routes)
        audio = next(w for w in data["wires"] if w["to"] == "dfplayer:RX")
        self.assertRegex(audio["spec"], r"1k\s*ohm")
        self.assertNotIn("telemetry", data["components"])

    def test_spare_ring_contacts_have_no_external_connections(self):
        data = self.load_data()
        for contact in ("CH4", "CH5"):
            ends = {f"ringBody:{contact}", f"ringDome:{contact}"}
            edges = [w for w in data["wires"]
                     if w["from"] in ends or w["to"] in ends]
            self.assertEqual(len(edges), 1)
            self.assertEqual(edges[0]["cat"], "ring")
            for component in ("ringBody", "ringDome"):
                self.assertIn("SPARE", data["components"][component]["portLabels"][contact])

    def test_pca_uses_motherboard_headers_and_separate_servo_power(self):
        data = self.load_data()
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        self.assertIn(("esp:I2C G", "pca:LOGIC GND"), routes)
        self.assertIn(("esp:3.3V", "pca:VCC 3.3V"), routes)
        self.assertIn(("domeServoFuse:OUT", "pca:V+ 5V"), routes)
        wires = {w["id"]: w for w in data["wires"]}
        for wire_id in ("pca-power", "pca-return"):
            self.assertEqual(wires[wire_id]["awg"], 16)
        self.assertTrue(all("esp:I2C V UNUSED" not in (w["from"], w["to"])
                            for w in data["wires"]))
        self.assertIn("VERIFY GND", data["components"]["pca"]["portLabels"]["GND"])
        servo_routes = [(w["from"], w["to"]) for w in data["wires"]
                        if w["from"].startswith("pca:CH")]
        self.assertEqual(servo_routes, [
            ("pca:CH0 S", "frontPan:PWM"), ("pca:CH1 S", "frontTilt:PWM"),
            ("pca:CH2 S", "rearPan:PWM"), ("pca:CH3 S", "rearTilt:PWM"),
            ("pca:CH4 S", "topPan:PWM"), ("pca:CH5 S", "topTilt:PWM"),
        ])

    def test_selected_amplifier_is_hf82(self):
        amp = self.load_data()["components"]["amp"]
        self.assertIn("HF82", amp["name"])
        self.assertIn("8-26V", amp["note"])
        self.assertIn("solder", amp["note"])

    def test_i2c_has_distinct_colors_and_renderer_uses_lane_policy(self):
        page = (ROOT / "wiring_visualizer.html").read_text()
        self.assertIn("signalColor(wire)", page)
        renderer = page.split('const powerRoutes=[];')[1]
        self.assertIn("wiresNeedSeparateLanes(wire,r.wire)", renderer)

    def test_default_volume_maps_to_ten_of_thirty(self):
        source = (ROOT / "ASTROPIXELS_PLUS_UNIFIED" /
                  "ASTROPIXELS_PLUS_UNIFIED.ino").read_text()
        value = int(re.search(r"#define MARC_SOUND_VOLUME\s+(\d+)", source).group(1))
        import math
        self.assertEqual(math.ceil(value / 1000 * 30), 10)

    def test_receiver_and_servo_use_local_body_shifter(self):
        data = self.load_data()
        self.assertIn("Body", data["components"]["receiver"]["area"])
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        for route in (
            ("bodyElectronics:RECEIVER +", "receiver:5V"),
            ("receiver:GND", "body5:GND"),
            ("receiver:iBUS SIG", "bodyShifter:HV1 iBUS 5V"),
            ("bodyShifter:LV1 iBUS 3.3V", "teensy:21 RX5"),
            ("receiver:SENS DATA", "bodyShifter:HV2 SENS 5V"),
            ("bodyShifter:LV2 SENS 3.3V", "teensy:24 SENS6"),
            ("teensy:2 SERVO", "bodyShifter:LV3 SERVO 3.3V"),
            ("bodyShifter:HV3 SERVO 5V", "domeServo:PWM"),
        ):
            self.assertIn(route, routes)
        self.assertRegex(data["components"]["teensy"]["note"], r"half.duplex")
        self.assertIn("open-drain", data["components"]["teensy"]["note"])

    def test_shifters_have_local_references_and_only_selected_channels(self):
        data = self.load_data()
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        for component, source, rail, ground in (
            ("bodyShifter", "teensy", "bodyElectronics", "body5"),
            ("shifter", "esp", "domeElectronics", "dome5"),
        ):
            self.assertIn((f"{source}:3.3V", f"{component}:LV 3.3V"), routes)
            self.assertIn((f"{rail}:SHIFTER +", f"{component}:HV 5V"), routes)
            self.assertIn((f"{component}:GND", f"{ground}:GND"), routes)
            shifter = data["components"][component]
            self.assertIn("B0FFMLDYNY", shifter["note"])
            self.assertIn("10k", shifter["note"])
            for port in shifter["ports"]:
                if port.startswith(("LV1", "LV2", "LV3", "LV4", "HV1", "HV2", "HV3", "HV4")):
                    self.assertRegex(shifter["portLabels"][port], r"[AB][1-4]")
        self.assertIn(("hall:SIGNAL", "shifter:HV2 Hall IN 5V"), routes)
        self.assertIn(("shifter:LV2 Hall OUT 3.3V", "esp:GPIO19 HALL"), routes)
        endpoints = {e for w in data["wires"] for e in (w["from"], w["to"])}
        for component, channels in (("bodyShifter", {"1", "2", "3"}),
                                    ("shifter", {"2"})):
            used = {re.search(r":(?:LV|HV)([1-4])", endpoint).group(1)
                    for endpoint in endpoints
                    if re.match(rf"{component}:(?:LV|HV)[1-4]", endpoint)}
            self.assertEqual(used, channels)

    def test_unused_terminals_never_have_active_wires(self):
        data = self.load_data()
        for wire in data["wires"]:
            self.assertNotIn("UNUSED", wire["from"] + wire["to"])
            self.assertNotRegex(wire["from"] + wire["to"], r"esp:GPIO(?:5|18|4)\b|CAN LINK")
        for controller in ("leftVesc", "rightVesc"):
            for port in ("COMM 5V UNUSED", "COMM 3.3V UNUSED",
                         "COMM ADC UNUSED", "COMM ADC2 UNUSED"):
                self.assertIn(port, data["components"][controller]["ports"])
            self.assertIn("CAN switch OFF", data["components"][controller]["note"])
        inventory = data["components"]["driveSupply"]["ports"]
        for port in ("RECEIVER GND UNUSED", "RECEIVER 5V UNUSED", "RECEIVER SIN UNUSED"):
            self.assertIn(port, inventory)
        endpoints = {e for w in data["wires"] for e in (w["from"], w["to"])}
        self.assertEqual(len([e for e in endpoints if e.startswith("teensy:")]), 14)

    def test_every_five_volt_load_is_downstream_of_its_approved_inline_fuse(self):
        data = self.load_data()
        branches = (
            ("bodyServoFuse", 5, "body5:FEED", ["domeServo:5V"]),
            ("bodyElectronicsFuse", 2, "body5:FEED",
             ["teensy:VIN", "receiver:5V", "dfplayer:5V", "bodyShifter:HV 5V"]),
            ("domeServoFuse", 5, "dome5:FEED",
             ["pca:V+ 5V"] + [f"{s}:5V" for s in
                             ("frontPan", "frontTilt", "rearPan", "rearTilt", "topPan", "topTilt")]),
            ("domeElectronicsFuse", 3, "dome5:FEED",
             ["esp:5V TERMINAL", "hall:5V", "shifter:HV 5V", "capacitor:+"] +
             [f"lights:{p}" for p in data["components"]["lights"]["ports"]]),
        )
        def reachable(source, removed=None):
            graph = {}
            pairs = [(w["from"], w["to"]) for w in data["wires"]]
            for component, record in data["components"].items():
                if component != removed:
                    pairs += [(f"{component}:{a}", f"{component}:{b}")
                              for a, b in record.get("connections", [])]
            for a, b in pairs:
                graph.setdefault(a, set()).add(b)
                graph.setdefault(b, set()).add(a)
            visited, pending = set(), [source]
            while pending:
                node = pending.pop()
                if node not in visited:
                    visited.add(node)
                    pending.extend(graph.get(node, set()) - visited)
            return visited
        for fuse, rating, source, loads in branches:
            with self.subTest(fuse=fuse):
                self.assertIn(fuse, data["components"])
                component = data["components"][fuse]
                self.assertEqual(component["amps"], rating)
                self.assertIn("B0FDJYRGB7", component["note"])
                self.assertEqual(component["connections"], [["IN", "OUT"]])
                powered = reachable(source)
                opened = reachable(source, fuse)
                for load in loads:
                    self.assertIn(load, powered, f"{load} has no explicit supply path")
                    self.assertNotIn(load, opened, f"{load} bypasses {fuse}")
        self.assertNotIn("dome5:FEED", reachable("body5:FEED"))

    def test_audio_isolator_and_btl_outputs_are_not_bridged_to_ground(self):
        data = self.load_data()
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        for route in (
            ("dfplayer:DAC_L", "isolator:IN L"),
            ("dfplayer:DAC_GND", "isolator:IN GND"),
            ("isolator:OUT L", "amp:LINE L"),
            ("isolator:OUT GND", "amp:LINE GND"),
            ("amp:L+", "speaker:+"), ("amp:L-", "speaker:-"),
        ):
            self.assertIn(route, routes)
        self.assertEqual(len([w for w in data["wires"] if
                              w["from"].startswith("speaker:") or
                              w["to"].startswith("speaker:")]), 2)
        self.assertFalse(data["components"]["isolator"].get("connections"))

    def test_signal_colors_distinguish_groups_and_follow_complete_paths(self):
        page = (ROOT / "wiring_visualizer.html").read_text()
        engine = re.search(r'<script id="routing-engine">(.*?)</script>', page, re.S)
        program = engine.group(1) + "\nconst data=" + json.dumps(self.load_data()) + r""";
            const groups=[
                ["body-link-tx","ring3","dome-link-rx"],
                ["dome-link-tx","ring6","body-link-rx"],
                ["ibus-shifter","ibus-low"],["sensor-high","sensor-low"],
                ["dome-pwm-body","dome-pwm-high"],["home-high","home-low"],
                ["left-vesc-tx"],["left-vesc-rx"],
                ["right-vesc-tx"],["right-vesc-rx"],
                ["audio-rx-body"],["audio-feedback"],["pca-sda"],["pca-scl"]
            ];
            const colors=groups.map(ids=>{
                const values=ids.map(id=>signalColor(data.wires.find(w=>w.id===id)));
                if(values.some(c=>!c)||new Set(values).size!==1)
                    throw Error("Broken end-to-end signal color: "+ids);
                return values[0];
            });
            if(new Set(colors).size!==groups.length)
                throw Error("Adjacent signal groups need distinct colors");
            for(const wire of data.wires) {
                const color=wireColor(wire);
                if(!/^#[0-9a-f]{6}$/i.test(color)) throw Error("Invalid color: "+wire.id);
                if(isGroundWire(wire)&&color!=="#000000")
                    throw Error("Ground must remain black: "+wire.id);
                if(!isGroundWire(wire)&&isPowerWire(wire)&&color!=="#ef4444")
                    throw Error("Positive supply must remain red: "+wire.id);
                if(wire.cat==="logic"&&!isGroundWire(wire)&&!isPowerWire(wire)&&
                   !signalColor(wire)) throw Error("Uncolored logic signal: "+wire.id);
            }
            console.log("Distinct signal groups and continuous paths verified");
        """
        result = subprocess.run(["node", "-e", program], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_routes_exit_terminals_and_avoid_every_component_box(self):
        page = (ROOT / "wiring_visualizer.html").read_text()
        engine = re.search(r'<script id="routing-engine">(.*?)</script>',
                           page, re.S)
        self.assertIsNotNone(engine, "Obstacle-aware routing engine is missing")
        data = self.load_data()
        program = engine.group(1) + "\nconst data=" + json.dumps(data) + r""";
            layoutComponents(data.components);
            for(const id of ["speaker-positive","speaker-negative"]) {
                if(isGroundWire(data.wires.find(w=>w.id===id)))
                    throw Error("BTL speaker lead marked ground: "+id);
            }
            for(const id of ["teensy-return","right-vesc-comm-ground","body-shifter-return",
                             "shifter-return","pca-return","ring2"]) {
                if(!isGroundWire(data.wires.find(w=>w.id===id)))
                    throw Error("Ground reference not marked ground: "+id);
            }
            for(const ids of [
                ["pca-sda","pca-scl"],
                ["left-h1","left-h2","left-h3"],["left-u","left-v","left-w"],
                ["body-link-tx","body-link-rx"],
                ["dome-link-tx","dome-link-rx"],
                ["left-vesc-tx","left-vesc-rx"],
                ["right-vesc-tx","right-vesc-rx"],
                ["audio-rx-body","audio-feedback"],
                data.wires.filter(w=>/:CH\d S$/.test(w.from)).map(w=>w.id)
            ]) {
                const colors=ids.map(id=>signalColor(data.wires.find(w=>w.id===id)));
                if(colors.some(c=>!c)||new Set(colors).size!==ids.length)
                    throw Error("Signals need distinct colors: "+ids);
                if(ids.some(id=>isGroundWire(data.wires.find(w=>w.id===id))))
                    throw Error("Signal incorrectly marked ground: "+ids);
            }
            const width=320, gap=24;
            const rectangles=Object.entries(data.components).map(([id,c])=>({
                id,x:c.x,y:c.y,w:width,h:70+c.ports.length*gap
            }));
            const endpoints=new Map();
            for (const [id,c] of Object.entries(data.components)) {
                c.ports.forEach((port,index)=>{
                    const side=terminalSide(id,port);
                    endpoints.set(id+":"+port,{
                        x:c.x+(side==="left"?0:width),
                        y:c.y+75+index*gap,side
                    });
                });
            }
            function crosses(a,b,r) {
                if (a.x===b.x)
                    return a.x>r.x && a.x<r.x+r.w &&
                        Math.max(a.y,b.y)>r.y && Math.min(a.y,b.y)<r.y+r.h;
                return a.y>r.y && a.y<r.y+r.h &&
                    Math.max(a.x,b.x)>r.x && Math.min(a.x,b.x)<r.x+r.w;
            }
            for(const a of rectangles) for(const b of rectangles)
                if(a.id!==b.id && a.x<b.x+b.w && a.x+a.w>b.x &&
                   a.y<b.y+b.h && a.y+a.h>b.y)
                    throw Error("Component boxes overlap: "+a.id+" / "+b.id);
            const powerRoutes=[];
            const reservations=terminalReservations(endpoints);
            for (const wire of data.wires) {
                const from=endpoints.get(wire.from), to=endpoints.get(wire.to);
                const ground=isGroundWire(wire);
                const occupied=powerRoutes.filter(r=>![wire.from,wire.to]
                    .some(endpoint=>endpoint===r.wire.from||endpoint===r.wire.to))
                    .flatMap(r=>r.segments)
                    .concat(reservations.filter(r=>![wire.from,wire.to]
                        .includes(r.endpoint)).map(r=>r.segment));
                let points;
                try { points=routeWire(from,to,rectangles,occupied); }
                catch(error) { throw Error(wire.id+": "+error.message); }
                if (JSON.stringify(points[0])!==JSON.stringify({x:from.x,y:from.y}) ||
                    JSON.stringify(points[points.length-1])!==JSON.stringify({x:to.x,y:to.y}))
                    throw Error("Incorrect terminal: "+wire.id);
                const first=points[1], last=points[points.length-2];
                if ((first.x-from.x)*(from.side==="left"?-1:1)<=0 ||
                    (last.x-to.x)*(to.side==="left"?-1:1)<=0)
                    throw Error("Wrong terminal exit: "+wire.id);
                for (let i=1;i<points.length;++i) {
                    const a=points[i-1], b=points[i];
                    if (a.x!==b.x && a.y!==b.y) throw Error("Nonorthogonal: "+wire.id);
                    if (i>=2) {
                        const p=points[i-2];
                        if ((a.x-p.x)*(b.x-a.x)+(a.y-p.y)*(b.y-a.y)<0)
                            throw Error("Wire doubles back: "+wire.id+" at "+JSON.stringify(a));
                    }
                    for (const rect of rectangles)
                        if (crosses(a,b,rect))
                            throw Error("Box collision: "+wire.id+" / "+rect.id);
                    for (const segment of occupied) {
                        const [c,d]=segment;
                        if ((a.x===b.x && c.x===d.x && Math.abs(a.x-c.x)<8 &&
                            Math.max(Math.min(a.y,b.y),Math.min(c.y,d.y))<
                            Math.min(Math.max(a.y,b.y),Math.max(c.y,d.y))) ||
                            (a.y===b.y && c.y===d.y && Math.abs(a.y-c.y)<8 &&
                            Math.max(Math.min(a.x,b.x),Math.min(c.x,d.x))<
                            Math.min(Math.max(a.x,b.x),Math.max(c.x,d.x))))
                            throw Error("Distinct terminal paths stacked: "+wire.id);
                    }
                }
                powerRoutes.push({ground,wire,
                    segments:points.slice(1).map((p,i)=>[points[i],p])});
            }
            console.log("All "+data.wires.length+" routes avoid boxes and reach correct terminals");
        """
        result = subprocess.run(["node", "-e", program],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f"All {len(data['wires'])} routes avoid boxes", result.stdout)


if __name__ == "__main__":
    unittest.main()
