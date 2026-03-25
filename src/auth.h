#pragma once
#include "auth_logic.h"
#include "influx.h"
#include <Preferences.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <InfluxDbClient.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>

// ── Forward declarations for globals defined in main.cpp ──────────────────
extern WebServer server;
extern Point     webaccess;
extern InfluxDBClient influxClient;
extern char      g_device_name[25];

// ── mbedtls SHA-256 wrapper (used as AuthHashFn) ─────────────────────────
inline void mbedHashFn(const uint8_t *data, size_t len, uint8_t *out32) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256
    mbedtls_sha256_update(&ctx, data, len);
    mbedtls_sha256_finish(&ctx, out32);
    mbedtls_sha256_free(&ctx);
}

// ── esp_random wrapper (used as AuthRandFn) ───────────────────────────────
inline void espRandFn(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i += 4) {
        uint32_t r = esp_random();
        size_t chunk = (len - i < 4) ? (len - i) : 4;
        memcpy(buf + i, &r, chunk);
    }
}

// ── Runtime state (defined in main.cpp) ───────────────────────────────────
extern AuthSession    g_auth_sessions[AUTH_MAX_SESSIONS];
extern AuthUserStore  g_auth_users;
extern char           g_db_url[128];   // external adapter URL (empty = disabled)
extern char           g_db_key[64];    // adapter API key

// ── NVS user store ────────────────────────────────────────────────────────
// Namespace: sauna_auth   Keys: u0_name, u0_hash, u0_salt, u0_role … u4_*
// plus db_url, db_key

inline void authNvsLoad(AuthUserStore *store) {
    Preferences prefs;
    prefs.begin("sauna_auth", true);  // read-only
    store->count = 0;
    for (int i = 0; i < AUTH_MAX_USERS; i++) {
        char keyName[12], keyHash[12], keySalt[12], keyRole[12], keyIter[12];
        snprintf(keyName, sizeof(keyName), "u%d_name", i);
        if (!prefs.isKey(keyName)) break;
        AuthUser &u = store->users[i];
        memset(&u, 0, sizeof(AuthUser));
        prefs.getString(keyName,                    u.name, 33);
        snprintf(keyHash, sizeof(keyHash), "u%d_hash", i);
        prefs.getString(keyHash,                    u.hash, 65);
        snprintf(keySalt, sizeof(keySalt), "u%d_salt", i);
        prefs.getString(keySalt,                    u.salt, 33);
        snprintf(keyRole, sizeof(keyRole), "u%d_role", i);
        prefs.getString(keyRole,                    u.role, 17);
        snprintf(keyIter, sizeof(keyIter), "u%d_iter", i);
        u.iterations = prefs.isKey(keyIter) ? (uint16_t)prefs.getUShort(keyIter, 0) : 0;
        u.active = true;
        store->count++;
    }
    prefs.end();
}

inline void authNvsSave(const AuthUserStore *store) {
    Preferences prefs;
    prefs.begin("sauna_auth", false);
    for (int i = 0; i < store->count; i++) {
        char keyName[12], keyHash[12], keySalt[12], keyRole[12], keyIter[12];
        snprintf(keyName, sizeof(keyName), "u%d_name", i);
        snprintf(keyHash, sizeof(keyHash), "u%d_hash", i);
        snprintf(keySalt, sizeof(keySalt), "u%d_salt", i);
        snprintf(keyRole, sizeof(keyRole), "u%d_role", i);
        snprintf(keyIter, sizeof(keyIter), "u%d_iter", i);
        prefs.putString(keyName, store->users[i].name);
        prefs.putString(keyHash, store->users[i].hash);
        prefs.putString(keySalt, store->users[i].salt);
        prefs.putString(keyRole, store->users[i].role);
        prefs.putUShort(keyIter, store->users[i].iterations);
    }
    // Clear name keys beyond current count — authNvsLoad breaks on first missing u{i}_name,
    // so orphaned hash/salt/role keys for removed slots are never read.
    for (int i = store->count; i < AUTH_MAX_USERS; i++) {
        char keyName[12];
        snprintf(keyName, sizeof(keyName), "u%d_name", i);
        if (prefs.isKey(keyName)) prefs.remove(keyName);
    }
    prefs.end();
}

