# Teensy Physical Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. Work directly unless the user requests delegation.

**Goal:** Replace the ESP32-only build instructions with one consistent Teensy-body / AstroPixels-dome wiring plan.

**Execution status:** Physical phase complete. Guides, BOM and wiring graph use the approved architecture and fuse schedule. Signal colours differentiate UART directions, iBUS, I2C SDA/SCL and other local signals; colours remain consistent through translators/ring contacts and are tracing aids, not physical wire requirements. All 47 host tests pass, including actual Node obstacle routing; local active-guide links, BOM XML and whitespace checks pass. Firmware is unchanged; hardware commissioning has not happened. No commits or pushes were made.

**Execution record:** Tasks 1-5 are implemented; Task 6 consistency gate passed. The original task checklists below retain the planned sequence and commit instructions for reference; no unchecked entry overrides this execution record. Per-task commits were omitted because the user requested documentation changes, not Git publication.

**Subsequent approved revision:** Firmware planning added a second KY-003 Hall module, still one magnet: front GPIO19 via dome shifter channel2; rear GPIO18 via channel3. CH3 is now unused, CH9 enables automatic dome behaviour rather than holos, and Faint has no motion lock. Firmware Task10 updates the earlier physical guides/BOM/graph/tests together. The revised design and firmware plan govern those changes; the prior47-test result predates them.

**Architecture:** Teensy 4.1 owns receiver input, both direct VESC UARTs, the body DFPlayer, dome servo output, and receiver telemetry. AstroPixels ESP32 retains lighting, holo servos, the Hall input, Wi-Fi, and choreography. A two-way serial connection crosses CH3/CH6 of the existing slip ring.

**Tech Stack:** Markdown/Mermaid, existing offline HTML wiring inspector, SpreadsheetML BOM, Python unittest, Node routing checks.

**Spec:** [Teensy body controller design](../specs/2026-10-09-teensy-body-controller-design.md)

## Global Constraints

- The body controller is a Teensy 4.1 in the ordered Treedix socketed screw-terminal carrier.
- Body and dome 5V positives remain separate. Grounds are common.
- Nothing has been wired. Produce first-time assembly instructions, not a retrofit or disconnection procedure.
- Prior wiring assignments, fuse values and assembly methods are not fixed constraints. Review changes for safety, simplicity and serviceability.
- Keep the owned body and dome screw-terminal 5V distribution. Use selective inline fuse holders for important branches; do not procure or implement separate 5V fuse boxes.
- All four output fuse ratings are approved: B-SERVO 5A, B-LOGIC 2A, D-SERVO 5A, D-LOGIC 3A. Use these values in every guide, BOM entry and graph. Do not reopen them without new fault evidence.
- Dome PCA9685 V+ branch is settled: 5A inline fuse, 16AWG positive feed and ground return, standard servo sockets. Keep logic VCC at ESP32 3.3V. Do not reopen this fuse choice without new fault evidence.
- Teensy USB arrangement is settled: cut PJRC's designated VUSB/VIN link once; externally power VIN and use normal USB for programming with Teensy installed. Show the exact Teensy 4.1 cut location and unpowered multimeter isolation check, including no carrier re-bridge. USB alone will no longer power Teensy.
- ESP32 body-link pins are settled: Serial2 RX GPIO16, TX GPIO17. GPIO5/18/4 are spare; Hall remains19 and PCA9685 SDA/SCL remain21/22.
- Use the owned B0FDJYRGB7 four-pack of 12AWG standard-blade inline holders. Specify standard ATO/ATC 2A and 3A separately, plus two 5A from the included assortment. No additional holders or 5V fuse boxes.
- CH6 / SwA is drive enable. Receiver failsafe sets CH6 OFF.
- VESC voltage supplies handheld battery telemetry. FS-CVT01 is not installed in the active wiring. F6 becomes an empty spare.
- Both VESC UARTs, both body/dome UART directions, and both DFPlayer UART directions bypass level shifters.
- Use two Lonely Binary four-channel MOSFET modules from kit B0FFMLDYNY, one body and one dome. Previous converters are being returned. Do not substitute TXS0108E or Pololu boards.
- No oscilloscope or logic analyzer is available or required. Write every assembly, commissioning and troubleshooting step for a multimeter, USB diagnostics, VESC Tool and the handheld display.
- Write plain, declarative instructions. Distinguish selected settings from measured commissioning results.
- Do not change firmware during this phase. State that the physical documents describe the target wiring until Phase 2 is complete.
- Assemble and change connections only with all power disconnected. Do not build superseded signal paths.
- Work in the current isolated worktree. Do not create another branch or touch the main checkout.
- Do not edit upstream reference files under `docs/astropixels/`.

