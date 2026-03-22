---
name: sauna-status-patterns
description: Coding patterns extracted from SaunaStatus git history — ESP32 embedded firmware, TDD with native tests, modular C++ architecture, and project workflow conventions
version: 1.0.0
source: local-git-analysis
analyzed_commits: 62
---

# SaunaStatus Patterns

## Commit Conventions

This project uses **conventional commits** with scopes:

| Type | Usage |
|------|-------|
| `feat` | New features (most common — 25 commits) |
| `docs` | Documentation, CLAUDE.md updates (12 commits) |
| `fix` | Bug fixes, usually targeted to a module (10 commits) |
| `chore` | Tooling, settings, CI/CD (7 commits) |
| `test` | Test additions in red phase (5 commits) |
| `refactor` | Code restructuring without behavior change (3 commits) |

**Common scopes:** `auth`, `web`, `sensors`, `mqtt`, `influx`, `kicad`, `firmware`, `claude`, `ui`

Examples from history:
```
feat(auth): add token eviction and constant-time compare
fix(firmware): sync WS_JSON_BUF_SIZE to 384 in main.cpp; update CLAUDE.md
test(web): add failing tests for buildJson() — red phase
refactor(main): remove extracted functions; main.cpp is now thin orchestrator
chore(claude): add JSON validation hook, kicad-debug skill
```

## Code Architecture

```
src/
├── main.cpp          # Thin orchestrator — globals, setup(), loop() only
├── globals.h         # All extern declarations; ARDUINO guards for native compat
├── sauna_logic.h     # Header-only pure C++ — config, sensors, JSON (natively testable)
├── motor_logic.h     # Header-only pure C++ — motor clamping (natively testable)
├── auth_logic.h      # Header-only pure C++ — auth, sessions, hashing (natively testable)
├── ota_logic.h       # Header-only pure C++ — OTA versioning/rollback (natively testable)
├── sensors.h/.cpp    # DHT/MAX31865/INA260 reading; stoveReading() inline in header
├── web.h/.cpp        # HTTP handlers, WebSocket; buildJson() inline in header
├── mqtt.h/.cpp       # MQTT connect/publish/subscribe/discovery
├── influx.h/.cpp     # InfluxDB write operations
└── auth.h            # Auth route handlers, requireAdmin(), NVS persistence

test/
├── test_sensor/          # fmtVal, c2f, f2c, JSON null handling
├── test_config/          # 3-tier config merge logic
├── test_websocket/       # buildJsonFull() output and stale detection
├── test_auth/            # Auth sessions, tokens, password, user store
├── test_ota/             # OTA version parsing, manifest, rollback
├── test_sensor_module/   # stoveReading() fallback logic
├── test_web_module/      # buildJson() struct assembly
└── test_motor_logic/     # motorClampCW() clamping behavior
```

## TDD Workflow

The project follows strict **red → green → refactor** TDD:

1. **Red phase commit** — `test(module): add failing tests for X — red phase`
2. **Green phase commit** — `feat(module): extract X module; Y inline in header for native tests`
3. **Fix/refactor commit** — `fix(module): ...` or `refactor(module): ...`

Key pattern: **functions intended for native testing are inlined in headers** so the native `test_env` can include them without Arduino/ESP-IDF dependencies.

```cpp
// PATTERN: inline in .h for native testability, defined in .cpp for hardware
// web.h — inline so test_web_module can include without Arduino
inline void buildJson(char* buf, size_t len) { ... }

// web.cpp — hardware-dependent handlers defined here
void handleRoot() { server.send(200, "text/html", ...); }
```

## Module Extraction Pattern

When refactoring `main.cpp` into modules:

1. Create `src/module.h` and `src/module.cpp`
2. Move hardware-dependent functions to `.cpp`
3. Move pure-logic functions **inline to `.h`** for native test access
4. Add `extern` declarations to `globals.h` (not directly in module headers)
5. Wrap hardware objects in `#ifdef ARDUINO` guards in `globals.h`
6. Write native tests **before** extracting (red phase first)
7. Commit module extraction and test addition separately

## Sensor Patterns

Always refer to `.claude/rules/sensor-patterns.md` for detailed rules. Key invariants:

- **Set to `NAN` on failure** — never retain stale values
- **Use `||` not `&&`** for `last_ok_ms` updates (either channel alive = sensor alive)
- **Each sensor fails independently** — no cross-sensor pollution
- **`isSensorStale()` in `sauna_logic.h`** — centralized staleness logic; `last_ok_ms==0` is always stale
- **Apply NaN/stale checks to ALL consumers**: JSON, MQTT, InfluxDB, PID, serial

## Config System Conventions

Three-tier persistence — always add new config keys to ALL tiers:

| Tier | Mechanism | Example |
|------|-----------|---------|
| 1. Build flags | `#ifndef`-guarded `#define` in source | `#ifndef STALE_THRESHOLD_MS` |
| 2. Fleet defaults | `/config.json` in LittleFS | `"sensor_read_interval_ms": 2000` |
| 3. Per-device NVS | `Preferences`, guarded by `prefs.isKey()` | `prefs.putUInt("sri", val)` |

Document every new build-flag in the CLAUDE.md Build-Flag Overrides table.

## HTTP Response Conventions

| Outcome | Status | Body |
|---------|--------|------|
| Success (mutation) | 200 | `{"ok":true}` |
| Validation failure | 400 | `{"ok":false,"error":"<message>"}` |
| Unauthorized | 401 | `{"error":"unauthorized"}` |
| Conflict | 409 | `{"error":"user limit reached"}` |

`handleConfigSave()` uses `goto send_error` — validate everything first, apply+persist in one block at the end. Never apply partial state on validation failure.

## Validation After Every Edit

After any file change, immediately validate:

| File type | Command |
|-----------|---------|
| C++ / `.h` | `pio run` |
| JSON | `python3 -m json.tool <file>` |
| Functional change | `pio test -e native` |

**Never leave a broken state and continue.** Fix validation errors before the next step.

## Testing Patterns

- Framework: Unity (via PlatformIO native environment)
- Run: `pio test -e native` (no device required)
- Test naming: `test_<what>_<condition>_<expected>` e.g. `test_stale_ceiling_gives_null`
- **setUp() must reset ALL mutable globals between tests** — global state leaks cause false passes
- Tests must mirror real behavior: stale flag assertions must check both `null` value AND `cst:1` flag

## Frequently Co-Changed Files

When touching these files, check the paired file too:

| File | Often changes with |
|------|--------------------|
| `src/sauna_logic.h` | `test/test_websocket/test_main.cpp` |
| `src/auth_logic.h` | `test/test_auth/test_main.cpp` |
| `src/motor_logic.h` | `test/test_motor_logic/test_main.cpp` |
| `src/main.cpp` | `CLAUDE.md` (document new defines/behavior) |
| `.claude/settings.local.json` | `.claude/rules/`, `.mcp.json` |

## CLAUDE.md Maintenance

After any of these changes, update CLAUDE.md:
- New `#define` build flag → add to Build-Flag Overrides table
- New NVS key → add to NVS Persistence table
- New HTTP route → add to HTTP Server table
- New test suite → add to Unit Tests section with test count
- New module → add to Architecture section

## Security Invariants

- All state-mutating HTTP routes protected by `requireAdmin()` → Bearer token check
- Default role for new users must be `""` (not `"admin"`)
- `authVerifyPassword()` uses constant-time compare — never use `strcmp`
- External auth adapter rejection takes precedence over NVS fallback (`ADAPTER_REJECTED` → stop)
- `AUTH_ADMIN_PASS` must be in `secrets.h` (not committed); enforced by `#error` guard