inline void authNvsLoadConfig(char *out_db_url, char *out_db_key) {
    Preferences prefs;
    prefs.begin("sauna_auth", true);
    if (prefs.isKey("db_url")) prefs.getString("db_url", out_db_url, 128);
    if (prefs.isKey("db_key")) prefs.getString("db_key", out_db_key, 64);
    prefs.end();
}

inline void authNvsSaveConfig(const char *db_url, const char *db_key) {
    Preferences prefs;
    prefs.begin("sauna_auth", false);
    prefs.putString("db_url", db_url);
    prefs.putString("db_key", db_key);
    prefs.end();
}

// ── Emergency admin seeding (call after WiFi connects for entropy) ────────
// Guarded by #error in main.cpp if AUTH_ADMIN_USER/PASS not defined
inline void authSeedEmergencyAdmin(AuthUserStore *store) {
    Preferences prefs;
    prefs.begin("sauna_auth", true);
    bool seeded = prefs.isKey("u0_name");
    prefs.end();
    if (seeded) return;  // already written — do not overwrite
    memset(store, 0, sizeof(AuthUserStore));
    authAddUser(store, AUTH_ADMIN_USER, AUTH_ADMIN_PASS, "admin",
                espRandFn, mbedHashFn);
    authNvsSave(store);
    Serial.println("Auth: emergency admin seeded");
}

// ── External adapter call ─────────────────────────────────────────────────
inline AdapterResult authCallAdapter(const char *username,
                                      const char *password,
                                      const char *db_url,
                                      const char *db_key,
                                      char *out_role) {
    if (!db_url || db_url[0] == '\0') return ADAPTER_ERROR;
    if (!authAdapterUrlValid(db_url)) return ADAPTER_ERROR;  // reject non-HTTPS
    HTTPClient http;
    char url[160];
    snprintf(url, sizeof(url), "%s/validate", db_url);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + db_key);
    http.setTimeout(3000);
    JsonDocument body;
    body["username"] = username;
    body["password"] = password;
    String bodyStr;
    serializeJson(body, bodyStr);
    int code = http.POST(bodyStr);
    if (code != 200) { http.end(); return ADAPTER_ERROR; }
    String resp = http.getString();
    http.end();
    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return ADAPTER_ERROR;
    bool valid = doc["valid"] | false;
    if (!valid) return ADAPTER_REJECTED;
    const char *role = doc["role"] | "";  // empty if missing — caller must validate; never default to a privilege level
    strncpy(out_role, role, 16); out_role[16] = '\0';
    return ADAPTER_OK;
}

// Adapter shim matching the AdapterFn signature
struct AdapterCtx { const char *db_url; const char *db_key; };

inline AdapterResult adapterShim(const char *username, const char *password,
                                   char *out_role, void *ctx) {
    AdapterCtx *c = (AdapterCtx *)ctx;
    return authCallAdapter(username, password, c->db_url, c->db_key, out_role);
}

// ── requireAdmin() guard ──────────────────────────────────────────────────
inline void authAddSecurityHeaders() {
    server.sendHeader("X-Frame-Options",        "DENY");
    server.sendHeader("X-Content-Type-Options", "nosniff");
    server.sendHeader("Content-Security-Policy",
        "default-src 'self'; "
        "script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net; "
        "style-src 'self' 'unsafe-inline'; "
        "img-src 'self' data:; "
        "connect-src 'self' ws:");
}

// Returns the validated session pointer, or nullptr (and sends 401) on failure
inline const AuthSession *requireAdmin() {
    authAddSecurityHeaders();
    if (!server.hasHeader("Authorization")) {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return nullptr;
    }
    String auth = server.header("Authorization");
    if (!auth.startsWith("Bearer ")) {
        server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
        return nullptr;
    }
    String token = auth.substring(7);
    uint32_t now = millis();
    const AuthSession *s = authValidateToken(g_auth_sessions, AUTH_MAX_SESSIONS,
                                              token.c_str(), now, AUTH_TOKEN_TTL_MS);
    if (!s) {
        server.send(401, "application/json", "{\"error\":\"token_invalid\"}");
        return nullptr;
    }
    return s;
}

// logAccessEvent() is now in influx.h — included above