---

## File responsibilities and execution boundary

| File | Responsibility |
| --- | --- |
| `BODY_CONTROLLER_WIRING.md` (new) | Authoritative carrier terminal map, two shifters, cables, slip ring and first-time assembly |
| `BODY_CONTROLLER_COMMISSIONING.md` (new) | Unpowered checks, first Teensy USB flash, staged hardware acceptance and recovery |
| `SYSTEM_ARCHITECTURE.md` | Ownership and body/dome block diagram |
| `DOME_WIRING_DIAGRAM.md` | Complete system wiring, dome hardware and inter-board paths |
| `POWER_HARNESS_GUIDE.md` | Power distribution, fuse schedule and assembly sequence |
| `VESC_DRIVE_INTEGRATION.md` | Actual connector inventory, independent UART setup and motor-side commissioning |
| `wiring_visualizer.html` | Inspectable terminal/wire graph, layout, print schedule and component notes |
| `Master_R2D2_BOM.xls` | Purchased boards, selected Lonely Binary kit/two allocated modules, cables, unused sensor and parts status |
| `README.md` | Navigation and architecture status |
| `ASTROPIXELS_PLUS_UNIFIED/README.md` | Target ESP32 pinout and build/flash instructions |
| `ESP32_DOME_BRAIN_GUIDE.md` | Historical-reference banner only |
| `tests/test_harness_diagram.py` | Executable topology and renderer invariants |
| `tests/test_body_documentation.py` (new) | Cross-document pin, fuse, BOM and status consistency |

Phase 1 ends with documentation and graph tests passing. The old firmware still builds with the old topology. Put this notice near the top of the wiring/firmware guides:

> This guide describes the Teensy body-controller wiring. Do not connect this wiring to the old ESP32-only firmware. Complete the firmware migration and flash both boards before combined testing.

Phase 2 removes the transition notice after both builds and their simulated integration pass. Hardware test results remain a separate acceptance record.

## Task 1: Establish the authoritative connection guide

**Files:**
- Create: `BODY_CONTROLLER_WIRING.md`
- Create: `tests/test_body_documentation.py`
- Modify: `README.md` (guide index and transition notice)

**Interfaces:**
- Consumes: design sections 1-4.
- Produces: the exact terminal map and signal names used by every other document.

- [ ] Review the first-build choices before making the pin-map tests authoritative: selective inline branch protection using the owned 5V screw-terminal distribution; USB/VIN isolation versus repeated Teensy removal; ESP32 boot-strapping and accessible UART pins; shifter suitability; connector keying and strain relief. Compare each against the selected components' ratings and manufacturer instructions. Record the chosen arrangement in the design, including additional parts and any user decision required. Update all affected plan tables/tests together; do not preserve a worse choice solely to match an old document.
- [x] Draw the four approved inline fuses at positive distribution takeoffs: body dome-servo 5A; body electronics 2A; dome PCA9685 V+ 5A; dome electronics/lighting 3A. Show insulated electronics fan-outs separate from unfused positives. Keep grounds unfused and body/dome positives separate. No separate 5V fuse box; matching 2A/3A standard fuses are additional parts.
- [ ] Add a failing consistency test. Use explicit rows instead of fuzzy prose matching:

```python
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class BodyDocumentationTests(unittest.TestCase):
    def test_body_guide_lists_selected_carrier_and_pin_map(self):
        guide = (ROOT / "BODY_CONTROLLER_WIRING.md").read_text()
        for text in (
            "Teensy 4.1", "Treedix", "B09NXYWYK7",
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
            with self.subTest(text=text):
                self.assertIn(text, guide)
```

