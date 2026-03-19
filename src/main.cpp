#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_INA219.h>
#include <Adafruit_MAX31865.h>
#include <CheapStepper.h>
#include <InfluxDbClient.h>
#include <DHT.h>
#include <QuickPID.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "secrets.h"
#include "sauna_logic.h"

// =============================================================================
// PIN MAPPING
// =============================================================================
// Sensors / SPI
//   GPIO  5  — MAX31865 CS    Stove PT1000 thermocouple (hardware SPI)
//   GPIO 16  — DHT21 AM2301   Ceiling sensor             (DHTPIN_CEILING)
//   GPIO 17  — DHT21 AM2301   Bench sensor               (DHTPIN_BENCH)
//   GPIO 18  — VSPI SCK       reserved — do not use
//   GPIO 19  — VSPI MISO      reserved — do not use
//   GPIO 23  — VSPI MOSI      reserved — do not use
//
// Power monitor / I2C (INA219)
//   GPIO  4  — SDA            (INA219_SDA)
//   GPIO 13  — SCL            (INA219_SCL)
//
// Outflow stepper — upper vent (CheapStepper → ULN2003)
//   GPIO 21  IN1  (dOUTFLOW1)
//   GPIO 25  IN2  (dOUTFLOW2)
//   GPIO 26  IN3  (dOUTFLOW3)
//   GPIO 14  IN4  (dOUTFLOW4)
//
// Inflow stepper  — lower vent (CheapStepper → ULN2003)
//   GPIO 22  IN1  (dINFLOW1)
//   GPIO 27  IN2  (dINFLOW2)
//   GPIO 32  IN3  (dINFLOW3)
//   GPIO 33  IN4  (dINFLOW4)
// =============================================================================

#define DEVICE "ESP32"
// Credentials (INFLUXDB_URL, INFLUXDB_TOKEN, INFLUXDB_ORG, INFLUXDB_BUCKET,
//              MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS,
//              WIFI_SSID, WIFI_PASSWORD) are defined in secrets.h

// Time zone info
#define TZ_INFO "CST6CDT,M3.2.0,M11.1.0"

// Local HTTP InfluxDB — no TLS certificate needed
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

Point status("sauna_status");
Point control("sauna_control");

// Add 3WIRE PT1000 sensor for the stove
// use hardware SPI, just pass in the CS pin
// The value of the Rref resistor. Use 430.0 for PT100 and 4300.0 for PT1000
// The 'nominal' 0-degrees-C resistance of the sensor
// 100.0 for PT100, 1000.0 for PT1000
Adafruit_MAX31865 stove_thermo = Adafruit_MAX31865(5);
#define RREF 4300.0
#define RNOMINAL 1000.0

// INA219 power monitor — I2C on GPIO4 (SDA) / GPIO13 (SCL)
// Wire a 0.1 Ω shunt in series with the positive supply rail.
#define INA219_SDA 4
#define INA219_SCL 13
Adafruit_INA219 ina219;
bool ina219_ok = false;

// Add AM2301 sensors aka DHT21
#define DHTPIN_CEILING 16
#define DHTPIN_BENCH 17
#define DHTTYPE DHT21

DHT dhtCeiling(DHTPIN_CEILING, DHTTYPE);
DHT dhtBench(DHTPIN_BENCH, DHTTYPE);

// Stepper motor pins (CheapStepper — IN1..IN4 per motor)
// Outflow (Upper) vent
const unsigned int dOUTFLOW1 = 21;
const unsigned int dOUTFLOW2 = 25;
const unsigned int dOUTFLOW3 = 26;
const unsigned int dOUTFLOW4 = 14;

// Inflow (Lower) vent
const unsigned int dINFLOW1 = 22;
const unsigned int dINFLOW2 = 27;
const unsigned int dINFLOW3 = 32;
const unsigned int dINFLOW4 = 33;

CheapStepper outflow(dOUTFLOW1, dOUTFLOW2, dOUTFLOW3, dOUTFLOW4);
CheapStepper inflow(dINFLOW1, dINFLOW2, dINFLOW3, dINFLOW4);

// Steps for fully-open position (~90° quarter-turn damper on 28BYJ-48)
#define VENT_STEPS 1024

// Safety cutoff — if any air sensor hits this, vents open and PID is suppressed.
// Override in platformio.ini with -DTEMP_LIMIT_C=115
#ifndef TEMP_LIMIT_C
#define TEMP_LIMIT_C 120.0f // 248 °F — above any normal sauna operating range
#endif

// Serial log interval — change here or override in platformio.ini with -DSERIAL_LOG_INTERVAL_MS=5000
#ifndef SERIAL_LOG_INTERVAL_MS
#define SERIAL_LOG_INTERVAL_MS 10000
#endif

// WiFi credentials from secrets.h
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// Static IP configuration (local_IP is computed at runtime from g_static_ip_str)
IPAddress gateway(192, 168, 1, 100);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);

WebServer server(80);
WebSocketsServer webSocket(81);
Preferences prefs;
WiFiClient mqttWifi;
PubSubClient mqttClient(mqttWifi);

// Staleness threshold for DHT sensors
#ifndef STALE_THRESHOLD_MS
#define STALE_THRESHOLD_MS 10000UL
#endif

// Runtime-configurable intervals and identity (Tier 3: NVS overrides build-flag defaults)
// These can be changed at runtime via the /config portal without a reboot.
unsigned long g_sensor_read_interval_ms = 2000UL;
unsigned long g_serial_log_interval_ms  = SERIAL_LOG_INTERVAL_MS;
// Static IP and device name require a restart to take effect.
char g_device_name[25]  = DEVICE;
char g_static_ip_str[16] = "192.168.1.200";

