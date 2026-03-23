# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MCP Server Configuration

→ Use skill **`diagnose-mcp-servers`** for full step-by-step diagnostics (config location, `enabledMcpjsonServers`, binary, Python deps, restart).

Project-specific facts:
- Config lives in `~/.mcp.json` (global) — NOT `settings.json > mcpServers`
- KiCad MCP Python package is **`kicad-skip`** (NOT `skip-python`); module name is `skip` (`import skip`, not `import kicad_skip`)
- Known upstream issue: Seeed-Studio server may have broken imports requiring a manual source patch
- All config changes require a full Claude Code restart — no hot-reload

## Settings File Conventions

When writing permission rules or any paths in `.claude/settings.local.json` or `.claude/settings.json`, always use `~/` for paths under the home directory. Never use `/home/<username>/` or any other absolute path containing a hardcoded username.

## Project Conventions

This project uses Python and C/C++ (ESP32 Arduino). Configuration values should be exposed as `#define` or `constexpr` where possible to keep them easily tunable.

### Config Persistence

→ See skill **`embedded-config-layering`** for the full pattern with code examples and common mistakes.

Three tiers applied in order during `setup()` — later wins: build-flag `#define`s → LittleFS `/config.json` → NVS namespace `sauna`.

Critical rules specific to this project:
- Setpoints stored internally in **°C**; HTTP/MQTT API accepts/returns **°F** — convert at the boundary only
- Every NVS read must be guarded by `prefs.isKey()` — missing key must never revert a fleet config value
- `handleConfigSave()` uses staged validation: declare all candidates → validate all → apply+persist in one block; never apply partial state on failure
- `static_ip` / `device_name` require restart → include `"restart_required":true` in HTTP response

### Sensor Handling

→ See skill **`esp32-sensor-patterns`** for checklist, code examples, and consumer audit table.

Key invariants: all sensor floats init to `NAN`; clear to `NAN` on any read failure; use `||` (not `&&`) for `last_ok_ms` updates; `isSensorStale()` and `!isnan()` are separate guards — both required at every consumer. INA260 is optional (`ina260_ok`); omit all power fields from JSON/InfluxDB when absent.

### Logging Configuration

Two independent intervals, both configurable at runtime via `/config/save` and persisted to NVS:

| Variable | NVS key | Default define | Controls |
|---|---|---|---|
| `g_sensor_read_interval_ms` | `sri` | `DEFAULT_SENSOR_READ_INTERVAL_MS` (2000 ms) | How often sensors are read, PID computed, WebSocket broadcast, MQTT published |
| `g_serial_log_interval_ms` | `slg` | `SERIAL_LOG_INTERVAL_MS` (10000 ms) | How often the tabular serial status table is printed (throttled inside the sensor read block) |

These are **independent**: reducing `g_serial_log_interval_ms` does not increase sensor read frequency, and vice versa. Both have min/max validation bounds (`SENSOR_READ_INTERVAL_MIN/MAX_MS`, `SERIAL_LOG_INTERVAL_MIN/MAX_MS`).

Serial log prints ERR (not 0.0) for NaN sensor values and `---` for unavailable readings.

## Project Overview

ESP32-based sauna automation system. Monitors temperature/humidity via PT1000 (stove) and dual DHT21 (ceiling/bench) sensors, monitors power via INA260, and controls two stepper-driven damper vents using dual PID controllers. Integrates with InfluxDB, MQTT (Home Assistant MQTT Discovery), and provides a local WebSocket/HTTP interface.

This is an ESP32 embedded project (sauna controller) using Arduino/PlatformIO. Key technologies: C++, ESP32, WebSocket, DHT sensors, KiCad for PCB design. Always consider memory constraints and real-time requirements when suggesting code changes.

- **After any C/C++ change**, run `pio run` to verify compilation before considering the change done.
- **Key paths:** `src/` for firmware source, `include/` for headers, `data/` for LittleFS web assets, `test/` for native unit tests.
- **Config:** 3-tier persistence system — build-flag defaults → LittleFS `config.json` → per-device NVS. Later tiers win.

## Hardware & Sensors

→ See skill **`esp32-sensor-patterns`**. Clear to `NAN` on read failure; use `||` not `&&` for `last_ok_ms`; apply validity checks to every consumer (JSON, PID, MQTT, InfluxDB, serial).

## Skills & Knowledge

Project-specific skills are in `.claude/skills/`. Reference these before implementing known patterns:

- `kicad-erc-drc-workflow` — ERC/DRC violation fixes, wire format, PCB parity, unconnected-* nets
- `esp32-sensor-patterns` — sensor staleness, NaN handling, PID guards
- `esp32-auth-bearer` — Bearer token auth implementation
- `esp32-ota-update` — OTA manifest flow and boot health

For KiCad-related work, check `.claude/skills/` and `~/.claude/skills/learned/` for ERC/DRC patterns and PCB text-editing lessons (e.g., `kicad-pcb-text-edit.md`, `kicad-schematic-text-edit.md`).

## Code Quality

After making bug fixes or feature changes to ESP32/embedded code, review all related state variables to ensure they are properly reset or invalidated on error conditions.

When editing JSON files (especially `settings.json`, `.mcp.json`, `config.json`), always validate syntax after changes — no trailing commas, proper bracket closure. Run `python3 -m json.tool <file>` to validate.

### Post-Edit Validation

After any code edit, immediately run the most relevant validation — never assume an edit is correct without it:

| File type | Validation command |
|---|---|
| C++ / `.h` | `pio run` — check for compiler errors and warnings |
| JSON | `python3 -m json.tool <file>` — catches trailing commas and syntax errors |
| Python | `python3 -m py_compile <file>` |
| Functional change | `pio test -e native` — run unit tests to confirm behavior |

If validation fails, fix the issue before moving on. Do not leave a broken state and continue with other changes.

## Testing

After implementing features, run the full test suite and report pass/fail counts:

```bash
pio test -e native
```

For auth/access changes, verify no privilege escalation in role defaults (default role must be `""`, never `"admin"`).

IntelliSense errors in VSCode may be false positives — distinguish between IDE warnings and actual compiler errors (`pio run`).

## Build Commands