- [ ] Run `python3 -m unittest discover -s tests -p test_body_documentation.py -v`. Expect failure because the guide does not exist.
- [ ] Write the guide from the spec's terminal table. Include power, signal ground, shifter references, crossed TX/RX, connector orientation, and the 14-terminal count.
- [ ] Identify Lonely Binary B0FFMLDYNY explicitly and map diagram LV1-LV4/HV1-HV4 to the module's A1-A4/B1-B4 labels. Document external LV3.3V/HV5V references and shared GND; no onboard voltage generation. Place body module beside receiver/Teensy with short iBUS runs.
- [ ] Give the single-wire telemetry path its own drawing: receiver SENSOR -> HV2/LV2 -> TX24. State native half-duplex plus open drain, RX25 unused, and no joined push-pull pins.
- [ ] Name the cable groups: `LEFT-VESC`, `RIGHT-VESC`, `AUDIO-UART`, `RADIO-IBUS`, `RADIO-SENS`, `DOME-LINK`, `DOME-SERVO`, `TEENSY-POWER`. Specify 22AWG short signal/ground runs; verify power gauges against branch current, protection, run length and connector ratings.
- [ ] Provide a first-time assembly table in harness order, including labels, connector orientation and unpowered checks. Mark earlier receiver-in-dome, VESC ring, DFPlayer CH4 and servo CH5 assignments as superseded; do not instruct the user to disconnect nonexistent wiring.
- [ ] Add the new guide and transition notice to root navigation.
- [ ] Rerun the test. Expect PASS.
- [ ] Commit only these task files with message `docs: define Teensy body wiring` and the required Copilot co-author trailer.

## Task 2: Update the machine-readable wiring inspector

**Files:**
- Modify: `wiring_visualizer.html` (`harness-data`, component layout, `terminalSide`, signal colours, legend and print notes)
- Modify: `tests/test_harness_diagram.py`

**Interfaces:**
- Consumes: the terminal map from Task 1.
- Produces: a single active wire graph matching the selected architecture.

Retain existing component ID `shifter` for the dome shifter. Add `bodyShifter` and `teensy`. Represent Treedix as the Teensy component's carrier, not as a second electrically independent microcontroller. Component terminals include unused terminals so the inspector can label them, but no active wire terminates on an UNUSED terminal.

Required graph routes:

```text
body5:TEENSY + -> teensy:VIN
teensy:GND -> body5:GND
teensy:3.3V -> bodyShifter:LV 3.3V
body5:RECEIVER + -> receiver:5V
receiver:GND -> body5:GND
receiver:iBUS SIG -> bodyShifter:HV1 iBUS 5V
bodyShifter:LV1 iBUS 3.3V -> teensy:21 RX5
receiver:SENS DATA -> bodyShifter:HV2 SENS 5V
bodyShifter:LV2 SENS 3.3V -> teensy:24 SENS6
teensy:2 SERVO -> bodyShifter:LV3 SERVO 3.3V
bodyShifter:HV3 SERVO 5V -> domeServo:PWM
teensy:1 TX1 -> leftVesc:COMM RX
leftVesc:COMM TX -> teensy:0 RX1
teensy:8 TX2 -> rightVesc:COMM RX
rightVesc:COMM TX -> teensy:7 RX2
teensy:14 TX3 -> dfplayer:RX
dfplayer:TX -> teensy:15 RX3
teensy:17 TX4 -> ringBody:CH3
ringDome:CH3 -> esp:GPIO16 RX
esp:GPIO17 TX -> ringDome:CH6
ringBody:CH6 -> teensy:16 RX4
```

The DFPlayer RX wire includes the 1k ohm resistor in its specification. Both COMM grounds have explicit ground-reference wires. Body shifter HV/GND have explicit supply wires. Keep physical internal ring contacts CH1-CH6 in the graph; CH4/CH5 have only their internal contact edges and are labelled SPARE at both ends.

- [ ] Replace old topology expectations with new failing tests:

```python
def test_body_controller_owns_direct_uart_links(self):
    data = self.load_data()
    routes = {(w["from"], w["to"]) for w in data["wires"]}
    for route in (
        ("teensy:1 TX1", "leftVesc:COMM RX"),
        ("leftVesc:COMM TX", "teensy:0 RX1"),
        ("teensy:8 TX2", "rightVesc:COMM RX"),
        ("rightVesc:COMM TX", "teensy:7 RX2"),
        ("dfplayer:TX", "teensy:15 RX3"),
        ("teensy:17 TX4", "ringBody:CH3"),
        ("ringBody:CH6", "teensy:16 RX4"),
    ):
        self.assertIn(route, routes)
    self.assertNotIn("telemetry", data["components"])

def test_spare_ring_contacts_have_no_external_connections(self):
    data = self.load_data()
    for contact in ("CH4", "CH5"):
        ends = {f"ringBody:{contact}", f"ringDome:{contact}"}
        edges = [w for w in data["wires"]
                 if w["from"] in ends or w["to"] in ends]
        self.assertEqual(len(edges), 1)
        self.assertEqual(edges[0]["cat"], "ring")
```

