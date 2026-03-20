# Auth & Access Control Design
**Date:** 2026-03-20
**Project:** SaunaStatus (ESP32)
**Status:** Approved

---

## Overview

Add user authentication and role-based access control to the SaunaStatus web interface. The site is split into public read-only content (live dashboard, temperature history, WebSocket state stream) and admin content (all controls, configuration, OTA, data management). Admin access requires a bearer token obtained via login with username + password.

---

## Requirements

- Strict public/admin split — read-only views are unauthenticated; all mutating or sensitive endpoints require a valid bearer token
- Multiple users — up to 5, stored locally in NVS with SHA-256+salt hashing (mbedtls, already linked)
- One permanent emergency admin in NVS slot 0 — cannot be deleted; credentials seeded from `secrets.h` at first boot
- Optional external credential store — a thin REST adapter in front of PostgreSQL/MySQL; device never holds DB credentials
- Fallback to NVS when external adapter is unreachable or not configured
- No TLS on the device — reverse proxy handles it if external exposure is ever needed
- Defense-in-depth response headers on all routes
- Stay on synchronous `WebServer` library — async refactor not justified by workload
- WebSocket (port 81) stays public — broadcast-only, read-only state stream

---

## Transport & Server

| Decision | Choice | Rationale |
|---|---|---|
| TLS on device | No | Memory pressure (~40–60 KB per TLS handshake), library switch required, LAN-only threat model |
| HTTP server | `WebServer` (synchronous, Arduino built-in) | Workload is low; 13-handler refactor to AsyncWebServer not justified |
| TLS if external | Reverse proxy (nginx/Caddy) | Handles TLS termination without touching device firmware |
| Security headers | Yes, all responses | `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`, `Cache-Control: no-store` on auth endpoints |

---

## Authentication Mechanism

**Bearer token, in-memory session store.**

```
POST /auth/login  {username, password}
  → validate against external adapter (if configured), fallback to NVS
  → on success: issue 64-char hex token (32 bytes from esp_random())
  → response: {token, expires_in, username, role}

All admin requests:
  Authorization: Bearer <token>
  → validateToken() scans g_sessions[], checks expiry
  → 401 if not found or expired

POST /auth/logout
  → invalidates token slot (zeroed, active=false)
```

### Session Store

Fixed array, no heap allocation:

```cpp
struct AuthSession {
    char     token[65];    // 64-char hex + null
    char     username[33]; // max 32 chars + null
    char     role[17];     // "admin" + null
    uint32_t expires_ms;   // millis() at expiry
    bool     active;
};

static AuthSession g_sessions[AUTH_MAX_SESSIONS]; // default 10
```

Memory: ~120 bytes × 10 slots = ~1.2 KB.

### Build-Flag Tunables

All `#ifndef`-guarded, overridable via `platformio.ini`:

| Define | Default | Description |
|---|---|---|
| `AUTH_TOKEN_TTL_MS` | `3600000UL` | Token lifetime (1 hour) |
| `AUTH_MAX_SESSIONS` | `10` | Max concurrent sessions |
| `AUTH_MAX_USERS` | `5` | Max users in NVS |

---

## Credential Storage

### NVS (namespace: `sauna_auth`)

Up to `AUTH_MAX_USERS` users. Each user occupies 4 keys:

| Key pattern | Type | Content |
|---|---|---|
| `u0_name` … `u4_name` | string (32) | Username |
| `u0_hash` … `u4_hash` | string (64) | SHA-256 hex of (salt_bytes ‖ pass_bytes) |
| `u0_salt` … `u4_salt` | string (32) | 16-byte random salt, hex-encoded |
| `u0_role` … `u4_role` | string (16) | `"admin"` (reserved for future roles) |

- **Slot 0** is the emergency admin — un-deletable via API or UI
- Credentials seeded from `secrets.h` defines `AUTH_ADMIN_USER` / `AUTH_ADMIN_PASS` on first boot (written once; not overwritten if key already exists)
- Hashing: `SHA-256(salt_bytes + password_bytes)` via `mbedtls_sha256()` — no new library needed

### External Adapter (optional)

A thin REST service run by the user on their home server. The device holds only:

| NVS key | Type | Description |
|---|---|---|
| `db_url` | string (128) | Adapter base URL, e.g. `http://192.168.1.10:5000` |
| `db_key` | string (64) | Shared API key (`Authorization: Bearer <db_key>`) |

If `db_url` is empty, external path is skipped.

#### Adapter Contract

