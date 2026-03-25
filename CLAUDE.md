# CLAUDE.md

This file provides behavioral guidance to Claude Code when working with this repository. For detailed specs, see `docs/`.

## Quick Reference — Where to Find Things

| Topic | Location |
|---|---|
| HTTP/WebSocket/MQTT/InfluxDB API | `docs/api-reference.md` |
| Build flags, config tiers, NVS keys | `docs/config-reference.md` |
| GPIO assignments, sensors, connectors | `docs/pinout.md` |
| KiCad schematic rules | `docs/kicad/SCHEMATIC_GEN_RULES.md` |
| Hardware design constraints | `docs/hardware-rules.md` (**⚠ old WROOM-32 board — GPIO numbers do not match current ESP32-S3**) |

## MCP Server Configuration

→ Use skill **`diagnose-mcp-servers`** for full step-by-step diagnostics.

- Config lives in `~/.mcp.json` (global) — NOT `settings.json > mcpServers`
- KiCad MCP Python package is **`kicad-skip`** (NOT `skip-python`); module name is `skip`
- All config changes require a full Claude Code restart — no hot-reload

## Settings File Conventions

When writing permission rules or any paths in `.claude/settings.local.json` or `.claude/settings.json`, always use `~/` for paths under the home directory. Never use `/home/<username>/` or any other absolute path containing a hardcoded username.

## Project Overview

ESP32-based sauna automation system. Monitors temperature/humidity via PT1000 (stove) and dual DHT21 (ceiling/bench) sensors, monitors power via INA260, and controls two stepper-driven damper vents using dual PID controllers. Integrates with InfluxDB, MQTT (Home Assistant MQTT Discovery), and provides a local WebSocket/HTTP interface. Always consider memory constraints and real-time requirements when suggesting code changes.

- **After any C/C++ change**, run `pio run` to verify compilation before considering the change done.
- **Key paths:** `src/` for firmware source, `include/` for headers, `data/` for LittleFS web assets, `test/` for native unit tests.

## Project Conventions

Configuration values should be exposed as `#define` or `constexpr` where possible to keep them easily tunable.

### Sensor Handling

→ See skill **`esp32-sensor-patterns`** for checklist, code examples, and consumer audit table.

Key invariants: all sensor floats init to `NAN`; clear to `NAN` on any read failure; use `||` (not `&&`) for `last_ok_ms` updates; `isSensorStale()` and `!isnan()` are separate guards — both required at every consumer. INA260 is optional (`ina260_ok`); omit all power fields from JSON/InfluxDB when absent.

### Config Persistence

→ See `docs/config-reference.md` for the full 3-tier system (build flags → LittleFS → NVS).

Critical rules:
- Setpoints stored internally in **°C**; HTTP/MQTT API accepts/returns **°F** — convert at the boundary only
- Every NVS read must be guarded by `prefs.isKey()` — missing key must never revert a fleet config value
- `handleConfigSave()` uses staged validation: declare all candidates → validate all → apply+persist in one block; never apply partial state on failure
- `static_ip` / `device_name` require restart → include `"restart_required":true` in HTTP response

### Logging Configuration

Two independent intervals, both configurable at runtime via `/config/save` and persisted to NVS:

| Variable | NVS key | Default define | Controls |
|---|---|---|---|
| `g_sensor_read_interval_ms` | `sri` | `DEFAULT_SENSOR_READ_INTERVAL_MS` (2000 ms) | How often sensors are read, PID computed, WebSocket broadcast, MQTT published |
| `g_serial_log_interval_ms` | `slg` | `SERIAL_LOG_INTERVAL_MS` (10000 ms) | How often the tabular serial status table is printed (throttled inside the sensor read block) |

These are **independent**: reducing `g_serial_log_interval_ms` does not increase sensor read frequency, and vice versa. Serial log prints ERR (not 0.0) for NaN sensor values and `---` for unavailable readings.

## Skills & Knowledge

Project-specific skills are in `.claude/skills/`. Reference these before implementing known patterns:

- `kicad-erc-drc-workflow` — ERC/DRC violation fixes, wire format, PCB parity, unconnected-* nets
- `esp32-sensor-patterns` — sensor staleness, NaN handling, PID guards
- `esp32-auth-bearer` — Bearer token auth implementation
- `esp32-ota-update` — OTA manifest flow and boot health

For KiCad-related work, check `.claude/skills/` and `~/.claude/skills/learned/` for ERC/DRC patterns and PCB text-editing lessons.