- [ ] Run `python3 -m unittest discover -s tests -p test_harness_diagram.py -v`. Expect failure on the new endpoints.
- [ ] Move receiver to the body column, add carrier/shifter, add right COMM TX/RX/GND, remove active CVT01 and CAN-forwarding edges, and empty F6. Add missing COMM 3.3V/ADC/ADC2 and shared RECEIVER connector to unused inventory notes.
- [ ] Remove obsolete signal edges entirely. Do not hide them behind a display filter.
- [ ] Update `terminalSide` for `teensy` and `bodyShifter`. Give opposite UART directions distinct colours. Keep power-return and phase/Hall rendering rules.
- [ ] Rewrite the existing PWM/audio, telemetry-sensor, receiver-location, F6 and VESC-master assertions. Keep rail isolation, shared supply, phase/Hall lengths, audio isolation and obstacle-routing tests.
- [ ] Add tests for no active VESC supply pin wiring, receiver powered from body, two local shifter references, and no active GPIO5/18/4 ESP32 wiring. Test dedicated GPIO16/17 serial signals.
- [ ] Rerun the harness tests, including their Node routing program. Expect PASS.
- [ ] Open the offline inspector and inspect diagram, search, terminal selection, wire highlighting, and print schedule. Confirm no line crosses a new component box.
- [ ] Commit with message `docs: migrate wiring inspector to Teensy control` and the required trailer.

## Task 3: Make architecture, power and dome diagrams agree

**Files:**
- Modify: `SYSTEM_ARCHITECTURE.md`
- Modify: `DOME_WIRING_DIAGRAM.md`
- Modify: `POWER_HARNESS_GUIDE.md`
- Modify: `ASTROPIXELS_PLUS_UNIFIED/README.md`
- Modify: `tests/test_body_documentation.py`

**Interfaces:**
- Consumes: Tasks 1-2 connection map.
- Produces: consistent overview, wiring diagrams, rail schedule and ESP32 pin guide.

- [ ] Add a failing test for the shared slip-ring table in all four guides:

```python
def test_active_guides_share_ring_assignments(self):
    for name in ("SYSTEM_ARCHITECTURE.md", "DOME_WIRING_DIAGRAM.md",
                 "POWER_HARNESS_GUIDE.md",
                 "ASTROPIXELS_PLUS_UNIFIED/README.md"):
        text = (ROOT / name).read_text()
        for row in (
            "| CH3 | Teensy TX17 | ESP32 GPIO16 | Body to dome serial |",
            "| CH4 | Unconnected | Unconnected | Spare |",
            "| CH5 | Unconnected | Unconnected | Spare |",
            "| CH6 | Teensy RX16 | ESP32 GPIO17 | Dome to body serial |",
        ):
            with self.subTest(file=name, row=row):
                self.assertIn(row, text)
```

- [ ] Run the documentation tests and confirm failure.
- [ ] Replace "all-in-one ESP32", "no body microcontroller", and "master/slave CAN" descriptions with the ownership table. Draw receiver -> Teensy -> two VESCs and Teensy <-> ESP32.
- [ ] Show DFPlayer serial and dome PWM as local body paths. Show Hall input and PCA9685 as local dome paths.
- [ ] Draw PCA9685 control as AstroPixels I2C G -> logic GND, C/GPIO22 -> SCL, D/GPIO21 -> SDA; leave I2C V disconnected. Draw a separate ESP32 3.3V -> PCA9685 VCC wire. Servo V+ uses the 5A-fused 5V branch and16AWG ground return, not logic VCC. No I2C shifter; address0x40; servo pairs0/1 front,2/3 rear,4/5 top pan/tilt.
- [ ] Update body 5V distribution to add Teensy, receiver and body shifter. Remove receiver from dome distribution. Remove F6 telemetry load from power diagrams and shopping allowances.
- [ ] Correct the existing stale harness testing text that labels CH3 as iBUS. It becomes body-to-dome serial; CH6 is the return.
- [ ] Add SwA/CH6 to radio tables. Replace "instant stop" claims with the exact detection deadlines and measured mechanical acceptance steps from the spec.
- [ ] Carry forward the reviewed power/protection decisions from Task 1. Retain the selected amplifier and ground-loop isolator unless the review identifies a specific reason to change them. Keep separate BTL speaker terminals; neither terminal is ground.
- [ ] Keep ESP32 flash instructions. Add the transition notice and GPIO16/17 body-link labels; remove local iBUS/audio/PWM roles.
- [ ] Rerun the two documentation/graph suites. Expect PASS.
- [ ] Commit with message `docs: align body and dome architecture guides` and the required trailer.