// Define data types
float ceiling_temp = NAN, ceiling_hum = NAN;
float bench_temp = NAN, bench_hum = NAN;
float stove_temp = NAN;
float pwr_bus_V = NAN, pwr_current_mA = NAN, pwr_mW = NAN;

unsigned long ceiling_last_ok_ms = 0;
unsigned long bench_last_ok_ms = 0;

unsigned short outflow_pos = 0;     // current open position, 0-100 %
int outflow_dir = 0;                // 1=CW, -1=CCW, 0=stopped
int outflow_target = 0;             // target step count from closed
int outflow_max_steps = VENT_STEPS; // calibrated full-open step count
unsigned short inflow_pos = 0;
int inflow_dir = 0;
int inflow_target = 0;
int inflow_max_steps = VENT_STEPS;

bool c_cons_mode = false;
bool b_cons_mode = false;
bool ceiling_pid_en = false;
bool bench_pid_en = false;
bool overheat_alarm = false;

// PID setpoints, outputs and tuning parameters
// Build-flag defaults (°F); override in platformio.ini with -DDEFAULT_CEILING_SP_F=xxx
#ifndef DEFAULT_CEILING_SP_F
#define DEFAULT_CEILING_SP_F 160.0f
#endif
#ifndef DEFAULT_BENCH_SP_F
#define DEFAULT_BENCH_SP_F 120.0f
#endif
float Ceilingpoint = (DEFAULT_CEILING_SP_F - 32.0f) * 5.0f / 9.0f;
float ceiling_output = 0;
float c_aggKp = 4, c_aggKi = 0.2, c_aggKd = 1;
float c_consKp = 1, c_consKi = 0.05, c_consKd = 0.25;

float Benchpoint = (DEFAULT_BENCH_SP_F - 32.0f) * 5.0f / 9.0f;
float bench_output = 0;
float b_aggKp = 4, b_aggKi = 0.2, b_aggKd = 1;
float b_consKp = 1, b_consKi = 0.05, b_consKd = 0.25;

// Load fleet-level defaults from /config.json in LittleFS.
// Setpoints use °F (consistent with the HTTP/MQTT API); PID enable flags are optional.
// Motor calibration (omx/imx) is device-specific and not read from this file.
// Call after LittleFS.begin() and before loadNVS() so NVS still wins for runtime changes.
static void loadLittleFSConfig()
{
  File f = LittleFS.open("/config.json", "r");
  if (!f)
  {
    Serial.println("Config: /config.json not found — using built-in defaults");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err)
  {
    Serial.printf("Config: JSON parse error: %s\n", err.c_str());
    return;
  }
  if (doc["ceiling_setpoint_f"].is<float>())
  {
    float v = doc["ceiling_setpoint_f"].as<float>();
    if (v >= 32.0f && v <= 300.0f)
      Ceilingpoint = (v - 32.0f) * 5.0f / 9.0f;
  }
  if (doc["bench_setpoint_f"].is<float>())
  {
    float v = doc["bench_setpoint_f"].as<float>();
    if (v >= 32.0f && v <= 300.0f)
      Benchpoint = (v - 32.0f) * 5.0f / 9.0f;
  }
  if (doc["ceiling_pid_enabled"].is<bool>())
    ceiling_pid_en = doc["ceiling_pid_enabled"].as<bool>();
  if (doc["bench_pid_enabled"].is<bool>())
    bench_pid_en = doc["bench_pid_enabled"].as<bool>();
  if (doc["sensor_read_interval_ms"].is<unsigned long>()) {
    unsigned long v = doc["sensor_read_interval_ms"].as<unsigned long>();
    if (v >= 500 && v <= 10000) g_sensor_read_interval_ms = v;
  }
  if (doc["serial_log_interval_ms"].is<unsigned long>()) {
    unsigned long v = doc["serial_log_interval_ms"].as<unsigned long>();
    if (v >= 1000 && v <= 60000) g_serial_log_interval_ms = v;
  }
  if (doc["static_ip"].is<const char *>()) {
    const char *s = doc["static_ip"].as<const char *>();
    IPAddress ip;
    if (s && ip.fromString(s))
      strncpy(g_static_ip_str, s, sizeof(g_static_ip_str) - 1);
  }
  if (doc["device_name"].is<const char *>()) {
    const char *s = doc["device_name"].as<const char *>();
    if (s && strlen(s) > 0 && strlen(s) < sizeof(g_device_name))
      strncpy(g_device_name, s, sizeof(g_device_name) - 1);
  }
  Serial.printf("Config: csp=%.1f°F bsp=%.1f°F cen=%d ben=%d\n",
                c2f(Ceilingpoint), c2f(Benchpoint),
                ceiling_pid_en, bench_pid_en);
}

static void savePrefs()
{
  prefs.begin("sauna", false);
  prefs.putFloat("csp", Ceilingpoint);
  prefs.putFloat("bsp", Benchpoint);
  prefs.putBool("cen", ceiling_pid_en);
  prefs.putBool("ben", bench_pid_en);
  prefs.putInt("omx", outflow_max_steps);
  prefs.putInt("imx", inflow_max_steps);
  prefs.end();
}

static const char *stepDirStr(int d)
{
  if (d > 0)
    return "       CW";
  if (d < 0)
    return "      CCW";
  return "     STOP";
}

