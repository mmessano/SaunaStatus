---
name: diagnose-mcp-servers
description: Use when an MCP server is missing from the tool list, not appearing after config changes, tools time out, or after installing or reconfiguring any MCP server.
---

# MCP Server Diagnostics

Run steps in order. Each step rules out one known failure mode.

## Step 1 — Config location: `~/.mcp.json`, NOT `settings.json`

MCP servers must be in `~/.mcp.json` (global) or `.mcp.json` (project root).
The old `settings.json > mcpServers` format is obsolete and ignored.

```bash
cat ~/.mcp.json
# Expected: { "mcpServers": { "kicad": { "command": "...", "args": [] } } }
```

**Auto-fix:** If the entry is in `settings.json`, move it to `~/.mcp.json`.

## Step 2 — Server in `enabledMcpjsonServers`

Lives in `~/.claude/settings.json` (user scope). NOT in project settings.

```bash
python3 -c "import json; print(json.load(open('$HOME/.claude/settings.json')).get('enabledMcpjsonServers'))"
# Must include the server name, e.g. ["kicad"]
```

**Auto-fix:** Add server name to the `enabledMcpjsonServers` array in `~/.claude/settings.json`.

## Step 3 — Server binary / command exists

```bash
# Check the command path from .mcp.json is reachable:
ls -la ~/.local/bin/kicad-mcp-server   # adjust path per server
```

**Auto-fix:** If missing, reinstall the server package (e.g. `pip install kicad-skip`).

## Step 4 — Python dependencies (Python-based servers)

```bash
pip show kicad-skip                        # correct package for kicad-mcp
pip show skip-python 2>/dev/null && echo "WARNING: wrong package installed"
python3 -c "import skip; print('OK')"     # module is 'skip', NOT 'kicad_skip'
```

**Known gotcha:** `kicad-skip` installs as module `skip`. `import kicad_skip` always fails — this is expected.

**Auto-fix wrong package:** `pip uninstall -y skip-python && pip install kicad-skip`

## Step 5 — Upstream import errors

If binary and deps are correct but server still crashes:

```bash
~/.local/bin/kicad-mcp-server --help 2>&1 | head -10
```

`ImportError` or `ModuleNotFoundError` on startup = upstream broken imports needing a manual patch in the server's Python source.

## Step 6 — Restart Claude Code

All MCP config changes (steps 1–5) require a full Claude Code restart. No hot-reload.

## Report Format

After checking, output a status table then list issues with specific fixes:

```
Server  | .mcp.json | Enabled | Binary | Deps OK
--------|-----------|---------|--------|--------
kicad   | ✓         | ✓       | ✓      | ✓
```

## Quick Reference

| Symptom | Fix |
|---|---|
| Server in `settings.json mcpServers` | Move entry to `~/.mcp.json` |
| Not in `enabledMcpjsonServers` | Add to array in `~/.claude/settings.json` |
| Binary missing | Reinstall server package |
| `import kicad_skip` fails | Expected — correct import is `import skip` |
| `import skip` fails | `pip install kicad-skip` |
| `skip-python` installed | `pip uninstall -y skip-python && pip install kicad-skip` |
| All OK but server missing | Restart Claude Code |