```bash
# Build firmware (compile only)
pio run -t build

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

`scripts/update-handoff.sh` regenerates `HANDOFF.md` after every commit (last 10 commits,
TODO scan, native test results, build warnings, pitfalls from this file). It is installed
as `.git/hooks/post-commit`, which is **not committed to git** — you must install it manually
on each new clone:

```bash
ln -s ../../scripts/update-handoff.sh .git/hooks/post-commit
# or copy:
cp scripts/update-handoff.sh .git/hooks/post-commit && chmod +x .git/hooks/post-commit
```

To regenerate `HANDOFF.md` manually without committing:

```bash
bash scripts/update-handoff.sh          # skips build if no C/C++ files changed
FORCE_BUILD=1 bash scripts/update-handoff.sh   # always runs pio build
SKIP_BUILD=1 bash scripts/update-handoff.sh    # always skips pio build
```

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

### Architecture Overview

#### Config (3-tier, later wins)
1. Build flags (`#define` + `-D` in `platformio.ini`) — compile-time defaults, all `#ifndef`-guarded
2. LittleFS `/config.json` — fleet-wide defaults uploaded with `pio run -t uploadfs`
3. NVS namespace `sauna` — per-device overrides written by HTTP/MQTT handlers; `prefs.isKey()` guards prevent missing keys from reverting Layer 2 values

#### Auth (Bearer token)
- Tokens: 32-byte random, hex-encoded (64 chars), 1-hour TTL, 10 concurrent sessions
- Passwords: SHA-256(salt_bytes || password_bytes), constant-time compare
- Login: optional external HTTP adapter first; `ADAPTER_ERROR` falls through to NVS; `ADAPTER_REJECTED` stops immediately
- First-boot emergency admin seeded from `secrets.h` if NVS slot 0 is empty

#### WebSocket live updates
- `buildJsonFull()` in `sauna_logic.h` serializes 23 fields every sensor read cycle (default 2 s)
- NaN sensor values → JSON `null`; stale readings (>10 s since last ok) → `null` + `cst`/`bst` flags
- New clients receive the full state immediately on connect; subsequent updates are broadcast to all

### Firmware: `src/main.cpp`

~870 lines (thin orchestrator after modular refactor). Includes all module headers. Retains:

- Pin mapping comment block
- All global variable **definitions** (sensors, motors, PID, config, auth sessions, runtime state)
- `loadLittleFSConfig()` — Layer 2 config from `/config.json`
- `savePrefs()` — writes all runtime state to NVS
- OTA helpers: `otaCheckBootHealth`, `otaMarkBootSuccessful`, `otaCheckPartialDownload`
- `setup()` — initializes all peripherals, WiFi, InfluxDB, HTTP, WebSocket, MQTT; loads config layers in order; seeds emergency admin
- `loop()` — calls `readSensors()`, `checkOverheat()`, PID compute, motor drive, WebSocket/MQTT/InfluxDB cycles

### `src/globals.h`

Single source of truth for all `extern` declarations for globals defined in `main.cpp`. Hardware objects (`CheapStepper`, `MAX31865`, `DHT`, `INA260`, `WebServer`, `WebSocketsServer`, `PubSubClient`, `InfluxDBClient`) wrapped in `#ifdef ARDUINO` guards for native test compatibility.

### `src/sensors.h/.cpp`

Sensor I/O and overheat protection.

- `stoveReading()` — **inline in header** (natively testable); returns `stove_temp` or ceiling/bench average fallback (both air sensors must be valid)
- `readSensors()` — reads DHT21 ceiling/bench, MAX31865 stove, INA260 power; updates globals; applies `||` rule for `last_ok_ms`
- `checkOverheat()` — rising-edge state machine; drives both vents fully open on alarm onset; returns `bool` (alarm active); motor drive is inside this function, not in caller

### `src/web.h/.cpp`

All HTTP handlers and WebSocket event handler.

- `buildJson()` — **inline in header** (natively testable); assembles globals into `SensorValues`/`MotorState`/`PIDState` structs and calls `buildJsonFull()`; guards `pwr_*` fields on `ina260_ok`
- All `handle*()` functions — see HTTP Server table
- `webSocketEvent()` — WebSocket connection handler

### `src/mqtt.h/.cpp`

MQTT connection lifecycle and publish/subscribe.

- `mqttConnect()` — connects to broker; subscribes to control topics; publishes HA discovery
- `mqttCallback()` — handles PID enable/disable and setpoint changes
- `mqttPublishState()` — publishes full JSON to `sauna/state`
- `mqttPublishDiscovery()` — publishes retained HA MQTT Discovery configs

### `src/influx.h/.cpp`

InfluxDB write operations.

- `writeInflux()` — writes `sauna_status` and `sauna_control` every 60 seconds; NaN fields omitted
- `logAccessEvent(event, username, auth_source, client_ip)` — writes login/logout/failure to `sauna_webaccess`; `client_ip` provided by caller

### sauna_logic.h

Header-only pure-C++ file (`src/sauna_logic.h`, 133 lines) with no Arduino or ESP-IDF dependencies. Exists so that portable logic can be compiled and unit-tested natively without the ESP32 toolchain.

Contains:

- `c2f(float c)` / `f2c(float f)` — Celsius/Fahrenheit conversion
- `fmtVal(char*, size_t, float)` — formats a float as `"%.1f"` or `"null"` if NaN
- `isSensorStale(last_ok_ms, now_ms, threshold_ms)` — stale detection; returns true if `last_ok_ms==0` (never read) or `(now_ms - last_ok_ms) > threshold_ms`; threshold 0 disables checking
- `struct SaunaConfig` — canonical config with defaults (ceiling 160°F, bench 120°F, both PIDs off)
- `struct ConfigLayer` — one optional-override layer with `has_*` presence flags
- `mergeConfigLayer(SaunaConfig&, const ConfigLayer&)` — applies a layer; validates setpoints 32–300°F
- `struct SensorValues`, `struct MotorState`, `struct PIDState` — data containers
- `buildJsonFull(sv, ms, ps, now_ms, buf, len)` — builds the 23-field WebSocket JSON; stale readings become `null`

### motor_logic.h

Header-only pure-C++ file (`src/motor_logic.h`) with no Arduino/hardware dependencies. Natively testable.

Contains:

- `motorClampCW(current_target, steps, max_steps)` — returns actual CW steps to move, clamped so `(current_target + actual) <= max_steps`; returns 0 if already at or beyond max. Used by `handleMotorCmd` in `web.cpp` for the `cw` command. The `ccw` floor-at-zero mirror uses the existing `min(steps, *tgt)` pattern inline.

## Sensor Layer

| Sensor | Type | Interface | GPIO | Notes |
|---|---|---|---|---|
| Stove | PT1000 via MAX31865 | Hardware SPI | CS=GPIO42, SCK=GPIO41, MISO=GPIO40, MOSI=GPIO39 | 3-wire mode; RREF=4300.0Ω, RNOMINAL=1000.0Ω; `SPI.begin(SCK,MISO,MOSI)` required before `begin()` |
| Ceiling | DHT21 (AM2301) | 1-wire | GPIO8 | External; connects via J4 (4-pin header, Pin1=VDD, Pin2=DATA, Pin3=NC, Pin4=GND); 10kΩ pull-up DATA→VCC |
| Bench | DHT21 (AM2301) | 1-wire | GPIO9 | External; connects via J5 (4-pin header, Pin1=VDD, Pin2=DATA, Pin3=NC, Pin4=GND); 10kΩ pull-up DATA→VCC |
| Power | INA260 | I2C | SDA=GPIO1, SCL=GPIO2 | Integrated 2mΩ shunt; no external resistor; optional — gracefully disabled if absent |

SPI pins GPIO39–42 are reserved for MAX31865; do not use for other purposes. All GPIO assignments are defined in `src/gpio_config.h` and verified by `pio test -e native` (test_gpio_config).

Stove fault handling: any MAX31865 fault or temperature outside −200°C to 900°C sets `stove_temp = NAN` and prints the fault bits to serial.

Fallback: if `stove_temp` is NaN, `stoveReading()` returns the average of ceiling and bench temperatures instead.

## Motor Control

### Outflow (upper vent) — CheapStepper → ULN2003 → 28BYJ-48

| IN pin | GPIO |
|---|---|
| IN1 | 4 |
| IN2 | 5 |
| IN3 | 6 |
| IN4 | 7 |

### Inflow (lower vent) — CheapStepper → ULN2003 → 28BYJ-48

| IN pin | GPIO |
|---|---|
| IN1 | 15 |
| IN2 | 16 |
| IN3 | 17 |
| IN4 | 18 |

Both ULN2003 boards powered at 5V.

- `VENT_STEPS = 1024` — default full-open step count (90° quarter-turn on 28BYJ-48)
- Both motors run at `MOTOR_RPM` RPM (default 12; override with `-DMOTOR_RPM=xxx`)
- Calibrated full-open step counts are stored in NVS as `omx` (outflow) and `imx` (inflow) and override `VENT_STEPS` at runtime
- Positions reported as 0–100% (`outflow_pos` / `inflow_pos`) computed as `target * 100 / max_steps`
- Minimum PID move threshold: 5 steps — deltas smaller than this are ignored to suppress jitter
- Motor calibration commands: `zero` marks current position as closed (step 0); `setopen` marks current position as fully open and persists `omx`/`imx` to NVS

## PID Controllers

| Controller | Input | Output | Motor |
|---|---|---|---|
| `CeilingPID` (QuickPID) | `ceiling_temp` (°C) | `ceiling_output` (0–255) | Outflow |
| `BenchPID` (QuickPID) | `bench_temp` (°C) | `bench_output` (0–255) | Inflow |

Output 0–255 is linearly mapped to 0–`max_steps` for the corresponding motor.

#### Dual-tuning (both controllers use the same values):

| Mode | Kp | Ki | Kd | Condition |
|---|---|---|---|---|
| Aggressive | 4.0 | 0.2 | 1.0 | error > `PID_CONSERVATIVE_THRESHOLD_C` (default 10°C) |
| Conservative | 1.0 | 0.05 | 0.25 | error ≤ `PID_CONSERVATIVE_THRESHOLD_C` |

Setpoints default to `DEFAULT_CEILING_SP_F=160°F` and `DEFAULT_BENCH_SP_F=120°F` (converted to °C internally). Both PIDs default to disabled at boot; must be explicitly enabled via HTTP, MQTT, or config.

When a PID is disabled, its output is forced to 0 and the corresponding vent is driven closed.

## Safety Systems

### Overheat Protection

Threshold: `TEMP_LIMIT_C = 120.0°C` (248°F). Overridable via build flag `-DTEMP_LIMIT_C=115`.

Trigger condition: `ceiling_temp >= TEMP_LIMIT_C` OR `bench_temp >= TEMP_LIMIT_C` (NaN readings are ignored).

When triggered (`checkOverheat()` returns true):
- `overheat_alarm = true`
- Both vents driven to fully open immediately
- PID computation suppressed for both controllers on every loop iteration

Alarm clears automatically when both air temps drop below `TEMP_LIMIT_C`. Serial messages printed on both state transitions.

### Stale Sensor Detection

Threshold: `STALE_THRESHOLD_MS = 10000UL` (10 seconds). Overridable via build flag `-DSTALE_THRESHOLD_MS=xxxxx`.

A DHT sensor reading is considered stale if:
- `last_ok_ms == 0` (sensor has never successfully returned a reading since boot), OR
- `(millis() - last_ok_ms) > STALE_THRESHOLD_MS`

Staleness is evaluated in `buildJsonFull()` at JSON build time. Stale readings are serialized as JSON `null` regardless of the stored float value. The `cst` (ceiling stale) and `bst` (bench stale) flags in the WebSocket JSON indicate staleness to the UI.

Stale detection does not affect PID computation — that relies on NaN checking of the raw sensor values.

## Network Stack

- WiFi static IP: `192.168.1.200`, gateway: `192.168.1.100`, subnet: `255.255.255.0`, DNS: `8.8.8.8`
- Credentials from `src/secrets.h`

### HTTP Server (port 80)

All state-mutating routes require `Authorization: Bearer <token>`. Unauthenticated requests return `401 application/json`.

