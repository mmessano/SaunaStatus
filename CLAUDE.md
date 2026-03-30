# CLAUDE.md

This file provides behavioral guidance to Claude Code when working with this repository. For detailed specs, see `docs/`.

## Quick Reference

| Topic | Location |
|---|---|
| Source tree, modules, PID, motors, OTA, auth details | @docs/architecture.md |
| HTTP/WebSocket/MQTT/InfluxDB API | @docs/api-reference.md |
| Build flags, config tiers, NVS keys | @docs/config-reference.md |
| GPIO assignments, sensors, connectors | @docs/pinout.md |
| Test suites, undocumented items | @docs/testing.md |
| KiCad schematic rules | `docs/kicad/SCHEMATIC_GEN_RULES.md` |
| Hardware design constraints | `docs/hardware-rules.md` (**⚠ old WROOM-32 board**) |

## Project Overview

ESP32-S3 sauna automation: dual DHT21 + PT1000 sensors, INA260 power monitor, two stepper-driven damper vents, dual PID controllers. InfluxDB, MQTT (Home Assistant Discovery), WebSocket/HTTP. Always consider memory constraints and real-time requirements.

**Key paths:** `src/` firmware, `data/` LittleFS web assets, `test/` native unit tests. After any C/C++ change, run `pio run` before considering the change done.

## Build Commands

```bash
pio run -t upload    # firmware only (bare `pio run` uploads firmware AND filesystem)
pio run -t uploadfs  # filesystem only
pio device monitor   # use UART connector (GPIO43/44), NOT USB-C OTG
pio test -e native                      # all 281 tests
pio test -e native -f test_gpio_config  # single suite
bash scripts/parallel-tdd.sh           # parallel TDD agent workflow
```

After any functional change: `pio test -e native`. After any JSON edit: `python3 -m json.tool <file>`.

## Git Hooks

`scripts/update-handoff.sh` regenerates `HANDOFF.md` after every commit. Install on each clone:

```bash
ln -s ../../scripts/update-handoff.sh .git/hooks/post-commit
```

## Skills

Use before implementing known patterns:

- `esp32-sensor-patterns` — staleness, NaN handling, PID guards, consumer audit
- `esp32-auth-bearer` — Bearer token auth
- `esp32-ota-update` — OTA manifest flow and boot health
- `kicad-erc-drc-workflow` — ERC/DRC fixes, wire format, PCB parity

## MCP Server Configuration

→ Use skill **`diagnose-mcp-servers`** for diagnostics.

- Config lives in `~/.mcp.json` (global) — NOT `settings.json > mcpServers`
- KiCad MCP package is **`kicad-skip`** (NOT `skip-python`); module name is `skip`
- Config changes require a full Claude Code restart

## Settings File Conventions

Always use `~/` for home-directory paths in `.claude/settings.local.json` or `.claude/settings.json`. Never hardcode `/home/<username>/`.

## Critical Coding Rules

### Sensors

All sensor floats init to `NAN`; clear to `NAN` on any read failure; use `||` (not `&&`) for `last_ok_ms`. `isSensorStale()` and `!isnan()` are **separate** guards — both required at every consumer. INA260 is optional; omit all power fields when absent.

### Config Persistence

→ See @docs/config-reference.md for NVS keys and tier details.

- Setpoints stored in **°C** internally; API accepts/returns **°F** — convert at the boundary only
- Every NVS read must be guarded by `prefs.isKey()` — missing key must never revert a fleet value
- `handleConfigSave()`: validate all candidates first, then apply+persist atomically; never apply partial state
- `static_ip` / `device_name` changes require restart → include `"restart_required":true` in response

### Auth

- Role from external adapter stored verbatim — **never default to a privilege level**; default role must be `""`, never `"admin"`
- Update `FIRMWARE_VERSION` in `platformio.ini` for every OTA release

## Automated Agents

- Check `PAPERCLIP_API_KEY` is set before starting heartbeat tasks
- If Bash tool permissions block external API calls, stop immediately — do not retry

## Common Pitfalls

| Pitfall | Fix |
|---|---|
| New JSON field ignored by UI/MQTT/InfluxDB | Audit all consumers after adding any field |
| JSON trailing comma | `python3 -m json.tool <file>` after every edit |
| Wrong pip package for KiCad MCP | Package is `kicad-skip`, NOT `skip-python` |
| KiCad PCB net regex too loose | Use `^\t\(net \d+ "` (exactly 1 tab) |
| LittleFS partition named wrong | CSV entry must be `spiffs`, not `littlefs` |
| Bare `pio run` overwrites filesystem | Use `pio run -t upload` or `pio run -t uploadfs` |