// Returns stove_temp if valid, otherwise falls back to average of ceiling+bench
static float stoveReading()
{
  if (!isnan(stove_temp))
    return stove_temp;
  if (!isnan(ceiling_temp) && !isnan(bench_temp))
    return (ceiling_temp + bench_temp) / 2.0f;
  return NAN;
}

// Opens both vents fully when air temps exceed TEMP_LIMIT_C; clears when safe.
// Returns true while alarm is active — callers must skip PID computation.
static bool checkOverheat()
{
  bool hot = (!isnan(ceiling_temp) && ceiling_temp >= TEMP_LIMIT_C) ||
             (!isnan(bench_temp) && bench_temp >= TEMP_LIMIT_C);
  if (hot && !overheat_alarm)
  {
    overheat_alarm = true;
    Serial.printf("!!! OVERHEAT: ceiling=%.1f bench=%.1f (limit %.0f °C) — opening vents\n",
                  ceiling_temp, bench_temp, (float)TEMP_LIMIT_C);
    // Drive both vents to fully open
    int od = outflow_max_steps - outflow_target;
    if (od > 0)
    {
      outflow_dir = 1;
      outflow.newMove(true, od);
    }
    outflow_target = outflow_max_steps;
    int id = inflow_max_steps - inflow_target;
    if (id > 0)
    {
      inflow_dir = 1;
      inflow.newMove(true, id);
    }
    inflow_target = inflow_max_steps;
  }
  else if (!hot && overheat_alarm)
  {
    overheat_alarm = false;
    Serial.println("Overheat cleared — resuming normal control.");
  }
  return overheat_alarm;
}

// Always builds a valid JSON string; NaN readings become JSON null
void buildJson(char *buf, size_t len)
{
  SensorValues sv;
  sv.ceiling_temp       = ceiling_temp;
  sv.ceiling_hum        = ceiling_hum;
  sv.bench_temp         = bench_temp;
  sv.bench_hum          = bench_hum;
  sv.stove_temp         = stove_temp;
  sv.pwr_bus_V          = pwr_bus_V;
  sv.pwr_current_mA     = pwr_current_mA;
  sv.pwr_mW             = pwr_mW;
  sv.ceiling_last_ok_ms = ceiling_last_ok_ms;
  sv.bench_last_ok_ms   = bench_last_ok_ms;
  sv.stale_threshold_ms = STALE_THRESHOLD_MS;

  MotorState ms;
  ms.outflow_pos = outflow_pos;
  ms.outflow_dir = outflow_dir;
  ms.inflow_pos  = inflow_pos;
  ms.inflow_dir  = inflow_dir;

  PIDState ps;
  ps.ceiling_output = ceiling_output;
  ps.bench_output   = bench_output;
  ps.c_cons_mode    = c_cons_mode;
  ps.b_cons_mode    = b_cons_mode;
  ps.ceiling_pid_en = ceiling_pid_en;
  ps.bench_pid_en   = bench_pid_en;
  ps.Ceilingpoint   = Ceilingpoint;
  ps.Benchpoint     = Benchpoint;
  ps.overheat_alarm = overheat_alarm;

  buildJsonFull(sv, ms, ps, millis(), buf, len);
}

