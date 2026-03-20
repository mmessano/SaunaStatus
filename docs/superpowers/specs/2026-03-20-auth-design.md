# Auth & Access Control Design
**Date:** 2026-03-20
**Project:** SaunaStatus (ESP32)
**Status:** Approved (rev 2 — post spec-review fixes)

---

## Overview

Add user authentication and role-based access control to the SaunaStatus web interface. The site is split into public read-only content (live dashboard, temperature history, WebSocket state stream) and admin content (all controls, configuration, OTA, data management). Admin access requires a bearer token obtained via login with username + password.

---

## Requirements

- Strict public/admin split — read-only views are unauthenticated; all mutating or sensitive endpoints require a valid bearer token
- Multiple users — up to 5, stored locally in NVS with SHA-256+salt hashing (mbedtls, already linked)
- One permanent emergency admin in NVS slot 0 — cannot be deleted or have password changed via `DELETE /users`; password change via `PUT /users` IS permitted (to allow rotation) but the slot cannot be removed
- Optional external credential store — a thin REST adapter in front of PostgreSQL/MySQL; device never holds DB credentials
- Fallback to NVS when external adapter is unreachable or not configured
- No TLS on the device — reverse proxy handles it if external exposure is ever needed
- Defense-in-depth response headers on all routes
- Stay on synchronous `WebServer` library — async refactor not justified by workload
- WebSocket (port 81) stays public — broadcast-only, read-only state stream
- Minimum password length of 8 characters enforced in firmware on user create and password change

---

## Transport & Server

| Decision | Choice | Rationale |
|---|---|---|
| TLS on device | No | Memory pressure (~40–60 KB per TLS handshake), library switch required, LAN-only threat model |
| HTTP server | `WebServer` (synchronous, Arduino built-in) | Workload is low; 13-handler refactor to AsyncWebServer not justified |
| TLS if external | Reverse proxy (nginx/Caddy) | Handles TLS termination without touching device firmware |
| Security headers | Yes, all responses | `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`, `Cache-Control: no-store` on auth endpoints |

**Known limitation:** When the external adapter is configured, the plaintext password traverses the LAN over HTTP (no TLS on device). On a private home LAN with WPA2 WiFi this is an accepted risk consistent with the overall threat model. If the adapter is co-located with a reverse proxy, run the adapter on loopback or a VLAN segment to minimize exposure.

---

## Authentication Mechanism

**Bearer token, in-memory session store.**

```
POST /auth/login  {username, password}
  → validate against external adapter (if configured), fallback to NVS
  → token generation: MUST occur after WiFi is connected (esp_random() requires
    active radio for full entropy per ESP32 TRM section 24)
  → on success: issue 64-char hex token (32 bytes from esp_random())
  → response: {token, expires_in, username, role}

All admin requests:
  Authorization: Bearer <token>
  → validateToken() scans g_sessions[], checks expiry (rollover-safe)
  → 401 if not found or expired

POST /auth/logout
  → invalidates token slot (zeroed, active=false)
  → client-side: a 401 from this endpoint means token was already expired;
    treat as success — clear sessionStorage and redirect to login (no error shown)
```

### `collectHeaders` Registration (required)

The Arduino `WebServer` library does not expose arbitrary request headers unless explicitly registered. This call **must** appear in `setup()` before `server.begin()`:

```cpp
const char *collectHdrs[] = {"Authorization"};
server.collectHeaders(collectHdrs, 1);
server.begin();
```

Without it, `server.hasHeader("Authorization")` always returns false and every protected route returns 401.

### Session Store

Fixed array, no heap allocation:

```cpp
struct AuthSession {
    char     token[65];    // 64-char hex + null
    char     username[33]; // max 32 chars + null
    char     role[17];     // "admin" + null (uint8_t flags if memory pressure arises)
    uint32_t issued_ms;    // millis() at issuance — used for rollover-safe expiry
    bool     active;
};

static AuthSession g_sessions[AUTH_MAX_SESSIONS]; // default 10
```

