# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## MCP Server Configuration

MCP servers are configured in `.mcp.json`, NOT in `settings.json` under `mcpServers`. Always check `.mcp.json` first when diagnosing MCP issues. After config changes, advise user to restart Claude Code.

- KiCad MCP server (Seeed-Studio) is configured globally in `~/.mcp.json`
- If MCP server not showing: check `enabledMcpjsonServers` in `settings.json`, verify Python dependencies (`kicad-skip`, NOT `skip-python`), and restart Claude Code
- Known issue: upstream Seeed-Studio package has broken imports that may need patching

## Settings File Conventions

When writing permission rules or any paths in `.claude/settings.local.json` or `.claude/settings.json`, always use `~/` for paths under the home directory. Never use `/home/<username>/` or any other absolute path containing a hardcoded username.

## Project Conventions

This project uses Python and C/C++ (ESP32 Arduino). Configuration values should be exposed as `#define` or `constexpr` where possible to keep them easily tunable.

### Config Persistence

Three-tier system — later tiers win. Applied in order during `setup()`:

1. **Build-flag defaults** — `#define` constants in `main.cpp`, all guarded with `#ifndef` so they can be overridden via `-D` in `platformio.ini` without touching source.
2. **Fleet defaults** — `/config.json` in LittleFS, loaded by `loadLittleFSConfig()`. Missing file is silently ignored. JSON parse errors are logged and the layer is skipped.
3. **Per-device NVS** — namespace `sauna`, loaded via `Preferences`. Each key is guarded by `prefs.isKey()` — a missing NVS key must never silently revert a Layer 2 value.

Key rules:
- Setpoints are stored internally in **°C**; the HTTP/MQTT API accepts and returns **°F**. Convert at the boundary, not inside logic.
- `savePrefs()` writes csp/bsp/cen/ben/omx/imx. Runtime-only fields (sri/slg/sip/dn) are written inline with `prefs.put*()` in `handleConfigSave()`.
- `static_ip` and `device_name` require a restart to take effect — include `"restart_required":true` in the HTTP response when these change.
- `handleConfigSave()` uses a **staged validation pattern**: declare all new values, validate every field, then apply and persist in one block at the end. Never apply a partial update on validation failure.

### Sensor Handling

- All sensor floats are initialized to `NAN` at declaration. **Never retain a stale value** — clear to `NAN` on any read failure or timeout.
- `ceiling_last_ok_ms` / `bench_last_ok_ms` are updated using `||` (either temp **or** humidity succeeding counts as "sensor alive"). Using `&&` falsely kills the sensor when one channel fails.
- Staleness is evaluated at read time via `isSensorStale(last_ok_ms, millis(), STALE_THRESHOLD_MS)` in `sauna_logic.h`. `last_ok_ms == 0` (never read) is always stale.
- PID controllers guard on both `!isnan()` **and** `!isSensorStale()` before calling `Compute()`. Neither check substitutes for the other.
- INA260 is optional — guarded by `ina260_ok`. All power fields are omitted from InfluxDB and JSON when `ina260_ok` is false.
- InfluxDB writes omit any field whose value is `NAN` rather than writing it as zero or a sentinel.

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

## ESP32 Project Conventions

- Platform: ESP32 with web interface (HTML/JSON APIs, WebSocket)
- Config uses a 3-tier persistence system — check existing config architecture before modifying
- Sensor code must handle independent failure per sensor (no stale values on disconnect)
- Logging intervals and sensor read intervals are separate configurable defines
- Always verify JSON config files have no trailing commas after editing
- Run `pio run` after any C/C++ changes

## Coding Conventions

When modifying sensor-related code, ensure stale/disconnected sensor values are handled explicitly (set to NaN or sentinel value, not retained). Always test the disconnect/reconnect path.

## Hardware & Sensors

When modifying sensor or hardware-related code, always handle failure/disconnection states explicitly — never retain stale values when a device goes offline.

## Code Quality

After making bug fixes or feature changes to ESP32/embedded code, review all related state variables to ensure they are properly reset or invalidated on error conditions.

### Post-Edit Validation

After any code edit, immediately run the most relevant validation — never assume an edit is correct without it:

