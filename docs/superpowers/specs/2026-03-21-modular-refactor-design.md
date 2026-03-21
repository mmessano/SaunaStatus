# SaunaStatus — Modular Refactor & Frontend Redesign

**Date:** 2026-03-21
**Status:** Approved
**Type:** Refactor (no behavior changes to firmware logic) + Frontend enhancement

---

## Goal

Break `src/main.cpp` (1931 lines) into focused modules with clear ownership boundaries, apply targeted C++ improvements during extraction, and update `data/index.html` to show the InfluxDB trend chart and inline login on the landing page.

No firmware behavior changes. All existing HTTP endpoints, WebSocket JSON schema, MQTT topics, NVS keys, and unit tests remain valid.

---

## Scope

### In scope
- Extract sensor reading, WebSocket/HTTP, MQTT, and InfluxDB logic into separate `.cpp/.h` files
- Targeted C++ quality improvements (const-correctness, references, header hygiene)
- Inline login form on `index.html`; trend chart visible pre-auth; action sections gated post-login
- New unit tests for extracted modules (TDD: tests written before implementation)

### Out of scope
- Behavior changes to PID, motor control, safety systems, or config persistence
- Changes to the WebSocket JSON schema (23 fields, field names, null rules)
- Changes to HTTP API surface (routes, methods, auth requirements, response shapes)
- Changes to NVS key names or the 3-tier config system
- ESPHome firmware (`sauna_esphome.yaml`)

---

## Architecture

### New file structure

```
src/
  main.cpp          # Globals, PID compute, motor drive, setup(), loop()
  sensors.h/.cpp    # Sensor reads, stale detection, overheat check
  web.h/.cpp        # All HTTP handlers + WebSocket event handler + buildJson()
  mqtt.h/.cpp       # mqttConnect(), mqttCallback(), mqttPublishState(), mqttPublishDiscovery()
  influx.h/.cpp     # writeInflux(), logAccessEvent() (moved from auth.h)
  sauna_logic.h     # Unchanged — portable structs & pure logic
  auth_logic.h      # Unchanged — portable auth logic
  auth.h            # Unchanged — Arduino auth wrappers (requireAdmin stays here)
  ota_logic.h       # Unchanged — portable OTA logic
  secrets.h         # Unchanged — credentials
```

### Module responsibilities

#### `sensors.h/.cpp`
Owns all sensor I/O and health tracking.

**Extracted from:** `loop()` sensor read block (~lines 1677–1780 of main.cpp)

**Functions:**
- `readSensors()` — reads DHT21 ceiling, DHT21 bench, MAX31865 stove, INA260 power; updates globals; applies `||` rule for `last_ok_ms`
- `checkOverheat()` — moved from main.cpp; evaluates ceiling/bench temps against `TEMP_LIMIT_C`; sets `overheat_alarm`; drives vents fully open
- `stoveReading()` — moved from main.cpp; returns stove_temp or ceiling/bench average fallback if stove is NaN

**Critical invariants preserved:**
- `ceiling_last_ok_ms` / `bench_last_ok_ms` updated with `||` (either temp OR humidity succeeding counts as alive)
- Sensor floats cleared to `NAN` on any read failure — never retain stale values
- INA260 reads guarded by `ina260_ok` flag

#### `web.h/.cpp`
Owns all HTTP and WebSocket presentation logic.

**Extracted from:** All `handle*` functions and `webSocketEvent()` from main.cpp

**Functions:**
- `webSocketEvent(num, type, payload, len)` — WebSocket connection handler
- `buildJson(buf, len)` — assembles SensorValues/MotorState/PIDState; calls `buildJsonFull()` from sauna_logic.h
- `handleRoot()`, `handleLog()`, `handleDeleteStatus()`, `handleDeleteControl()`
- `handleHistory()`, `handleMotorCmd()`, `handlePidToggle()`, `handleSetpoint()`
- `handleOtaStatus()`, `handleOtaUpdate()`
- `handleConfigPage()`, `handleConfigGet()`, `handleConfigSave()`
- `handleAuthLoginPage()`, `handleAuthLogin()`, `handleAuthLogout()`, `handleAuthStatus()`
- `handleUsersGet()`, `handleUsersCreate()`, `handleUsersDelete()`, `handleUsersChangePassword()`

**Note:** `requireAdmin()` stays in `auth.h` — it is auth logic, not web logic.

#### `mqtt.h/.cpp`
Owns the MQTT connection lifecycle and publish/subscribe logic.

**Extracted from:** `mqttConnect()`, `mqttCallback()`, `mqttPublishState()`, `mqttPublishDiscovery()`

**Functions:**
- `mqttConnect()` — connects to broker; subscribes to topics; publishes discovery configs
- `mqttCallback(topic, payload, len)` — handles incoming messages (PID enable/disable, setpoint changes)
- `mqttPublishState()` — publishes full JSON to `sauna/state`
- `mqttPublishDiscovery()` — publishes retained HA MQTT Discovery configs

#### `influx.h/.cpp`
Owns all InfluxDB write operations.

**Extracted from:** `writeInflux()` in main.cpp; `logAccessEvent()` from auth.h

**Functions:**
- `writeInflux()` — writes `sauna_status` (sensor fields, NaN omitted) and `sauna_control` (motor/PID state) every 60 seconds
- `logAccessEvent(event, username, auth_source)` — writes login/logout/failure events to `sauna_webaccess`

**Rationale for moving `logAccessEvent()`:** It creates InfluxDB point objects and calls the InfluxDB client. Auth logic should not depend on the InfluxDB client. `influx.h` is the correct owner.