bool writeInflux()
{
  // sauna_status — sensor readings and motor state
  status.clearFields();
  if (!isnan(ceiling_temp))
    status.addField("ceiling_temp", ceiling_temp);
  if (!isnan(ceiling_hum))
    status.addField("ceiling_hum", ceiling_hum);
  if (!isnan(bench_temp))
    status.addField("bench_temp", bench_temp);
  if (!isnan(bench_hum))
    status.addField("bench_hum", bench_hum);
  float stove_log = stoveReading();
  if (!isnan(stove_log))
    status.addField("stove_temp", stove_log);
  // Power monitor fields must be added before writePoint — bug fix
  if (ina219_ok)
  {
    if (!isnan(pwr_bus_V))
      status.addField("bus_voltage_V", pwr_bus_V);
    if (!isnan(pwr_current_mA))
      status.addField("current_mA", pwr_current_mA);
    if (!isnan(pwr_mW))
      status.addField("power_mW", pwr_mW);
  }
  bool ok = client.writePoint(status);

  // sauna_control — PID controller state and motor output
  control.clearFields();
  control.addField("outflow_pos", (int)outflow_pos);
  control.addField("inflow_pos", (int)inflow_pos);
  control.addField("ceiling_setpoint", Ceilingpoint);
  control.addField("ceiling_pid_out", ceiling_output);
  control.addField("bench_setpoint", Benchpoint);
  control.addField("bench_pid_out", bench_output);
  ok &= client.writePoint(control);

  return ok;
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

void handleDeleteStatus() { handleDeleteMeasurement("sauna_status"); }
void handleDeleteControl() { handleDeleteMeasurement("sauna_control"); }

void handleMotorCmd()
{
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
    *tgt += steps;
    *dir = 1;
    m->newMove(true, steps);
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
  if (server.hasArg("ceiling"))
    ceiling_pid_en = server.arg("ceiling") == "1";
  if (server.hasArg("bench"))
    bench_pid_en = server.arg("bench") == "1";
  savePrefs();
  server.send(200, "text/plain", "OK");
}

void handleSetpoint()
{
  if (server.hasArg("ceiling"))
  {
    float f = server.arg("ceiling").toFloat();
    if (f >= 32.0f && f <= 300.0f)
      Ceilingpoint = (f - 32.0f) * 5.0f / 9.0f;
  }
  if (server.hasArg("bench"))
  {
    float f = server.arg("bench").toFloat();
    if (f >= 32.0f && f <= 300.0f)
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
  if (writeInflux())
  {
    Serial.println("InfluxDB manual write OK");
    server.send(200, "text/plain", "OK");
  }
  else
  {
    Serial.print("InfluxDB manual write failed: ");
    Serial.println(client.getLastErrorMessage());
    server.send(500, "text/plain", client.getLastErrorMessage());
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  if (type == WStype_CONNECTED)
  {
    // Push current readings immediately to the newly connected client
    char json[320];
    buildJson(json, sizeof(json));
    webSocket.sendTXT(num, json);
  }
}

// CeilingPID uses dhtCeiling temperature as input
QuickPID CeilingPID(&ceiling_temp, &ceiling_output, &Ceilingpoint);
QuickPID BenchPID(&bench_temp, &bench_output, &Benchpoint);

// ---------------------------------------------------------------------------
// MQTT helpers
// ---------------------------------------------------------------------------
static void mqttCallback(char *topic, byte *payload, unsigned int len)
{
  char msg[32];
  if (len == 0 || len >= sizeof(msg))
    return;
  memcpy(msg, payload, len);
  msg[len] = '\0';

  if (strcmp(topic, "sauna/ceiling_pid/set") == 0)
  {
    ceiling_pid_en = (strcmp(msg, "ON") == 0);
    savePrefs();
  }
  else if (strcmp(topic, "sauna/bench_pid/set") == 0)
  {
    bench_pid_en = (strcmp(msg, "ON") == 0);
    savePrefs();
  }
  else if (strcmp(topic, "sauna/ceiling_setpoint/set") == 0)
  {
    float f = atof(msg);
    if (f >= 32.0f && f <= 300.0f)
    {
      Ceilingpoint = (f - 32.0f) * 5.0f / 9.0f;
      savePrefs();
    }
  }
  else if (strcmp(topic, "sauna/bench_setpoint/set") == 0)
  {
    float f = atof(msg);
    if (f >= 32.0f && f <= 300.0f)
    {
      Benchpoint = (f - 32.0f) * 5.0f / 9.0f;
      savePrefs();
    }
  }
}

static void mqttPublishState()
{
  char buf[512];
  char ct[12], ch[12], bt[12], bh[12], st[12];
  char pv[12], pc[12], pw[12];
  fmtVal(ct, sizeof(ct), c2f(ceiling_temp));
  fmtVal(ch, sizeof(ch), ceiling_hum);
  fmtVal(bt, sizeof(bt), c2f(bench_temp));
  fmtVal(bh, sizeof(bh), bench_hum);
  fmtVal(st, sizeof(st), c2f(stoveReading()));
  fmtVal(pv, sizeof(pv), pwr_bus_V);
  fmtVal(pc, sizeof(pc), pwr_current_mA);
  fmtVal(pw, sizeof(pw), pwr_mW);

  snprintf(buf, sizeof(buf),
           "{\"ceiling_temp\":%s,\"ceiling_hum\":%s,"
           "\"bench_temp\":%s,\"bench_hum\":%s,"
           "\"stove_temp\":%s,"
           "\"outflow_pos\":%u,\"inflow_pos\":%u,"
           "\"ceiling_pid_out\":%.1f,\"bench_pid_out\":%.1f,"
           "\"ceiling_pid_en\":\"%s\",\"bench_pid_en\":\"%s\","
           "\"ceiling_setpoint\":%.1f,\"bench_setpoint\":%.1f,"
           "\"bus_voltage\":%s,\"current_mA\":%s,\"power_mW\":%s}",
           ct, ch, bt, bh, st,
           outflow_pos, inflow_pos,
           ceiling_output / 255.0f * 100.0f,
           bench_output / 255.0f * 100.0f,
           ceiling_pid_en ? "ON" : "OFF",
           bench_pid_en ? "ON" : "OFF",
           c2f(Ceilingpoint), c2f(Benchpoint),
           pv, pc, pw);

  mqttClient.publish("sauna/state", buf);
}

static void mqttPublishDiscovery()
{
  static const char *dev =
      "\"device\":{\"identifiers\":[\"sauna_esp32\"],\"name\":\"Sauna\","
      "\"model\":\"ESP32\",\"manufacturer\":\"Custom\"}";
  char buf[512];

// Sensor with device_class
#define PUB_S(id, nm, vt, unit, dc)                                    \
  snprintf(buf, sizeof(buf),                                           \
           "{\"name\":\"%s\",\"state_topic\":\"sauna/state\","         \
           "\"value_template\":\"%s\",\"unit_of_measurement\":\"%s\"," \
           "\"device_class\":\"%s\",\"state_class\":\"measurement\","  \
           "\"unique_id\":\"sauna_" id "\",%s}",                       \
           nm, vt, unit, dc, dev);                                     \
  mqttClient.publish("homeassistant/sensor/sauna_esp32/" id "/config", buf, true);

// Sensor without device_class
#define PUB_SN(id, nm, vt, unit)                                       \
  snprintf(buf, sizeof(buf),                                           \
           "{\"name\":\"%s\",\"state_topic\":\"sauna/state\","         \
           "\"value_template\":\"%s\",\"unit_of_measurement\":\"%s\"," \
           "\"state_class\":\"measurement\","                          \
           "\"unique_id\":\"sauna_" id "\",%s}",                       \
           nm, vt, unit, dev);                                         \
  mqttClient.publish("homeassistant/sensor/sauna_esp32/" id "/config", buf, true);

// Switch
#define PUB_SW(id, nm, vt, cmd)                                  \
  snprintf(buf, sizeof(buf),                                     \
           "{\"name\":\"%s\",\"state_topic\":\"sauna/state\","   \
           "\"value_template\":\"%s\",\"command_topic\":\"%s\"," \
           "\"unique_id\":\"sauna_" id "\",%s}",                 \
           nm, vt, cmd, dev);                                    \
  mqttClient.publish("homeassistant/switch/sauna_esp32/" id "/config", buf, true);

// Number (setpoint)
#define PUB_N(id, nm, vt, cmd)                                   \
  snprintf(buf, sizeof(buf),                                     \
           "{\"name\":\"%s\",\"state_topic\":\"sauna/state\","   \
           "\"value_template\":\"%s\",\"command_topic\":\"%s\"," \
           "\"min\":32,\"max\":250,\"step\":1,"                  \
           "\"unit_of_measurement\":\"\xc2\xb0"                  \
           "F\","                                                \
           "\"unique_id\":\"sauna_" id "\",%s}",                 \
           nm, vt, cmd, dev);                                    \
  mqttClient.publish("homeassistant/number/sauna_esp32/" id "/config", buf, true);

  PUB_S("ceiling_temp", "Ceiling Temperature", "{{ value_json.ceiling_temp | round(1) }}", "\xc2\xb0"
                                                                                           "F",
        "temperature")
  PUB_S("ceiling_hum", "Ceiling Humidity", "{{ value_json.ceiling_hum | round(1) }}", "%", "humidity")
  PUB_S("bench_temp", "Bench Temperature", "{{ value_json.bench_temp | round(1) }}", "\xc2\xb0"
                                                                                     "F",
        "temperature")
  PUB_S("bench_hum", "Bench Humidity", "{{ value_json.bench_hum | round(1) }}", "%", "humidity")
  PUB_S("stove_temp", "Stove Temperature", "{{ value_json.stove_temp | round(1) }}", "\xc2\xb0"
                                                                                     "F",
        "temperature")
  PUB_SN("outflow_pos", "Outflow Position", "{{ value_json.outflow_pos }}", "%")
  PUB_SN("inflow_pos", "Inflow Position", "{{ value_json.inflow_pos }}", "%")
  PUB_SN("ceiling_pid_out", "Ceiling PID Output", "{{ value_json.ceiling_pid_out | round(1) }}", "%")
  PUB_SN("bench_pid_out", "Bench PID Output", "{{ value_json.bench_pid_out | round(1) }}", "%")
  PUB_SW("ceiling_pid", "Ceiling PID", "{{ value_json.ceiling_pid_en }}", "sauna/ceiling_pid/set")
  PUB_SW("bench_pid", "Bench PID", "{{ value_json.bench_pid_en }}", "sauna/bench_pid/set")
  PUB_S("bus_voltage", "Bus Voltage", "{{ value_json.bus_voltage | round(2) }}", "V", "voltage")
  PUB_SN("current_mA", "Current", "{{ value_json.current_mA | round(1) }}", "mA")
  PUB_SN("power_mW", "Power", "{{ value_json.power_mW | round(1) }}", "mW")
  PUB_N("ceiling_setpoint", "Ceiling Setpoint", "{{ value_json.ceiling_setpoint | round(0) | int }}", "sauna/ceiling_setpoint/set")
  PUB_N("bench_setpoint", "Bench Setpoint", "{{ value_json.bench_setpoint | round(0) | int }}", "sauna/bench_setpoint/set")

#undef PUB_S
#undef PUB_SN
#undef PUB_SW
#undef PUB_N
}

static void mqttConnect()
{
  if (mqttClient.connected())
    return;
  Serial.print("MQTT connecting...");
  bool ok = (MQTT_USER[0] != '\0')
                ? mqttClient.connect("sauna_esp32", MQTT_USER, MQTT_PASS)
                : mqttClient.connect("sauna_esp32");
  if (ok)
  {
    Serial.println(" connected");
    mqttPublishDiscovery();
    mqttClient.subscribe("sauna/ceiling_pid/set");
    mqttClient.subscribe("sauna/bench_pid/set");
    mqttClient.subscribe("sauna/ceiling_setpoint/set");
    mqttClient.subscribe("sauna/bench_setpoint/set");
  }
  else
  {
    Serial.printf(" failed (rc=%d)\n", mqttClient.state());
  }
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
    if (v < 32.0f || v > 300.0f) {
      snprintf(errmsg, sizeof(errmsg), "ceiling_setpoint_f must be 32–300");
      goto send_error;
    }
    new_ceiling_sp = v;
  }

  if (server.hasArg("bench_setpoint_f")) {
    float v = server.arg("bench_setpoint_f").toFloat();
    if (v < 32.0f || v > 300.0f) {
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
    if (v < 500 || v > 10000) {
      snprintf(errmsg, sizeof(errmsg), "sensor_read_interval_ms must be 500–10000");
      goto send_error;
    }
    new_sri = (unsigned long)v;
  }

  if (server.hasArg("serial_log_interval_ms")) {
    long v = server.arg("serial_log_interval_ms").toInt();
    if (v < 1000 || v > 60000) {
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
      strncpy(g_static_ip_str, new_ip, sizeof(g_static_ip_str) - 1);
      prefs.putString("sip", g_static_ip_str);
      restart_required = true;
    }
    if (has_dn && strcmp(new_dn, g_device_name) != 0) {
      strncpy(g_device_name, new_dn, sizeof(g_device_name) - 1);
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

// begin Setup
void setup()
{
  Serial.begin(115200);

  if (!LittleFS.begin(true))
    Serial.println("LittleFS mount failed");

  // Layer 2: fleet defaults from /config.json (overrides build-flag defaults)
  loadLittleFSConfig();

  // Layer 3: per-device runtime changes from NVS (overrides config.json)
  // isKey() guards prevent a missing key from silently reverting a config.json value.
  if (!prefs.begin("sauna", true))
  {
    Serial.println("NVS: open failed — no persisted settings (partition full or corrupt?)");
  }
  else
  {
    if (prefs.isKey("csp")) Ceilingpoint     = prefs.getFloat("csp", Ceilingpoint);
    if (prefs.isKey("bsp")) Benchpoint       = prefs.getFloat("bsp", Benchpoint);
    if (prefs.isKey("cen")) ceiling_pid_en   = prefs.getBool("cen",  ceiling_pid_en);
    if (prefs.isKey("ben")) bench_pid_en     = prefs.getBool("ben",  bench_pid_en);
    if (prefs.isKey("omx")) outflow_max_steps = prefs.getInt("omx",  outflow_max_steps);
    if (prefs.isKey("imx")) inflow_max_steps  = prefs.getInt("imx",  inflow_max_steps);
    if (prefs.isKey("sri")) g_sensor_read_interval_ms = prefs.getUInt("sri", (unsigned int)g_sensor_read_interval_ms);
    if (prefs.isKey("slg")) g_serial_log_interval_ms  = prefs.getUInt("slg", (unsigned int)g_serial_log_interval_ms);
    if (prefs.isKey("sip")) {
      String s = prefs.getString("sip", "");
      IPAddress ip;
      if (s.length() > 0 && s.length() < sizeof(g_static_ip_str) && ip.fromString(s))
        s.toCharArray(g_static_ip_str, sizeof(g_static_ip_str));
    }
    if (prefs.isKey("dn")) {
      String s = prefs.getString("dn", "");
      if (s.length() > 0 && s.length() < sizeof(g_device_name))
        s.toCharArray(g_device_name, sizeof(g_device_name));
    }
    prefs.end();
  }
  Serial.printf("Settings: csp=%.1f°F bsp=%.1f°F cen=%d ben=%d omx=%d imx=%d\n",
                c2f(Ceilingpoint), c2f(Benchpoint),
                ceiling_pid_en, bench_pid_en,
                outflow_max_steps, inflow_max_steps);

  // INA219 power monitor
  Wire.begin(INA219_SDA, INA219_SCL);
  ina219_ok = ina219.begin();
  if (ina219_ok)
    Serial.println("INA219 power monitor ready");
  else
    Serial.println("INA219 not found — power monitoring disabled");

  // Thermocouple - PT1000
  Serial.println("Stove Thermocouple Sensor Test!");
  stove_thermo.begin(MAX31865_3WIRE);

  Serial.println(F("AM2301 Ceiling Test"));
  dhtCeiling.begin();

  Serial.println(F("AM2301 Bench Test"));
  dhtBench.begin();

  Serial.print("Connecting to ");
  Serial.println(ssid);
  {
    IPAddress local_IP;
    if (!local_IP.fromString(g_static_ip_str)) {
      Serial.printf("Config: invalid static_ip \"%s\", using 192.168.1.200\n", g_static_ip_str);
      local_IP.fromString("192.168.1.200");
    }
    WiFi.config(local_IP, gateway, subnet, primaryDNS);
  }
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // NTP server pairs tried in order. Router-first is most reliable on a LAN
  // since it doesn't require outbound UDP 123 to the internet.
  static const char *ntpA[] = { "192.168.1.100", "pool.ntp.org",    "pool.ntp.org" };
  static const char *ntpB[] = { "pool.ntp.org", "time.nist.gov",   "time.cloudflare.com" };

  // Attempt NTP sync; retry up to 3 times if still at epoch (year < 2020)
  for (int ntpAttempt = 0; ntpAttempt < 3; ntpAttempt++) {
    if (ntpAttempt > 0) {
      Serial.printf("NTP sync failed (attempt %d), retrying in 5s...\n", ntpAttempt);
      delay(5000);
    }
    Serial.printf("NTP: trying %s / %s\n", ntpA[ntpAttempt], ntpB[ntpAttempt]);
    timeSync(TZ_INFO, ntpA[ntpAttempt], ntpB[ntpAttempt]);
    time_t t = time(nullptr);
    struct tm chk;
    gmtime_r(&t, &chk);
    if (chk.tm_year > 120) break; // tm_year is years since 1900; >120 means >2020
  }

  time_t now = time(nullptr);
  struct tm utcTm, localTm;
  gmtime_r(&now, &utcTm);
  localtime_r(&now, &localTm);
  char timeBuf[40];
  strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &utcTm);
  if (utcTm.tm_year <= 120)
    Serial.println("WARNING: NTP sync failed after 3 attempts — timestamps will be wrong");
  Serial.print("UTC:   ");
  Serial.println(timeBuf);
  strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &localTm);
  Serial.print("Local: ");
  Serial.println(timeBuf);

  // Add tags
  status.addTag("device", g_device_name);
  status.addTag("SSID", WiFi.SSID());
  control.addTag("device", g_device_name);
  control.addTag("SSID", WiFi.SSID());

  // Validate InfluxDB connection
  if (client.validateConnection())
  {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  }
  else
  {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  outflow.setRpm(12);
  inflow.setRpm(12);

  CeilingPID.SetMode(QuickPID::Control::automatic);
  CeilingPID.SetOutputLimits(0, 255);
  BenchPID.SetMode(QuickPID::Control::automatic);
  BenchPID.SetOutputLimits(0, 255);

  server.on("/", handleRoot);
  server.on("/log", handleLog);
  server.on("/delete/status", handleDeleteStatus);
  server.on("/delete/control", handleDeleteControl);
  server.on("/history", handleHistory);
  server.on("/setpoint", handleSetpoint);
  server.on("/pid", handlePidToggle);
  server.on("/motor", handleMotorCmd);
  server.on("/config", HTTP_GET, handleConfigPage);
  server.on("/config/get", HTTP_GET, handleConfigGet);
  server.on("/config/save", HTTP_POST, handleConfigSave);
  server.begin();
  Serial.println("HTTP server started on port 80");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  mqttConnect();
}

void loop()
{
  server.handleClient();
  webSocket.loop();

  if (!mqttClient.connected())
  {
    static unsigned long lastMqttRetry = 0;
    if (millis() - lastMqttRetry > 5000)
    {
      lastMqttRetry = millis();
      mqttConnect();
    }
  }
  mqttClient.loop();
  outflow.run();
  inflow.run();

  // Clear direction immediately when motor finishes (between 2-second blocks)
  if (outflow.getStepsLeft() == 0 && outflow_dir != 0)
    outflow_dir = 0;
  if (inflow.getStepsLeft() == 0 && inflow_dir != 0)
    inflow_dir = 0;

  static unsigned long lastRead = 0;
  if (millis() - lastRead >= g_sensor_read_interval_ms)
  {
    lastRead = millis();

    // Read all sensors
    ceiling_hum = dhtCeiling.readHumidity();
    ceiling_temp = dhtCeiling.readTemperature();
    if (!isnan(ceiling_temp) && !isnan(ceiling_hum))
      ceiling_last_ok_ms = millis();

    bench_hum = dhtBench.readHumidity();
    bench_temp = dhtBench.readTemperature();
    if (!isnan(bench_temp) && !isnan(bench_hum))
      bench_last_ok_ms = millis();

    float raw_temp = stove_thermo.temperature(RNOMINAL, RREF);
    uint8_t fault = stove_thermo.readFault();
    if (fault || raw_temp < -200.0f || raw_temp > 900.0f)
    {
      stove_temp = NAN;
      if (fault)
      {
        Serial.printf("Stove fault 0x%02X:", fault);
        if (fault & MAX31865_FAULT_HIGHTHRESH)
          Serial.print(" HIGH_THRESH");
        if (fault & MAX31865_FAULT_LOWTHRESH)
          Serial.print(" LOW_THRESH");
        if (fault & MAX31865_FAULT_REFINLOW)
          Serial.print(" REFINLOW");
        if (fault & MAX31865_FAULT_REFINHIGH)
          Serial.print(" REFINHIGH");
        if (fault & MAX31865_FAULT_RTDINLOW)
          Serial.print(" RTDINLOW");
        if (fault & MAX31865_FAULT_OVUV)
          Serial.print(" OV/UV");
        Serial.println();
        stove_thermo.clearFault();
      }
    }
    else
    {
      stove_temp = raw_temp;
    }

    // INA219 power monitor
    if (ina219_ok)
    {
      pwr_bus_V = ina219.getBusVoltage_V();
      pwr_current_mA = ina219.getCurrent_mA();
      pwr_mW = ina219.getPower_mW();
    }

    // Safety check — opens vents and suppresses PID if air temps are too high
    bool alarm = checkOverheat();

    // CeilingPID → Outflow motor (A)
    bool c_cons = false;
    if (!alarm && ceiling_pid_en && !isnan(ceiling_temp))
    {
      float c_gap = fabsf(Ceilingpoint - ceiling_temp);
      c_cons = c_gap < 10.0f;
      c_cons_mode = c_cons;
      CeilingPID.SetTunings(c_cons ? c_consKp : c_aggKp,
                            c_cons ? c_consKi : c_aggKi,
                            c_cons ? c_consKd : c_aggKd);
      CeilingPID.Compute();
      int c_new = (int)(ceiling_output / 255.0f * outflow_max_steps);
      int c_delta = c_new - outflow_target;
      if (abs(c_delta) >= 5)
      {
        outflow_target = c_new;
        if (c_delta > 0)
        {
          outflow_dir = 1;
          outflow.newMove(true, c_delta);
        }
        else
        {
          outflow_dir = -1;
          outflow.newMove(false, -c_delta);
        }
      }
      else if (outflow.getStepsLeft() == 0)
      {
        outflow_dir = 0;
      }
    }
    else if (!alarm)
    {
      ceiling_output = 0;
      if (outflow_target > 0)
      {
        int d = outflow_target;
        outflow_target = 0;
        outflow_dir = -1;
        outflow.newMove(false, d);
      }
      else if (outflow.getStepsLeft() == 0)
      {
        outflow_dir = 0;
        outflow.stop();
      }
    }

    // BenchPID → Inflow stepper
    bool b_cons = false;
    if (!alarm && bench_pid_en && !isnan(bench_temp))
    {
      float b_gap = fabsf(Benchpoint - bench_temp);
      b_cons = b_gap < 10.0f;
      b_cons_mode = b_cons;
      BenchPID.SetTunings(b_cons ? b_consKp : b_aggKp,
                          b_cons ? b_consKi : b_aggKi,
                          b_cons ? b_consKd : b_aggKd);
      BenchPID.Compute();
      int b_new = (int)(bench_output / 255.0f * inflow_max_steps);
      int b_delta = b_new - inflow_target;
      if (abs(b_delta) >= 5)
      {
        inflow_target = b_new;
        if (b_delta > 0)
        {
          inflow_dir = 1;
          inflow.newMove(true, b_delta);
        }
        else
        {
          inflow_dir = -1;
          inflow.newMove(false, -b_delta);
        }
      }
      else if (inflow.getStepsLeft() == 0)
      {
        inflow_dir = 0;
      }
    }
    else if (!alarm)
    {
      bench_output = 0;
      if (inflow_target > 0)
      {
        int d = inflow_target;
        inflow_target = 0;
        inflow_dir = -1;
        inflow.newMove(false, d);
      }
      else if (inflow.getStepsLeft() == 0)
      {
        inflow_dir = 0;
        inflow.stop();
      }
    }

    // Compute position percentage from calibrated range
    outflow_pos = outflow_max_steps > 0 ? (unsigned short)(outflow_target * 100 / outflow_max_steps) : 0;
    inflow_pos = inflow_max_steps > 0 ? (unsigned short)(inflow_target * 100 / inflow_max_steps) : 0;

    // Serial log — throttled to SERIAL_LOG_INTERVAL_MS (default 10 s)
    static unsigned long lastLog = 0;
    if (millis() - lastLog >= g_serial_log_interval_ms)
    {
      lastLog = millis();

      // Build sensor table lines
      char l0[36], l1[36], l2[36], l3[36], l4[36];
      char r0[36], r1[36], r2[36], r3[36];

      strcpy(l0, "Sensor    |  Temp (F) |   Hum (%)");
      strcpy(l1, "----------|-----------|----------");
      if (!isnan(ceiling_temp) && !isnan(ceiling_hum))
        snprintf(l2, sizeof(l2), "Ceiling   | %9.1f | %9.1f", c2f(ceiling_temp), ceiling_hum);
      else
        strcpy(l2, "Ceiling   |       ERR |       ERR");
      if (!isnan(bench_temp) && !isnan(bench_hum))
        snprintf(l3, sizeof(l3), "Bench     | %9.1f | %9.1f", c2f(bench_temp), bench_hum);
      else
        strcpy(l3, "Bench     |       ERR |       ERR");
      if (!isnan(stove_temp))
        snprintf(l4, sizeof(l4), "Stove     | %9.1f |       N/A", c2f(stove_temp));
      else
        strcpy(l4, "Stove     |       ERR |       N/A");

      // Build motor table lines
      strcpy(r0, "Motor     |     Pos % | Direction");
      strcpy(r1, "----------|-----------|----------");
      snprintf(r2, sizeof(r2), "Outflow   | %9u | %9s", outflow_pos, stepDirStr(outflow_dir));
      snprintf(r3, sizeof(r3), "Inflow    | %9u | %9s", inflow_pos, stepDirStr(inflow_dir));

      // Print sensor/motor tables side by side
      const char *gap = "   ";
      Serial.printf("%s%s%s\n", l0, gap, r0);
      Serial.printf("%s%s%s\n", l1, gap, r1);
      Serial.printf("%s%s%s\n", l2, gap, r2);
      Serial.printf("%s%s%s\n", l3, gap, r3);
      Serial.printf("%s\n", l4);

      // Build and print PID controller table
      const char *c_tune = !isnan(ceiling_temp) ? (c_cons ? "Conservative" : "  Aggressive") : "         ---";
      const char *b_tune = !isnan(bench_temp) ? (b_cons ? "Conservative" : "  Aggressive") : "         ---";
      char p2[65], p3[65];
      if (!isnan(ceiling_temp))
        snprintf(p2, sizeof(p2), "Ceiling    | %9.1f | %9.1f | %9.1f | %12s",
                 c2f(Ceilingpoint), c2f(ceiling_temp), ceiling_output, c_tune);
      else
        snprintf(p2, sizeof(p2), "Ceiling    | %9.1f |       ERR |       --- | %12s",
                 c2f(Ceilingpoint), c_tune);
      if (!isnan(bench_temp))
        snprintf(p3, sizeof(p3), "Bench      | %9.1f | %9.1f | %9.1f | %12s",
                 c2f(Benchpoint), c2f(bench_temp), bench_output, b_tune);
      else
        snprintf(p3, sizeof(p3), "Bench      | %9.1f |       ERR |       --- | %12s",
                 c2f(Benchpoint), b_tune);
      Serial.println();
      Serial.println(F("Controller |  Setpoint |     Input |    Output |       Tuning"));
      Serial.println(F("-----------|-----------|-----------|-----------|-------------"));
      Serial.println(p2);
      Serial.println(p3);
      Serial.println();

      // Power monitor
      if (ina219_ok)
      {
        Serial.printf("Power     | %6.2f V | %7.1f mA | %8.1f mW\n",
                      pwr_bus_V, pwr_current_mA, pwr_mW);
        Serial.println();
      }
    }

    // Broadcast updated readings to all connected WebSocket clients
    char json[320];
    buildJson(json, sizeof(json));
    webSocket.broadcastTXT(json);

    // Publish to MQTT
    if (mqttClient.connected())
      mqttPublishState();
  }

  static unsigned long lastInflux = 0;
  if (millis() - lastInflux >= 60000)
  {
    lastInflux = millis();

    if (writeInflux())
    {
      Serial.println("InfluxDB write OK");
    }
    else
    {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    }
  }
}
