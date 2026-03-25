// src/web.cpp — HTTP and WebSocket handlers extracted from main.cpp
// This file is Arduino-only; it must not be compiled in native test builds.
#ifdef ARDUINO

#include "secrets.h"
#include "web.h"
#include "motor_logic.h"
#include "auth.h"
#include "influx.h"
#include "ota_logic.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <Preferences.h>

// Build-flag overrides for constants used in handlers
#ifndef VENT_STEPS
#define VENT_STEPS 1024
#endif
#ifndef SETPOINT_MIN_F
#define SETPOINT_MIN_F 32.0f
#endif
#ifndef SETPOINT_MAX_F
#define SETPOINT_MAX_F 300.0f
#endif
#ifndef SENSOR_READ_INTERVAL_MIN_MS
#define SENSOR_READ_INTERVAL_MIN_MS 500UL
#endif
#ifndef SENSOR_READ_INTERVAL_MAX_MS
#define SENSOR_READ_INTERVAL_MAX_MS 10000UL
#endif
#ifndef SERIAL_LOG_INTERVAL_MIN_MS
#define SERIAL_LOG_INTERVAL_MIN_MS 1000UL
#endif
#ifndef SERIAL_LOG_INTERVAL_MAX_MS
#define SERIAL_LOG_INTERVAL_MAX_MS 60000UL
#endif
#ifndef WS_JSON_BUF_SIZE
// Worst-case buildJsonFull output ≈ 300 chars (212 fixed + 88 max-width values).
// 384 gives 80+ bytes of headroom for future field additions without silent truncation.
#define WS_JSON_BUF_SIZE 384
#endif
#ifndef WS_MAX_CLIENTS
#define WS_MAX_CLIENTS 8
#endif
#ifndef WS_AUTH_TIMEOUT_MS
#define WS_AUTH_TIMEOUT_MS 5000UL
#endif

// Per-client WebSocket authentication state
static bool     ws_authenticated[WS_MAX_CLIENTS] = {};
static uint32_t ws_connect_time[WS_MAX_CLIENTS]  = {};


// Internal helper — not declared in web.h
static void handleDeleteMeasurement(const char *measurement)
{
  HTTPClient http;
  char url[192];
  snprintf(url, sizeof(url), "%s/api/v2/delete?org=%s&bucket=%s",
           INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET);
  http.begin(url);
  http.addHeader("Authorization", "Token " INFLUXDB_TOKEN);
  http.addHeader("Content-Type", "application/json");
  char body[192];
  snprintf(body, sizeof(body),
           "{\"start\":\"1970-01-01T00:00:00Z\","
           "\"stop\":\"2099-12-31T23:59:59Z\","
           "\"predicate\":\"_measurement=\\\"%s\\\"\"}",
           measurement);
  int code = http.POST(body);
  http.end();
  if (code == 204)
    server.send(200, "text/plain", "OK");
  else
    server.send(500, "text/plain", "Delete failed");
}

void handleRoot()
{
  File f = LittleFS.open("/index.html", "r");
  if (!f)
  {
    server.send(500, "text/plain", "index.html not found");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "text/html");
  f.close();
}

void handleDeleteStatus() { if (!requireAdmin()) return; handleDeleteMeasurement("sauna_status"); }
void handleDeleteControl() { if (!requireAdmin()) return; handleDeleteMeasurement("sauna_control"); }

