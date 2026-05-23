# Testing

Two layers of automated coverage, plus a hardware checklist.

| Layer | Tool | Scope | Run |
|---|---|---|---|
| Native unit tests | PlatformIO + Unity | Pure-C++ logic in headers + Arduino-stubbed handler RBAC | `pio test -e native` |
| UI integration tests | Playwright + Chromium (via `tools/ui-test/`) | `data/*.html` rendered in a real browser against a mock ESP32 server | `cd tools/ui-test && ./node_modules/.bin/playwright test` |
| Doc drift check | `scripts/verify_doc_drift.py` | Registered HTTP routes + high-risk constants match docs | `python3 scripts/verify_doc_drift.py` |
| Hardware smoke | Manual | Live device behavior after flash/OTA/Wi-Fi/safety-path changes | [docs/hardware-smoke.md](hardware-smoke.md) |

Native total: **320 tests** as of 2026-04-26. UI total: **44 tests** as of 2026-05-22.

## Test Suites

| Suite | Tests | What's covered |
|---|---|---|
| `test/test_sensor/` | 8 | `c2f`/`f2c`/`fmtVal`; NaN→null in JSON per sensor |
| `test/test_config/` | 33 | 3-tier merge logic; range validation; malformed/partial fleet config parsing; LittleFS unavailable/missing/malformed boot semantics; NVS-wins-over-fleet; power-cycle scenarios |
| `test/test_websocket/` | 20 | `buildJsonFull()` output; stale detection edge cases; broadcast timing; buffer sizing |
| `test/test_auth/` | 69 | Tokens, passwords, user store, login fallback, adapter orchestration, PBKDF2, rate limiting |
| `test/test_web_rbac/` | 20 | Route-level RBAC regressions for admin-only handlers in `src/web.cpp` and `src/web_auth.cpp` using native Arduino/WebServer stubs, including all `/users` methods plus admin-path state mutation checks for `/config/save`, `/motor`, `/setpoint`, and `/pid` |
| `test/test_ota/` | 39 | Version parsing/comparison, manifest, rollback, partial download; `formatVersion`, `isDowngrade`, `isSameVersion` |
| `test/test_sensor_module/` | 5 | `stoveReading()` fallback to ceiling/bench average |
| `test/test_web_module/` | 6 | `buildJson()` struct assembly; INA260 absent path |
| `test/test_motor_logic/` | 8 | `motorClampCW()` clamping at max; CCW floor at zero |
| `test/test_motor_utils/` | 23 | `motorClampCCW`, `motorPosToPercent`, `motorPercentToSteps`; roundtrip |
| `test/test_overheat/` | 19 | `tickOverheat()` state machine; NaN handling; hysteresis; multi-tick lifecycle |
| `test/test_config_json/` | 15 | `buildConfigJson()` output format; keys; buffer safety |
| `test/test_version_utils/` | 24 | `formatVersion`, `isDowngrade`, `isSameVersion`; invalid/both-invalid edge cases |
| `test/test_gpio_config/` | 21 | Pin values, adjacency, uniqueness, restricted pins, coil order |

## UI Integration Suite

The `tools/ui-test/` directory holds a Playwright harness that renders `data/*.html` in a real Chromium against a Node mock that fakes every HTTP and WebSocket endpoint the dashboard, config portal, and login page call. Runs both Desktop Chrome (1280×800) and Pixel 5 viewports — 44 tests across `login.spec.js`, `index.spec.js`, and `config.spec.js`. Setup, fixtures, and the `:81 → :18081` WebSocket-port patch are documented in `tools/ui-test/README.md`.

Notable assertions, mapped to design-review findings (see `BACKLOG.md` UI lane):

- **C3** Motor card has separated "CALIBRATE" and "MOVE TO POSITION" sub-sections with unambiguous labels
- **H3** Connection state class (`.ok`/`.err`/`.connecting`) plus ARIA `role="status"` / `aria-live="polite"`
- **H4** Status threshold subtext (e.g. "140–194°F" under "Ready")
- **H5** `index.html` redirects to `/auth/login` on 401, matching `config.html`
- **H6** `.grid` resolves to `display: grid` with non-empty `grid-template-columns`
- **L7** Unauthenticated `/` redirects to `/auth/login` (the inline login panel was removed; there is only one login UI now)
- **M4** Out-of-range setpoints get a client-side error banner, no server round-trip
- **M5** Themed confirm modal with type-to-confirm for destructive bucket resets
- **M6** Setpoint dirty/edited badge appears when user diverges from live value; WS overwrite suppressed while dirty
- **M7** `#refreshCountdown` displays "(auto in 0:NN)" within seconds of page load
- **M9** 429 lockout from `/auth/login` surfaces distinct "Too many attempts" copy
- **M10** Chart.js + adapter load from `/chart.umd.min.js` and `/chart-adapter.min.js`, no jsdelivr/unpkg requests, canvas non-empty

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