| Route | Method | Auth | Handler | Description |
|---|---|---|---|---|
| `/` | GET | None | `handleRoot` | Serves `index.html` from LittleFS |
| `/log` | GET | Bearer | `handleLog` | Triggers immediate InfluxDB write |
| `/delete/status` | GET | Bearer | `handleDeleteStatus` | Deletes all `sauna_status` data from InfluxDB |
| `/delete/control` | GET | Bearer | `handleDeleteControl` | Deletes all `sauna_control` data from InfluxDB |
| `/history?range=Xh` | GET | None | `handleHistory` | Proxies Flux query; returns CSV of ceiling/bench/stove temps (default 1h; alphanumeric range only) |
| `/setpoint?ceiling=F&bench=F` | GET | Bearer | `handleSetpoint` | Sets PID setpoints in °F (32–300); persists to NVS |
| `/pid?ceiling=0\|1&bench=0\|1` | GET | Bearer | `handlePidToggle` | Enables/disables PID controllers; persists to NVS |
| `/motor?motor=outflow\|inflow&cmd=CMD&steps=N` | GET | Bearer | `handleMotorCmd` | Motor control |
| `/config` | GET | None | `handleConfigPage` | Serves `config.html` from LittleFS |
| `/config/get` | GET | Bearer | `handleConfigGet` | Returns current runtime config as JSON |
| `/config/save` | POST | Bearer | `handleConfigSave` | Updates runtime config (setpoints, PID flags, intervals, IP, device name); persists to NVS |
| `/ota/status` | GET | Bearer | `handleOtaStatus` | Returns `{"version","partition","boot_failures"}` |
| `/ota/update?manifest=<url>` | POST | Bearer | `handleOtaUpdate` | Manifest-based OTA update; reboots on success |
| `/auth/login` | GET | None | `handleAuthLoginPage` | Serves `login.html` from LittleFS |
| `/auth/login` | POST | None | `handleAuthLogin` | JSON login; returns `{"token","expires_in","username","role"}` |
| `/auth/logout` | POST | Bearer | `handleAuthLogout` | Invalidates token; logs to InfluxDB |
| `/auth/status` | GET | Bearer | `handleAuthStatus` | Returns `{"valid":true,"username","role"}` |
| `/users` | GET | Bearer | `handleUsersGet` | Lists all users |
| `/users` | POST | Bearer | `handleUsersCreate` | Creates user (JSON body) |
| `/users` | DELETE | Bearer | `handleUsersDelete` | Deletes user by `?username=` (slot 0 protected) |
| `/users` | PUT | Bearer | `handleUsersChangePassword` | Changes password (`?username=` + JSON body) |

Motor `cmd` values: `cw`, `ccw`, `open`, `close`, `third`, `twothird`, `stop`, `zero` (mark closed), `setopen` (mark fully open + persist). Default `steps=64`; clamped to `[1, VENT_STEPS*4]`.

### WebSocket Server (port 81)

Broadcasts JSON state every 2 seconds (tied to the sensor read cycle). Also pushes current state immediately to each newly connected client.

### MQTT (broker: `192.168.1.125`, port: 1883)

Client ID: `sauna_esp32`. Reconnect retry interval: 5 seconds. Buffer size: 512 bytes.

| Topic | Direction | Payload |
|---|---|---|
| `sauna/state` | Publish | Full JSON (temps in °F, positions, PID state, power monitor) |
| `sauna/ceiling_pid/set` | Subscribe | `ON` or `OFF` |
| `sauna/bench_pid/set` | Subscribe | `ON` or `OFF` |
| `sauna/ceiling_setpoint/set` | Subscribe | °F float string (32–300) |
| `sauna/bench_setpoint/set` | Subscribe | °F float string (32–300) |

Home Assistant MQTT Discovery publishes retained configs to `homeassistant/sensor/sauna_esp32/<id>/config`, `homeassistant/switch/sauna_esp32/<id>/config`, and `homeassistant/number/sauna_esp32/<id>/config` on each MQTT connect.

## WebSocket JSON Schema

23 fields, all produced by `buildJsonFull()` in `sauna_logic.h`. NaN floats serialize as JSON `null`.

| Key | Type | Description |
|---|---|---|
| `clt` | float\|null | Ceiling temperature (°F); null if NaN or stale |
| `clh` | float\|null | Ceiling humidity (%); null if NaN or stale |
| `d5t` | float\|null | Bench temperature (°F); null if NaN or stale |
| `d5h` | float\|null | Bench humidity (%); null if NaN or stale |
| `tct` | float\|null | Stove temperature (°F); null if NaN |
| `ofs` | uint | Outflow vent position (0–100%) |
| `ofd` | int | Outflow direction: 1=CW, −1=CCW, 0=stopped |
| `ifs` | uint | Inflow vent position (0–100%) |
| `ifd` | int | Inflow direction: 1=CW, −1=CCW, 0=stopped |
| `csp` | float\|null | Ceiling PID setpoint (°F) |
| `cop` | float\|null | Ceiling PID output (0–255) |
| `ctm` | 0\|1 | Ceiling PID in conservative mode (1) or aggressive (0) |
| `cen` | 0\|1 | Ceiling PID enabled |
| `bsp` | float\|null | Bench PID setpoint (°F) |
| `bop` | float\|null | Bench PID output (0–255) |
| `btm` | 0\|1 | Bench PID in conservative mode (1) or aggressive (0) |
| `ben` | 0\|1 | Bench PID enabled |
| `pvolt` | float\|null | Bus voltage (V); null if INA260 absent or NaN |
| `pcurr` | float\|null | Current (mA); null if INA260 absent or NaN |
| `pmw` | float\|null | Power (mW); null if INA260 absent or NaN |
| `oa` | 0\|1 | Overheat alarm active |
| `cst` | 0\|1 | Ceiling sensor stale (1 = data is stale or never read) |
| `bst` | 0\|1 | Bench sensor stale (1 = data is stale or never read) |

## Configuration System

Three-tier layered config; later layers win. Applied in this order during `setup()`:

**Layer 1: Build-flag defaults** (compile time)
- Set via `-D` flags in `platformio.ini`
- Keys: see **Build-Flag Overrides** table below for the full list