void handleMotorCmd()
{
  if (!requireAdmin()) return;
  String motor = server.arg("motor");
  String cmd = server.arg("cmd");
  int steps = server.hasArg("steps") ? server.arg("steps").toInt() : 64;
  if (steps < 1 || steps > VENT_STEPS * 4)
    steps = 64;

  CheapStepper *m = nullptr;
  int *tgt = nullptr;
  int *dir = nullptr;
  unsigned short *pos = nullptr;
  int *mx = nullptr;

  if (motor == "outflow")
  {
    m = &outflow;
    tgt = &outflow_target;
    dir = &outflow_dir;
    pos = &outflow_pos;
    mx = &outflow_max_steps;
  }
  else if (motor == "inflow")
  {
    m = &inflow;
    tgt = &inflow_target;
    dir = &inflow_dir;
    pos = &inflow_pos;
    mx = &inflow_max_steps;
  }
  else
  {
    server.send(400, "text/plain", "Bad motor");
    return;
  }

  // Helper lambda-equivalent: move to an absolute target step
  auto moveTo = [&](int dest)
  {
    int d = dest - *tgt;
    if (d > 0)
    {
      *dir = 1;
      m->newMove(true, d);
    }
    else if (d < 0)
    {
      *dir = -1;
      m->newMove(false, -d);
    }
    *tgt = dest;
  };

  if (cmd == "cw")
  {
    int actual = motorClampCW(*tgt, steps, *mx);  // clamp: never exceed max steps
    if (actual > 0)
    {
      *tgt += actual;
      *dir = 1;
      m->newMove(true, actual);
    }
    else
      *dir = 0;  // already at max
  }
  else if (cmd == "ccw")
  {
    int actual = min(steps, *tgt); // floor at 0 — can't step below closed
    if (actual > 0)
    {
      *tgt -= actual;
      *dir = -1;
      m->newMove(false, actual);
    }
    else
      *dir = 0;
  }
  else if (cmd == "open")
  {
    moveTo(*mx);
  }
  else if (cmd == "close")
  {
    moveTo(0);
  }
  else if (cmd == "third")
  {
    moveTo(*mx / 3);
  }
  else if (cmd == "twothird")
  {
    moveTo(*mx * 2 / 3);
  }
  else if (cmd == "zero")
  {
    // Mark current physical position as closed (step 0)
    m->stop();
    *tgt = 0;
    *dir = 0;
  }
  else if (cmd == "setopen")
  {
    // Mark current physical position as fully open
    m->stop();
    if (*tgt > 0)
      *mx = *tgt;
    *dir = 0;
    savePrefs();
  }
  else if (cmd == "stop")
  {
    m->stop();
    *dir = 0;
  }
  else
  {
    server.send(400, "text/plain", "Bad cmd");
    return;
  }

  int p = *mx > 0 ? *tgt * 100 / *mx : 0;
  *pos = (unsigned short)(p < 0 ? 0 : p > 100 ? 100
                                              : p);
  server.send(200, "text/plain", "OK");
}

void handlePidToggle()
{
  if (!requireAdmin()) return;
  if (server.hasArg("ceiling"))
    ceiling_pid_en = server.arg("ceiling") == "1";
  if (server.hasArg("bench"))
    bench_pid_en = server.arg("bench") == "1";
  savePrefs();
  server.send(200, "text/plain", "OK");
}

void handleSetpoint()
{
  if (!requireAdmin()) return;
  if (server.hasArg("ceiling"))
  {
    float f = server.arg("ceiling").toFloat();
    if (f >= SETPOINT_MIN_F && f <= SETPOINT_MAX_F)
      Ceilingpoint = (f - 32.0f) * 5.0f / 9.0f;
  }
  if (server.hasArg("bench"))
  {
    float f = server.arg("bench").toFloat();
    if (f >= SETPOINT_MIN_F && f <= SETPOINT_MAX_F)
      Benchpoint = (f - 32.0f) * 5.0f / 9.0f;
  }
  savePrefs();
  server.send(200, "text/plain", "OK");
}

