// src/globals.h
// Single source of truth for extern declarations of all globals defined in main.cpp.
// Hardware objects and Arduino-specific types are wrapped in #ifdef ARDUINO guards
// so native test builds compile cleanly.
#pragma once

// ─── Portable includes ────────────────────────────────────────────────────────
#include <math.h>
#include "sauna_logic.h"
#include "auth_logic.h"
#include "ota_logic.h"

// ─── Portable globals (plain C++ types — safe for native test builds) ─────────

// Sensor readings — all NAN until first successful read
extern float ceiling_temp, ceiling_hum;
extern float bench_temp,   bench_hum;
extern float stove_temp;
extern float pwr_bus_V, pwr_current_mA, pwr_mW;
extern unsigned long ceiling_last_ok_ms;
extern unsigned long bench_last_ok_ms;
extern bool ina260_ok;

// Motor state
extern unsigned short outflow_pos;
extern int outflow_dir;
extern int outflow_target;
extern int outflow_max_steps;
extern unsigned short inflow_pos;
extern int inflow_dir;
extern int inflow_target;
extern int inflow_max_steps;

// PID state
extern bool c_cons_mode, b_cons_mode;
extern bool ceiling_pid_en, bench_pid_en;
extern bool overheat_alarm;
extern float Ceilingpoint, Benchpoint;
extern float ceiling_output, bench_output;
extern float c_aggKp, c_aggKi, c_aggKd;
extern float c_consKp, c_consKi, c_consKd;
extern float b_aggKp, b_aggKi, b_aggKd;
extern float b_consKp, b_consKi, b_consKd;

// Runtime config
extern unsigned long g_sensor_read_interval_ms;
extern unsigned long g_serial_log_interval_ms;
extern bool g_littlefs_mounted;
// char arrays (not String) — require restart to take effect when changed
extern char g_device_name[];
extern char g_static_ip_str[];

// Auth
extern AuthSession   g_auth_sessions[];
extern AuthUserStore g_auth_users;
extern RateLimiter   g_rate_limiter;

// Functions defined in main.cpp, called by modules
extern void savePrefs();

// Deferred NVS save flag — set by MQTT callback, flushed in loop() before sensor
// reads. Prevents calling savePrefs() (which opens NVS) from inside a callback
// that may fire while loop() is already mid-execution on the same stack.
extern bool g_needs_save;
// External adapter config (char arrays, not String)
extern char g_db_url[];
extern char g_db_key[];

// ─── Arduino-only globals (hardware objects + Arduino-specific types) ──────────
#ifdef ARDUINO
#include <Arduino.h>
#include <WiFi.h>
#include <CheapStepper.h>
#include <Adafruit_MAX31865.h>
#include <DHT.h>
#include <Adafruit_INA260.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <InfluxDbClient.h>
#include <Preferences.h>
extern Preferences prefs;
extern IPAddress gateway;
extern IPAddress subnet;
extern IPAddress primaryDNS;

extern CheapStepper outflow;
extern CheapStepper inflow;
extern Adafruit_MAX31865 stove_thermo;
extern DHT dhtCeiling;
extern DHT dhtBench;
extern Adafruit_INA260 ina260;
extern WebServer server;
extern WebSocketsServer webSocket;
extern WiFiClient mqttWifi;
extern PubSubClient mqttClient;
extern InfluxDBClient influxClient;
extern Point status;
extern Point control;
extern Point webaccess;
#endif // ARDUINO
