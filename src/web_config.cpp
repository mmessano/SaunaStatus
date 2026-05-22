// src/web_config.cpp — Configuration portal HTTP handlers extracted from web.cpp
// This file is Arduino-only; it must not be compiled in native test builds.
#ifdef ARDUINO

#include "secrets.h"
#include "web.h"
#include "web_internal.h"
#include "auth.h"
#include "sauna_logic.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

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

// GET /config — serve the settings HTML page from LittleFS
void handleConfigPage()
{
  authAddSecurityHeaders();
  if (!ensureLittleFsMounted()) return;
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

#endif // ARDUINO
