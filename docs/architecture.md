# Architecture

ESP32-S3 sauna automation firmware. Dual DHT21 (ceiling/bench) + MAX31865 PT1000 (stove) + INA260 power monitor; two 28BYJ-48 stepper-driven damper vents; dual QuickPID controllers. InfluxDB, MQTT (Home Assistant Discovery), WebSocket/HTTP interface.

## Source File Tree

```
src/
├── main.cpp        ~886 lines  Thin orchestrator: global definitions, setup(), loop(), loadLittleFSConfig(), savePrefs(), OTA helpers
├── globals.h        101 lines  Extern declarations for all globals defined in main.cpp; Arduino types in #ifdef ARDUINO guards
├── gpio_config.h     66 lines  GPIO pin assignments for LB-ESP32S3-N16R8; no Arduino deps — natively testable
├── sauna_logic.h    134 lines  Portable pure-C++: c2f/f2c/fmtVal, isSensorStale, SaunaConfig+merge, buildJsonFull (23-field JSON), OverheatGuard+tickOverheat, buildConfigJson
├── auth_logic.h     616 lines  Portable pure-C++: 64-char token sessions, PBKDF2 passwords (10000 iterations), rate limiting, user store, authAttemptLogin()
├── ota_logic.h      188 lines  Portable pure-C++: version parsing/compare, OtaManifest, boot-health, partial-download detection, formatVersion, isDowngrade, isSameVersion
├── motor_logic.h     16 lines  Portable pure-C++: motorClampCW/CCW, motorPosToPercent, motorPercentToSteps
├── sensors.h         32 lines  stoveReading() inline (natively testable); readSensors()/checkOverheat() declared (Arduino-only)
├── sensors.cpp      103 lines  readSensors() — DHT21×2, MAX31865, INA260; checkOverheat() rising-edge state machine
├── web.h             82 lines  buildJson() inline (natively testable); all handle*() HTTP and webSocketEvent() declared
├── web_internal.h     9 lines  Shared Arduino-only internal helpers for split web translation units
├── web.cpp          791 lines  Core HTTP handlers, config/OTA routes, WebSocket event handler, buildJsonFull() calls
├── web_auth.cpp     182 lines  Auth/login/logout/status and user-management HTTP handlers
├── mqtt.h            20 lines  mqttConnect/Callback/PublishState/PublishDiscovery declared (Arduino-only)
├── mqtt.cpp         195 lines  MQTT lifecycle, HA Discovery, sauna/state publish, control topic subscriptions
├── influx.h          21 lines  writeInflux() and logAccessEvent() declared (Arduino-only)
├── influx.cpp        59 lines  InfluxDB write (60s interval, NaN fields omitted), access event logging
├── auth.h           206 lines  requireAdmin() Bearer check; auth HTTP handlers; NVS user-store I/O; security headers
└── secrets.h         26 lines  WiFi/InfluxDB/MQTT credentials + AUTH_ADMIN_USER/PASS — NOT committed to git

include/
└── README            PlatformIO placeholder (all project headers live in src/)
```

## Key Module Details

**`src/main.cpp`** — Thin orchestrator. Global variable definitions, `loadLittleFSConfig()`, `savePrefs()`, OTA helpers, `setup()`, `loop()`.

**`src/sauna_logic.h`** — Header-only pure-C++ (no Arduino deps). `c2f`/`f2c`/`fmtVal`, `isSensorStale`, `SaunaConfig`+`ConfigLayer`+`mergeConfigLayer`, `buildJsonFull()` (23-field WebSocket JSON), `OverheatGuard`+`tickOverheat()`, `buildConfigJson()`.

**`src/sensors.h/.cpp`** — `stoveReading()` inline in header (natively testable); returns `stove_temp` or ceiling/bench average fallback. `readSensors()` reads all sensors, applies `||` rule for `last_ok_ms`. `checkOverheat()` rising-edge state machine; motor drive is inside this function. Note: `checkOverheat()` has no hysteresis — `tickOverheat()` in `sauna_logic.h` uses `OVERHEAT_CLEAR_HYSTERESIS_C = 10.0f`.

**`src/web.h`** — `buildJson()` inline in header (natively testable). Public declarations for all `handle*()` HTTP handlers and `webSocketEvent()`.

**`src/web.cpp`** — Core HTTP handlers, config/OTA routes, WebSocket auth/broadcast flow, and filesystem page serving.

**`src/web_auth.cpp`** — Auth/login/logout/status routes plus admin-only user CRUD/password-change handlers.

**`src/auth_logic.h`** — Header-only pure-C++. Token sessions (64 chars, 1-hour TTL, 10 concurrent), PBKDF2 passwords (10000 iterations) with constant-time compare, rate limiting (5 failures per 60 s window → 5-min lockout, 8 tracked slots), adapter-first login fallback.

**`src/ota_logic.h`** — Header-only pure-C++. Version parsing/compare, manifest handling, boot-health rollback, partial-download detection, `formatVersion`, `isDowngrade`, `isSameVersion`.

**`src/motor_logic.h`** — Header-only pure-C++. `motorClampCW()` / `motorClampCCW()`, `motorPosToPercent()`, `motorPercentToSteps()`.

## PID Controllers

| Controller | Input | Output | Motor |
|---|---|---|---|
| `CeilingPID` (QuickPID) | `ceiling_temp` (°C) | `ceiling_output` (0–255) | Outflow |
| `BenchPID` (QuickPID) | `bench_temp` (°C) | `bench_output` (0–255) | Inflow |

Output 0–255 is linearly mapped to 0–`max_steps` for the corresponding motor. Dual-tuning: aggressive (Kp=4.0, Ki=0.2, Kd=1.0) when error > 10°C, conservative (1.0/0.05/0.25) otherwise. Both PIDs default to disabled at boot. When disabled, output is forced to 0 and the vent is driven closed.