**Layer 2: Fleet defaults — `/config.json` in LittleFS** (loaded by `loadLittleFSConfig()`)
- JSON file uploaded with filesystem image (`pio run -t uploadfs`)
- Overrides build-flag defaults; applies the same values to every device flashed with the same image
- Supported keys: `ceiling_setpoint_f` (float, °F), `bench_setpoint_f` (float, °F), `ceiling_pid_enabled` (bool), `bench_pid_enabled` (bool), `sensor_read_interval_ms` (uint, 500–10000), `serial_log_interval_ms` (uint, 1000–60000), `static_ip` (string, e.g. `"192.168.1.200"`), `device_name` (string, alphanumeric/`_`/`-`, max 24 chars)
- Motor calibration (`omx`/`imx`) is device-specific and intentionally not read from this file
- Missing file is silently ignored; JSON parse errors are logged and the layer is skipped

**Layer 3: Per-device NVS** (namespace `sauna`, loaded via `Preferences`)
- Written by HTTP `/setpoint`, `/pid`, `/motor?cmd=setopen`, `/config/save` endpoints and MQTT subscription callbacks
- Each key is guarded by `prefs.isKey()` so a missing NVS key never silently reverts a Layer 2 value

## Build-Flag Overrides

All can be set in `platformio.ini` under `build_flags` using `-DNAME=value`:

| Define | Default | Description |
|---|---|---|
| `DEFAULT_CEILING_SP_F` | `160.0f` | Initial ceiling setpoint (°F) |
| `DEFAULT_BENCH_SP_F` | `120.0f` | Initial bench setpoint (°F) |
| `TEMP_LIMIT_C` | `120.0f` | Overheat alarm threshold (°C) |
| `SERIAL_LOG_INTERVAL_MS` | `10000` | Serial status log throttle (ms) |
| `STALE_THRESHOLD_MS` | `10000UL` | DHT stale-reading timeout (ms); 0 disables |
| `INFLUX_WRITE_INTERVAL_MS` | `60000UL` | InfluxDB write interval (ms) |
| `MQTT_RECONNECT_INTERVAL_MS` | `5000UL` | MQTT reconnect retry interval (ms) |
| `MOTOR_RPM` | `12` | Stepper motor speed (RPM) |
| `PID_MIN_STEP_DELTA` | `5` | Minimum PID output delta to actuate motor (steps) |
| `PID_CONSERVATIVE_THRESHOLD_C` | `10.0f` | PID mode switch threshold (°C error from setpoint) |
| `SETPOINT_MIN_F` | `32.0f` | Minimum valid setpoint (°F) |
| `SETPOINT_MAX_F` | `300.0f` | Maximum valid setpoint (°F) |
| `DEFAULT_SENSOR_READ_INTERVAL_MS` | `2000UL` | Default sensor read interval (ms) |
| `DEFAULT_STATIC_IP` | `"192.168.1.200"` | Default device static IP |
| `WS_JSON_BUF_SIZE` | `384` | WebSocket JSON output buffer (bytes) |
| `MQTT_BUF_SIZE` | `512` | MQTT client buffer size (bytes) |
| `NTP_SERVER_LOCAL` | `"192.168.1.100"` | Primary NTP server |
| `WIFI_GATEWAY_IP` | `192, 168, 1, 100` | WiFi gateway (IPAddress initializer) |
| `WIFI_DNS_IP` | `8, 8, 8, 8` | Primary DNS (IPAddress initializer) |
| `RREF` | `4300.0` | MAX31865 reference resistor value (Ω) |
| `RNOMINAL` | `1000.0` | MAX31865 nominal resistance at 0°C (Ω) — PT1000 = 1000 |
| `SENSOR_READ_INTERVAL_MIN_MS` | `500UL` | Minimum sensor read interval (ms) |
| `SENSOR_READ_INTERVAL_MAX_MS` | `10000UL` | Maximum sensor read interval (ms) |
| `SERIAL_LOG_INTERVAL_MIN_MS` | `1000UL` | Minimum serial log interval (ms) |
| `SERIAL_LOG_INTERVAL_MAX_MS` | `60000UL` | Maximum serial log interval (ms) |

## NVS Persistence

Namespace: `sauna`. Written by `savePrefs()` and by inline `prefs.put*()` calls in `handleConfigSave()`. Read during `setup()` after LittleFS config.

| Key | Type | Stores |
|---|---|---|
| `csp` | float | Ceiling setpoint (°C) |
| `bsp` | float | Bench setpoint (°C) |
| `cen` | bool | Ceiling PID enabled |
| `ben` | bool | Bench PID enabled |
| `omx` | int | Outflow motor calibrated full-open step count |
| `imx` | int | Inflow motor calibrated full-open step count |
| `sri` | uint | Sensor read interval (ms) |
| `slg` | uint | Serial log interval (ms) |
| `sip` | string | Static IP address (requires restart) |
| `dn` | string | Device name (requires restart) |

Note: setpoints are stored in °C internally; the HTTP/MQTT API accepts and returns °F.

## InfluxDB

Endpoint: `http://192.168.1.125:30115` (defined in `src/secrets.h`). Write interval: 60 seconds.

InfluxDB token is never exposed to HTTP clients; the `/history` endpoint proxies Flux queries server-side.

Tags on all points: `device=ESP32`, `SSID=<wifi-ssid>`.

**Measurement `sauna_status`** — sensor readings:

| Field | Type | Description |
|---|---|---|
| `ceiling_temp` | float | Ceiling temperature (°C); omitted if NaN |
| `ceiling_hum` | float | Ceiling humidity (%); omitted if NaN |
| `bench_temp` | float | Bench temperature (°C); omitted if NaN |
| `bench_hum` | float | Bench humidity (%); omitted if NaN |
| `stove_temp` | float | Stove temp or ceiling/bench average fallback (°C); omitted if NaN |
| `bus_voltage_V` | float | INA260 bus voltage (V); omitted if INA260 absent |
| `current_mA` | float | INA260 current (mA); omitted if INA260 absent |
| `power_mW` | float | INA260 power (mW); omitted if INA260 absent |

**Measurement `sauna_control`** — PID and motor state:

| Field | Type | Description |
|---|---|---|
| `outflow_pos` | int | Outflow vent position (0–100%) |
| `inflow_pos` | int | Inflow vent position (0–100%) |
| `ceiling_setpoint` | float | Ceiling setpoint (°C) |
| `ceiling_pid_out` | float | Ceiling PID output (0–255) |
| `bench_setpoint` | float | Bench setpoint (°C) |
| `bench_pid_out` | float | Bench PID output (0–255) |

## Unit Tests

