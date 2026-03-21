---
name: esp32-auth-bearer
description: Implement and extend the token-based auth system in the SaunaStatus ESP32 project — session management, route guards, NVS persistence, and external adapter fallback.
---

# ESP32 Bearer Token Auth System

## When to Use

Use this skill when adding new protected HTTP routes, modifying the user store, adjusting token TTL, or integrating the external adapter. The auth system is split across three files: `src/auth_logic.h` (pure portable logic), `src/auth.h` (ESP32 glue: NVS, SHA-256, esp_random, HTTP helpers), and `src/main.cpp` (route registration, global state).

## Pattern / Rules

1. **Protect routes with `requireAdmin()`**: Call at the top of any handler. Returns `nullptr` and sends `401` automatically on failure. The caller must `return` immediately if `nullptr` is received. Never check roles manually — all current users are admin.

2. **Token format**: 32 random bytes from `esp_random()` hex-encoded = 64-char string. Tokens expire after `AUTH_TOKEN_TTL_MS` (default 1 hour). Expiry uses unsigned `uint32_t` subtraction — safe across `millis()` rollover.

3. **Session eviction order**: inactive slot → expired slot → oldest-by-issued_ms. Never displaces a valid session unless all 10 slots are active and unexpired.

4. **Slot 0 protection**: `authDeleteUser()` rejects deletes of `emergency_slot` (always 0 in production). The emergency admin in slot 0 can have its password changed but cannot be deleted.

5. **Adapter fallback logic**: `ADAPTER_REJECTED` is final — do NOT fall through to NVS. `ADAPTER_ERROR` (timeout/network) DOES fall through to NVS. Never default the role to a privilege level when the adapter returns an empty role.

6. **NVS namespace**: `sauna_auth`. Keys: `u{i}_name`, `u{i}_hash`, `u{i}_salt`, `u{i}_role` for i in 0..4. Adapter config: `db_url`, `db_key`. Always call `authNvsSave()` after any user store mutation.

7. **Auth build flag constants**: All tunable via `-D` in `platformio.ini`. Defaults: `AUTH_TOKEN_TTL_MS=3600000`, `AUTH_MAX_SESSIONS=10`, `AUTH_MAX_USERS=5`, `AUTH_MIN_PASS_LEN=8`, `AUTH_MAX_PASS_LEN=72`.

8. **Security headers**: Always call `authAddSecurityHeaders()` on auth-related responses (`X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`).

9. **Access logging**: Use `logAccessEvent(event, username, auth_source)` for login_success, login_failure, logout. Writes to `sauna_webaccess` InfluxDB measurement. Fire-and-forget.

10. **Default role must always be `""`**: When creating users, never hardcode a default role of `"admin"` — require the caller to supply it explicitly. The `handleUsersCreate` handler currently defaults to `"admin"` only because all users are expected to be admins; revisit if roles diversify.

## Code Template

```cpp
// Adding a new protected route:
void handleMyNewEndpoint() {
    if (!requireAdmin()) return;   // sends 401 and returns nullptr on failure
    // ... handler logic ...
    server.send(200, "application/json", "{\"ok\":true}");
}

// Register in setup():
server.on("/my/endpoint", HTTP_POST, handleMyNewEndpoint);
// Ensure Authorization header is collected (already done in setup() via collectHeaders)
```

## Testing

`test/test_auth/` — 35 native tests covering: hex helpers, constant-time comparison, token issue/validate/expire/rollover/eviction, password hash/verify/length, user add/delete/protect/change-password, login adapter fallback (OK/REJECTED/ERROR), and InfluxDB log event fields. Run with `pio test -e native`.
