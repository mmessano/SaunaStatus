#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_INA260.h>
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
#include <Update.h>
#include <esp_ota_ops.h>
#include <SPI.h>
#include "gpio_config.h"
#include "secrets.h"
#ifndef AUTH_ADMIN_USER
#error "AUTH_ADMIN_USER must be defined in secrets.h (e.g. #define AUTH_ADMIN_USER \"admin\")"
#endif
#ifndef AUTH_ADMIN_PASS
#error "AUTH_ADMIN_PASS must be defined in secrets.h (e.g. #define AUTH_ADMIN_PASS \"yourpassword\")"
#endif
#include "auth.h"
#include "sauna_logic.h"
#include "ota_logic.h"
#include "sensors.h"
#include "web.h"
#include "mqtt.h"
#include "influx.h"

// =============================================================================
// PIN MAPPING — LB-ESP32S3-N16R8-Pinout-Modified
// All assignments are in gpio_config.h; grouped by physical adjacency.
//
// Left header  (GPIO4–9, 15–18): motors + DHT sensors
//   GPIO  4  — OUTFLOW_IN1    Outflow (upper) vent stepper IN1
//   GPIO  5  — OUTFLOW_IN2    Outflow stepper IN2
//   GPIO  6  — OUTFLOW_IN3    Outflow stepper IN3
//   GPIO  7  — OUTFLOW_IN4    Outflow stepper IN4
//   GPIO 15  — INFLOW_IN1     Inflow (lower) vent stepper IN1
//   GPIO 16  — INFLOW_IN2     Inflow stepper IN2
//   GPIO 17  — INFLOW_IN3     Inflow stepper IN3
//   GPIO 18  — INFLOW_IN4     Inflow stepper IN4
//   GPIO  8  — DHTPIN_CEILING DHT21 ceiling sensor data
//   GPIO  9  — DHTPIN_BENCH   DHT21 bench sensor data
//
// Right header (GPIO1–2, 39–42): I2C + SPI
//   GPIO  1  — INA260_SDA     I2C data  (INA260 power monitor)
//   GPIO  2  — INA260_SCL     I2C clock (INA260 power monitor)
//   GPIO 42  — SPI_CS_PIN     MAX31865 chip select
//   GPIO 41  — SPI_SCK_PIN    SPI clock
//   GPIO 40  — SPI_MISO_PIN   SPI MISO (MAX31865 SDO)
//   GPIO 39  — SPI_MOSI_PIN   SPI MOSI (MAX31865 SDI)
// =============================================================================

#define DEVICE "ESP32-S3"
// Credentials (INFLUXDB_URL, INFLUXDB_TOKEN, INFLUXDB_ORG, INFLUXDB_BUCKET,
//              MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS,
//              WIFI_SSID, WIFI_PASSWORD) are defined in secrets.h

// Time zone info
#define TZ_INFO "CST6CDT,M3.2.0,M11.1.0"

// Local HTTP InfluxDB — no TLS certificate needed
InfluxDBClient influxClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

Point status("sauna_status");
Point control("sauna_control");
Point          webaccess("sauna_webaccess");
AuthSession    g_auth_sessions[AUTH_MAX_SESSIONS];
AuthUserStore  g_auth_users;
RateLimiter    g_rate_limiter = {};
char           g_db_url[128] = "";
char           g_db_key[64]  = "";
// Deferred NVS save flag. Set by mqttCallback(); flushed in loop() before the
// sensor-read block so savePrefs() never runs from inside a callback context.
bool g_needs_save = false;

// PT1000 stove thermocouple via MAX31865 breakout.
// ESP32-S3 has no fixed SPI defaults — SPI.begin(SCK,MISO,MOSI) is called in
// setup() before stove_thermo.begin() to bind SPI to the gpio_config.h pins.
Adafruit_MAX31865 stove_thermo = Adafruit_MAX31865(SPI_CS_PIN, &SPI);
// #ifndef guards: authoritative values live in sensors.cpp; these are retained
// for documentation only and must not conflict with the sensors.cpp definitions.
#ifndef RREF
#define RREF 4300.0
#endif
#ifndef RNOMINAL
#define RNOMINAL 1000.0
#endif

// INA260 power monitor — I2C on INA260_SDA / INA260_SCL (from gpio_config.h)
// Integrated 2 mΩ shunt; no external resistor required.
// I2C address 0x40 (A0=GND, A1=GND).
Adafruit_INA260 ina260;
bool ina260_ok = false;

