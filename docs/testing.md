# Testing

Run with `pio test -e native`. Total: **300 tests** as of 2026-04-25.
Run `python3 scripts/verify_doc_drift.py` to verify documented HTTP routes and high-risk constants still match source.

Hardware-only verification is tracked separately in [docs/hardware-smoke.md](/home/mmessano/Documents/PlatformIO/Projects/SaunaStatus/docs/hardware-smoke.md:1). Use that checklist after firmware flashes, LittleFS uploads, OTA changes, Wi-Fi/MQTT changes, or safety-path edits.

## Test Suites

| Suite | Tests | What's covered |
|---|---|---|
| `test/test_sensor/` | 8 | `c2f`/`f2c`/`fmtVal`; NaN→null in JSON per sensor |
| `test/test_config/` | 33 | 3-tier merge logic; range validation; malformed/partial fleet config parsing; LittleFS unavailable/missing/malformed boot semantics; NVS-wins-over-fleet; power-cycle scenarios |
| `test/test_websocket/` | 20 | `buildJsonFull()` output; stale detection edge cases; broadcast timing; buffer sizing |
| `test/test_auth/` | 69 | Tokens, passwords, user store, login fallback, adapter orchestration, PBKDF2, rate limiting |
| `test/test_ota/` | 39 | Version parsing/comparison, manifest, rollback, partial download; `formatVersion`, `isDowngrade`, `isSameVersion` |
| `test/test_sensor_module/` | 5 | `stoveReading()` fallback to ceiling/bench average |
| `test/test_web_module/` | 6 | `buildJson()` struct assembly; INA260 absent path |
| `test/test_motor_logic/` | 8 | `motorClampCW()` clamping at max; CCW floor at zero |
| `test/test_motor_utils/` | 23 | `motorClampCCW`, `motorPosToPercent`, `motorPercentToSteps`; roundtrip |
| `test/test_overheat/` | 19 | `tickOverheat()` state machine; NaN handling; hysteresis; multi-tick lifecycle |
| `test/test_config_json/` | 15 | `buildConfigJson()` output format; keys; buffer safety |
| `test/test_version_utils/` | 24 | `formatVersion`, `isDowngrade`, `isSameVersion`; invalid/both-invalid edge cases |
| `test/test_gpio_config/` | 21 | Pin values, adjacency, uniqueness, restricted pins, coil order |

## Undocumented Items

Items found in the codebase not yet covered in `docs/`. Verify and expand as needed.

### API Routes (registered in `src/main.cpp`)

- [x] `POST /log` — `handleLog`
- [x] `DELETE /delete/status` — `handleDeleteStatus`
- [x] `DELETE /delete/control` — `handleDeleteControl`
- [x] `GET /history` — `handleHistory`
- [x] `POST /setpoint` — `handleSetpoint`
- [x] `POST /pid` — `handlePidToggle`
- [x] `POST /motor` — `handleMotorCmd`
- [x] `GET /config/get` — `handleConfigGet`
- [x] `POST /auth/logout` — `handleAuthLogout`
- [x] `GET /auth/status` — `handleAuthStatus`
- [x] `GET /users` — `handleUsersGet`
- [x] `POST /users` — `handleUsersCreate`
- [x] `DELETE /users` — `handleUsersDelete`
- [x] `PUT /users` — `handleUsersChangePassword`

### NVS Keys (in code, not in `docs/config-reference.md`)

- [x] `csp` — ceiling setpoint (°C float)
- [x] `bsp` — bench setpoint (°C float)
- [x] `cen` — ceiling PID enabled (bool)
- [x] `ben` — bench PID enabled (bool)
- [x] `ota_ip` — OTA download in progress (bool)
- [x] `ota_exp` — OTA expected byte count (uint)
- [x] `ota_wrt` — OTA bytes written so far (uint)
- [x] `db_url` — external auth adapter URL (string, ≤128)
- [x] `db_key` — external auth adapter API key (string, ≤64)
- [x] `u<N>_name` / `u<N>_hash` / `u<N>_salt` / `u<N>_role` / `u<N>_iter` — per-user NVS keys (N = 0..AUTH_MAX_USERS-1)

### `#define` Constants (in `src/*.h`, not in `docs/config-reference.md`)

- [x] `AUTH_MAX_USERS 5`
- [x] `AUTH_MIN_PASS_LEN 8` / `AUTH_MAX_PASS_LEN 72`
- [x] `AUTH_MIN_USER_LEN 1` / `AUTH_MAX_USER_LEN 32`
- [x] `AUTH_PBKDF2_ITERATIONS 10000`
- [x] `AUTH_RATE_LIMIT_MAX_FAILURES 5`
- [x] `AUTH_RATE_LIMIT_WINDOW_MS 60000UL`
- [x] `AUTH_RATE_LIMIT_LOCKOUT_MS 300000UL`
- [x] `AUTH_RATE_LIMIT_SLOTS 8`
- [x] `OTA_ALLOWED_HOSTS ""` — empty disables OTA; verify configured allowlist hosts succeed
- [x] `OVERHEAT_CLEAR_HYSTERESIS_C 10.0f` — clear band below `TEMP_LIMIT_C`; `checkOverheat()` in `sensors.cpp` has no equivalent