Tests run natively (no device required) using the Unity framework.

Total: **157 tests** across 9 suites.

| Suite | Tests | What's covered |
|---|---|---|
| `test/test_sensor/` | 8 | `c2f`/`f2c`/`fmtVal`; NaN→null in JSON per sensor |
| `test/test_config/` | 9 | 3-tier merge logic; range validation; NVS-wins-over-fleet |
| `test/test_websocket/` | 12 | `buildJsonFull()` output; stale detection edge cases (threshold boundaries, `last_ok_ms==0`) |
| `test/test_auth/` | 35 | Tokens, passwords, user store, login fallback, adapter orchestration |
| `test/test_ota/` | 17 | Version parsing/comparison, manifest, rollback, partial download |
| `test/test_sensor_module/` | 5 | `stoveReading()` fallback to ceiling/bench average (both required or NaN) |
| `test/test_web_module/` | 6 | `buildJson()` struct assembly; INA260 absent path |
| `test/test_motor_logic/` | 8 | `motorClampCW()` clamping at max; CCW floor at zero |
| `test/test_gpio_config/` | 21 | Pin values, adjacency, uniqueness, restricted pins (boot/USB/OPI/strap), coil order |

Note: `test_sensor_module` is named to avoid ambiguity with `test_sensor`. `test_gpio_config` tests `src/gpio_config.h` — a pure `#define` header with no Arduino deps.

## Web UI

`data/index.html` — single-file HTML/JS dashboard served from LittleFS at `GET /`. Must be uploaded separately from the firmware:

```bash
pio run -t uploadfs
```

The UI connects to the WebSocket server at `ws://<device-ip>:81` and renders live readings. It uses Chart.js (CDN) for the temperature trend chart. The `/history` endpoint feeds the chart with 5-minute aggregated temperature data. Motor control buttons and PID enable toggles are included.

## Authentication System

All state-mutating HTTP routes are protected by `requireAdmin()` — a Bearer token check in `auth.h`. The token must be presented as `Authorization: Bearer <token>` in the request header. Unauthenticated requests receive `401 application/json {"error":"unauthorized"}`. The server collects the `Authorization` header via `server.collectHeaders()` in `setup()`.

### auth_logic.h — Portable auth logic (no Arduino deps)

Header-only pure-C++ file. Testable natively. Contains:
- `AuthSession` (token + username + role + issued_ms) — 10 concurrent sessions (`AUTH_MAX_SESSIONS`)
- `AuthUserStore` — up to 5 users (`AUTH_MAX_USERS`), each with name/hash/salt/role
- `authIssueToken()` — generates 32-byte random token (hex-encoded, 64 chars), evicts oldest/expired slot if full
- `authValidateToken()` — constant-time token match; expiry uses unsigned subtraction (rollover-safe at `uint32_t` boundary)
- `authHashPassword()` — SHA-256(salt_bytes || password_bytes); stored as hex
- `authVerifyPassword()` — constant-time compare to prevent timing side-channel
- `authAttemptLogin()` — adapter-first fallback orchestration (see below)
- `authFindEvictSlot()` — prefers expired/inactive slots over oldest-valid eviction

### Token TTL and auth constants

All tunable via build flags (`-D` in `platformio.ini`):

| Define | Default | Description |
|---|---|---|
| `AUTH_TOKEN_TTL_MS` | `3600000UL` (1 hour) | Session token time-to-live |
| `AUTH_MAX_SESSIONS` | `10` | Concurrent session slots |
| `AUTH_MAX_USERS` | `5` | Maximum stored users |
| `AUTH_MIN_PASS_LEN` | `8` | Minimum password length |
| `AUTH_MAX_PASS_LEN` | `72` | Maximum password length (bounds SHA-256 buffer) |
| `OTA_MAX_BOOT_FAILURES` | `3` | Consecutive boot failures before rollback |

### NVS user store

Namespace: `sauna_auth`. Users stored as `u0_name`, `u0_hash`, `u0_salt`, `u0_role` … `u4_*`. `authNvsLoad()` breaks on first missing `u{i}_name` — orphaned hash/salt/role keys beyond that are never read. `authNvsSave()` clears name keys for removed users to prevent orphans.

External adapter config stored as `db_url` and `db_key` keys in the same namespace.

### Emergency admin seeding

`authSeedEmergencyAdmin()` runs once in `setup()` after WiFi connects (for RNG entropy). It writes the first user only if `u0_name` is absent from NVS. Credentials come from `AUTH_ADMIN_USER` and `AUTH_ADMIN_PASS` in `secrets.h` — both are required `#define`s enforced by `#error` guards in `main.cpp`. Change the password immediately after first boot.

### External adapter (optional)

If `g_db_url` is non-empty, login attempts go through an external HTTP adapter at `<db_url>/validate` with Bearer `g_db_key`. Adapter returns `{"valid":true,"role":"admin"}`. Fallback behavior:
- `ADAPTER_OK` → token issued, NVS not consulted
- `ADAPTER_REJECTED` → login rejected, NVS not consulted (deliberate rejection takes precedence)
- `ADAPTER_ERROR` (network/timeout) → falls through to NVS

Role from adapter is used as-is; empty role is stored verbatim — never default to a privilege level when the adapter doesn't specify one.

### Auth HTTP routes

| Route | Method | Auth | Description |
|---|---|---|---|
| `/auth/login` | GET | None | Serves `login.html` from LittleFS |
| `/auth/login` | POST | None | JSON body `{"username","password"}`; returns `{"token","expires_in","username","role"}` |
| `/auth/logout` | POST | Bearer | Invalidates current token; logs event to InfluxDB |
| `/auth/status` | GET | Bearer | Returns `{"valid":true,"username","role"}` |
| `/users` | GET | Bearer | Lists all users (username, role, protected flag) |
| `/users` | POST | Bearer | Creates user; JSON body `{"username","password","role"}` |
| `/users` | DELETE | Bearer | Deletes user by `?username=`; slot 0 (emergency admin) is protected |
| `/users` | PUT | Bearer | Changes password; `?username=` + JSON body `{"password"}` |

### Access logging (InfluxDB)

Every login, logout, and failure is written to the `sauna_webaccess` measurement. Fields: `client_ip`, `auth_source`. Tags: `device`, `event`, `username`. Written fire-and-forget (`logAccessEvent()` in `auth.h`).