## Code Quality

After making bug fixes or feature changes to ESP32/embedded code, review all related state variables to ensure they are properly reset or invalidated on error conditions.

When editing JSON files (especially `settings.json`, `.mcp.json`, `config.json`), always validate syntax after changes — no trailing commas, proper bracket closure. Run `python3 -m json.tool <file>` to validate.

### Post-Edit Validation

| File type | Validation command |
|---|---|
| C++ / `.h` | `pio run` — check for compiler errors and warnings |
| JSON | `python3 -m json.tool <file>` — catches trailing commas and syntax errors |
| Python | `python3 -m py_compile <file>` |
| Functional change | `pio test -e native` — run unit tests to confirm behavior |

If validation fails, fix the issue before moving on. Do not leave a broken state and continue with other changes.

## Testing

After implementing features, run `pio test -e native` and report pass/fail counts.

For auth/access changes, verify no privilege escalation in role defaults (default role must be `""`, never `"admin"`).

IntelliSense errors in VSCode may be false positives — distinguish between IDE warnings and actual compiler errors (`pio run`).

## Build Commands

```bash
# ⚠ targets = upload, uploadfs in platformio.ini — bare `pio run` uploads BOTH firmware AND
# filesystem. Use explicit targets to avoid overwriting a customized filesystem image:
pio run -t upload    # firmware only
pio run -t uploadfs  # filesystem only (web UI in data/)

# Open serial monitor (115200 baud)
# ⚠ Two USB connectors on board — use UART connector (GPIO43/44), NOT USB-C OTG (GPIO19/20)
pio device monitor

# Clean build
pio run -t clean

# Run all native unit tests (no device required)
pio test -e native

# Run a single test suite
pio test -e native -f test_gpio_config
```

All firmware commands default to the `lb_esp32s3` environment (board: `lolin_s3`, ESP32-S3 N16R8). Unit tests use the `native` environment.

## Git Hooks

`scripts/update-handoff.sh` regenerates `HANDOFF.md` after every commit. Installed as `.git/hooks/post-commit` (not committed — install manually on each clone):

```bash
ln -s ../../scripts/update-handoff.sh .git/hooks/post-commit
```

To regenerate manually: `bash scripts/update-handoff.sh` (use `FORCE_BUILD=1` or `SKIP_BUILD=1` to control build).

## Architecture

### Source File Tree

```
src/
├── main.cpp        ~874 lines  Thin orchestrator: global definitions, setup(), loop(), loadLittleFSConfig(), savePrefs(), OTA helpers
├── globals.h        100 lines  Extern declarations for all globals defined in main.cpp; Arduino types in #ifdef ARDUINO guards
├── gpio_config.h     66 lines  GPIO pin assignments for LB-ESP32S3-N16R8; no Arduino deps — natively testable
├── sauna_logic.h    134 lines  Portable pure-C++: c2f/f2c/fmtVal, isSensorStale, SaunaConfig+merge, buildJsonFull (23-field JSON)
├── auth_logic.h     352 lines  Portable pure-C++: 64-char token sessions, SHA-256 passwords, user store, authAttemptLogin()
├── ota_logic.h      121 lines  Portable pure-C++: version parsing/compare, OtaManifest, boot-health, partial-download detection
├── motor_logic.h     16 lines  Portable pure-C++: motorClampCW() — CW step clamping at max_steps ceiling
├── sensors.h         32 lines  stoveReading() inline (natively testable); readSensors()/checkOverheat() declared (Arduino-only)
├── sensors.cpp      103 lines  readSensors() — DHT21×2, MAX31865, INA260; checkOverheat() rising-edge state machine
├── web.h             80 lines  buildJson() inline (natively testable); all handle*() HTTP and webSocketEvent() declared
├── web.cpp          782 lines  HTTP route handlers, WebSocket event handler, buildJsonFull() calls
├── mqtt.h            20 lines  mqttConnect/Callback/PublishState/PublishDiscovery declared (Arduino-only)
├── mqtt.cpp         195 lines  MQTT lifecycle, HA Discovery, sauna/state publish, control topic subscriptions
├── influx.h          21 lines  writeInflux() and logAccessEvent() declared (Arduino-only)
├── influx.cpp        59 lines  InfluxDB write (60s interval, NaN fields omitted), access event logging
├── auth.h           195 lines  requireAdmin() Bearer check; auth HTTP handlers; NVS user-store I/O; security headers
└── secrets.h         26 lines  WiFi/InfluxDB/MQTT credentials + AUTH_ADMIN_USER/PASS — NOT committed to git

include/
└── README            PlatformIO placeholder (all project headers live in src/)
```