#### `main.cpp` (remaining)
Thin orchestrator. Retains:
- All global variable declarations (sensors, motors, PID, config, auth sessions, runtime state)
- PID compute block in `loop()` — tightly coupled to motor drive state; not worth separating
- Motor drive logic in `loop()`
- `setup()` — peripheral init, config layer loading, route registration, auth seeding
- `loop()` — calls into modules; maintains timing cycles

---

## C++ Improvements

Applied during extraction. No behavioral changes.

### `const` correctness
- Read-only struct parameters use `const T &` (e.g., `buildJson(const SensorValues &sv, ...)`)
- String literal parameters use `const char *`
- Setters and output-writing parameters use `T &`

### References over raw pointers
- Internal helpers that pass structs use references, not pointers, where ownership is clear
- WebServer handler functions remain parameter-free (library constraint)

### Header hygiene
- `#pragma once` on all new headers (consistent with existing `sauna_logic.h` pattern)
- Each `.cpp` includes only the headers it directly needs
- No "include everything" monolith at the top of new files

### Dead code removal
- Commented-out blocks superseded by active code are removed during extraction
- Duplicate `server.send()` error paths from copy-pasted handlers are de-duplicated where safe

### `logAccessEvent()` relocation
- Moved from `auth.h` to `influx.h/.cpp`
- `auth.h` gets a forward declaration or thin wrapper to maintain call sites in auth handlers
- Eliminates the InfluxDB client dependency from the auth layer

---

## Test-Driven Development Process

**For each new module, in order:**

1. **Write tests first** — before any `.cpp` implementation code
2. **Run tests** — confirm they fail (red)
3. **Implement** — write `.cpp` to make tests pass
4. **Iterate** — compile and run until all tests are green
5. **Report** — summarize which edge cases the tests caught

**New test suites:**

#### `test/test_sensors/`
Tests for the logic extracted into `sensors.cpp` that can be exercised natively.

Planned cases:
- Normal read: all sensor values valid → globals updated correctly
- DHT temp NaN, humidity valid → `last_ok_ms` updated (|| rule), temp = NaN
- DHT humidity NaN, temp valid → `last_ok_ms` updated (|| rule), humidity = NaN
- Both DHT channels NaN → `last_ok_ms` NOT updated, both values = NaN
- MAX31865 fault → `stove_temp = NAN`
- MAX31865 value out of range (< -200°C or > 900°C) → `stove_temp = NAN`
- Overheat: ceiling_temp >= TEMP_LIMIT_C → alarm set
- Overheat: bench_temp >= TEMP_LIMIT_C → alarm set
- Overheat: both temps NaN → alarm NOT set (NaN ignored)
- Overheat clears when both temps drop below threshold
- `stoveReading()` returns stove_temp when valid
- `stoveReading()` returns ceiling/bench average when stove is NaN
- `stoveReading()` returns NaN when stove AND ceiling AND bench are all NaN

#### `test/test_web/`
Tests for JSON assembly and WebSocket output (the pure-logic portions of web.cpp).

Most WebSocket JSON logic is already covered by `test/test_websocket/` (tests `buildJsonFull()` in `sauna_logic.h`). New tests focus on the `buildJson()` wrapper that constructs the structs from globals.

Planned cases:
- `buildJson()` produces valid JSON (starts `{`, ends `}`)
- `buildJson()` populates all 23 required keys
- Stale ceiling sensor → `clt` and `clh` are null, `cst` is 1
- Stale bench sensor → `d5t` and `d5h` are null, `bst` is 1
- NaN stove → `tct` is null
- INA260 absent (`ina260_ok = false`) → `pvolt`, `pcurr`, `pmw` are null

**Existing tests remain unchanged.** The 81 tests across `test_sensor/`, `test_config/`, `test_websocket/`, `test_auth/`, `test_ota/` continue to pass.

---

## Frontend Changes (`data/index.html`)

### Inline login form
- A login panel (username + password + submit button) is embedded directly in `index.html`
- On page load, if no valid token exists in `localStorage`, the login panel is shown and action sections are hidden
- On successful `POST /auth/login`, token is stored, login panel hides, action sections reveal
- On logout (`POST /auth/logout`), token cleared, login panel re-appears — no page navigation

### Trend chart pre-auth
- `/history` is already unauthenticated — no server changes needed
- Chart initializes and loads on every page load, before authentication
- Chart continues periodic refresh regardless of auth state

### Auth-gated sections
- Sensor readings (WebSocket data) — visible pre-auth (read-only)
- Action sections hidden until authenticated:
  - Motor controls
  - PID controls and setpoints
  - Logging / reset buttons
  - User management
- Implemented via a CSS class toggle (e.g., `body.authenticated`) that `display: none` hides action cards

### `login.html` unchanged
- Still served at `GET /auth/login`; no server-side changes needed

---

## Validation

After implementation:

```bash
# Compile firmware
pio run

# Run all unit tests (native, no device required)
pio test -e native

# Validate JSON config files
python3 -m json.tool data/config.json

# Check no hardcoded user paths in settings files
grep -n '/home/' .claude/settings.local.json .claude/settings.json 2>/dev/null \
  && echo "ERROR: hardcoded user paths" || echo "OK"
```

All 81 existing tests must pass. New sensor and web tests must pass. Zero compiler warnings from `pio run` (beyond the existing `-Wno-parentheses` suppression).

---

## Non-Goals / Constraints

- **No global variable renames** — `ceiling_temp`, `bench_temp`, `stove_temp`, etc. keep their names; `extern` declarations in module headers reference them
- **No new heap allocations** — ESP32 SRAM is limited; no `new`/`malloc` in hot paths
- **No class hierarchies** — structs and free functions only; consistent with existing codebase style
- **No API surface changes** — routes, JSON schema, MQTT topics, NVS keys all unchanged
- **No ESPHome changes** — `sauna_esphome.yaml` is out of scope
