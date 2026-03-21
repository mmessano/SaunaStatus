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
- Shared `globals.h` that declares all `extern` globals for use by modules
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
  main.cpp          # Global definitions, PID compute, motor drive, setup(), loop()
  globals.h         # extern declarations for all globals; included by modules that need them
  sensors.h/.cpp    # Sensor reads, stale detection, checkOverheat(), stoveReading()
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

#### `globals.h`
Declares (does not define) all globals that need to be shared across modules. Included by each module `.cpp` that references a global defined in `main.cpp`.

This is the single source of truth for `extern` declarations. No module defines its own `extern` blocks inline.

The following shows the structure of `globals.h` with verified variable names from the current source. The implementer must verify the complete list by reading `main.cpp` before writing `globals.h`.

```cpp
#pragma once
#include <math.h>
// Sensor readings (all initialized to NAN)
extern float ceiling_temp, ceiling_hum, bench_temp, bench_hum, stove_temp;
extern float pwr_bus_V, pwr_current_mA, pwr_mW;
extern unsigned long ceiling_last_ok_ms, bench_last_ok_ms;
extern bool ina260_ok;
// Motor state
extern int outflow_target, outflow_max_steps, outflow_dir;
extern int inflow_target, inflow_max_steps, inflow_dir;
// PID setpoints (°C, verified names from main.cpp line 275/280)
extern float Ceilingpoint, Benchpoint;
extern float ceiling_output, bench_output;
extern bool ceiling_pid_en, bench_pid_en, c_cons_mode, b_cons_mode;
// Safety
extern bool overheat_alarm;
// Runtime config
extern unsigned long g_sensor_read_interval_ms, g_serial_log_interval_ms;
extern String g_device_name, g_static_ip_str;
// ... (complete list populated during implementation from main.cpp globals)
```

Hardware object `extern` declarations (CheapStepper `outflow`/`inflow`, MAX31865, DHT, INA260) are wrapped in `#ifdef ARDUINO` guards in `globals.h` so that native test builds compile cleanly without the Arduino SDK:

```cpp
#ifdef ARDUINO
#include <CheapStepper.h>
extern CheapStepper outflow;
extern CheapStepper inflow;
// ... other hardware objects
#endif
```

This ensures `test_sensor_module` and `test_web_module` can include `globals.h` without pulling in Arduino-only headers.

#### `sensors.h/.cpp`
Owns all sensor I/O and health tracking.

**Extracted from:** `loop()` sensor read block (~lines 1677–1730 of main.cpp)

**Functions:**
- `readSensors()` — reads DHT21 ceiling, DHT21 bench, MAX31865 stove, INA260 power; updates globals; applies `||` rule for `last_ok_ms`
- `checkOverheat()` — moved verbatim from main.cpp; preserves the rising-edge state-machine behavior (motors driven only on alarm onset transition via `if (hot && !overheat_alarm)`, not on every loop tick); returns `bool` (alarm active). Motor drive is **inside** `checkOverheat()`, not in the caller. Has CheapStepper dependency; **not natively testable** — verified by on-device integration testing only.
- `stoveReading()` — moved verbatim from main.cpp; returns `stove_temp` if valid, `(ceiling_temp + bench_temp) / 2.0f` if both air sensors are valid, otherwise `NAN`; pure function, no side effects

**Why `checkOverheat()` is moved verbatim (not refactored):**
The current implementation uses a rising-edge guard (`if (hot && !overheat_alarm)`) to drive motors only on the alarm-onset transition. Any restructuring that drives motors outside this guard would spam `newMove()` every 2 seconds while alarmed and corrupt CheapStepper's step tracking. Testability is achieved for `stoveReading()` and the sensor-value logic; `checkOverheat()` retains its hardware dependency.

**`loop()` in `main.cpp` uses `checkOverheat()`'s return value only to suppress PID computation** — it does NOT drive motors based on the return value. Motor drive remains exclusively inside `checkOverheat()`.

**Critical invariants preserved:**
- `ceiling_last_ok_ms` / `bench_last_ok_ms` updated with `||` (either temp OR humidity succeeding counts as alive)
- Sensor floats cleared to `NAN` on any read failure — never retain stale values
- INA260 reads guarded by `ina260_ok` flag

#### `web.h/.cpp`
Owns all HTTP and WebSocket presentation logic.

**Extracted from:** All `handle*` functions and `webSocketEvent()` from main.cpp

**Functions:**
- `webSocketEvent(num, type, payload, len)` — WebSocket connection handler
- `buildJson(buf, len)` — assembles SensorValues/MotorState/PIDState from globals; calls `buildJsonFull()` from sauna_logic.h
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
- `logAccessEvent(const char *event, const char *username, const char *auth_source, const char *client_ip)` — writes login/logout/failure events to `sauna_webaccess`. The `client_ip` parameter is added vs. the current signature; callers pass `server.client().remoteIP().toString().c_str()`. This removes the `server` dependency from inside the function and keeps `influx.h` free of WebServer dependency. Existing call sites in `auth.h` handlers update their call to pass the IP.

**Include dependency:** `auth.h` includes `influx.h` (one-way). `influx.h` must NOT include `auth.h`. This is safe: `logAccessEvent()` only needs `AuthLogEvent` from `auth_logic.h`, and `influx.h` includes `auth_logic.h` directly, not `auth.h`.

**Rationale for moving `logAccessEvent()`:** It creates InfluxDB point objects and calls the InfluxDB client. Auth logic should not depend on the InfluxDB client. `influx.h` is the correct owner.

#### `main.cpp` (remaining)
Thin orchestrator. Retains:
- All global variable **definitions** (sensors, motors, PID, config, auth sessions, runtime state)
- PID compute block in `loop()` — tightly coupled to motor drive state
- Motor drive logic in `loop()`
- Calls `checkOverheat()` from `sensors.h`; uses return value only to suppress PID computation (motor drive is inside `checkOverheat()` itself)
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