### Key Module Details

**`src/main.cpp`** — Thin orchestrator. Global variable definitions, `loadLittleFSConfig()`, `savePrefs()`, OTA helpers, `setup()`, `loop()`.

**`src/sauna_logic.h`** — Header-only pure-C++ (no Arduino deps). `c2f`/`f2c`/`fmtVal`, `isSensorStale`, `SaunaConfig`+`ConfigLayer`+`mergeConfigLayer`, `buildJsonFull()` (23-field WebSocket JSON).

**`src/sensors.h/.cpp`** — `stoveReading()` inline in header (natively testable); returns `stove_temp` or ceiling/bench average fallback. `readSensors()` reads all sensors, applies `||` rule for `last_ok_ms`. `checkOverheat()` rising-edge state machine; motor drive is inside this function.

**`src/web.h/.cpp`** — `buildJson()` inline in header (natively testable). All `handle*()` HTTP handlers and `webSocketEvent()`.

**`src/auth_logic.h`** — Header-only pure-C++. Token sessions (64 chars, 1-hour TTL, 10 concurrent), SHA-256 passwords with constant-time compare, adapter-first login fallback.

**`src/ota_logic.h`** — Header-only pure-C++. Version parsing/compare, manifest handling, boot-health rollback, partial-download detection.

**`src/motor_logic.h`** — Header-only pure-C++. `motorClampCW()` clamps CW steps at `max_steps` ceiling.

## PID Controllers

| Controller | Input | Output | Motor |
|---|---|---|---|
| `CeilingPID` (QuickPID) | `ceiling_temp` (°C) | `ceiling_output` (0–255) | Outflow |
| `BenchPID` (QuickPID) | `bench_temp` (°C) | `bench_output` (0–255) | Inflow |

Output 0–255 is linearly mapped to 0–`max_steps` for the corresponding motor. Dual-tuning: aggressive (Kp=4.0, Ki=0.2, Kd=1.0) when error > 10°C, conservative (1.0/0.05/0.25) otherwise. Both PIDs default to disabled at boot. When disabled, output is forced to 0 and the vent is driven closed.

## Safety Systems

### Overheat Protection

Threshold: `TEMP_LIMIT_C = 120.0°C` (248°F). Trigger: `ceiling_temp >= TEMP_LIMIT_C` OR `bench_temp >= TEMP_LIMIT_C` (NaN ignored). When triggered: both vents driven fully open, PID suppressed. Clears when both temps drop below threshold.

### Stale Sensor Detection

Threshold: `STALE_THRESHOLD_MS = 10000UL`. Stale if `last_ok_ms == 0` (never read) or `(millis() - last_ok_ms) > threshold`. Stale readings → JSON `null` via `buildJsonFull()`. Stale detection does not affect PID — that uses NaN checking of raw values.

## Motor Control

→ See `docs/pinout.md` for GPIO assignments.

- `VENT_STEPS = 1024` — default full-open step count (90° quarter-turn on 28BYJ-48)
- Calibrated max steps stored in NVS as `omx` (outflow) / `imx` (inflow)
- Positions reported as 0–100%
- Minimum PID move threshold: 5 steps (suppresses jitter)
- Calibration: `zero` marks closed (step 0); `setopen` marks fully open and persists to NVS

## Authentication System

All state-mutating HTTP routes require `Authorization: Bearer <token>`. See `docs/api-reference.md` for route details.

- Emergency admin seeded from `secrets.h` on first boot if `u0_name` absent from NVS
- Optional external adapter: `ADAPTER_OK` → skip NVS; `ADAPTER_REJECTED` → stop; `ADAPTER_ERROR` → fall through to NVS
- Role from adapter stored verbatim — never default to a privilege level
- `authAddSecurityHeaders()` sends `X-Frame-Options: DENY` and `X-Content-Type-Options: nosniff`

## OTA Update System

→ See `docs/api-reference.md` for `/ota/status` and `/ota/update` routes.

Flow: fetch manifest → parse version → compare to `FIRMWARE_VERSION` → stream binary → reboot. Refuses downgrades and same-version re-flashes.

Boot health: `otaCheckBootHealth()` increments `boot_fail` in NVS on every boot. `otaMarkBootSuccessful()` resets it after WiFi connects. Rollback if `boot_fail >= OTA_MAX_BOOT_FAILURES`. `FIRMWARE_VERSION` defined in `platformio.ini` — update for every release.