```
POST /validate      {username, password}        → {valid: bool, role: string}
GET  /users         (bearer db_key)             → [{username, role}, ...]
POST /users         {username, salt, hash, role} → {ok: bool, error?: string}
DELETE /users/:name (bearer db_key)             → {ok: bool}
PUT /users/:name    {salt, hash}                → {ok: bool}
```

Device sends pre-computed salt+hash on create/update — adapter stores what it receives.

#### Fallback Logic

```
Login attempt
  ├─ db_url set?
  │     yes → POST /validate to adapter
  │             ├─ 200 {valid:true}  → issue token ✓
  │             ├─ 200 {valid:false} → 401 ✗  (no NVS fallback — DB said no)
  │             └─ timeout/error     → fall through to NVS
  └─ NVS SHA-256 verify
        ├─ match    → issue token ✓
        └─ no match → 401 ✗
```

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

### Complete Route Table

| Method | Path | Access | Change |
|---|---|---|---|
| GET | `/` | Public | Add security headers |
| GET | `/history` | Public | Add security headers |
| GET | `/auth/login` | Public | New — serves `login.html` |
| POST | `/auth/login` | Public | New — issues token |
| POST | `/auth/logout` | Admin | New — invalidates token |
| GET | `/auth/status` | Admin | New — `{valid, username, role}` |
| GET | `/users` | Admin | New — list users |
| POST | `/users` | Admin | New — create user |
| DELETE | `/users` | Admin | New — delete user (`?username=X`) |
| PUT | `/users` | Admin | New — change password |
| GET | `/log` | Admin | Add `requireAdmin()` |
| GET | `/delete/status` | Admin | Add `requireAdmin()` |
| GET | `/delete/control` | Admin | Add `requireAdmin()` |
| GET | `/setpoint` | Admin | Add `requireAdmin()` |
| GET | `/pid` | Admin | Add `requireAdmin()` |
| GET | `/motor` | Admin | Add `requireAdmin()` |
| GET | `/config` | Admin | Add `requireAdmin()` |
| GET | `/config/get` | Admin | Add `requireAdmin()` |
| POST | `/config/save` | Admin | Add `requireAdmin()` |
| GET | `/ota/status` | Admin | Add `requireAdmin()` |
| POST | `/ota/update` | Admin | Add `requireAdmin()` |

WebSocket (port 81): **Public** — broadcast-only, no inbound commands.

---

## Web UI Changes

### New: `data/login.html`

~3 KB standalone page, no external dependencies:
- Username + password form
- `POST /auth/login` on submit
- On success: store token in `sessionStorage['sauna_token']`, redirect to `/`
- On failure: inline error message
- On load: if valid token already in `sessionStorage`, redirect to `/` immediately

### Modified: `data/index.html`

**On page load:**
1. Read `sauna_token` from `sessionStorage`
2. `GET /auth/status` with token — if 401, redirect to `/auth/login`
3. Display username + **Logout** button (top-right)

**`authFetch` wrapper** replaces all direct `fetch()` calls:

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
```

All existing `fetch()` calls to admin endpoints updated to `authFetch()`.

**New Users card** (admin section):
- List current users (username + role)
- Add user form (username + password)
- Delete button per user (disabled for slot-0 emergency admin)
- Change password form

### Modified: `data/config.html`

Same `authFetch` pattern; redirect to login on 401; logout button.

### Token Storage

`sessionStorage` (not `localStorage`) — token cleared when browser tab closes, limiting exposure window on shared devices.

---

## New Files Summary

| File | Location | Purpose |
|---|---|---|
| `auth.h` | `src/auth.h` | Session store, token issuance/validation, SHA-256 helpers, NVS user CRUD, external adapter HTTP calls |
| `login.html` | `data/login.html` | Login page served from LittleFS |

All other changes are additive modifications to `src/main.cpp`, `data/index.html`, and `data/config.html`.

---

## New Unit Tests

### `test/test_auth/` (native, no device required)

- Token issuance and validation
- Token expiry (expired token rejected)
- Token invalidation on logout
- Session slot eviction (oldest expired slot reclaimed when full)
- SHA-256 credential verification (correct password accepted, wrong password rejected)
- NVS user CRUD (create, list, delete, update password)
- Emergency admin slot 0 cannot be deleted
- `requireAdmin()` rejects missing header, malformed header, invalid token, expired token
- Fallback logic: adapter timeout falls through to NVS; adapter `{valid:false}` does not fall through
- Max user limit enforced

---

## Out of Scope

- HTTPS/TLS on the device (use reverse proxy)
- Role differentiation beyond `admin` (reserved for future)
- Rate limiting on `/auth/login` (nice-to-have; not in v1)
- Multi-device session sharing
- Password complexity enforcement (UI hint only, not enforced in firmware)
