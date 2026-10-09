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
                        "dome-feed-return", "amp-return", "vesc-comm-ground"):
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
        self.assertEqual(wires["vesc-telem-body"]["from"], "leftVesc:COMM TX")
        self.assertEqual(wires["vesc-telem-body"]["to"], "ringBody:CH3")
        self.assertEqual(wires["vesc-telem-dome"]["from"], "ringDome:CH3")
        self.assertEqual(wires["vesc-telem-dome"]["to"], "esp:GPIO5 RX")

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
        self.assertTrue(all(w["length_m"] == 2 for w in hall_wires))
        phase_wires = [w for w in data["wires"] if w["cat"] == "phase"]
        self.assertEqual(len(phase_wires), 6)
        self.assertTrue(all(w["awg"] == 12 and w["length_m"] == 2
                            for w in phase_wires))

    def test_protected_branch_schedule_matches_approved_values(self):
        data = self.load_data()
        self.assertEqual(data["components"]["fuseBlock"]["fuses"],
                         {"F1": 15, "F3": 7.5,
                          "F4": 7.5, "F5": 5, "F6": 1})
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

    def test_pwm_and_audio_bypass_shifter(self):
        data = self.load_data()
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        self.assertIn(("esp:GPIO4 PWM", "ringDome:CH5"), routes)
        self.assertIn(("esp:GPIO17 TX", "ringDome:CH4"), routes)
        for port in data["components"]["shifter"]["ports"]:
            self.assertFalse(port.startswith(("HV3", "LV3", "HV4", "LV4")))

    def test_pca_uses_motherboard_headers_and_separate_servo_power(self):
        data = self.load_data()
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        self.assertIn(("esp:I2C G", "pca:LOGIC GND"), routes)
        self.assertIn(("esp:3.3V", "pca:VCC 3.3V"), routes)
        self.assertIn(("dome5:PCA +", "pca:V+ 5V"), routes)
        self.assertTrue(all("esp:I2C V UNUSED" not in (w["from"], w["to"])
                            for w in data["wires"]))
        self.assertIn("VERIFY GND", data["components"]["pca"]["portLabels"]["GND"])

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

    def test_voltage_telemetry_uses_separate_sense_and_receiver_connections(self):
        data = self.load_data()
        self.assertNotIn("voltmeter", data["components"])
        routes = {(w["from"], w["to"]) for w in data["wires"]}
        self.assertIn(("ringDome:CH1", "telemetry:SENSE+"), routes)
        self.assertIn(("telemetry:SENSE-", "ringDome:CH2"), routes)
        self.assertIn(("receiver:SENS 5V", "telemetry:SUPPLY 5V"), routes)
        self.assertIn(("receiver:SENS GND", "telemetry:GND"), routes)
        self.assertIn(("telemetry:DATA", "receiver:SENS DATA"), routes)
        self.assertIn(("receiver:iBUS SIG", "shifter:HV1 iBUS IN 5V"), routes)

    def test_routes_exit_terminals_and_avoid_every_component_box(self):
        page = (ROOT / "wiring_visualizer.html").read_text()
        engine = re.search(r'<script id="routing-engine">(.*?)</script>',
                           page, re.S)
        self.assertIsNotNone(engine, "Obstacle-aware routing engine is missing")
        data = self.load_data()
        program = engine.group(1) + "\nconst data=" + json.dumps(data) + r""";
            layoutComponents(data.components);
            for(const ids of [
                ["pca-sda","pca-scl"],
                ["left-h1","left-h2","left-h3"],["left-u","left-v","left-w"],
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
            const powerRoutes=[];
            for (const wire of data.wires) {
                const from=endpoints.get(wire.from), to=endpoints.get(wire.to);
                const ground=isGroundWire(wire);
                const occupied=powerRoutes.filter(r=>![wire.from,wire.to]
                    .some(endpoint=>endpoint===r.wire.from||endpoint===r.wire.to))
                    .flatMap(r=>r.segments);
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
                            throw Error("Opposite polarity paths stacked: "+wire.id);
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


if __name__ == "__main__":
    unittest.main()