## Web UI

`data/index.html` — single-file HTML/JS dashboard served from LittleFS. Upload with `pio run -t uploadfs`. Connects to `ws://<device-ip>:81` for live readings. Uses Chart.js (CDN) for temperature trends.

## LittleFS Layout

| File | Served at | Description |
|---|---|---|
| `index.html` | `GET /` | Main dashboard |
| `login.html` | `GET /auth/login` | Login page |
| `config.html` | `GET /config` | Configuration portal page |
| `config.json` | Layer 2 config | Fleet defaults (read by `loadLittleFSConfig()`) |

`Cache-Control: no-store` on all HTML pages. `LittleFS.begin(true)` formats partition on first boot.

## platformio.ini Structure

Three environments:
- `lb_esp32s3` (default) — LB-ESP32S3-N16R8; board `lolin_s3`; 16 MB OPI flash/8 MB OPI PSRAM; partition table `partitions_ota_16mb.csv`
- `upesy_wroom` (legacy reference only) — **Do not flash** — GPIO assignments target ESP32-S3 and will not match
- `native` — native unit tests only; `std=c++14`; `test_build_src = false`

`extra_scripts = scripts/upload_fs.py` hooks the filesystem upload. `targets = upload, uploadfs` makes bare `pio run` upload both.

### Partition Table Rules

**The LittleFS partition must be named `spiffs`.** `LittleFS.begin()` searches for `spiffs` by default — not `littlefs`. This has burned us twice.

## Alternative Firmware

`sauna_esphome.yaml` — ESPHome configuration. Lacks native InfluxDB support and runtime motor calibration. Do not merge concerns between the two firmware approaches.

## Unit Tests

157 tests across 9 suites. Run with `pio test -e native`.

| Suite | Tests | What's covered |
|---|---|---|
| `test/test_sensor/` | 8 | `c2f`/`f2c`/`fmtVal`; NaN→null in JSON per sensor |
| `test/test_config/` | 9 | 3-tier merge logic; range validation; NVS-wins-over-fleet |
| `test/test_websocket/` | 12 | `buildJsonFull()` output; stale detection edge cases |
| `test/test_auth/` | 35 | Tokens, passwords, user store, login fallback, adapter orchestration |
| `test/test_ota/` | 17 | Version parsing/comparison, manifest, rollback, partial download |
| `test/test_sensor_module/` | 5 | `stoveReading()` fallback to ceiling/bench average |
| `test/test_web_module/` | 6 | `buildJson()` struct assembly; INA260 absent path |
| `test/test_motor_logic/` | 8 | `motorClampCW()` clamping at max; CCW floor at zero |
| `test/test_gpio_config/` | 21 | Pin values, adjacency, uniqueness, restricted pins, coil order |

## Common Pitfalls

| Pitfall | Symptom | Fix |
|---|---|---|
| **Stale sensor values retained** | Vent moves on old data after sensor disconnects | Clear to `NAN` on read failure; use `\|\|` not `&&` for `last_ok_ms` (either temp OR humidity succeeding means sensor is alive) |
| **Stale detection vs NaN — two separate checks** | Stale-but-non-NaN reading drives PID motors | Both `!isnan()` AND `!isSensorStale()` required at every consumer (display, PID, MQTT, InfluxDB, serial) |
| **New JSON field not consumed** | Field transmitted correctly but UI/MQTT/InfluxDB ignores it | After adding any field, audit ALL consumers: dashboard JS, MQTT handler, InfluxDB writer, serial log |
| **JSON trailing comma** | `config.json` / `settings.json` silently rejected | Always run `python3 -m json.tool <file>` after any JSON edit |
| **Wrong pip package** | MCP server crashes on import | Package is `kicad-skip`, NOT `skip-python` |
| **KiCad PCB net regex too loose** | New nets inserted inside a pad block, corrupting footprint | Use `^\t\(net \d+ "` (exactly 1 tab) — `\s+` also matches pad-internal refs at 3-tab indent |
| **LittleFS partition named wrong** | `partition "spiffs" could not be found` at boot | Partition CSV entry must be named `spiffs`, not `littlefs` |
| **`extra_scripts` uploads both firmware and FS** | Overwrites customized `data/` image | Use `pio run -t upload` (firmware only) or `pio run -t uploadfs` (FS only) |