// Proxies a 1-hour Flux history query to InfluxDB and returns raw CSV.
// The InfluxDB token never leaves the device.
// Optional ?range=Xh (default 1h). Only [0-9a-zA-Z] accepted to prevent injection.
void handleHistory()
{
  String range = server.hasArg("range") ? server.arg("range") : "1h";
  bool valid = range.length() > 0 && range.length() <= 8;
  for (size_t i = 0; valid && i < range.length(); i++)
    if (!isalnum((unsigned char)range[i]))
      valid = false;
  if (!valid)
    range = "1h";

  HTTPClient http;
  char url[192];
  snprintf(url, sizeof(url), "%s/api/v2/query?org=%s", INFLUXDB_URL, INFLUXDB_ORG);
  http.begin(url);
  http.addHeader("Authorization", "Token " INFLUXDB_TOKEN);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/csv");

  char body[640];
  snprintf(body, sizeof(body),
           "{\"query\":\"from(bucket:\\\"%s\\\") |> range(start:-%s)"
           " |> filter(fn:(r) => r._measurement == \\\"sauna_status\\\")"
           " |> filter(fn:(r) => r._field == \\\"ceiling_temp\\\" or r._field == \\\"bench_temp\\\" or r._field == \\\"stove_temp\\\")"
           " |> aggregateWindow(every:5m, fn:mean, createEmpty:false)"
           " |> pivot(rowKey:[\\\"_time\\\"],columnKey:[\\\"_field\\\"],valueColumn:\\\"_value\\\")"
           " |> keep(columns:[\\\"_time\\\",\\\"ceiling_temp\\\",\\\"bench_temp\\\",\\\"stove_temp\\\"])\","
           "\"dialect\":{\"annotations\":[],\"header\":true,\"delimiter\":\",\"}}",
           INFLUXDB_BUCKET, range.c_str());

  int code = http.POST(body);
  if (code == 200)
  {
    server.send(200, "text/csv", http.getString());
  }
  else
  {
    Serial.printf("History query failed: HTTP %d\n", code);
    server.send(502, "text/plain", "InfluxDB query failed");
  }
  http.end();
}

void handleLog()
{
  if (!requireAdmin()) return;
  if (writeInflux())
  {
    Serial.println("InfluxDB manual write OK");
    server.send(200, "text/plain", "OK");
  }
  else
  {
    Serial.print("InfluxDB manual write failed: ");
    Serial.println(influxClient.getLastErrorMessage());
    server.send(500, "text/plain", influxClient.getLastErrorMessage());
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (num >= WS_MAX_CLIENTS) return;

  switch (type) {
    case WStype_CONNECTED:
      ws_authenticated[num] = false;
      ws_connect_time[num]  = millis();
      // Send auth challenge — client must reply with {"token":"..."}
      webSocket.sendTXT(num, "{\"auth_required\":true}");
      break;

    case WStype_TEXT:
      if (!ws_authenticated[num]) {
        // Expect {"token":"<64-char hex>"}
        JsonDocument doc;
        if (deserializeJson(doc, (const char *)payload) != DeserializationError::Ok) {
          webSocket.sendTXT(num, "{\"error\":\"invalid_json\"}");
          webSocket.disconnect(num);
          return;
        }
        const char *token = doc["token"] | "";
        if (token[0] == '\0') {
          webSocket.sendTXT(num, "{\"error\":\"token_missing\"}");
          webSocket.disconnect(num);
          return;
        }
        const AuthSession *s = authValidateToken(
            g_auth_sessions, AUTH_MAX_SESSIONS,
            token, millis(), AUTH_TOKEN_TTL_MS);
        if (!s) {
          webSocket.sendTXT(num, "{\"error\":\"token_invalid\"}");
          webSocket.disconnect(num);
          return;
        }
        ws_authenticated[num] = true;
        // Push current readings now that client is authenticated
        char json[WS_JSON_BUF_SIZE];
        buildJson(json, sizeof(json));
        webSocket.sendTXT(num, json);
      }
      break;

    case WStype_DISCONNECTED:
      ws_authenticated[num] = false;
      ws_connect_time[num]  = 0;
      break;

    default:
      break;
  }
}

// Broadcast to authenticated WebSocket clients only
void wsBroadcastAuthenticated(const char *payload) {
  for (uint8_t i = 0; i < WS_MAX_CLIENTS; i++) {
    if (ws_authenticated[i]) {
      webSocket.sendTXT(i, payload);
    }
  }
}

// Disconnect clients that haven't authenticated within the timeout
void wsCheckAuthTimeouts() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < WS_MAX_CLIENTS; i++) {
    if (!ws_authenticated[i] && ws_connect_time[i] != 0 &&
        (now - ws_connect_time[i]) > WS_AUTH_TIMEOUT_MS) {
      webSocket.sendTXT(i, "{\"error\":\"auth_timeout\"}");
      webSocket.disconnect(i);
      ws_connect_time[i] = 0;
    }
  }
}

