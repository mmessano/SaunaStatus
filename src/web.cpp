// src/web.cpp — HTTP and WebSocket handlers extracted from main.cpp
// This file is Arduino-only; it must not be compiled in native test builds.
#ifdef ARDUINO

#include "secrets.h"
#include "web.h"
#include "web_internal.h"
#include "motor_logic.h"
#include "auth.h"
#include "influx.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <HTTPClient.h>

// Build-flag overrides for constants used in handlers
#ifndef VENT_STEPS
#define VENT_STEPS 1024
#endif
// Default step count for /motor?cmd=cw|ccw when ?steps= param is absent or out of range
#ifndef MOTOR_CMD_DEFAULT_STEPS
#define MOTOR_CMD_DEFAULT_STEPS 64
#endif
#ifndef SETPOINT_MIN_F
#define SETPOINT_MIN_F 32.0f
#endif
#ifndef SETPOINT_MAX_F
#define SETPOINT_MAX_F 300.0f
#endif
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

bool ensureLittleFsMounted() {
  if (g_littlefs_mounted) return true;
  server.send(503, "text/plain",
              "LittleFS unavailable — repair or upload filesystem image");
  return false;
}

void handleRoot()
{
  authAddSecurityHeaders();
  if (!ensureLittleFsMounted()) return;
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

// Streams a static JS bundle from LittleFS. Unauthenticated — these are
// public vendor assets just like the HTML pages. Caches forever because the
// filenames carry the exact version we shipped (M10 — self-hosted Chart.js).
static void streamVendorJs(const char *path)
{
  if (!ensureLittleFsMounted()) return;
  File f = LittleFS.open(path, "r");
  if (!f) { server.send(404, "text/plain", "not found"); return; }
  server.sendHeader("Cache-Control", "public, max-age=31536000, immutable");
  server.streamFile(f, "application/javascript");
  f.close();
}
void handleChartJs()        { streamVendorJs("/chart.umd.min.js"); }
void handleChartAdapterJs() { streamVendorJs("/chart-adapter.min.js"); }

void handleMotorCmd()
{
  if (!requireAdmin()) return;
  String motor = server.arg("motor");
  String cmd = server.arg("cmd");
  int steps = server.hasArg("steps") ? server.arg("steps").toInt() : MOTOR_CMD_DEFAULT_STEPS;
  if (steps < 1 || steps > VENT_STEPS * 4)
    steps = MOTOR_CMD_DEFAULT_STEPS;

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
  // Whitelist of allowed range values — prevents arbitrary Flux queries
  static const char *allowedRanges[] = {"1h", "6h", "12h", "24h", "48h", "7d"};
  static const int numAllowed = sizeof(allowedRanges) / sizeof(allowedRanges[0]);
  String range = "1h";
  if (server.hasArg("range")) {
    String req = server.arg("range");
    for (int i = 0; i < numAllowed; i++) {
      if (req == allowedRanges[i]) { range = req; break; }
    }
  }

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
    server.send(500, "application/json", "{\"error\":\"database write failed\"}");
  }
}

#endif // ARDUINO
