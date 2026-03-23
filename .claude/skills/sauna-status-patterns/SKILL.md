---
name: sauna-status-patterns
description: Coding patterns extracted from SaunaStatus git history — ESP32 embedded firmware, TDD with native tests, modular C++ architecture, and project workflow conventions
version: 1.1.0
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

→ See **CLAUDE.md `## Architecture`** for the full annotated file tree with line counts. Summary:

- `*_logic.h` files (sauna, auth, ota, motor) — header-only pure C++, no Arduino deps, natively testable
- `*.h/.cpp` pairs (sensors, web, mqtt, influx, auth) — portable declarations in `.h`, Arduino-dependent implementations in `.cpp`
- `globals.h` — single source of truth for all `extern` declarations; hardware objects in `#ifdef ARDUINO` guards
- `main.cpp` — thin orchestrator only; all logic lives in modules

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

→ See skill **`esp32-sensor-patterns`** for the full checklist, code examples, and consumer audit table.

Key invariants: clear to `NAN` on failure; `||` not `&&` for `last_ok_ms`; apply both `!isnan()` and `!isSensorStale()` at every consumer — neither substitutes for the other.

## Config System

→ See skill **`embedded-config-layering`** for the full pattern, code examples, and common mistakes.

When adding a new config key, add it to ALL three tiers and document the build flag in CLAUDE.md's Build-Flag Overrides table.

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
- New `#define` build flag → add to **Build-Flag Overrides** table
- New NVS key → add to **NVS Persistence** table
- New HTTP route → add to **HTTP Server** table
- New test suite → add to **Unit Tests** section with test count
- New module → add to **Architecture** section

## HTTP Response Conventions

→ See **CLAUDE.md `## HTTP Response Conventions`** for the full table. Quick summary:

| Outcome | Status | Body pattern |
|---------|--------|------|
| Success | 200 | `{"ok":true}` |
| Validation failure | 400 | `{"ok":false,"error":"..."}` |
| Unauthorized | 401 | `{"error":"unauthorized"}` |
| Conflict | 409 | `{"error":"user limit reached"}` |

`handleConfigSave()` uses `goto send_error` — validate everything first, apply+persist atomically.

## Security Invariants

- All state-mutating HTTP routes protected by `requireAdmin()` → Bearer token check
- Default role for new users must be `""` (not `"admin"`)
- `authVerifyPassword()` uses constant-time compare — never use `strcmp`
- External auth adapter rejection takes precedence over NVS fallback (`ADAPTER_REJECTED` → stop)
- `AUTH_ADMIN_PASS` must be in `secrets.h` (not committed); enforced by `#error` guard