| File type | Validation command |
|---|---|
| C++ / `.h` | `pio run` — check for compiler errors and warnings |
| JSON | `python3 -m json.tool <file>` — catches trailing commas and syntax errors |
| Python | `python3 -m py_compile <file>` |
| Functional change | `pio test -e native` — run unit tests to confirm behavior |

If validation fails, fix the issue before moving on. Do not leave a broken state and continue with other changes.

### JSON Editing Rules

When editing JSON files, always validate syntax after changes — especially check for trailing commas. Use `python3 -m json.tool <file>` to validate.

After editing any JSON file, always validate it (e.g., `python3 -c "import json; json.load(open('file.json'))"`) to catch trailing commas or syntax errors.

## Testing

After implementing features, run the full test suite and report pass/fail counts:

```bash
pio test -e native
```

For auth/access changes, verify no privilege escalation in role defaults (default role must be `""`, never `"admin"`).

## Build & Testing

After making firmware changes, remind the user to compile with `pio run` and check for warnings. IntelliSense errors in VSCode may be false positives — distinguish between IDE warnings and actual compiler errors.

## Build Commands

```bash
# Build firmware
pio run

# Upload firmware to device
pio run -t upload

# Build and upload filesystem (web UI in data/)
pio run -t uploadfs

# Open serial monitor (115200 baud)
pio device monitor

# Clean build
pio run -t clean

# Run native unit tests (no device required)
pio test -e native
```

All firmware commands default to the `upesy_wroom` environment (board: `esp32doit-devkit-v1`, ESP32-WROOM-32). Unit tests use the `native` environment.

## Architecture

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
| Stove | PT1000 via MAX31865 | Hardware SPI (VSPI) | CS=GPIO5, SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23 | 3-wire mode; RREF=4300.0Ω, RNOMINAL=1000.0Ω |
| Ceiling | DHT21 (AM2301) | 1-wire | GPIO16 | External; connects via J4 (4-pin header, Pin1=VDD, Pin2=DATA, Pin3=NC, Pin4=GND); 10kΩ pull-up DATA→VCC |
| Bench | DHT21 (AM2301) | 1-wire | GPIO17 | External; connects via J5 (4-pin header, Pin1=VDD, Pin2=DATA, Pin3=NC, Pin4=GND); 10kΩ pull-up DATA→VCC |
| Power | INA260 | I2C | SDA=GPIO4, SCL=GPIO13 | Integrated 2mΩ shunt; no external resistor; optional — gracefully disabled if absent |

SPI pins GPIO18/19/23 are reserved for MAX31865; do not use for other purposes.

Stove fault handling: any MAX31865 fault or temperature outside −200°C to 900°C sets `stove_temp = NAN` and prints the fault bits to serial.

Fallback: if `stove_temp` is NaN, `stoveReading()` returns the average of ceiling and bench temperatures instead.

## Motor Control

### Outflow (upper vent) — CheapStepper → ULN2003 → 28BYJ-48

| IN pin | GPIO |
|---|---|
| IN1 | 21 |
| IN2 | 25 |
| IN3 | 26 |
| IN4 | 14 |

### Inflow (lower vent) — CheapStepper → ULN2003 → 28BYJ-48

| IN pin | GPIO |
|---|---|
| IN1 | 22 |
| IN2 | 27 |
| IN3 | 32 |
| IN4 | 33 |

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

**Dual-tuning (both controllers use the same values):**

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
| `WS_JSON_BUF_SIZE` | `320` | WebSocket JSON output buffer (bytes) |
| `MQTT_BUF_SIZE` | `512` | MQTT client buffer size (bytes) |
| `NTP_SERVER_LOCAL` | `"192.168.1.100"` | Primary NTP server |
| `WIFI_GATEWAY_IP` | `192, 168, 1, 100` | WiFi gateway (IPAddress initializer) |
| `WIFI_DNS_IP` | `8, 8, 8, 8` | Primary DNS (IPAddress initializer) |
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

Tests run natively (no device required) using the Unity framework. Run with:

```bash
pio test -e native
```

Total: **136 tests** across 8 suites (verified via `pio test -e native`).

### `test/test_sensor/` — Sensor value formatting and JSON null handling (8 tests)