void handleOtaStatus()
{
  if (!requireAdmin()) return;
  const esp_partition_t *running = esp_ota_get_running_partition();
  char buf[192];
  snprintf(buf, sizeof(buf),
           "{\"version\":\"%s\",\"partition\":\"%s\",\"boot_failures\":%d}",
           FIRMWARE_VERSION,
           running ? running->label : "unknown",
           []() {
             prefs.begin("sauna", true);
             int f = prefs.getInt("boot_fail", 0);
             prefs.end();
             return f;
           }());
  server.send(200, "application/json", buf);
}

// POST /ota/update?manifest=<url>
// Downloads the JSON manifest, checks version, then streams the firmware binary.
// The partial-download state is persisted to NVS so a power failure is detectable.
void handleOtaUpdate()
{
  if (!requireAdmin()) return;
  if (!server.hasArg("manifest")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing manifest param\"}");
    return;
  }
  String manifestUrl = server.arg("manifest");

  // Fetch manifest
  HTTPClient http;
  http.begin(manifestUrl);
  int code = http.GET();
  if (code != 200) {
    http.end();
    char err[80];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"manifest fetch failed: HTTP %d\"}", code);
    server.send(502, "application/json", err);
    return;
  }
  String body = http.getString();
  http.end();

  OtaManifest manifest = parseOtaManifest(body.c_str());
  if (!manifest.valid) {
    server.send(400, "application/json",
                "{\"ok\":false,\"error\":\"invalid manifest: missing version or url\"}");
    return;
  }

  // Version check — refuse downgrades and same-version re-flashes
  FirmwareVersion current  = parseVersion(FIRMWARE_VERSION);
  FirmwareVersion incoming = parseVersion(manifest.version);
  if (!isUpdateAvailable(current, incoming)) {
    char msg[96];
    snprintf(msg, sizeof(msg),
             "{\"ok\":true,\"updated\":false,\"reason\":\"current %s >= manifest %s\"}",
             FIRMWARE_VERSION, manifest.version);
    server.send(200, "application/json", msg);
    return;
  }

  // Fetch firmware binary
  http.begin(manifest.url);
  int fwCode = http.GET();
  if (fwCode != 200) {
    http.end();
    char err[80];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"firmware fetch failed: HTTP %d\"}", fwCode);
    server.send(502, "application/json", err);
    return;
  }

  int fwSize = http.getSize();
  if (fwSize <= 0) {
    http.end();
    server.send(502, "application/json",
                "{\"ok\":false,\"error\":\"firmware size unknown\"}");
    return;
  }

  if (!Update.begin(fwSize)) {
    http.end();
    char err[96];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"Update.begin failed: %s\"}",
             Update.errorString());
    server.send(500, "application/json", err);
    return;
  }

  if (manifest.md5[0]) Update.setMD5(manifest.md5);

  // Mark download in progress in NVS so a power failure is detectable at next boot
  prefs.begin("sauna", false);
  prefs.putBool("ota_ip", true);
  prefs.putUInt("ota_exp", (unsigned int)fwSize);
  prefs.putUInt("ota_wrt", 0);
  prefs.end();

  Serial.printf("OTA: writing %d bytes from %s\n", fwSize, manifest.url);
  size_t written = Update.writeStream(*http.getStreamPtr());
  http.end();

  // Update NVS with bytes written (best-effort — power may fail here)
  prefs.begin("sauna", false);
  prefs.putUInt("ota_wrt", (unsigned int)written);
  prefs.end();

  if (!Update.end(true) || !Update.isFinished()) {
    char err[96];
    snprintf(err, sizeof(err), "{\"ok\":false,\"error\":\"Update.end failed: %s\"}",
             Update.errorString());
    server.send(500, "application/json", err);
    return;
  }

  // Clear in-progress flag — download completed successfully
  prefs.begin("sauna", false);
  prefs.putBool("ota_ip", false);
  prefs.end();

  Serial.printf("OTA: success (%zu bytes), rebooting to %s\n", written, manifest.version);
  server.send(200, "application/json",
              "{\"ok\":true,\"updated\":true,\"rebooting\":true}");
  delay(500);
  esp_restart();
}