// DHT21 sensors — pins from gpio_config.h (DHTPIN_CEILING, DHTPIN_BENCH, DHTTYPE)
DHT dhtCeiling(DHTPIN_CEILING, DHTTYPE);
DHT dhtBench(DHTPIN_BENCH, DHTTYPE);

// Stepper motors — pins from gpio_config.h (OUTFLOW_IN*, INFLOW_IN*)
CheapStepper outflow(OUTFLOW_IN1, OUTFLOW_IN2, OUTFLOW_IN3, OUTFLOW_IN4);
CheapStepper inflow(INFLOW_IN1,  INFLOW_IN2,  INFLOW_IN3,  INFLOW_IN4);

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

// Sensor read interval validation bounds (ms); applied in loadLittleFSConfig() and /config/save
#ifndef SENSOR_READ_INTERVAL_MIN_MS
#define SENSOR_READ_INTERVAL_MIN_MS 500UL
#endif
#ifndef SENSOR_READ_INTERVAL_MAX_MS
#define SENSOR_READ_INTERVAL_MAX_MS 10000UL
#endif

// Serial log interval validation bounds (ms)
#ifndef SERIAL_LOG_INTERVAL_MIN_MS
#define SERIAL_LOG_INTERVAL_MIN_MS 1000UL
#endif
#ifndef SERIAL_LOG_INTERVAL_MAX_MS
#define SERIAL_LOG_INTERVAL_MAX_MS 60000UL
#endif

// WiFi credentials from secrets.h
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

// WiFi static network config; override with build flags or move to secrets.h
#ifndef WIFI_GATEWAY_IP
#define WIFI_GATEWAY_IP 192, 168, 1, 100
#endif
#ifndef WIFI_DNS_IP
#define WIFI_DNS_IP 8, 8, 8, 8
#endif

// Default static IP; override via LittleFS /config.json "static_ip" key or NVS "sip"
#ifndef DEFAULT_STATIC_IP
#define DEFAULT_STATIC_IP "192.168.1.200"
#endif

// Default sensor read interval (ms); override via /config.json "sensor_read_interval_ms" or NVS "sri"
#ifndef DEFAULT_SENSOR_READ_INTERVAL_MS
#define DEFAULT_SENSOR_READ_INTERVAL_MS 2000UL
#endif

// Static IP configuration (local_IP is computed at runtime from g_static_ip_str)
IPAddress gateway(WIFI_GATEWAY_IP);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(WIFI_DNS_IP);

WebServer server(80);
WebSocketsServer webSocket(81);
Preferences prefs;
WiFiClient mqttWifi;
PubSubClient mqttClient(mqttWifi);

// Staleness threshold for DHT sensors
#ifndef STALE_THRESHOLD_MS
#define STALE_THRESHOLD_MS 10000UL
#endif

// InfluxDB write interval; override with -DINFLUX_WRITE_INTERVAL_MS=xxx
#ifndef INFLUX_WRITE_INTERVAL_MS
#define INFLUX_WRITE_INTERVAL_MS 60000UL
#endif

// MQTT reconnect retry interval; override with -DMQTT_RECONNECT_INTERVAL_MS=xxx
#ifndef MQTT_RECONNECT_INTERVAL_MS
#define MQTT_RECONNECT_INTERVAL_MS 5000UL
#endif

// Motor speed in RPM; override with -DMOTOR_RPM=xxx
#ifndef MOTOR_RPM
#define MOTOR_RPM 12
#endif

// Minimum PID output delta (steps) to suppress jitter; override with -DPID_MIN_STEP_DELTA=xxx
#ifndef PID_MIN_STEP_DELTA
#define PID_MIN_STEP_DELTA 5
#endif

// QuickPID output range (0–255 maps to 0–max_steps for motor control)
#ifndef PID_OUTPUT_MIN
#define PID_OUTPUT_MIN 0
#endif
#ifndef PID_OUTPUT_MAX
#define PID_OUTPUT_MAX 255
#endif

// WebSocket broadcast JSON buffer size; override with -DWS_JSON_BUF_SIZE=xxx
// Worst-case buildJsonFull output ≈ 300 chars; 384 gives 80+ bytes of headroom.
// Must match the definition in web.cpp — both use the same JSON builder.
#ifndef WS_JSON_BUF_SIZE
#define WS_JSON_BUF_SIZE 384
#endif

// MQTT publish/receive buffer size; override with -DMQTT_BUF_SIZE=xxx
#ifndef MQTT_BUF_SIZE
#define MQTT_BUF_SIZE 512
#endif