- `test_fmtVal_nan` — NaN serializes as `"null"`
- `test_fmtVal_valid` — valid float serializes as `"%.1f"`
- `test_c2f` — Celsius-to-Fahrenheit: 0→32, 100→212, known sauna value
- `test_f2c` — Fahrenheit-to-Celsius: 32→0, 212→100
- `test_c2f_f2c_roundtrip` — round-trip precision within 0.01°
- `test_ceiling_nan_gives_null_in_json` — ceiling NaN → `"clt":null` in JSON
- `test_bench_nan_independent` — bench NaN does not affect ceiling
- `test_both_sensors_nan` — all three temp fields null simultaneously

### `test/test_config/` — 3-tier configuration merge logic (9 tests)

- `test_sauna_config_defaults` — `SaunaConfig` defaults: 160°F / 120°F / PIDs off
- `test_fleet_config_overrides_defaults` — Layer 2 values win over Layer 1
- `test_out_of_range_rejected` — value above 300°F rejected; default preserved
- `test_out_of_range_low_rejected` — value below 32°F rejected; default preserved
- `test_nvs_wins_over_fleet` — Layer 3 beats Layer 2 when both set the same key
- `test_nvs_missing_key_preserves_fleet` — absent NVS key leaves Layer 2 value intact
- `test_partial_fleet_only_ceiling` — partial fleet layer doesn't clobber unset bench
- `test_pid_enable_flag` — `has_ceiling_en` gate applies correctly
- `test_nvs_can_disable_fleet_pid` — Layer 3 can override a Layer 2 PID enable to false

### `test/test_websocket/` — `buildJsonFull()` output and stale detection (12 tests)

- `test_json_contains_all_keys` — all 23 required keys present
- `test_json_braces` — JSON starts with `{` and ends with `}`
- `test_stale_ceiling_gives_null` — stale ceiling → `"clt":null` + `"cst":1`, bench unaffected
- `test_stale_bench_gives_null` — stale bench → `"d5t":null` + `"bst":1`, ceiling unaffected
- `test_fresh_readings_not_stale` — readings within threshold: `cst:0`, `bst:0`
- `test_never_read_sensor_stale` — `last_ok_ms==0` is always stale
- `test_stale_disabled_threshold_zero` — threshold=0 disables stale detection entirely
- `test_overheat_alarm_in_json` — `overheat_alarm=true` → `"oa":1`
- `test_motor_positions_in_json` — `ofs`/`ofd`/`ifs`/`ifd` values correct
- `test_staleness_exactly_at_threshold_not_stale` — diff == threshold is NOT stale (strict `>`)
- `test_staleness_one_over_threshold_is_stale` — diff > threshold IS stale
- `test_staleness_threshold_zero_never_stale` — threshold=0 never stale regardless of timestamps

### `test/test_auth/` — Auth system (35 tests)

See **Authentication System** section for the full test list.

### `test/test_ota/` — OTA logic (17 tests)

See **OTA Update System** section for the full test list.

### `test/test_sensor_module/` — `stoveReading()` fallback logic (5 tests)

Named `test_sensor_module` (not `test_sensor`) to avoid ambiguity with the existing `test_sensor/` suite. Tests the inline function from `sensors.h` natively.

- stove_temp valid → returns stove_temp
- stove_temp NaN, ceiling and bench both valid → returns their average
- stove_temp NaN, only ceiling valid (bench NaN) → returns NaN (both required)
- stove_temp NaN, only bench valid (ceiling NaN) → returns NaN (both required)
- stove_temp NaN, ceiling and bench both NaN → returns NaN

### `test/test_web_module/` — `buildJson()` struct assembly (6 tests)

Tests the inline `buildJson()` from `web.h` natively (no hardware dependencies).

- `buildJson()` produces valid JSON (starts `{`, ends `}`)
- `buildJson()` populates all 23 required keys
- Stale ceiling sensor → `clt` and `clh` are null, `cst` is 1
- Stale bench sensor → `d5t` and `d5h` are null, `bst` is 1
- NaN stove → `tct` is null
- INA260 absent (`ina260_ok = false`) → `pvolt`, `pcurr`, `pmw` are null

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

Two environments:
- `upesy_wroom` (default) — ESP32 targets; board `esp32doit-devkit-v1`; filesystem `littlefs`; partition table `partitions_ota.csv`; `monitor_speed 115200`
- `native` — native unit tests only; `std=c++14`; `test_build_src = false`

`extra_scripts = scripts/upload_fs.py` hooks the filesystem upload. `targets = upload, uploadfs` makes both firmware and filesystem upload by default.

