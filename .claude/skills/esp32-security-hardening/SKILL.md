---
name: esp32-security-hardening
description: Use when performing a security audit or remediation pass on ESP32 firmware — covers the CRIT/HIGH/MED/LOW checklist proven on this project
---

# ESP32 Security Hardening

## When to Use
Invoke before any production release or after a security review surfaces findings. Each tier below maps directly to commits applied in the 2026-03-25 remediation pass.

## Pattern / Rules

### CRITICAL — fix before any other work
1. **HTTPS-only for external URLs** — auth adapter URL and OTA manifest URL must start with `https://`. Validate at save time, not at call time.
   ```cpp
   if (url.length() && !url.startsWith("https://"))
       return false; // reject http:// and anything else
   ```
2. **OTA URL allowlist** — `OTA_ALLOWED_HOSTS` (comma-separated, empty = any). Check parsed hostname against list before streaming.
3. **Per-IP login rate limiting** — track failure counts in a fixed-size slot array; lockout after `AUTH_RATE_LIMIT_MAX_FAILURES` within `AUTH_RATE_LIMIT_WINDOW_MS`. Reuse oldest slot when full.
4. **WebSocket token auth** — require `Authorization: Bearer <token>` on the WS upgrade handshake. Reject with `WS_EVT_CONNECT` returning `false` if missing or invalid. Store per-client auth state keyed by `client->id()`.

### HIGH — fix in the same sprint
5. **Role selector + server-side role validation** — never trust a role sent by the client. Validate against an allowlist (`["", "admin", "user"]`) server-side; reject unknown values.
6. **OTA integrity: SHA-256, not MD5** — manifest must carry `sha256` field. Compute running hash during stream; abort and rollback if mismatch.
7. **Parameter whitelisting** — for `/history?range=N`, parse as integer and clamp to an allowed set `{1, 6, 12, 24, 48, 168}`; reject anything else with 400.
8. **OTA boot-success marker timing** — call `otaMarkBootSuccessful()` only after WiFi, MQTT, and HTTP are all fully initialized. Premature marking hides boot loops caused by subsystem failures.

### MEDIUM — fix same week
9. **PBKDF2 for password storage** — replace any direct SHA-256/MD5 of passwords with PBKDF2-SHA256, 10 000 iterations. Store `iterations` field per user for future re-hashing. See `~/.claude/skills/learned/backward-compatible-hash-upgrade.md`.
10. **CSP + security headers on all HTML pages** — `Content-Security-Policy`, `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`. Apply via a single `authAddSecurityHeaders()` helper called at the top of every HTML route handler.
11. **innerHTML → textContent for user-supplied data** — any username, device name, or message rendered in the dashboard must use `textContent`/`innerText`, never `innerHTML`.
12. **Username validation** — enforce charset (`[a-zA-Z0-9_-]`) and length (`AUTH_MIN_USER_LEN`..`AUTH_MAX_USER_LEN`) at creation time; reject at the boundary.
13. **MQTT broker ACL** — document in `docs/api-reference.md` that the MQTT broker must be configured with per-topic ACLs. The firmware cannot enforce this.

### LOW — fix opportunistically
14. **Error message masking** — auth failures return generic `"Invalid credentials"`, never `"user not found"` vs `"wrong password"`.
15. **Audit logging** — log login attempts (success + failure) via `logAccessEvent()` to InfluxDB. Include IP, username, result.
16. **Secrets template** — provide `src/secrets.h.example` with placeholder values; document in README that `secrets.h` is gitignored.

## Testing Each Fix
- Every CRIT/HIGH fix needs at least one unit test in `test/test_auth/` or `test/test_ota/`.
- Run `pio test -e native` after each fix; all 200+ tests must stay green.
- For WebSocket auth: write an integration note (no native test possible) in the PR description.