## Task 4: Replace VESC master/slave setup and update the BOM

**Files:**
- Modify: `VESC_DRIVE_INTEGRATION.md`
- Modify: `Master_R2D2_BOM.xls`
- Modify: `tests/test_body_documentation.py`

**Interfaces:**
- Consumes: direct per-wheel UART architecture and user-selected telemetry source.
- Produces: correct purchasing status, connector inventory and commissioning settings record.

- [ ] Add tests that parse SpreadsheetML rather than scanning raw XML:

```python
def test_bom_records_ordered_body_hardware(self):
    import xml.etree.ElementTree as ET
    ns = {"s": "urn:schemas-microsoft-com:office:spreadsheet"}
    rows = [
        " | ".join(d.text or "" for d in r.findall("s:Cell/s:Data", ns))
        for r in ET.parse(ROOT / "Master_R2D2_BOM.xls")
                   .findall(".//s:Row", ns)
    ]
    self.assertTrue(any("Teensy 4.1" in r and "Ordered" in r for r in rows))
    self.assertTrue(any("Treedix" in r and "B09NXYWYK7" in r
                        and "Ordered" in r for r in rows))
    self.assertTrue(any("FS-CVT01" in r and "Unused" in r for r in rows))
```

- [ ] Run documentation tests and confirm failure.
- [ ] Add purchased Teensy and carrier rows. Do not infer vendor SKU for the ordered Teensy; record Teensy 4.1 and its actual header assembly requirement. No separate perfboard or female sockets are required with Treedix.
- [ ] Replace the old converter BOM entry with the selected Lonely Binary kit B0FFMLDYNY and record two allocated four-channel modules: body receiver/telemetry/servo, dome Hall sensor. Mark the old converters as being returned; do not claim the new kit is ordered until confirmed. Update ESP32, receiver, DFPlayer, servo, slip-ring and body buck notes. Remove obsolete volume-pot and compulsory-capacitor claims where these directly contradict current audio/power selections.
- [ ] Record the owned screw-terminal 5V distribution and selected inline fuse holders/fuses. Do not add separate 5V fuse boxes to the BOM.
- [ ] Keep FS-CVT01 in the BOM as unused, not needed for this design. Remove its active wiring and F6 fuse requirement.
- [ ] Rewrite VESC configuration as left and right, each USB/UART configured separately. CAN OFF, UART 115200 both, no command forwarding. Keep distinct controller IDs for identification.
- [ ] Add actual connector inventory from the spec, including one RECEIVER/SIN connector and separate USB ports.
- [ ] Replace unsupported "safe current" claims with the commissioning record: firmware, motor detection, current limits, brake magnitude, timeout braking, direction, full-pack braking result and stopping distance. Blank measurement cells mean "not commissioned"; they are not guessed settings.
- [ ] State brake command versus duty-zero explicitly. Document normalized rate limits, both-controller feedback and CH6 neutral arming.
- [ ] Rerun tests and parse the BOM with `xml.etree.ElementTree.parse`. Expect valid XML and PASS.
- [ ] Commit with message `docs: update VESC setup and body controller BOM` and the required trailer.

## Task 5: Write the staged wiring, flash and commissioning guide

**Files:**
- Create: `BODY_CONTROLLER_COMMISSIONING.md`
- Modify: `POWER_HARNESS_GUIDE.md`
- Modify: `README.md`
- Modify: `ESP32_DOME_BRAIN_GUIDE.md` (historical banner)
- Modify: `tests/test_body_documentation.py`

**Interfaces:**
- Consumes: all Phase 1 wiring documents and the firmware design.
- Produces: executable acceptance sequence and a table for recorded hardware results.

- [ ] Add a failing test that requires explicit acceptance items:

```python
def test_commissioning_has_distinct_failure_tests(self):
    text = (ROOT / "BODY_CONTROLLER_COMMISSIONING.md").read_text()
    for heading in (
        "Bare Teensy USB flash", "Carrier continuity",
        "Logic-shifter communication check", "Transmitter switched off",
        "Receiver iBUS unplugged", "Left VESC UART unplugged",
        "Right VESC UART unplugged", "Dome link unplugged",
        "Web STOP acknowledgement", "Maintenance lock before OTA",
        "Hall update lost during homing", "Full-pack braking",
    ):
        self.assertIn(heading, text)
```

- [ ] Run documentation tests and confirm failure.
- [ ] Write the stages in order: continuity; isolated rails; bare-board USB flash; body idle boot; receiver and sensor bus; body/dome handshake; DFPlayer; unloaded servo; Hall/homing; elevated VESCs; slow floor test; OTA/recovery.
- [ ] Include the exact future build commands, with a transition notice until firmware exists:

```sh
pio run -d TEENSY_BODY_CONTROLLER -e teensy41
pio run -d TEENSY_BODY_CONTROLLER -e teensy41 -t upload
pio device monitor -d TEENSY_BODY_CONTROLLER -e teensy41 --baud 115200
pio run -d ASTROPIXELS_PLUS_UNIFIED -e astropixelsplus
```

- [ ] Explain Teensy's Program button if the loader requests it. Do not describe it as a general reset button. Show a verified Teensy 4.1 underside illustration identifying only the designated VUSB/VIN link. Remove/disconnect the board before cutting; test isolation with a multimeter, including carrier continuity. Document external VIN power for all post-cut uploads and diagnostics, common ground, wheels elevated and motion inhibited. Bare first flash may precede the cut. Never bridge the pads for routine programming.
- [ ] Add recorded-value columns for sensor communication/errors, receiver failsafe channels, both VESC firmware versions, brake/current profile, firmware scheduling latency, homing delay, stopping distance and thermal results. Verify the two emulated sensors first, then with channel frames flowing concurrently. Do not require waveform, rise-time or electrical turnaround measurements. Distinguish firmware timing counters from electrical measurements.
- [ ] Write no-scope communication troubleshooting: disable motion; check unpowered continuity/channel pairing; check LV/HV rails and ground with a multimeter; inspect solder joints; test each iBUS connection separately with short leads; swap in a spare Lonely Binary module; repeat the concurrent test. An unresolved failure blocks motion enable, not a demand to purchase test equipment.
- [ ] State expected failure outcomes exactly: VESC loss stops both wheels; ordinary dome-link loss cancels remote actions but leaves healthy manual feet available; existing STOP/Faint/maintenance locks stay latched. Include an unloaded CPU-stall/servo-reset test: the VESC150ms timeout does not apply to the dome servo.
- [ ] Point old FastLED/Nano alternatives at the historical section rather than updating their firmware.
- [ ] Run `python3 -m unittest discover -s tests -p 'test_*documentation.py' -v` and the harness suite. Expect PASS.
- [ ] Commit with message `docs: add Teensy commissioning walkthrough` and the required trailer.

## Task 6: Phase 1 consistency gate

**Files:**
- Review all files changed in Tasks 1-5.

**Interfaces:**
- Consumes: updated guides, BOM and diagram.
- Produces: a documented target wiring plan ready for firmware work, not a claim of commissioned hardware.

- [ ] Search only active guides for retired claims:

```sh
rg -n 'receiver sits in the dome|Receiver in Dome|no body microcontroller|COMM_FORWARD_CAN|CH4.*audio|CH5.*PWM|safe.*12\.0|sends 0 duty' \
  README.md SYSTEM_ARCHITECTURE.md POWER_HARNESS_GUIDE.md \
  DOME_WIRING_DIAGRAM.md VESC_DRIVE_INTEGRATION.md \
  BODY_CONTROLLER_WIRING.md BODY_CONTROLLER_COMMISSIONING.md \
  ASTROPIXELS_PLUS_UNIFIED/README.md
```

- [ ] Inspect each match. Historical-removal instructions may mention old assignments; no active wiring instruction may use them.
- [ ] Run `python3 -m unittest discover -s tests -v` and `git diff --check`.
- [ ] Verify BOM quantities/status, all local Markdown links, both shifters, all 14 Teensy terminals, full VESC connector inventory and two spare contacts.
- [ ] Keep the transition notice. Hand off to [the firmware plan](2026-10-09-teensy-firmware.md).

Every task commit must include:

```text
Co-authored-by: Copilot App <223556219+Copilot@users.noreply.github.com>
```

Do not push or create a pull request unless the user requests it.