**Expiry check (rollover-safe):** `(millis() - session.issued_ms) > AUTH_TOKEN_TTL_MS`
— never compare `millis()` against an absolute expiry timestamp; subtraction handles 32-bit rollover correctly, matching the pattern used in sensor staleness checks.

Memory: ~120 bytes × 10 slots = ~1.2 KB.

### Session Slot Exhaustion Policy

When all `AUTH_MAX_SESSIONS` slots are active with valid (non-expired) tokens and a new login is accepted:

1. Scan for any expired slot first — reclaim it
2. If none expired, evict the **oldest slot by `issued_ms`** (largest value of `millis() - issued_ms` — the session that has been alive the longest) regardless of whether it is still valid — the oldest session is displaced
3. If `issued_ms` values are all equal (e.g., device just booted), evict slot index 0

This ensures a login never fails due to slot exhaustion alone. The displaced user's next request will return 401 and the UI will redirect them to login.

### Token Comparison

Token validation uses a constant-time byte comparison to avoid timing side-channels:

```cpp
// Constant-time comparison — do not use strcmp() for token matching
bool tokenEqual(const char *a, const char *b) {
    uint8_t diff = 0;
    for (int i = 0; i < 64; i++) diff |= (uint8_t)a[i] ^ (uint8_t)b[i];
    return diff == 0;
}
```

### Build-Flag Tunables

All `#ifndef`-guarded, overridable via `platformio.ini`:

| Define | Default | Description |
|---|---|---|
| `AUTH_TOKEN_TTL_MS` | `3600000UL` | Token lifetime (1 hour) |
| `AUTH_MAX_SESSIONS` | `10` | Max concurrent sessions |
| `AUTH_MAX_USERS` | `5` | Max users in NVS |
| `AUTH_MIN_PASS_LEN` | `8` | Minimum password length (enforced in firmware) |

---

## File Structure

The auth implementation is split into two files following the `sauna_logic.h` / `main.cpp` precedent:

| File | Dependencies | Purpose |
|---|---|---|
| `src/auth_logic.h` | None (portable C++) | Session store struct/operations, token issuance logic, SHA-256 credential verification, constant-time comparison, fallback decision logic — **natively unit-testable** |
| `src/auth.h` | Arduino, Preferences, HTTPClient | NVS user CRUD (`Preferences` namespace `sauna_auth`), external adapter HTTP calls, `requireAdmin()` guard using `WebServer` — **device-only, not in native tests** |

This separation means `test/test_auth/` can test all portable logic natively. NVS CRUD and HTTP adapter calls require device testing or a mock layer (out of scope for v1 — see Test Coverage section).

---

## Credential Storage

### NVS (namespace: `sauna_auth`)

Up to `AUTH_MAX_USERS` users. Each user occupies 4 keys — 20 keys total at max users, plus `db_url` and `db_key` = 22 keys. The default NVS partition (24 KB, ~100 entries per namespace) is sufficient.

Open the `sauna_auth` Preferences object separately from the main `sauna` namespace and close it before opening `sauna` to avoid exceeding the NVS handle limit.

| Key pattern | Type | Content |
|---|---|---|
| `u0_name` … `u4_name` | string (32) | Username |
| `u0_hash` … `u4_hash` | string (64) | SHA-256 hex of (salt_bytes ‖ pass_bytes) |
| `u0_salt` … `u4_salt` | string (32) | 16-byte random salt, hex-encoded |
| `u0_role` … `u4_role` | string (16) | `"admin"` (reserved for future roles) |

Note: key naming uses a longer descriptive convention (`u0_name`) rather than the 2–3 char style of the `sauna` namespace (`csp`, `bsp`). This is intentional — the namespaces are separate; clarity is preferred here.

