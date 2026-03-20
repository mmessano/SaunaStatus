---
name: kicad-debug
description: Use when the KiCad MCP server is missing from the tool list, kicad tools are unavailable, or after config changes that should have enabled the server.
---

# KiCad MCP Debug

Step-by-step checklist to diagnose why the KiCad MCP server is not appearing.

## Checklist

1. **Check global MCP config** — `~/.mcp.json` (NOT project root `.mcp.json`)
   ```bash
   cat ~/.mcp.json
   # Must have a "kicad" entry under "mcpServers"
   ```

2. **Check `enabledMcpjsonServers`** — lives in `~/.claude/settings.json` (user scope)
   ```bash
   python3 -c "import json; d=json.load(open('$HOME/.claude/settings.json')); print(d.get('enabledMcpjsonServers'))"
   # Must include "kicad"
   ```

3. **Check Python package** — `kicad-skip` (NOT `skip-python`)
   ```bash
   pip show kicad-skip
   pip show skip-python  # Should NOT be installed
   ```

4. **Test import** — module is `skip`, NOT `kicad_skip`
   ```bash
   python3 -c "import skip; print('OK')"
   # 'import kicad_skip' always fails — use 'import skip'
   ```

5. **Check server binary**
   ```bash
   ls -la ~/.local/bin/kicad-mcp-server
   ```

6. **If all pass** → restart Claude Code. MCP config changes require a restart.

## Known Gotchas

| Symptom | Fix |
|---|---|
| `import kicad_skip` fails | Expected — correct name is `import skip` |
| `import skip` fails | `pip install kicad-skip` |
| `skip-python` installed | `pip uninstall skip-python && pip install kicad-skip` |
| Server listed but tools timeout | Check binary path: `~/.local/bin/kicad-mcp-server` |
| Config correct but server missing | Restart Claude Code |
