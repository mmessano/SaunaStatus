# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based sauna automation system. Monitors temperature/humidity via PT1000 (stove) and dual DHT21 (ceiling/bench) sensors, monitors power via INA260, and controls two stepper-driven damper vents using dual PID controllers. Integrates with InfluxDB, MQTT (Home Assistant MQTT Discovery), and provides a local WebSocket/HTTP interface.

This is an ESP32 embedded project (sauna controller) using Arduino/PlatformIO. Key technologies: C++, ESP32, WebSocket, DHT sensors, KiCad for PCB design. Always consider memory constraints and real-time requirements when suggesting code changes.

## Coding Conventions

When modifying sensor-related code, ensure stale/disconnected sensor values are handled explicitly (set to NaN or sentinel value, not retained). Always test the disconnect/reconnect path.

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

All firmware commands default to the `upesy_wroom` environment (board: `denky32`, ESP32-WROOM-32). Unit tests use the `native` environment.

## Architecture

### Firmware: `src/main.cpp`

1204 lines. Includes `sauna_logic.h` for all portable pure-logic functions. Key sections:

- Pin mapping comment block (lines 20–46)
- Sensor, motor, and PID global declarations
- `loadLittleFSConfig()` — Layer 2 config from `/config.json`
- `savePrefs()` — writes all runtime state to NVS
- `checkOverheat()` — safety system; see Safety Systems section
- `buildJson()` — assembles structs and calls `buildJsonFull()` from sauna_logic.h
- `writeInflux()` — writes `sauna_status` and `sauna_control` to InfluxDB
- HTTP handlers: `handleRoot`, `handleLog`, `handleDeleteStatus`, `handleDeleteControl`, `handleHistory`, `handleMotorCmd`, `handlePidToggle`, `handleSetpoint`
- MQTT: `mqttCallback`, `mqttPublishState`, `mqttPublishDiscovery`, `mqttConnect`
- `setup()` — initializes all peripherals, WiFi, InfluxDB, HTTP, WebSocket, MQTT; loads config layers in order
- `loop()` — 2-second sensor/PID/WebSocket/MQTT cycle; 60-second InfluxDB write cycle

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

## Sensor Layer

| Sensor | Type | Interface | GPIO | Notes |
|---|---|---|---|---|
| Stove | PT1000 via MAX31865 | Hardware SPI (VSPI) | CS=GPIO5, SCK=GPIO18, MISO=GPIO19, MOSI=GPIO23 | 3-wire mode; RREF=4300.0Ω, RNOMINAL=1000.0Ω |
| Ceiling | DHT21 (AM2301) | 1-wire | GPIO16 | 10kΩ pull-up DATA→VCC |
| Bench | DHT21 (AM2301) | 1-wire | GPIO17 | 10kΩ pull-up DATA→VCC |
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
- Both motors run at 12 RPM (`outflow.setRpm(12)` / `inflow.setRpm(12)`)
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
| Aggressive | 4.0 | 0.2 | 1.0 | error > 10°C from setpoint |
| Conservative | 1.0 | 0.05 | 0.25 | error ≤ 10°C from setpoint |

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

- WiFi static IP: `192.168.1.200`, gateway: `192.168.1.1`, subnet: `255.255.255.0`, DNS: `8.8.8.8`
- Credentials from `src/secrets.h`

### HTTP Server (port 80)

| Route | Handler | Description |
|---|---|---|
| `GET /` | `handleRoot` | Serves `index.html` from LittleFS |
| `GET /log` | `handleLog` | Triggers immediate InfluxDB write |
| `GET /delete/status` | `handleDeleteStatus` | Deletes all `sauna_status` data from InfluxDB |
| `GET /delete/control` | `handleDeleteControl` | Deletes all `sauna_control` data from InfluxDB |
| `GET /history?range=Xh` | `handleHistory` | Proxies Flux query; returns CSV of ceiling/bench/stove temps (default 1h; alphanumeric range only) |
| `GET /setpoint?ceiling=F&bench=F` | `handleSetpoint` | Sets PID setpoints in °F (32–300); persists to NVS |
| `GET /pid?ceiling=0\|1&bench=0\|1` | `handlePidToggle` | Enables/disables PID controllers; persists to NVS |
| `GET /motor?motor=outflow\|inflow&cmd=CMD&steps=N` | `handleMotorCmd` | Motor control |

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
- Keys: `DEFAULT_CEILING_SP_F` (160.0), `DEFAULT_BENCH_SP_F` (120.0), `TEMP_LIMIT_C` (120.0), `SERIAL_LOG_INTERVAL_MS` (10000), `STALE_THRESHOLD_MS` (10000)

**Layer 2: Fleet defaults — `/config.json` in LittleFS** (loaded by `loadLittleFSConfig()`)
- JSON file uploaded with filesystem image (`pio run -t uploadfs`)
- Overrides build-flag defaults; applies the same values to every device flashed with the same image
- Supported keys: `ceiling_setpoint_f` (float, °F), `bench_setpoint_f` (float, °F), `ceiling_pid_enabled` (bool), `bench_pid_enabled` (bool)
- Motor calibration (`omx`/`imx`) is device-specific and intentionally not read from this file
- Missing file is silently ignored; JSON parse errors are logged and the layer is skipped

**Layer 3: Per-device NVS** (namespace `sauna`, loaded via `Preferences`)
- Written by HTTP `/setpoint`, `/pid`, `/motor?cmd=setopen` endpoints and MQTT subscription callbacks
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

## NVS Persistence

Namespace: `sauna`. Written by `savePrefs()`. Read during `setup()` after LittleFS config.

| Key | Type | Stores |
|---|---|---|
| `csp` | float | Ceiling setpoint (°C) |
| `bsp` | float | Bench setpoint (°C) |
| `cen` | bool | Ceiling PID enabled |
| `ben` | bool | Bench PID enabled |
| `omx` | int | Outflow motor calibrated full-open step count |
| `imx` | int | Inflow motor calibrated full-open step count |

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

## Web UI

`data/index.html` — single-file HTML/JS dashboard served from LittleFS at `GET /`. Must be uploaded separately from the firmware:

```bash
pio run -t uploadfs
```

The UI connects to the WebSocket server at `ws://<device-ip>:81` and renders live readings. It uses Chart.js (CDN) for the temperature trend chart. The `/history` endpoint feeds the chart with 5-minute aggregated temperature data. Motor control buttons and PID enable toggles are included.

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
```