### Security headers

`authAddSecurityHeaders()` sends `X-Frame-Options: DENY` and `X-Content-Type-Options: nosniff` on all auth-related responses.

## OTA Update System

### ota_logic.h — Portable OTA logic (no Arduino deps)

Header-only pure-C++ file. Testable natively. Contains:
- `FirmwareVersion` — major/minor/patch struct; `parseVersion()` validates `"X.Y.Z"` format (rejects trailing garbage, negative values, components >255)
- `compareVersion()` / `isUpdateAvailable()` — refuses downgrades and same-version re-flashes
- `OtaManifest` — `{version, url, md5}`; parsed via lightweight string extractor (no ArduinoJson dep)
- `OtaDownloadState` / `isOtaIncomplete()` — detects power-failure mid-flash via NVS flags
- `shouldRollback(boot_failures, max_failures)` — threshold comparison for boot health

### OTA HTTP routes

| Route | Method | Auth | Description |
|---|---|---|---|
| `/ota/status` | GET | Bearer | Returns `{"version","partition","boot_failures"}` |
| `/ota/update?manifest=<url>` | POST | Bearer | Fetches manifest JSON, checks version, streams firmware binary |

OTA update flow: fetch manifest → parse version → compare to `FIRMWARE_VERSION` → fetch binary → `Update.begin(size)` → set MD5 if present → stream write → `Update.end(true)` → reboot.

### Boot health / rollback

`otaCheckBootHealth()` runs at the very top of `setup()`, before LittleFS or NVS config load. It increments `boot_fail` in NVS on every boot. `otaMarkBootSuccessful()` resets `boot_fail` to 0 and calls `esp_ota_mark_app_valid_cancel_rollback()` once WiFi connects. If `boot_fail >= OTA_MAX_BOOT_FAILURES`, calls `esp_ota_mark_app_invalid_rollback_and_reboot()`. The firmware binary in `platformio.ini` uses a custom OTA partition table (`partitions_ota.csv`).

### Partial download detection

`otaCheckPartialDownload()` reads `ota_ip` / `ota_exp` / `ota_wrt` from NVS. A partial write is logged at startup but does not block normal operation — the bootloader ignores unvalidated OTA slots.

### NVS keys used by OTA (namespace `sauna`)

| Key | Type | Stores |
|---|---|---|
| `boot_fail` | int | Consecutive boot failure count |
| `ota_ip` | bool | OTA download in-progress flag |
| `ota_exp` | uint | Expected firmware bytes |
| `ota_wrt` | uint | Bytes written so far |

### Firmware version

`FIRMWARE_VERSION` is defined in `platformio.ini` via `-DFIRMWARE_VERSION=\"1.0.0\"`. Update this string for every release or OTA will refuse re-flashing the same version.

## LittleFS Layout

Files served from `data/` directory, uploaded via `pio run -t uploadfs`.

| File | Served at | Description |
|---|---|---|
| `index.html` | `GET /` | Main dashboard |
| `login.html` | `GET /auth/login` | Login page |
| `config.html` | `GET /config` | Configuration portal page |
| `config.json` | Layer 2 config | Fleet defaults (read by `loadLittleFSConfig()`) |

`Cache-Control: no-store` is sent on all HTML pages. LittleFS is mounted with `LittleFS.begin(true)` — the `true` argument formats the partition if mount fails (first boot).

## platformio.ini Structure

Three environments:
- `lb_esp32s3` (default) — LB-ESP32S3-N16R8; board `lolin_s3`; 16 MB OPI flash/8 MB OPI PSRAM; `board_build.arduino.memory_type = qio_opi`; partition table `partitions_ota_16mb.csv`; `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0`
- `upesy_wroom` (legacy reference only) — ESP32-WROOM-32 DevKit; board `esp32doit-devkit-v1`; partition table `partitions_ota.csv`. **Do not flash** — GPIO assignments in `gpio_config.h` target ESP32-S3 and will not match old hardware.
- `native` — native unit tests only; `std=c++14`; `test_build_src = false`

`extra_scripts = scripts/upload_fs.py` hooks the filesystem upload. `targets = upload, uploadfs` makes both firmware and filesystem upload by default.

### Partition Table Rules

**The LittleFS partition must be named `spiffs`.** The Arduino ESP32 `LittleFS.begin()` call searches for a partition named `spiffs` by default — not `littlefs`. Using any other name causes `partition "spiffs" could not be found` at boot and LittleFS fails to mount. This has burned us twice.

When creating or editing a partition CSV, verify:
1. The filesystem partition name is `spiffs` (not `littlefs` or anything else)
2. The subtype is `spiffs`
3. Offsets and sizes sum to ≤ total flash size

```
# Correct — Arduino LittleFS finds this
spiffs,     data, spiffs,   0x810000,  0x7F0000,

# Wrong — LittleFS.begin() will not find this
littlefs,   data, spiffs,   0x810000,  0x7F0000,
```

Library dependencies (shared via `[common_libs]`): Adafruit Unified Sensor, DHT sensor library, WebSockets (links2004), ESP8266 Influxdb (tobiasschuerg), Adafruit MAX31865, QuickPID (dlloydev), CheapStepper (tyhenry), PubSubClient (knolleary), Adafruit INA260, ArduinoJson (bblanchon). All pinned by semver range.

## NTP Time Sync

Time zone: `CST6CDT,M3.2.0,M11.1.0` (US Central). NTP uses `timeSync()` from the InfluxDB client library. On first boot, tries up to 3 server pairs in sequence: local router (`NTP_SERVER_LOCAL`) + `pool.ntp.org` first, then public fallbacks. Succeeds if `tm_year > 120` (year > 2020). If sync fails after 3 attempts, timestamps are wrong but operation continues; a warning is printed to serial.

## WiFi Connection

WiFi uses a static IP (from `g_static_ip_str`). No reconnect logic in `loop()` — if WiFi drops, the device does not attempt to reconnect; a reboot is required. MQTT reconnect is handled independently via `MQTT_RECONNECT_INTERVAL_MS` retry in `loop()`.

## HTTP Response Conventions

All JSON API responses follow these patterns:

