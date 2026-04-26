# Backlog

## Session Checkpoint

<!-- CHECKPOINT:START -->
- Refreshed: 2026-04-26
- Branch: `master`
- Latest commit: `2c903d6 ref(handoff): Make checkpoint generation manual`
- Tracked checkpoint command: `bash scripts/update-checkpoint.sh`
- Local handoff artifact: `bash scripts/update-handoff.sh` writes ignored `HANDOFF.md`
- Validation baseline:
  - `pio test -e native`
  - `pio run -e lb_esp32s3 -t buildprog`
- Current focus:
  - P1 hardware validation on a real board
  - P2 documentation drift reduction for routes, config keys, and constants
<!-- CHECKPOINT:END -->

## P1

- Validate real hardware behavior on device:
  - sensor freshness and NaN handling across PT1000, dual DHT21, and optional INA260
  - damper motor direction, limit behavior, and PID actuation under live readings
  - OTA, restart-required config changes, and LittleFS asset delivery on hardware

## P2

- Finish the documentation backlog already identified in `docs/testing.md`: undocumented routes, undocumented NVS keys, and undocumented config/auth/OTA constants.
- Add or generate route/config inventories so docs can be checked against implementation instead of drifting manually.

## P3

- Consider splitting the remaining large Arduino-only modules, especially `src/web.cpp`, to reduce coupling between auth, OTA, config, and UI transport handlers.
- Consider narrowing `refresh-docs.sh` further so AI refresh work updates only tracked docs and never depends on local-only artifacts.