- **Slot 0** is the emergency admin — un-deletable via API (`DELETE /users` returns 403 for slot-0 username)
- Password change (`PUT /users`) IS permitted for slot 0 to allow rotation
- Credentials seeded from `secrets.h` defines `AUTH_ADMIN_USER` / `AUTH_ADMIN_PASS` on first boot. `secrets.h` **must** define both or the build fails:
  ```cpp
  #ifndef AUTH_ADMIN_USER
  #error "AUTH_ADMIN_USER must be defined in secrets.h"
  #endif
  #ifndef AUTH_ADMIN_PASS
  #error "AUTH_ADMIN_PASS must be defined in secrets.h"
  #endif
  ```
- Seeding writes once only — skipped if `u0_name` key already exists in NVS
- Seeding occurs in `setup()` **after** `WiFi.begin()` returns connected, so `esp_random()` has full entropy for salt generation
- Hashing: `SHA-256(salt_bytes ‖ password_bytes)` via `mbedtls_sha256()` — no new library needed
- **Security note:** SHA-256+salt is a pragmatic choice given ESP32 memory/CPU constraints that rule out bcrypt/scrypt. The NVS hash is only as strong as physical access to the device is controlled. This is acceptable for the LAN-only threat model.

### External Adapter (optional)

A thin REST service run by the user on their home server. The device holds only:

| NVS key | Type | Description |
|---|---|---|
| `db_url` | string (128) | Adapter base URL, e.g. `http://192.168.1.10:5000` |
| `db_key` | string (64) | Shared API key (`Authorization: Bearer <db_key>`) |

Configurable via `POST /config/save`. If `db_url` is empty, external path is skipped entirely.

#### Adapter Contract

```
POST /validate      {username, password}         → {valid: bool, role: string}
GET  /users         (bearer db_key)              → [{username, role}, ...]
POST /users         {username, salt, hash, role}  → {ok: bool, error?: string}
DELETE /users/:name (bearer db_key)              → {ok: bool}
PUT /users/:name    {salt, hash}                 → {ok: bool}
```

Device sends pre-computed salt+hash on create/update — adapter stores what it receives.

#### Fallback Logic

```
Login attempt
  ├─ db_url set?
  │     yes → POST /validate to adapter
  │             ├─ 200 {valid:true}  → issue token ✓
  │             ├─ 200 {valid:false} → 401 ✗  (deliberate rejection — no NVS fallback)
  │             └─ timeout/error     → fall through to NVS
  └─ NVS SHA-256 verify
        ├─ match    → issue token ✓
        └─ no match → 401 ✗
```

A deliberate `{valid:false}` from the adapter does **not** fall through to NVS — it means the DB was reachable and rejected the credentials. Only network-level failures (timeout, connection refused, non-200 HTTP status) trigger the NVS fallback.

#### Reference Adapter (Python/Flask, home server)

```python
from flask import Flask, request, jsonify
import psycopg2, hashlib, os, binascii

app = Flask(__name__)
API_KEY  = os.environ["ADAPTER_KEY"]
DATABASE = os.environ["DATABASE_URL"]

def require_key():
    return request.headers.get("Authorization","") == f"Bearer {API_KEY}"

@app.post("/validate")
def validate():
    if not require_key(): return jsonify({"valid": False}), 401
    d = request.json
    # fetch row, compute SHA-256(salt+pass), compare hash
    ...

@app.get("/users")
def list_users():
    if not require_key(): return jsonify([]), 401
    # return [{username, role}] list, never hashes
    ...
```

#### DB Schema

```sql
CREATE TABLE sauna_users (
    username   VARCHAR(32) PRIMARY KEY,
    salt       VARCHAR(32) NOT NULL,
    hash       VARCHAR(64) NOT NULL,
    role       VARCHAR(16) NOT NULL DEFAULT 'admin',
    updated_at TIMESTAMP   DEFAULT NOW()
);
```

---

## Route Access Control

### Guard Function

