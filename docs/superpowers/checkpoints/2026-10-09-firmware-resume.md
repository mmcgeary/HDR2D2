# Firmware resume checkpoint - 2026-10-09

This is a user-requested transfer checkpoint, not a hardware-ready release.
Resume on branch `mmcgeary-r2-build-plan-review`. The user requested a pause,
then authorized committing all workspace changes and pushing this branch
so development can continue on another machine.

## Read first

- [Binding design](../specs/2026-10-09-teensy-body-controller-design.md)
- [Firmware implementation plan](../plans/2026-10-09-teensy-firmware.md)
- [Exported execution ledger](firmware-progress.md)
- Task reports `firmware-task-1-report.md` through `firmware-task-4-report.md`
  in this directory document interfaces, validation and review fixes.

The exported ledger is historical: its references to local ignored artifacts,
uncommitted documentation and a running agent describe the earlier session.
This checkpoint supersedes those pause-state statements.

## Reviewed work

| Task | Deliverable | Last reviewed commit |
| --- | --- | --- |
| 1 | Shared COBS/CRC protocol, typed messages, inert Teensy target | `9d7817e` |
| 2 | Reliable sessions/queues/retries and persistent profiles | `8272d89` |
| 3 | iBUS receiver and half-duplex handheld telemetry | `2faf224` |
| 4 | Two independent nonblocking VESC UART drivers | `b4314a3` |

Do not reimplement Tasks 1-4. Their review findings were resolved.
Teensy EEPROM hardware adaptation remains for body integration.
VESC firmware capture and comparison against VESC Tool are unperformed.

## Exact resume point: Task 5 is unreviewed

The Task 5 agent was cancelled during the pause. No final Task 5 report was
produced. Its partial drive-control implementation is included in this
transfer commit to avoid losing work:

- `TEENSY_BODY_CONTROLLER/src/body/DriveController.h` and `.cpp`
- `TEENSY_BODY_CONTROLLER/src/body/VescLink.h` and `.cpp`
- `TEENSY_BODY_CONTROLLER/src/main.cpp`
- `shared/R2BodyLink/Messages.h`
- `tests/test_body_drive.py`, `test_body_radio.py`, `test_body_vesc.py`

First compare these files against Task 5 and the binding design, finish any
missing requirements, and perform the Task 5 spec/quality review. The comparison
base for Task 5 code is `b4314a3`; the transfer commit also includes unrelated,
previously approved physical documentation, so scope that review to code/tests.
Do not mark Task 5 complete merely because its tests and target build pass.

Fresh transfer validation:

- `python3 -m unittest discover -s tests -q`: 148 tests passed.
- `pio run -d TEENSY_BODY_CONTROLLER -e teensy41`: succeeded.
- PlatformIO's host Python reported a LibreSSL/urllib3 compatibility warning.
- No hardware validation and no new ESP32 migration/build acceptance.

## Remaining implementation

Tasks 6 and 6b: dome arbitration, dual-Hall position estimation and idle scheduler.
Task 7: DFPlayer serializer and metadata.
Task 8: body scheduler, watchdog, hardware adapters and commissioning dispatch.
Tasks 9-11: ESP32 body client, ownership migration, choreography and OTA.
Task 12: phone/Wi-Fi commissioning and complete two-board integration.

The ESP32 still contains historical body-hardware owners. Do not flash the
current combination as migrated firmware. Ordinary motion must remain disabled
until explicit commissioning and acceptance. Software validation does not
establish safe motor limits or physical braking performance.

## Preserve decisions

Use two rotating Hall modules (front GPIO19, rear GPIO18) and one stationary
magnet. CH6 enables feet; CH9 independently enables automatic dome movement.
Faint does not lock motion. Manual/drive takeover permanently cancels only an
event's dome participation. No assembled rotation test requires USB; phone
commissioning is mandatory. Dome removal is the service-access method, with
keyed body-side slip-ring connectors and vertically disengaging restraint.

The physical guides, BOM and inspector are included, but their remaining
dual-Hall/CH9/Faint consistency updates belong to Task 10. Preserve the
distinct signal colours and the user's removal of the inspector firmware
warning banner.

## New-machine setup

Check out this branch and use its existing PlatformIO manifests. Install
PlatformIO locally if unavailable; do not rely on the original machine's
session virtualenv paths in the historical reports. Test with the commands
above. Do not create a pull request, merge, or start additional work until
the user resumes implementation.
