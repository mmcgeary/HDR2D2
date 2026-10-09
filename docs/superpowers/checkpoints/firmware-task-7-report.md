# Task 7 report — Bidirectional DFPlayer driver and track metadata

## Status and scope

**Software implementation verified and reviewed.**
Task 7 delivers the bidirectional DFPlayer Mini audio driver, flash-resident track catalog generator, track completion guards, request-correlated playback lifecycle events, and Teensy `main.cpp` pipeline integration.

Deliverables:
- Production `TEENSY_BODY_CONTROLLER/src/body/DfPlayer.h`
- Production `TEENSY_BODY_CONTROLLER/src/body/DfPlayer.cpp`
- Hardware adapter `AudioPort` in `TEENSY_BODY_CONTROLLER/src/body/HardwareAdapters.h`
- Pipeline integration in `TEENSY_BODY_CONTROLLER/src/main.cpp`
- Pre-build tool `tools/generate_track_catalog.py`
- Seed catalog `config/tracks.csv`
- Pre-build script registered in `TEENSY_BODY_CONTROLLER/platformio.ini`
- Generated header `TEENSY_BODY_CONTROLLER/include/TrackCatalog.h` ignored in `.gitignore`
- Shared audio enums & operators in `shared/R2BodyLink/Messages.h`
- Unit tests in `tests/test_track_catalog.py` (4 tests)
- Unit tests in `tests/test_body_audio.py` (8 tests)

## Implementation details and design rules

### 1. DFPlayer Driver (`body::DfPlayer`)
- **BytePort Abstraction:** Communicates via `r2link::BytePort` allowing deterministic unit testing on host without physical hardware.
- **Packet Serialization & Checksum:** Standard 10-byte DFPlayer frame `[0x7E, 0xFF, 0x06, cmd, feedback, param_h, param_l, sum_h, sum_l, 0xEF]`. Checksum is calculated as `-sum(bytes 1..6)`.
- **Hardware Spacing Constraint:** Enforces $\ge 100\text{ ms}$ spacing between consecutive transmitted commands to avoid dropping packets or locking up the DFPlayer hardware FIFO.
- **Startup Sequence:** Non-blocking 3-second startup discovery with Reset (`0x0C`), volume initialization (`0x06`), and EQ initialization (`0x07`). Audio absence after 3s reports `AudioState::Offline` without disabling or delaying manual drive.
- **Command Prioritization:** High-priority commands (e.g. Stop `0x16`, Volume `0x06`, Pause `0x0E`) supersede queued play requests. Foreground play requests supersede ambient play requests; ambient play requests cannot interrupt an active foreground track (`Result::Inhibited`).
- **Telemetry & Event Correlation:**
  - Query state (`0x42`) polled every 500ms without monopolizing the UART.
  - State response (`0x42`, `param_l == 1`) transitions to `Playing` and emits `EventKind::PlaybackStarted` identifying the request sequence number.
  - Track finish (`0x3D`) or query state indicating stopped (`0x42`, `param_l == 0`) emits `EventKind::Completed` correlated to `owner_seq`.
  - Pause latches elapsed time; Resume shifts start time and preserves accurate elapsed duration estimates.
  - Peer disconnect cancels remote-owned playback and sends Stop (`0x16`).

### 2. Track Catalog & Completion Guards
- **Flash-Resident Table:** Generated deterministically from `config/tracks.csv` into `TrackCatalog.h`.
- **Completion Guard Timing:** Unknown tracks use default 600,000ms (10 minutes) guard. Seeded macro tracks (102, 106, 107, 109, 110, 255) define completion guards set to choreography duration plus 2000ms.
- **Guard Expiry:** If no completion packet is received before the guard expires, the driver emits `EventKind::Timeout` with `Detail::AudioGuardExpired`, returns state to `Idle`, and enqueues Stop (`0x16`).

## Verification and test results

- `python3 -m unittest discover -s tests -p test_track_catalog.py -v`: 4 tests passed (0.13s).
- `python3 -m unittest discover -s tests -p test_body_audio.py -v`: 8 tests passed (2.61s).
- `python3 -m unittest discover tests`: 182 tests passed (53.1s, all green across entire repository).
