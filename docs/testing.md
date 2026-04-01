# Testing

Run with `pio test -e native`. Total: **281 tests** as of 2026-03-29.

## Test Suites

| Suite | Tests | What's covered |
|---|---|---|
| `test/test_sensor/` | 8 | `c2f`/`f2c`/`fmtVal`; NaN→null in JSON per sensor |
| `test/test_config/` | 24 | 3-tier merge logic; range validation; NVS-wins-over-fleet; power-cycle scenarios |
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

- [ ] `GET /log` — `handleLog`
- [ ] `GET /delete/status` — `handleDeleteStatus`
- [ ] `GET /delete/control` — `handleDeleteControl`
- [ ] `GET /history` — `handleHistory`
- [ ] `GET|POST /setpoint` — `handleSetpoint`
- [ ] `GET|POST /pid` — `handlePidToggle`
- [ ] `GET|POST /motor` — `handleMotorCmd`
- [ ] `GET /config/get` — `handleConfigGet`
- [ ] `POST /auth/logout` — `handleAuthLogout`
- [ ] `GET /auth/status` — `handleAuthStatus`
- [ ] `GET /users` — `handleUsersGet`
- [ ] `POST /users` — `handleUsersCreate`
- [ ] `DELETE /users` — `handleUsersDelete`
- [ ] `PUT /users` — `handleUsersChangePassword`

### NVS Keys (in code, not in `docs/config-reference.md`)

- [ ] `csp` — ceiling setpoint (°C float)
- [ ] `bsp` — bench setpoint (°C float)
- [ ] `cen` — ceiling PID enabled (bool)
- [ ] `ben` — bench PID enabled (bool)
- [ ] `ota_ip` — OTA download in progress (bool)
- [ ] `ota_exp` — OTA expected byte count (uint)
- [ ] `ota_wrt` — OTA bytes written so far (uint)
- [ ] `db_url` — external auth adapter URL (string, ≤128)
- [ ] `db_key` — external auth adapter API key (string, ≤64)
- [ ] `u<N>_name` / `u<N>_hash` / `u<N>_salt` / `u<N>_role` / `u<N>_iter` — per-user NVS keys (N = 0..AUTH_MAX_USERS-1)

### `#define` Constants (in `src/*.h`, not in `docs/config-reference.md`)

- [ ] `AUTH_MAX_USERS 5`
- [ ] `AUTH_MIN_PASS_LEN 8` / `AUTH_MAX_PASS_LEN 72`
- [ ] `AUTH_MIN_USER_LEN 1` / `AUTH_MAX_USER_LEN 32`
- [ ] `AUTH_PBKDF2_ITERATIONS 10000`
- [ ] `AUTH_RATE_LIMIT_MAX_FAILURES 5`
- [ ] `AUTH_RATE_LIMIT_WINDOW_MS 60000UL`
- [ ] `AUTH_RATE_LIMIT_LOCKOUT_MS 300000UL`
- [ ] `AUTH_RATE_LIMIT_SLOTS 8`
- [ ] `OTA_ALLOWED_HOSTS ""` — empty = any host allowed
- [ ] `OVERHEAT_CLEAR_HYSTERESIS_C 10.0f` — clear band below `TEMP_LIMIT_C`; `checkOverheat()` in `sensors.cpp` has no equivalent
