# CLAUDE.md

This file provides behavioral guidance to Claude Code when working with this repository. For detailed specs, see `docs/`.

## Quick Reference — Where to Find Things

| Topic | Location |
|---|---|
| Source tree, modules, PID, motors, LittleFS | @docs/architecture.md |
| HTTP/WebSocket/MQTT/InfluxDB API | @docs/api-reference.md |
| Build flags, config tiers, NVS keys | @docs/config-reference.md |
| GPIO assignments, sensors, connectors | @docs/pinout.md |
| Test suites, counts, undocumented items | @docs/testing.md |
| KiCad schematic rules | `docs/kicad/SCHEMATIC_GEN_RULES.md` |
| Hardware design constraints | `docs/hardware-rules.md` (**⚠ old WROOM-32 board — GPIO numbers do not match current ESP32-S3**) |

## Project Overview

ESP32-S3 sauna automation: PT1000 stove sensor, dual DHT21 ceiling/bench sensors, INA260 power monitor, two stepper-driven damper vents with dual PID controllers. Integrates InfluxDB, MQTT (Home Assistant Discovery), WebSocket/HTTP. Always consider memory constraints and real-time requirements.

- **After any C/C++ change**, run `pio run` to verify compilation.
- **Key paths:** `src/` firmware, `data/` LittleFS web assets, `test/` native unit tests.

## Build Commands

```bash
pio run -t upload    # firmware only (bare `pio run` uploads firmware AND filesystem)
pio run -t uploadfs  # filesystem only (web UI in data/)
pio device monitor   # serial monitor — use UART connector (GPIO43/44), NOT USB-C OTG
pio run -t clean
pio test -e native                        # all 281 tests
pio test -e native -f test_gpio_config    # single suite
bash scripts/parallel-tdd.sh             # parallel TDD agent workflow
```

All firmware commands target `lb_esp32s3` (lolin_s3, ESP32-S3 N16R8). Tests use `native`.

## Git Hooks

`scripts/update-handoff.sh` regenerates `HANDOFF.md` after every commit. Install on each clone:

```bash
ln -s ../../scripts/update-handoff.sh .git/hooks/post-commit
```

## Skills & Knowledge

Project-specific skills are in `.claude/skills/`. Use before implementing known patterns:

- `esp32-sensor-patterns` — sensor staleness, NaN handling, PID guards, consumer audit
- `esp32-auth-bearer` — Bearer token auth implementation
- `esp32-ota-update` — OTA manifest flow and boot health
- `kicad-erc-drc-workflow` — ERC/DRC violation fixes, wire format, PCB parity

## MCP Server Configuration

→ Use skill **`diagnose-mcp-servers`** for diagnostics.

- Config lives in `~/.mcp.json` (global) — NOT `settings.json > mcpServers`
- KiCad MCP package is **`kicad-skip`** (NOT `skip-python`); module name is `skip`
- Config changes require a full Claude Code restart — no hot-reload

## Settings File Conventions

Always use `~/` for home-directory paths in `.claude/settings.local.json` or `.claude/settings.json`. Never hardcode `/home/<username>/`.

## Project Conventions

### Sensor Handling

All sensor floats init to `NAN`; clear to `NAN` on any read failure; use `||` (not `&&`) for `last_ok_ms` updates. `isSensorStale()` and `!isnan()` are **separate** guards — both required at every consumer (display, PID, MQTT, InfluxDB, serial). INA260 is optional (`ina260_ok`); omit all power fields when absent.

### Config Persistence

→ See @docs/config-reference.md for the full 3-tier system (build flags → LittleFS → NVS).

- Setpoints stored internally in **°C**; HTTP/MQTT accepts/returns **°F** — convert at the boundary only
- Every NVS read must be guarded by `prefs.isKey()` — missing key must never revert a fleet value
- `handleConfigSave()`: declare all candidates → validate all → apply+persist in one block; never apply partial state
- `static_ip` / `device_name` require restart → include `"restart_required":true` in response

### Logging

Two independent runtime-configurable intervals (both persisted to NVS):

| Variable | NVS key | Default | Controls |
|---|---|---|---|
| `g_sensor_read_interval_ms` | `sri` | `DEFAULT_SENSOR_READ_INTERVAL_MS` (2000 ms) | Sensor reads, PID, WebSocket broadcast, MQTT publish |
| `g_serial_log_interval_ms` | `slg` | `SERIAL_LOG_INTERVAL_MS` (10000 ms) | Serial status table print rate |

Serial log prints `ERR` for NaN values and `---` for unavailable readings.

## Authentication Rules

→ See @docs/api-reference.md for route details.

- Emergency admin seeded from `secrets.h` on first boot if `u0_name` absent from NVS
- Role from external adapter stored verbatim — **never default to a privilege level**; default role must be `""`, never `"admin"`
- External adapter: `ADAPTER_OK` → skip NVS; `ADAPTER_REJECTED` → stop; `ADAPTER_ERROR` → fall through to NVS
- Rate limiting: 5 failures / 60 s → 5-min lockout, 8 tracked IP slots

## OTA Rules

→ See @docs/api-reference.md for `/ota/status` and `/ota/update` routes.

- Refuses downgrades and same-version re-flashes
- `otaCheckBootHealth()` increments `boot_fail` on every boot; reset after WiFi connects; rollback if `>= OTA_MAX_BOOT_FAILURES`
- `FIRMWARE_VERSION` defined in `platformio.ini` — update for every release

## Post-Edit Validation

| File type | Command |
|---|---|
| C++ / `.h` | `pio run` |
| JSON | `python3 -m json.tool <file>` |
| Python | `python3 -m py_compile <file>` |
| Functional change | `pio test -e native` |

Fix failures before moving on.

## ESP32 Development

When changing sensor or hardware code, verify stale state cleanup — disconnected devices must not retain old values in memory or UI. Clear to `NAN` on failure; never leave the previous reading in place.

## Documentation

When generating `HANDOFF.md` manually, check for mangled markdown headers, broken lists, and inconsistent indentation before finishing. The post-commit hook handles regeneration automatically.

## Automated Agents

- Always check `PAPERCLIP_API_KEY` is set and non-empty before starting heartbeat tasks
- If Bash tool permissions block external API calls, flag immediately and stop — do not retry

## Common Pitfalls

| Pitfall | Symptom | Fix |
|---|---|---|
| **Stale sensor values retained** | Vent moves on old data after disconnect | Clear to `NAN` on read failure; use `\|\|` not `&&` for `last_ok_ms` |
| **Stale vs NaN — two separate checks** | Stale-but-non-NaN reading drives PID | Both `!isnan()` AND `!isSensorStale()` required at every consumer |
| **New JSON field not consumed** | Field in JSON but ignored by UI/MQTT/InfluxDB | Audit all consumers after adding any field |
| **JSON trailing comma** | `config.json` / `settings.json` silently rejected | Run `python3 -m json.tool <file>` after every JSON edit |
| **Wrong pip package** | MCP server crashes on import | Package is `kicad-skip`, NOT `skip-python` |
| **KiCad PCB net regex too loose** | New nets inserted inside pad block, corrupting footprint | Use `^\t\(net \d+ "` (exactly 1 tab) |
| **LittleFS partition named wrong** | `partition "spiffs" could not be found` at boot | CSV entry must be named `spiffs`, not `littlefs` |
| **Bare `pio run` overwrites filesystem** | Customized `data/` image lost | Use `pio run -t upload` or `pio run -t uploadfs` explicitly |