### HTTP error path deduplication
- Removed from scope. Deduplicating `server.send()` error paths risks subtle response body differences (status code, content type, field names). Since the goal is no behavior changes, error paths are moved verbatim without refactoring.

### `logAccessEvent()` relocation
- Moved from `auth.h` to `influx.h/.cpp`
- `auth.h` includes `influx.h` to call `logAccessEvent()` at existing call sites — no source changes required at call sites in auth handlers
- Eliminates the InfluxDB client dependency from the auth layer

---

## Test-Driven Development Process

**For each new module, in order:**

1. **Write tests first** — before any `.cpp` implementation code
2. **Run tests** — confirm they fail (red)
3. **Implement** — write `.cpp` to make tests pass
4. **Iterate** — compile and run until all tests are green
5. **Report** — summarize which edge cases the tests caught

**Baseline:** `pio test -e native` was run on 2026-03-21 and passed **113 tests** across 5 suites (`test_websocket`, `test_auth`, `test_ota`, `test_sensor`, `test_config`). All 113 must continue to pass after this refactor.

**Note:** CLAUDE.md documents 81 tests; that count is stale. 32 additional tests were added to `test_config/` and `test_auth/` since CLAUDE.md was last updated. The correct authoritative baseline is 113, verified by running the suite. CLAUDE.md will be updated to reflect 113 in a separate task.

**New test suites:**

#### `test/test_sensor_module/`
Named `test_sensor_module` (not `test_sensors`) to avoid ambiguity with the existing `test/test_sensor/` suite (which tests `sauna_logic.h`). Both suites coexist and run together under `pio test -e native`.

Tests for `checkOverheat()` and `stoveReading()` — the pure functions extracted to `sensors.cpp`. These have no Arduino/CheapStepper dependencies and are natively testable.

`readSensors()` has hardware dependencies (DHT, MAX31865, INA260) and is **not** natively testable. Its behavior is verified by integration testing on-device, not by this suite.

Planned native test cases:

**`checkOverheat()` — not natively testable:**

`checkOverheat()` has CheapStepper dependency and is excluded from this native test suite. On-device integration testing only.

**`stoveReading()` — pure fallback logic (verified against main.cpp lines 367–374):**

Exact behavior: returns `stove_temp` if valid; returns `(ceiling_temp + bench_temp) / 2.0f` if both air sensors are non-NaN; otherwise returns `NAN`. There is no single-sensor fallback — both air sensors must be valid for the fallback to activate.

Test cases:
- stove_temp valid → returns stove_temp
- stove_temp NaN, ceiling and bench both valid → returns their average
- stove_temp NaN, only ceiling valid (bench NaN) → returns NaN (both required)
- stove_temp NaN, only bench valid (ceiling NaN) → returns NaN (both required)
- stove_temp NaN, ceiling and bench both NaN → returns NaN

**Test fixture approach for native sensor module tests:**

The test binary in `test/test_sensor_module/` provides a `test_globals.cpp` that defines stub values for all globals referenced by `checkOverheat()` and `stoveReading()` (e.g., `float ceiling_temp = NAN;`, `bool overheat_alarm = false;`, etc.). These are simple variable definitions, no hardware objects needed.

#### `test/test_web_module/`
Tests for `buildJson()` — the wrapper that constructs SensorValues/MotorState/PIDState structs from globals and calls `buildJsonFull()`.

`buildJson()` has no hardware dependencies; it reads globals and calls pure functions from `sauna_logic.h`. Natively testable.

**Test fixture approach:** `test/test_web_module/test_globals.cpp` defines all globals referenced by `buildJson()` with safe initial values. Identical pattern to `test_sensor_module`.

Planned test cases:
- `buildJson()` produces valid JSON (starts `{`, ends `}`)
- `buildJson()` populates all 23 required keys
- Stale ceiling sensor → `clt` and `clh` are null, `cst` is 1
- Stale bench sensor → `d5t` and `d5h` are null, `bst` is 1
- NaN stove → `tct` is null
- INA260 absent (`ina260_ok = false`) → `pvolt`, `pcurr`, `pmw` are null

**Note:** These tests overlap intentionally with `test/test_websocket/`, which tests `buildJsonFull()` directly. The new tests cover the struct-assembly layer (`buildJson()`), not the JSON serialization layer.

**Existing tests remain unchanged.** The 113 tests across `test_sensor/`, `test_config/`, `test_websocket/`, `test_auth/`, `test_ota/` must all continue to pass.

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
# Must show: 113+ tests passed (113 baseline + new sensor_module + web_module tests)
pio test -e native

# Validate JSON config files
python3 -m json.tool data/config.json

# Check no hardcoded user paths in settings files
grep -n '/home/' .claude/settings.local.json .claude/settings.json 2>/dev/null \
  && echo "ERROR: hardcoded user paths" || echo "OK"
```

All 113 existing tests must pass. New `test_sensor_module` and `test_web_module` tests must pass. Zero new compiler warnings from `pio run`.

---

## Non-Goals / Constraints

- **No global variable renames** — `ceiling_temp`, `bench_temp`, `stove_temp`, etc. keep their names; `globals.h` provides their `extern` declarations
- **No new heap allocations** — ESP32 SRAM is limited; no `new`/`malloc` in hot paths
- **No class hierarchies** — structs and free functions only; consistent with existing codebase style
- **No API surface changes** — routes, JSON schema, MQTT topics, NVS keys all unchanged
- **No error path refactoring** — `server.send()` error paths moved verbatim; no deduplication
- **No ESPHome changes** — `sauna_esphome.yaml` is out of scope