## Safety Systems

### Overheat Protection

Threshold: `TEMP_LIMIT_C = 120.0°C` (248°F). Trigger: `ceiling_temp >= TEMP_LIMIT_C` OR `bench_temp >= TEMP_LIMIT_C` (NaN ignored). When triggered: both vents driven fully open, PID suppressed. Clears when both temps drop below threshold.

The portable `tickOverheat()` in `sauna_logic.h` adds a `OVERHEAT_CLEAR_HYSTERESIS_C = 10.0f` band — it clears at `threshold - 10°C`. `checkOverheat()` in `sensors.cpp` does not have this hysteresis. Document the difference if migrating.

### Stale Sensor Detection

Threshold: `STALE_THRESHOLD_MS = 10000UL`. Stale if `last_ok_ms == 0` (never read) or `(millis() - last_ok_ms) > threshold`. Stale readings → JSON `null` via `buildJsonFull()`. Stale detection does not affect PID — that uses NaN checking of raw values.

## Motor Control

→ See `docs/pinout.md` for GPIO assignments.

- `VENT_STEPS = 1024` — default full-open step count (90° quarter-turn on 28BYJ-48)
- Calibrated max steps stored in NVS as `omx` (outflow) / `imx` (inflow)
- Positions reported as 0–100% via `motorPosToPercent()`
- Minimum PID move threshold: 5 steps (suppresses jitter)
- Calibration: `zero` marks closed (step 0); `setopen` marks fully open and persists to NVS

## Web UI

`data/index.html` — single-file HTML/JS dashboard served from LittleFS. Upload with `pio run -t uploadfs`. Connects to `ws://<device-ip>:81` for live readings. Uses Chart.js (CDN) for temperature trends.

## LittleFS Layout

| File | Served at | Description |
|---|---|---|
| `index.html` | `GET /` | Main dashboard |
| `login.html` | `GET /auth/login` | Login page |
| `config.html` | `GET /config` | Configuration portal page |
| `config.json` | Layer 2 config | Fleet defaults (read by `loadLittleFSConfig()`) |

`Cache-Control: no-store` on all HTML pages. `LittleFS.begin(false)` preserves the filesystem on mount failure; if the mount fails, the device runs in degraded mode with LittleFS-backed pages and fleet `/config.json` disabled until the filesystem is repaired and re-uploaded.

Fleet `/config.json` boot semantics:

- missing file: Tier 2 is skipped and built-in defaults remain active
- malformed JSON: Tier 2 is skipped and built-in defaults remain active
- valid file with one bad field: only that field is ignored; other valid fields still apply
- later NVS loads still override any valid fleet defaults

## platformio.ini Structure

Three environments:
- `lb_esp32s3` (default) — LB-ESP32S3-N16R8; board `lolin_s3`; 16 MB OPI flash/8 MB OPI PSRAM; partition table `partitions_ota_16mb.csv`
- `upesy_wroom` (legacy reference only) — **Do not flash** — GPIO assignments target ESP32-S3 and will not match
- `native` — native unit tests only; `std=c++14 -Wno-narrowing`; `test_build_src = false`

`extra_scripts = scripts/upload_fs.py` hooks the filesystem upload. `targets = upload, uploadfs` makes bare `pio run` upload both.

**The LittleFS partition must be named `spiffs`.** `LittleFS.begin()` searches for `spiffs` by default — not `littlefs`. This has burned us twice.

## OTA Update System

→ See `docs/api-reference.md` for `/ota/status` and `/ota/update` route details.

Flow: fetch manifest → parse version → compare to `FIRMWARE_VERSION` → stream binary → reboot. Refuses downgrades and same-version re-flashes. `OTA_ALLOWED_HOSTS` is a comma-separated hostname allowlist; the default `""` disables OTA until one or more permitted hosts are configured.

Boot health: `otaCheckBootHealth()` increments `boot_fail` in NVS on every boot. `otaMarkBootSuccessful()` resets it after WiFi connects. Rolls back if `boot_fail >= OTA_MAX_BOOT_FAILURES`. In-progress download tracked via NVS keys `ota_ip` (bool), `ota_exp` (expected bytes), `ota_wrt` (written bytes) — all cleared on successful flash or rollback. `FIRMWARE_VERSION` is defined in `platformio.ini`.

## Authentication System

→ See `docs/api-reference.md` for route details.

- Emergency admin seeded from `secrets.h` on first boot if `u0_name` absent from NVS
- External adapter: `ADAPTER_OK` → skip NVS; `ADAPTER_REJECTED` → stop; `ADAPTER_ERROR` → fall through to NVS; URL/key stored in NVS under `db_url`/`db_key`
- `authAddSecurityHeaders()` sends `X-Frame-Options: DENY` and `X-Content-Type-Options: nosniff`
- Rate limiting: `AUTH_RATE_LIMIT_MAX_FAILURES` failures within `AUTH_RATE_LIMIT_WINDOW_MS` → `AUTH_RATE_LIMIT_LOCKOUT_MS` lockout; `AUTH_RATE_LIMIT_SLOTS` tracked IP slots (see `docs/config-reference.md` for values)
- Password: `AUTH_MIN_PASS_LEN`–`AUTH_MAX_PASS_LEN` chars; usernames `AUTH_MIN_USER_LEN`–`AUTH_MAX_USER_LEN` chars; max `AUTH_MAX_USERS` users

## Alternative Firmware

`sauna_esphome.yaml` — ESPHome configuration. Lacks native InfluxDB support and runtime motor calibration. Do not merge concerns between the two firmware approaches.