// ---------------------------------------------------------------------------
// Configuration portal handlers
// ---------------------------------------------------------------------------

// GET /config — serve the settings HTML page from LittleFS
void handleConfigPage()
{
  File f = LittleFS.open("/config.html", "r");
  if (!f)
  {
    server.send(500, "text/plain", "config.html not found — upload filesystem image");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "text/html");
  f.close();
}

// GET /config/get — return current config as JSON
void handleConfigGet()
{
  if (!requireAdmin()) return;
  char buf[320];
  snprintf(buf, sizeof(buf),
           "{\"ceiling_setpoint_f\":%.1f,\"bench_setpoint_f\":%.1f,"
           "\"ceiling_pid_en\":%s,\"bench_pid_en\":%s,"
           "\"sensor_read_interval_ms\":%lu,\"serial_log_interval_ms\":%lu,"
           "\"static_ip\":\"%s\",\"device_name\":\"%s\"}",
           c2f(Ceilingpoint), c2f(Benchpoint),
           ceiling_pid_en ? "true" : "false",
           bench_pid_en ? "true" : "false",
           g_sensor_read_interval_ms, g_serial_log_interval_ms,
           g_static_ip_str, g_device_name);
  server.send(200, "application/json", buf);
}

// POST /config/save — validate all fields first, then apply and persist
// Returns: {"ok":true,"restart_required":false} or {"ok":false,"error":"..."}
void handleConfigSave()
{
  if (!requireAdmin()) return;
  bool restart_required = false;

  // Staged values — only applied after all validation passes
  float     new_ceiling_sp = -1.0f;
  float     new_bench_sp   = -1.0f;
  int       new_ceiling_en = -1;     // -1 = not present
  int       new_bench_en   = -1;
  unsigned long new_sri    = 0;      // 0 = not present
  unsigned long new_slg    = 0;
  char      new_ip[16]     = "";
  char      new_dn[25]     = "";
  bool      has_ip         = false;
  bool      has_dn         = false;

  char errmsg[80] = "";

  // --- Validate all inputs before touching any state ---

  if (server.hasArg("ceiling_setpoint_f")) {
    float v = server.arg("ceiling_setpoint_f").toFloat();
    if (v < SETPOINT_MIN_F || v > SETPOINT_MAX_F) {
      snprintf(errmsg, sizeof(errmsg), "ceiling_setpoint_f must be 32–300");
      goto send_error;
    }
    new_ceiling_sp = v;
  }

  if (server.hasArg("bench_setpoint_f")) {
    float v = server.arg("bench_setpoint_f").toFloat();
    if (v < SETPOINT_MIN_F || v > SETPOINT_MAX_F) {
      snprintf(errmsg, sizeof(errmsg), "bench_setpoint_f must be 32–300");
      goto send_error;
    }
    new_bench_sp = v;
  }

  if (server.hasArg("ceiling_pid_en")) {
    String v = server.arg("ceiling_pid_en");
    if (v == "1" || v == "true" || v == "on")        new_ceiling_en = 1;
    else if (v == "0" || v == "false" || v == "off") new_ceiling_en = 0;
    else { snprintf(errmsg, sizeof(errmsg), "invalid ceiling_pid_en value"); goto send_error; }
  }

  if (server.hasArg("bench_pid_en")) {
    String v = server.arg("bench_pid_en");
    if (v == "1" || v == "true" || v == "on")        new_bench_en = 1;
    else if (v == "0" || v == "false" || v == "off") new_bench_en = 0;
    else { snprintf(errmsg, sizeof(errmsg), "invalid bench_pid_en value"); goto send_error; }
  }

  if (server.hasArg("sensor_read_interval_ms")) {
    long v = server.arg("sensor_read_interval_ms").toInt();
    if (v < (long)SENSOR_READ_INTERVAL_MIN_MS || v > (long)SENSOR_READ_INTERVAL_MAX_MS) {
      snprintf(errmsg, sizeof(errmsg), "sensor_read_interval_ms must be 500–10000");
      goto send_error;
    }
    new_sri = (unsigned long)v;
  }

  if (server.hasArg("serial_log_interval_ms")) {
    long v = server.arg("serial_log_interval_ms").toInt();
    if (v < (long)SERIAL_LOG_INTERVAL_MIN_MS || v > (long)SERIAL_LOG_INTERVAL_MAX_MS) {
      snprintf(errmsg, sizeof(errmsg), "serial_log_interval_ms must be 1000–60000");
      goto send_error;
    }
    new_slg = (unsigned long)v;
  }

  if (server.hasArg("static_ip")) {
    String s = server.arg("static_ip");
    IPAddress ip;
    if (s.length() == 0 || s.length() >= sizeof(new_ip) || !ip.fromString(s)) {
      snprintf(errmsg, sizeof(errmsg), "invalid static_ip address");
      goto send_error;
    }
    s.toCharArray(new_ip, sizeof(new_ip));
    has_ip = true;
  }

  if (server.hasArg("device_name")) {
    String s = server.arg("device_name");
    if (s.length() == 0 || s.length() >= sizeof(new_dn)) {
      snprintf(errmsg, sizeof(errmsg), "device_name must be 1–24 characters");
      goto send_error;
    }
    for (size_t i = 0; i < s.length(); i++) {
      char c = s[i];
      if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
        snprintf(errmsg, sizeof(errmsg), "device_name: only letters, digits, _ and - allowed");
        goto send_error;
      }
    }
    s.toCharArray(new_dn, sizeof(new_dn));
    has_dn = true;
  }

  // --- All validation passed — apply and persist ---
  {
    prefs.begin("sauna", false);

    if (new_ceiling_sp >= 32.0f) {
      Ceilingpoint = (new_ceiling_sp - 32.0f) * 5.0f / 9.0f;
      prefs.putFloat("csp", Ceilingpoint);
    }
    if (new_bench_sp >= 32.0f) {
      Benchpoint = (new_bench_sp - 32.0f) * 5.0f / 9.0f;
      prefs.putFloat("bsp", Benchpoint);
    }
    if (new_ceiling_en >= 0) {
      ceiling_pid_en = (new_ceiling_en == 1);
      prefs.putBool("cen", ceiling_pid_en);
    }
    if (new_bench_en >= 0) {
      bench_pid_en = (new_bench_en == 1);
      prefs.putBool("ben", bench_pid_en);
    }
    if (new_sri > 0) {
      g_sensor_read_interval_ms = new_sri;
      prefs.putUInt("sri", (unsigned int)new_sri);
    }
    if (new_slg > 0) {
      g_serial_log_interval_ms = new_slg;
      prefs.putUInt("slg", (unsigned int)new_slg);
    }
    if (has_ip && strcmp(new_ip, g_static_ip_str) != 0) {
      strncpy(g_static_ip_str, new_ip, 15); // g_static_ip_str is 16 bytes
      g_static_ip_str[15] = '\0';
      prefs.putString("sip", g_static_ip_str);
      restart_required = true;
    }
    if (has_dn && strcmp(new_dn, g_device_name) != 0) {
      strncpy(g_device_name, new_dn, 24); // g_device_name is 25 bytes
      g_device_name[24] = '\0';
      prefs.putString("dn", g_device_name);
      restart_required = true;
    }

    prefs.end();
  }

  {
    char resp[64];
    snprintf(resp, sizeof(resp), "{\"ok\":true,\"restart_required\":%s}",
             restart_required ? "true" : "false");
    server.send(200, "application/json", resp);
  }
  return;