```cpp
void addSecurityHeaders() {
    server.sendHeader("X-Frame-Options", "DENY");
    server.sendHeader("X-Content-Type-Options", "nosniff");
}

bool requireAdmin() {
    addSecurityHeaders();
    if (!server.hasHeader("Authorization")) {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return false;
    }
    String auth = server.header("Authorization");
    if (!auth.startsWith("Bearer ")) {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return false;
    }
    if (!validateToken(auth.substring(7))) {
        server.send(401, "application/json", "{\"error\":\"token_invalid\"}");
        return false;
    }
    return true;
}

// Usage — one line added per protected handler:
void handleSetpoint() {
    if (!requireAdmin()) return;
    // ... existing code unchanged ...
}
```

**CSRF:** All admin routes require an explicit `Authorization: Bearer` header — not a cookie. Browsers cannot set custom headers on cross-origin requests, which inherently prevents CSRF. Future maintainers must not switch to cookie-based auth without adding CSRF tokens.

### Complete Route Table

| Method | Path | Access | Change |
|---|---|---|---|
| GET | `/` | Public | Add security headers |
| GET | `/history` | Public | Add security headers |
| GET | `/auth/login` | Public | New — serves `login.html` (HTML page, client-side redirect on valid token) |
| POST | `/auth/login` | Public | New — issues token |
| POST | `/auth/logout` | Admin | New — invalidates token; client treats 401 as success (token already expired) |
| GET | `/auth/status` | Admin | New — `{valid, username, role}` |
| GET | `/users` | Admin | New — list users (names + roles, no hashes) |
| POST | `/users` | Admin | New — create user; enforces `AUTH_MIN_PASS_LEN` |
| DELETE | `/users` | Admin | New — delete user (`?username=X`); 403 for slot-0 |
| PUT | `/users` | Admin | New — change password (`?username=X`); enforces `AUTH_MIN_PASS_LEN`; permitted for slot-0 |
| GET | `/log` | Admin | Add `requireAdmin()` |
| GET | `/delete/status` | Admin | Add `requireAdmin()` |
| GET | `/delete/control` | Admin | Add `requireAdmin()` |
| GET | `/setpoint` | Admin | Add `requireAdmin()` |
| GET | `/pid` | Admin | Add `requireAdmin()` |
| GET | `/motor` | Admin | Add `requireAdmin()` |
| GET | `/config` | Admin | Add `requireAdmin()`; serves `config.html` (HTML) — client redirects to login on 401 |
| GET | `/config/get` | Admin | Add `requireAdmin()` |
| POST | `/config/save` | Admin | Add `requireAdmin()` |
| GET | `/ota/status` | Admin | Add `requireAdmin()` |
| POST | `/ota/update` | Admin | Add `requireAdmin()` |

WebSocket (port 81): **Public** — broadcast-only, no inbound commands.

Note: `DELETE /users` uses `?username=X` query parameter (not path segment) — consistent with existing codebase convention (`/motor?motor=...`).

---

## Web UI Changes

### New: `data/login.html`

~3 KB standalone page, no external dependencies:
- Username + password form
- `POST /auth/login` on submit
- On success: store token in `sessionStorage['sauna_token']`, redirect to `/`
- On failure: inline error message
- On load: if valid token already in `sessionStorage` (`GET /auth/status` returns 200), redirect to `/` immediately

### Modified: `data/index.html`

**On page load:**
1. Read `sauna_token` from `sessionStorage`
2. `GET /auth/status` with token — if 401, redirect to `/auth/login`
3. Display username + **Logout** button (top-right)

**`authFetch` wrapper** replaces all direct `fetch()` calls to admin endpoints:

```js
function authFetch(url, options = {}) {
    const token = sessionStorage.getItem('sauna_token');
    options.headers = {
        ...options.headers,
        'Authorization': `Bearer ${token}`
    };
    return fetch(url, options).then(r => {
        if (r.status === 401) {
            sessionStorage.removeItem('sauna_token');
            window.location.href = '/auth/login';
        }
        return r;
    });
}

// Logout — treat 401 as success (token was already expired)
function logout() {
    authFetch('/auth/logout', { method: 'POST' }).finally(() => {
        sessionStorage.removeItem('sauna_token');
        window.location.href = '/auth/login';
    });
}
```

