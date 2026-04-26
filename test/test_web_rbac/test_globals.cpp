#define ARDUINO 1

#include <cstring>
#include "globals.h"

unsigned long millis() { return 5000UL; }

StubSerial Serial;
LittleFSClass LittleFS;
UpdateClass Update;
int HTTPClient::post_code_ = 204;
int HTTPClient::get_code_ = 200;
String HTTPClient::body_ = String("");
int HTTPClient::size_ = 0;

float ceiling_temp = __builtin_nanf("");
float ceiling_hum = __builtin_nanf("");
float bench_temp = __builtin_nanf("");
float bench_hum = __builtin_nanf("");
float stove_temp = __builtin_nanf("");
float pwr_bus_V = __builtin_nanf("");
float pwr_current_mA = __builtin_nanf("");
float pwr_mW = __builtin_nanf("");
unsigned long ceiling_last_ok_ms = 0;
unsigned long bench_last_ok_ms = 0;
bool ina260_ok = false;

unsigned short outflow_pos = 0;
int outflow_dir = 0;
int outflow_target = 0;
int outflow_max_steps = 1024;
unsigned short inflow_pos = 0;
int inflow_dir = 0;
int inflow_target = 0;
int inflow_max_steps = 1024;

bool c_cons_mode = false;
bool b_cons_mode = false;
bool ceiling_pid_en = false;
bool bench_pid_en = false;
bool overheat_alarm = false;
float Ceilingpoint = 71.1f;
float Benchpoint = 48.9f;
float ceiling_output = 0.0f;
float bench_output = 0.0f;
float c_aggKp = 0.0f;
float c_aggKi = 0.0f;
float c_aggKd = 0.0f;
float c_consKp = 0.0f;
float c_consKi = 0.0f;
float c_consKd = 0.0f;
float b_aggKp = 0.0f;
float b_aggKi = 0.0f;
float b_aggKd = 0.0f;
float b_consKp = 0.0f;
float b_consKi = 0.0f;
float b_consKd = 0.0f;

unsigned long g_sensor_read_interval_ms = 5000UL;
unsigned long g_serial_log_interval_ms = 10000UL;
bool g_littlefs_mounted = true;
char g_device_name[25] = "ESP32-S3";
char g_static_ip_str[16] = "192.168.1.201";

AuthSession g_auth_sessions[AUTH_MAX_SESSIONS] = {};
AuthUserStore g_auth_users = {};
RateLimiter g_rate_limiter = {};
bool g_needs_save = false;
char g_db_url[128] = "";
char g_db_key[64] = "";

Preferences prefs;
IPAddress gateway;
IPAddress subnet;
IPAddress primaryDNS;
CheapStepper outflow;
CheapStepper inflow;
Adafruit_MAX31865 stove_thermo;
DHT dhtCeiling;
DHT dhtBench;
Adafruit_INA260 ina260;
WebServer server;
WebSocketsServer webSocket;
WiFiClient mqttWifi;
PubSubClient mqttClient;
InfluxDBClient influxClient;
Point status;
Point control;
Point webaccess;

void savePrefs() {}
bool writeInflux() { return true; }
void logAccessEvent(const char *, const char *, const char *, const char *) {}