Library dependencies (`lib_deps` in `upesy_wroom`): Adafruit Unified Sensor, DHT sensor library, WebSockets (links2004), ESP8266 Influxdb (tobiasschuerg), Adafruit MAX31865, QuickPID (dlloydev), CheapStepper (tyhenry), PubSubClient (knolleary), Adafruit INA260, ArduinoJson (bblanchon). All pinned by semver range.

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

## Unit Tests — Complete Suite

Total: **136 tests** across 8 suites (verified via `pio test -e native`). Includes `test_sensor_module` (5 tests for `stoveReading()`), `test_web_module` (6 tests for `buildJson()`), and `test_motor_logic` (8 tests for `motorClampCW()`) added during subsequent refactors.

### `test/test_auth/` — Auth system (35 tests)

- Hex encode/decode: `test_bytes_to_hex`, `test_hex_to_bytes`
- Constant-time comparison: `test_token_equal_same/different/empty_vs_nonempty`
- Token operations: `test_short_token_rejected`, `test_issue_token_populates_slot`, `test_issued_token_validates`, `test_wrong_token_rejected`, `test_expired_token_rejected`, `test_expiry_across_millis_rollover`, `test_logout_invalidates_token`, `test_expired_slot_reclaimed_before_valid`, `test_oldest_valid_evicted_when_all_full`
- Password: `test_generate_salt_length`, `test_hash_and_verify_correct_password`, `test_wrong_password_rejected`, `test_empty/below/at/above/max_len_*`
- User store: `test_add_user_and_find`, `test_max_users_enforced`, `test_delete_user`, `test_slot0_delete_rejected`, `test_slot0_password_change_permitted`, `test_delete_non_slot0_preserves_slot0_protection`, `test_password_below_min_rejected_on_add`
- Login fallback: `test_adapter_success_issues_token`, `test_adapter_rejection_no_nvs_fallthrough`, `test_adapter_error_falls_through_to_nvs_success/failure`, `test_no_adapter_configured_uses_nvs_directly`
- Logging: `test_influx_log_event_fields`

### `test/test_ota/` — OTA logic (17 tests)

- Version parsing: valid, empty, null, malformed, zeros
- Comparison: newer/older patch, equal, newer minor/major
- Manifest: valid, missing url, missing version, md5 optional, no update needed (same/older), update available
- Rollback: below/at/above threshold, zero failures
- Partial download: incomplete, complete, not started, zero expected

## Alternative Firmware

`sauna_esphome.yaml` — ESPHome configuration. Lacks native InfluxDB support and runtime motor calibration. Do not merge concerns between the two firmware approaches.

## Hardware Reference

Full GPIO table and connector details: `docs/pinout.md`

KiCad schematics: `docs/kicad/`

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

Both `AUTH_ADMIN_USER` and `AUTH_ADMIN_PASS` are enforced by `#error` guards at the top of `main.cpp` — omitting either causes a compile error. See also **Credentials & Secrets — Updated** section below for details.

## Lessons Learned

**Use `||` not `&&` when updating sensor "last seen" timestamps.** A DHT sensor returns temp and humidity as separate readings; either can be NaN independently. Using `&&` to gate `last_ok_ms` means a sensor with a failed humidity channel is falsely declared dead, triggering stale detection and closing the vents. Use `||`: if any reading from the sensor succeeds, the sensor is alive.

**When adding a field to the WebSocket JSON schema, verify all consumers use it.** The `cst`/`bst` stale flags were correctly computed and transmitted for multiple sessions before the UI was found to silently ignore them. After adding any new JSON field, check every consumer (dashboard JS, MQTT handler, InfluxDB writer) before considering the feature complete.

**Apply sensor validity checks to every data consumer, not just the display path.** Stale detection was added to `buildJsonFull()` for display, but the PID controllers only checked `!isnan()` — so a stale-but-non-NaN reading could still drive the motors. Whenever a new validity condition is introduced (NaN guard, staleness, range check), audit all consumers: display, PID, MQTT, InfluxDB, and serial log.

## ESP32 / Embedded Project Notes

- Config persistence uses a 3-tier system (defaults → NVS → runtime)
- Sensor logging interval and sensor read interval are independent and configurable via `#define`
- When sensors disconnect, stale values must be cleared (don't retain last-known values)
