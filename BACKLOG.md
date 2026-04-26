# Backlog

## Session Checkpoint

- Last policy change: `HANDOFF.md` is now manual. Refresh it explicitly with `bash scripts/update-handoff.sh` when preparing a checkpoint, handoff, or release note snapshot.
- Current validation baseline:
  - `pio test -e native`
  - `pio run -e lb_esp32s3 -t buildprog`
- Current highest-value follow-up: finish the remaining documentation backlog in `docs/testing.md` and keep route/config inventories from drifting.

## P1

- Restore and document a working local validation path for PlatformIO. `pio test -e native` was broken in this workspace because the `~/.platformio/penv` virtualenv no longer matched the system Python version.

## P2

- Finish the documentation backlog already identified in `docs/testing.md`: undocumented routes, undocumented NVS keys, and undocumented config/auth/OTA constants.
- Add direct route-level regression coverage for `admin` versus `viewer` access to `/users`, `/config/save`, `/motor`, `/log`, `/delete/*`, `/setpoint`, `/pid`, `/ota/status`, and `/ota/update`. Current coverage is strong at the auth helper layer but still not end-to-end at the HTTP handler layer.

## P3

- Reduce doc drift by generating route/constant inventories from code or by adding a verification script that compares docs against the implementation.
- Consider splitting the remaining large Arduino-only modules, especially `src/web.cpp`, to reduce coupling between auth, OTA, config, and UI transport handlers.