| Outcome | Status | Body pattern |
|---|---|---|
| Success (mutation) | 200 | `{"ok":true}` or `{"ok":true,"restart_required":false}` |
| Success (data) | 200 | `application/json` with data fields |
| Bad request / validation | 400 | `{"ok":false,"error":"<message>"}` or `{"error":"<message>"}` |
| Unauthorized | 401 | `{"error":"unauthorized"}` or `{"error":"token_invalid"}` |
| Forbidden (slot 0) | 403 | `{"error":"cannot delete emergency admin"}` |
| Not found | 404 | `{"error":"user not found"}` |
| Conflict | 409 | `{"error":"user limit reached"}` or `{"error":"username taken"}` |
| Upstream failure | 502 | `{"ok":false,"error":"..."}` or plain text |
| Server error | 500 | `{"ok":false,"error":"..."}` or plain text |

`handleConfigSave()` uses `goto send_error` to jump from any validation failure to a single error-emit block. This ensures no partial state is applied before a validation error is returned.


## Alternative Firmware

`sauna_esphome.yaml` — ESPHome configuration. Lacks native InfluxDB support and runtime motor calibration. Do not merge concerns between the two firmware approaches.

## Hardware Reference

Full GPIO table and connector details: `docs/pinout.md`

KiCad schematics: `docs/kicad/`

### KiCad Footprint Decision

**Use the WEMOS S3 / Lolin S3 footprint** — 2×20 through-hole, 2.54 mm pitch. The LB-ESP32S3-N16R8 is pin-for-pin compatible with this form factor (confirmed by `board = lolin_s3` in `platformio.ini` and verified against the board silkscreen). Source from SnapEDA or the WEMOS KiCad repo. Do not use the DOIT DevKit footprint (`esp32_devkit_v1_doit.kicad_mod`) — it is 2×15 pins and incompatible.

Left header (20 pins): 3V3×2, RST, GPIO3–18 (sequential), GND
Right header (20 pins + 2 GND): GND, GPIO43, GPIO44, GPIO1, GPIO2, GPIO42–39, GPIO38–35, GPIO0, GPIO45, GPIO48, GPIO47, GPIO21, GPIO20, GPIO19, GND×2

**Note on GPIO35–38:** These appear as accessible header pins on the board silkscreen (labeled SPIIO6, SPIIO7/SPIID7, SPIDQS, FSPIWP) but may be internally connected to the OPI flash interface on the N16R8 module. Treat as reserved until verified with hardware.

## Credentials & Secrets

`src/secrets.h` is not committed to version control. It must define:

```cpp
#define WIFI_SSID        "..."
#define WIFI_PASSWORD    "..."
#define INFLUXDB_URL     "http://192.168.1.125:30115"
#define INFLUXDB_TOKEN   "..."
#define INFLUXDB_ORG     "..."
#define INFLUXDB_BUCKET  "..."
#define MQTT_BROKER      "192.168.1.125"
#define MQTT_PORT        1883
#define MQTT_USER        "..."   // set to "" to connect without credentials
#define MQTT_PASS        "..."
#define AUTH_ADMIN_USER  "admin" // emergency admin seeded on first boot
#define AUTH_ADMIN_PASS  "..."   // min 8 chars; change immediately after first boot
```

Both `AUTH_ADMIN_USER` and `AUTH_ADMIN_PASS` are enforced by `#error` guards at the top of `main.cpp` — omitting either causes a compile error.

## Common Pitfalls

Issues that have bitten this project more than once — check these first before debugging.

| Pitfall | Symptom | Fix |
|---|---|---|
| **Stale sensor values retained** | Vent moves on old data after sensor disconnects | Clear to `NAN` on read failure; use `\|\|` not `&&` for `last_ok_ms` |
| **JSON trailing comma** | `config.json` / `settings.json` silently rejected at boot or startup | Always run `python3 -m json.tool <file>` after any JSON edit |
| **Wrong pip package** | MCP server crashes on import (`kicad-skip` functionality missing) | Package is `kicad-skip`, NOT `skip-python`; verify name before installing |
| **KiCad PCB net regex too loose** | New nets inserted inside a pad block, corrupting the footprint | Use `^\t\(net \d+ "` (exactly 1 tab) — `\s+` also matches pad-internal refs at 3-tab indent |
| **LittleFS partition named wrong** | `partition "spiffs" could not be found` at boot | Partition CSV entry must be named `spiffs`, not `littlefs` |
| **Stale detection vs NaN — two separate checks** | Stale-but-non-NaN reading drives PID motors | `isSensorStale()` in `buildJsonFull()` guards display only; PID needs its own `!isnan()` AND `!isSensorStale()` |
| **`&&` vs `\|\|` on last_ok_ms** | Sensor falsely declared dead when only humidity channel fails | Use `\|\|`: either temp OR humidity succeeding means sensor is alive |
| **`extra_scripts` uploads both firmware and FS** | Overwrites customized `data/` image unexpectedly | Use `pio run -t upload` (firmware only) or `pio run -t uploadfs` (FS only) instead of bare `pio run` |

## Lessons Learned

**Use `||` not `&&` when updating sensor "last seen" timestamps.** A DHT sensor returns temp and humidity as separate readings; either can be NaN independently. Using `&&` to gate `last_ok_ms` means a sensor with a failed humidity channel is falsely declared dead, triggering stale detection and closing the vents. Use `||`: if any reading from the sensor succeeds, the sensor is alive.

**When adding a field to the WebSocket JSON schema, verify all consumers use it.** The `cst`/`bst` stale flags were correctly computed and transmitted for multiple sessions before the UI was found to silently ignore them. After adding any new JSON field, check every consumer (dashboard JS, MQTT handler, InfluxDB writer) before considering the feature complete.

**Apply sensor validity checks to every data consumer, not just the display path.** Stale detection was added to `buildJsonFull()` for display, but the PID controllers only checked `!isnan()` — so a stale-but-non-NaN reading could still drive the motors. Whenever a new validity condition is introduced (NaN guard, staleness, range check), audit all consumers: display, PID, MQTT, InfluxDB, and serial log.

## ESP32 / Embedded Project Notes

- Config persistence uses a 3-tier system (defaults → NVS → runtime)
- Sensor logging interval and sensor read interval are independent and configurable via `#define`
- When sensors disconnect, stale values must be cleared (don't retain last-known values)
