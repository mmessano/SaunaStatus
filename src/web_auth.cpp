// src/web_auth.cpp — auth and user-management HTTP handlers extracted from web.cpp
// This file is Arduino-only; it must not be compiled in native test builds.
#ifdef ARDUINO

#include "secrets.h"
#include "web.h"
#include "web_internal.h"
#include "auth.h"
#include "influx.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

void handleAuthLoginPage() {
    authAddSecurityHeaders();
    if (!ensureLittleFsMounted()) return;
    File f = LittleFS.open("/login.html", "r");
    if (!f) { server.send(404, "text/plain", "login.html not found"); return; }
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(f, "text/html");
    f.close();
}

void handleAuthLogin() {
    authAddSecurityHeaders();
    server.sendHeader("Cache-Control", "no-store");

    uint32_t clientIp = (uint32_t)server.client().remoteIP();
    uint32_t ipHash = authIpHash(clientIp);
    if (rateLimitIsLocked(&g_rate_limiter, ipHash, millis())) {
        server.send(429, "application/json", "{\"error\":\"too many attempts, try again later\"}");
        return;
    }

    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
    }
    const char *username = doc["username"] | "";
    const char *password = doc["password"] | "";
    bool adapterConfigured = g_db_url[0] != '\0';
    AdapterCtx ctx{ g_db_url, g_db_key };
    LoginOutcome out = authAttemptLogin(username, password,
                                        adapterConfigured,
                                        adapterConfigured ? adapterShim : nullptr,
                                        adapterConfigured ? &ctx : nullptr,
                                        &g_auth_users, mbedHashFn);
    const char *srcStr = (out.source == AUTH_SRC_ADAPTER) ? "adapter" : "nvs";
    if (out.result != LOGIN_OK) {
        bool locked = rateLimitRecordFailure(&g_rate_limiter, ipHash, millis());
        logAccessEvent("login_failure", username, srcStr,
                       server.client().remoteIP().toString().c_str());
        if (locked) {
            server.send(429, "application/json", "{\"error\":\"too many attempts, try again later\"}");
        } else {
            server.send(401, "application/json", "{\"error\":\"invalid credentials\"}");
        }
        return;
    }

    rateLimitClear(&g_rate_limiter, ipHash);
    char token[65];
    authIssueToken(g_auth_sessions, AUTH_MAX_SESSIONS,
                   username, out.role, millis(), espRandFn, token);
    logAccessEvent("login_success", username, srcStr,
                   server.client().remoteIP().toString().c_str());
    JsonDocument resp;
    resp["token"]      = token;
    resp["expires_in"] = AUTH_TOKEN_TTL_MS / 1000;
    resp["username"]   = username;
    resp["role"]       = out.role;
    String body; serializeJson(resp, body);
    server.send(200, "application/json", body);
}

void handleAuthLogout() {
    authAddSecurityHeaders();
    const AuthSession *s = requireSession();
    if (!s) return;
    char username[33];
    strncpy(username, s->username, 32);
    username[32] = '\0';
    String auth = server.header("Authorization");
    String token = auth.substring(7);
    authInvalidateToken(g_auth_sessions, AUTH_MAX_SESSIONS, token.c_str());
    logAccessEvent("logout", username, "none",
                   server.client().remoteIP().toString().c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleAuthStatus() {
    authAddSecurityHeaders();
    const AuthSession *s = requireSession();
    if (!s) return;
    JsonDocument doc;
    doc["valid"]    = true;
    doc["username"] = s->username;
    doc["role"]     = s->role;
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

void handleUsersGet() {
    if (!requireAdmin()) return;
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < g_auth_users.count; i++) {
        JsonObject u = arr.add<JsonObject>();
        u["username"]  = g_auth_users.users[i].name;
        u["role"]      = g_auth_users.users[i].role;
        u["protected"] = (i == 0);
    }
    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

void handleUsersCreate() {
    if (!requireAdmin()) return;
    if (!server.hasArg("plain")) { server.send(400, "application/json", "{\"error\":\"no body\"}"); return; }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
    }
    const char *username = doc["username"] | "";
    const char *password = doc["password"] | "";
    const char *role     = doc["role"] | "";
    if (strcmp(role, "admin") != 0 && strcmp(role, "viewer") != 0 && strcmp(role, "") != 0) {
        server.send(400, "application/json", "{\"error\":\"invalid role\"}");
        return;
    }
    AuthUserResult r = authAddUser(&g_auth_users, username, password, role,
                                   espRandFn, mbedHashFn);
    if (r == AUTH_USER_BAD_NAME) { server.send(400, "application/json", "{\"error\":\"invalid username\"}"); return; }
    if (r == AUTH_USER_BAD_PASS) { server.send(400, "application/json", "{\"error\":\"password too short\"}"); return; }
    if (r == AUTH_USER_FULL)     { server.send(409, "application/json", "{\"error\":\"user limit reached\"}"); return; }
    if (r == AUTH_USER_EXISTS)   { server.send(409, "application/json", "{\"error\":\"username taken\"}"); return; }
    authNvsSave(&g_auth_users);
    logAccessEvent("user_create", username, "admin_action",
                   server.client().remoteIP().toString().c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleUsersDelete() {
    if (!requireAdmin()) return;
    if (!server.hasArg("username")) { server.send(400, "application/json", "{\"error\":\"missing username\"}"); return; }
    String username = server.arg("username");
    AuthUserResult r = authDeleteUser(&g_auth_users, username.c_str(), 0);
    if (r == AUTH_USER_PROTECTED) { server.send(403, "application/json", "{\"error\":\"cannot delete emergency admin\"}"); return; }
    if (r == AUTH_USER_NOT_FOUND) { server.send(404, "application/json", "{\"error\":\"user not found\"}"); return; }
    authNvsSave(&g_auth_users);
    logAccessEvent("user_delete", username.c_str(), "admin_action",
                   server.client().remoteIP().toString().c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleUsersChangePassword() {
    if (!requireAdmin()) return;
    if (!server.hasArg("username") || !server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"missing fields\"}");
        return;
    }
    String username = server.arg("username");
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }
    const char *newPass = doc["password"] | "";
    AuthUserResult r = authChangePassword(&g_auth_users, username.c_str(),
                                          newPass, espRandFn, mbedHashFn);
    if (r == AUTH_USER_BAD_PASS)  { server.send(400, "application/json", "{\"error\":\"password too short\"}"); return; }
    if (r == AUTH_USER_NOT_FOUND) { server.send(404, "application/json", "{\"error\":\"user not found\"}"); return; }
    authNvsSave(&g_auth_users);
    logAccessEvent("password_change", username.c_str(), "admin_action",
                   server.client().remoteIP().toString().c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

#endif // ARDUINO
