# No Hardcoded User Paths in Config Files

## Rule: Never write `/home/<username>/` in any config or settings file

Any path under the home directory **must** use `~/` — never a hardcoded absolute path containing a username.

**Applies to:**
- `.claude/settings.local.json` (permissions, hooks)
- `.claude/settings.json`
- Any JSON, YAML, TOML, or INI config file committed to the repo
- MCP server configs (`.mcp.json`, `~/.mcp.json`)

---

## Pattern

```
# WRONG — hardcoded username, breaks on any other machine or user
"Bash(cp /home/mmessano/Documents/...)"
"Read(/home/mmessano/.claude/**)"

# RIGHT — portable, works for any user
"Bash(cp ~/Documents/...)"
"Read(~/.claude/**)"
```

---

## Verification

Before writing any permission rule or path to a settings file, mentally check:

> "Does this path start with `/home/`?"

If yes — replace everything up to and including the username with `~/`.

---

## Self-Audit

After any session that modifies `.claude/settings.local.json` or `.claude/settings.json`, run:

```bash
grep -n '/home/' .claude/settings.local.json .claude/settings.json 2>/dev/null \
  && echo "ERROR: hardcoded user paths found — fix before committing" \
  || echo "OK: no hardcoded user paths"
```

Expected output: `OK: no hardcoded user paths`