send_error:
  {
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"%s\"}", errmsg);
    server.send(400, "application/json", resp);
  }
}

// ---------------------------------------------------------------------------
// Auth route handlers
// ---------------------------------------------------------------------------

void handleAuthLoginPage() {
    authAddSecurityHeaders();
    File f = LittleFS.open("/login.html", "r");
    if (!f) { server.send(404, "text/plain", "login.html not found"); return; }
    server.sendHeader("Cache-Control", "no-store");
    server.streamFile(f, "text/html");
    f.close();
}

void handleAuthLogin() {
    authAddSecurityHeaders();
    server.sendHeader("Cache-Control", "no-store");
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
        logAccessEvent("login_failure", username, srcStr,
                       server.client().remoteIP().toString().c_str());
        server.send(401, "application/json", "{\"error\":\"invalid credentials\"}");
        return;
    }
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
    const AuthSession *s = requireAdmin();
    if (!s) return;
    char username[33]; strncpy(username, s->username, 32); username[32] = '\0';
    String auth = server.header("Authorization");
    String token = auth.substring(7);
    authInvalidateToken(g_auth_sessions, AUTH_MAX_SESSIONS, token.c_str());
    logAccessEvent("logout", username, "none",
                   server.client().remoteIP().toString().c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleAuthStatus() {
    authAddSecurityHeaders();
    const AuthSession *s = requireAdmin();
    if (!s) return;
    JsonDocument doc;
    doc["valid"]    = true;
    doc["username"] = s->username;
    doc["role"]     = s->role;
    String body; serializeJson(doc, body);
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
    String body; serializeJson(doc, body);
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
    const char *role     = doc["role"]     | "";  // no implicit privilege escalation
    AuthUserResult r = authAddUser(&g_auth_users, username, password, role,
                                    espRandFn, mbedHashFn);
    if (r == AUTH_USER_BAD_PASS) { server.send(400, "application/json", "{\"error\":\"password too short\"}"); return; }
    if (r == AUTH_USER_FULL)     { server.send(409, "application/json", "{\"error\":\"user limit reached\"}"); return; }
    if (r == AUTH_USER_EXISTS)   { server.send(409, "application/json", "{\"error\":\"username taken\"}"); return; }
    authNvsSave(&g_auth_users);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleUsersDelete() {
    if (!requireAdmin()) return;
    if (!server.hasArg("username")) { server.send(400, "application/json", "{\"error\":\"missing username\"}"); return; }
    String username = server.arg("username");
    AuthUserResult r = authDeleteUser(&g_auth_users, username.c_str(), 0);
    if (r == AUTH_USER_PROTECTED)  { server.send(403, "application/json", "{\"error\":\"cannot delete emergency admin\"}"); return; }
    if (r == AUTH_USER_NOT_FOUND)  { server.send(404, "application/json", "{\"error\":\"user not found\"}"); return; }
    authNvsSave(&g_auth_users);
    server.send(200, "application/json", "{\"ok\":true}");
}

void handleUsersChangePassword() {
    if (!requireAdmin()) return;
    if (!server.hasArg("username") || !server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"missing fields\"}"); return;
    }
    String username = server.arg("username");
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
        server.send(400, "application/json", "{\"error\":\"bad json\"}"); return;
    }
    const char *newPass = doc["password"] | "";
    AuthUserResult r = authChangePassword(&g_auth_users, username.c_str(),
                                           newPass, espRandFn, mbedHashFn);
    if (r == AUTH_USER_BAD_PASS)   { server.send(400, "application/json", "{\"error\":\"password too short\"}"); return; }
    if (r == AUTH_USER_NOT_FOUND)  { server.send(404, "application/json", "{\"error\":\"user not found\"}"); return; }
    authNvsSave(&g_auth_users);
    server.send(200, "application/json", "{\"ok\":true}");
}

#endif // ARDUINO