// Local NTP server (router); override with -DNTP_SERVER_LOCAL="x.x.x.x"
#ifndef NTP_SERVER_LOCAL
#define NTP_SERVER_LOCAL "192.168.1.100"
#endif

// Runtime-configurable intervals and identity (Tier 3: NVS overrides build-flag defaults)
// These can be changed at runtime via the /config portal without a reboot.
unsigned long g_sensor_read_interval_ms = DEFAULT_SENSOR_READ_INTERVAL_MS;
unsigned long g_serial_log_interval_ms  = SERIAL_LOG_INTERVAL_MS;
// Static IP and device name require a restart to take effect.
char g_device_name[25]  = DEVICE;
char g_static_ip_str[16] = DEFAULT_STATIC_IP;

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

// Setpoint validation bounds (°F) — used in HTTP handlers, MQTT callbacks, and LittleFS config loading
#ifndef SETPOINT_MIN_F
#define SETPOINT_MIN_F 32.0f
#endif
#ifndef SETPOINT_MAX_F
#define SETPOINT_MAX_F 300.0f
#endif

// PID conservative/aggressive mode threshold: switch to conservative when error < this value (°C)
#ifndef PID_CONSERVATIVE_THRESHOLD_C
#define PID_CONSERVATIVE_THRESHOLD_C 10.0f
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
    if (v >= SETPOINT_MIN_F && v <= SETPOINT_MAX_F)
      Ceilingpoint = (v - 32.0f) * 5.0f / 9.0f;
  }
  if (doc["bench_setpoint_f"].is<float>())
  {
    float v = doc["bench_setpoint_f"].as<float>();
    if (v >= SETPOINT_MIN_F && v <= SETPOINT_MAX_F)
      Benchpoint = (v - 32.0f) * 5.0f / 9.0f;
  }
  if (doc["ceiling_pid_enabled"].is<bool>())
    ceiling_pid_en = doc["ceiling_pid_enabled"].as<bool>();
  if (doc["bench_pid_enabled"].is<bool>())
    bench_pid_en = doc["bench_pid_enabled"].as<bool>();
  if (doc["sensor_read_interval_ms"].is<unsigned long>()) {
    unsigned long v = doc["sensor_read_interval_ms"].as<unsigned long>();
    if (v >= SENSOR_READ_INTERVAL_MIN_MS && v <= SENSOR_READ_INTERVAL_MAX_MS) g_sensor_read_interval_ms = v;
  }
  if (doc["serial_log_interval_ms"].is<unsigned long>()) {
    unsigned long v = doc["serial_log_interval_ms"].as<unsigned long>();
    if (v >= SERIAL_LOG_INTERVAL_MIN_MS && v <= SERIAL_LOG_INTERVAL_MAX_MS) g_serial_log_interval_ms = v;
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

void savePrefs()
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

// CeilingPID uses dhtCeiling temperature as input
QuickPID CeilingPID(&ceiling_temp, &ceiling_output, &Ceilingpoint);
QuickPID BenchPID(&bench_temp, &bench_output, &Benchpoint);

// ---------------------------------------------------------------------------
// OTA update handlers
// ---------------------------------------------------------------------------

#ifndef OTA_MAX_BOOT_FAILURES
#define OTA_MAX_BOOT_FAILURES 3
#endif

// Called early in setup() — increments the consecutive-failure counter.
// If the threshold is reached, attempts to roll back to the previous firmware.
static void otaCheckBootHealth()
{
  prefs.begin("sauna", false);
  int failures = prefs.getInt("boot_fail", 0) + 1;
  prefs.putInt("boot_fail", failures);
  prefs.end();
  Serial.printf("OTA: boot attempt %d (rollback threshold %d)\n",
                failures, OTA_MAX_BOOT_FAILURES);
  if (shouldRollback(failures, OTA_MAX_BOOT_FAILURES)) {
    Serial.println("OTA: consecutive boot failures exceeded threshold — rolling back");
    // Reset counter first so we don't loop if rollback is unavailable
    prefs.begin("sauna", false);
    prefs.putInt("boot_fail", 0);
    prefs.end();
    esp_ota_mark_app_invalid_rollback_and_reboot();
    // Reaches here only when no previous OTA slot is available
    Serial.println("OTA: rollback unavailable (no previous firmware slot)");
  }
}

// Call once WiFi and sensors are confirmed healthy to reset the failure counter.
static void otaMarkBootSuccessful()
{
  prefs.begin("sauna", false);
  prefs.putInt("boot_fail", 0);
  prefs.end();
  esp_ota_mark_app_valid_cancel_rollback();
}

// Detect and log a previously interrupted download (power failure mid-flash).
// Call early in setup() before WiFi — any incomplete OTA slot is harmless
// (the bootloader ignores unvalidated slots), but we log it for visibility.
static void otaCheckPartialDownload()
{
  prefs.begin("sauna", true);
  bool inProgress = prefs.getBool("ota_ip", false);
  unsigned int expected = prefs.getUInt("ota_exp", 0);
  unsigned int written  = prefs.getUInt("ota_wrt", 0);
  prefs.end();

  if (inProgress) {
    OtaDownloadState s;
    s.in_progress    = true;
    s.bytes_expected = expected;
    s.bytes_written  = written;

    if (isOtaIncomplete(s)) {
      Serial.printf("OTA: previous download incomplete (%u/%u bytes) — will retry on next /ota/update\n",
                    written, expected);
    } else {
      Serial.println("OTA: previous update did not complete (reset during apply?) — recovery pending");
    }
    // Clear the flag; the slot is invalid so the bootloader already ignored it
    prefs.begin("sauna", false);
    prefs.putBool("ota_ip", false);
    prefs.end();
  }
}

// begin Setup
void setup()
{
  Serial.begin(115200);

  // OTA: check for incomplete previous download and consecutive boot failures
  // Run before LittleFS/NVS config load so a bad firmware is caught early.
  otaCheckPartialDownload();
  otaCheckBootHealth();

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

  // INA260 power monitor
  Wire.begin(INA260_SDA, INA260_SCL);
  ina260_ok = ina260.begin();
  if (ina260_ok)
    Serial.println("INA260 power monitor ready");
  else
    Serial.println("INA260 not found — power monitoring disabled");

  // Thermocouple - PT1000
  // On ESP32-S3, SPI pins must be configured explicitly before begin().
  Serial.println("Stove Thermocouple Sensor Test!");
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
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
      Serial.printf("Config: invalid static_ip \"%s\", using " DEFAULT_STATIC_IP "\n", g_static_ip_str);
      local_IP.fromString(DEFAULT_STATIC_IP);
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
  // NOTE: otaMarkBootSuccessful() moved to after full subsystem init
  // (server, WebSocket, MQTT, auth) — see end of setup()

  // NTP server pairs tried in order. Router-first is most reliable on a LAN
  // since it doesn't require outbound UDP 123 to the internet.
  static const char *ntpA[] = { NTP_SERVER_LOCAL, "pool.ntp.org",    "pool.ntp.org" };
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
  webaccess.addTag("device", g_device_name);
  webaccess.addTag("SSID",   WiFi.SSID());

  // Validate InfluxDB connection
  if (influxClient.validateConnection())
  {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(influxClient.getServerUrl());
  }
  else
  {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }

  outflow.setRpm(MOTOR_RPM);
  inflow.setRpm(MOTOR_RPM);

  CeilingPID.SetMode(QuickPID::Control::automatic);
  CeilingPID.SetOutputLimits(PID_OUTPUT_MIN, PID_OUTPUT_MAX);
  BenchPID.SetMode(QuickPID::Control::automatic);
  BenchPID.SetOutputLimits(PID_OUTPUT_MIN, PID_OUTPUT_MAX);

  // Auth: load config, init session store, load users, seed emergency admin
  authNvsLoadConfig(g_db_url, g_db_key);
  memset(g_auth_sessions, 0, sizeof(g_auth_sessions));
  authNvsLoad(&g_auth_users);
  authSeedEmergencyAdmin(&g_auth_users);

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
  server.on("/ota/status", HTTP_GET, handleOtaStatus);
  server.on("/ota/update", HTTP_POST, handleOtaUpdate);
  server.on("/auth/login",  HTTP_GET,  handleAuthLoginPage);
  server.on("/auth/login",  HTTP_POST, handleAuthLogin);
  server.on("/auth/logout", HTTP_POST, handleAuthLogout);
  server.on("/auth/status", HTTP_GET,  handleAuthStatus);
  server.on("/users",       HTTP_GET,  handleUsersGet);
  server.on("/users",       HTTP_POST, handleUsersCreate);
  server.on("/users",       HTTP_DELETE, handleUsersDelete);
  server.on("/users",       HTTP_PUT,  handleUsersChangePassword);
  const char *authHdrs[] = {"Authorization"};
  server.collectHeaders(authHdrs, 1);
  server.begin();
  Serial.println("HTTP server started on port 80");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");

  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(MQTT_BUF_SIZE);
  mqttConnect();

  // All subsystems initialized — mark boot successful to cancel OTA rollback.
  // Placed here (not after WiFi connect) so a firmware with broken HTTP,
  // auth, or MQTT handlers triggers rollback instead of being marked valid.
  otaMarkBootSuccessful();
}

void loop()
{
  // Flush any NVS save requested by the MQTT callback (H1: deferred savePrefs).
  // Must run before mqttClient.loop() which may re-trigger the callback.
  if (g_needs_save)
  {
    g_needs_save = false;
    savePrefs();
  }

  server.handleClient();
  webSocket.loop();
  wsCheckAuthTimeouts();

  if (!mqttClient.connected())
  {
    static unsigned long lastMqttRetry = 0;
    if (millis() - lastMqttRetry > MQTT_RECONNECT_INTERVAL_MS)
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
    // Cache now_ms once so all staleness checks within this block are consistent.
    // readSensors() can take up to ~250 ms (DHT timing); re-calling millis() mid-block
    // could cause a sensor to appear stale or fresh depending on call order.
    const unsigned long now_ms = lastRead;

    // Read all sensors (DHT21 ceiling+bench, MAX31865 stove, INA260 power)
    readSensors();

    // Safety check — opens vents and suppresses PID if air temps are too high
    bool alarm = checkOverheat();

    // CeilingPID → Outflow motor (A)
    bool c_cons = false;
    if (!alarm && ceiling_pid_en && !isnan(ceiling_temp) && !isSensorStale(ceiling_last_ok_ms, now_ms, STALE_THRESHOLD_MS))
    {
      float c_gap = fabsf(Ceilingpoint - ceiling_temp);
      c_cons = c_gap < PID_CONSERVATIVE_THRESHOLD_C;
      c_cons_mode = c_cons;
      CeilingPID.SetTunings(c_cons ? c_consKp : c_aggKp,
                            c_cons ? c_consKi : c_aggKi,
                            c_cons ? c_consKd : c_aggKd);
      CeilingPID.Compute();
      int c_new = (int)(ceiling_output / (float)PID_OUTPUT_MAX * outflow_max_steps);
      int c_delta = c_new - outflow_target;
      if (abs(c_delta) >= PID_MIN_STEP_DELTA)
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
    if (!alarm && bench_pid_en && !isnan(bench_temp) && !isSensorStale(bench_last_ok_ms, now_ms, STALE_THRESHOLD_MS))
    {
      float b_gap = fabsf(Benchpoint - bench_temp);
      b_cons = b_gap < PID_CONSERVATIVE_THRESHOLD_C;
      b_cons_mode = b_cons;
      BenchPID.SetTunings(b_cons ? b_consKp : b_aggKp,
                          b_cons ? b_consKi : b_aggKi,
                          b_cons ? b_consKd : b_aggKd);
      BenchPID.Compute();
      int b_new = (int)(bench_output / (float)PID_OUTPUT_MAX * inflow_max_steps);
      int b_delta = b_new - inflow_target;
      if (abs(b_delta) >= PID_MIN_STEP_DELTA)
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
      if (ina260_ok)
      {
        Serial.println(F("Power     |  Voltage  |   Current  |     Power"));
        Serial.println(F("----------|-----------|------------|----------"));
        if (!isnan(pwr_bus_V) && !isnan(pwr_current_mA) && !isnan(pwr_mW))
          Serial.printf("INA260    | %7.2f V | %8.1f mA | %7.1f mW\n",
                        pwr_bus_V, pwr_current_mA, pwr_mW);
        else
          Serial.println("INA260    |       ERR |        ERR |       ERR");
        Serial.println();
      }
    }

    // Broadcast updated readings to authenticated WebSocket clients only
    char json[WS_JSON_BUF_SIZE];
    buildJson(json, sizeof(json));
    wsBroadcastAuthenticated(json);

    // Publish to MQTT
    if (mqttClient.connected())
      mqttPublishState();
  }

  static unsigned long lastInflux = 0;
  if (millis() - lastInflux >= INFLUX_WRITE_INTERVAL_MS)
  {
    lastInflux = millis();

    if (writeInflux())
    {
      Serial.println("InfluxDB write OK");
    }
    else
    {
      Serial.print("InfluxDB write failed: ");
      Serial.println(influxClient.getLastErrorMessage());
    }
  }
}