All existing `fetch()` calls to admin endpoints updated to `authFetch()`. Public endpoints (`/history`, WebSocket) continue using plain `fetch()`.

**New Users card** (admin section):
- List current users (username + role)
- Add user form (username + password, with client-side length hint ≥ 8 chars)
- Delete button per user (hidden/disabled for slot-0 emergency admin)
- Change password form (available for all users including slot-0)

### Modified: `data/config.html`

Same `authFetch` pattern; redirect to login on 401; logout button.

### Token Storage

`sessionStorage` (not `localStorage`) — token cleared when browser tab closes, limiting exposure window on shared devices. `sessionStorage` is not accessible cross-origin, preventing XSS from other tabs.

---

## New Files Summary

| File | Location | Purpose |
|---|---|---|
| `auth_logic.h` | `src/auth_logic.h` | Portable: session store, token ops, SHA-256 verification, constant-time compare — **natively unit-testable** |
| `auth.h` | `src/auth.h` | ESP32-specific: NVS user CRUD, external adapter calls, `requireAdmin()` guard |
| `login.html` | `data/login.html` | Login page served from LittleFS |

All other changes are additive modifications to `src/main.cpp`, `data/index.html`, and `data/config.html`.

---

## New Unit Tests

### `test/test_auth/` (native — tests `auth_logic.h` only)

**Session / token:**
- Token issuance populates a session slot correctly
- Issued token validates successfully
- Wrong token is rejected
- Expired token rejected (rollover-safe: `millis() - issued_ms > TTL`)
- Expiry check at exactly TTL boundary (not expired); one ms over (expired)
- Token expiry across 32-bit `millis()` rollover boundary
- Token invalidation on logout (slot marked inactive)
- Slot eviction: oldest-issued slot displaced when all 10 slots hold valid tokens
- Slot eviction: expired slot reclaimed before displacing valid session

**Credential verification:**
- Correct password verifies against stored SHA-256+salt hash
- Wrong password rejected
- Empty password rejected (min length check)
- Password below `AUTH_MIN_PASS_LEN` rejected
- Constant-time comparator returns true for equal tokens, false for differing tokens

**Fallback logic (mockable interface):**
- Adapter success (`{valid:true}`) → token issued, NVS not consulted
- Adapter hard rejection (`{valid:false}`) → 401, NVS not consulted (no fallthrough)
- Adapter timeout/error → falls through to NVS; NVS match succeeds
- Adapter timeout/error → falls through to NVS; NVS mismatch → 401
- `db_url` empty → NVS consulted directly, no adapter call

**User management (portable logic only — NVS I/O is mocked):**
- Max user limit enforced (6th user rejected)
- Slot-0 delete attempt rejected with error code
- Slot-0 password change permitted

### Not covered by native tests (requires device or mock framework)

- NVS `Preferences` read/write round-trips (`sauna_auth` namespace)
- External adapter HTTP calls via `HTTPClient`
- `requireAdmin()` / `server.header()` interaction (requires `WebServer`)

### Manual Integration Checklist

- `server.collectHeaders({"Authorization"}, 1)` registered before `server.begin()` — verify by confirming a protected route returns 200 (not 401) when given a valid token
- `esp_random()` salt generation occurs after WiFi connects — verify by checking serial log order in `setup()`
- Emergency admin slot seeded correctly from `secrets.h` on first boot

---

## Out of Scope

- HTTPS/TLS on the device (use reverse proxy)
- Role differentiation beyond `admin` (reserved for future)
- Login rate limiting / brute-force lockout — known gap; an attacker on the LAN can attempt passwords at loop frequency. Acceptable for v1 given LAN-only threat model; add in v2 (5 failures per IP in 60 seconds → 60-second lockout, ~30 lines of code)
- Multi-device session sharing
- Password complexity beyond minimum length (8 chars enforced; complexity rules are UI hints only)
