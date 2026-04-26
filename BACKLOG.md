# Backlog

## P1

- Restore and document a working local validation path for PlatformIO. `pio test -e native` was broken in this workspace because the `~/.platformio/penv` virtualenv no longer matched the system Python version.

## P2

- Finish the documentation backlog already identified in `docs/testing.md`: undocumented routes, undocumented NVS keys, and undocumented config/auth/OTA constants.
- Add direct route-level regression coverage for `admin` versus `viewer` access to `/users`, `/config/save`, `/motor`, `/log`, `/delete/*`, `/setpoint`, `/pid`, `/ota/status`, and `/ota/update`. Current coverage is strong at the auth helper layer but still not end-to-end at the HTTP handler layer.

## P3

- Reduce doc drift by generating route/constant inventories from code or by adding a verification script that compares docs against the implementation.
- Consider splitting the remaining large Arduino-only modules, especially `src/web.cpp`, to reduce coupling between auth, OTA, config, and UI transport handlers.
