// src/mqtt.cpp
#include "mqtt.h"
#include <Arduino.h>
#include "secrets.h"
#include "sensors.h"   // stoveReading()
#include "sauna_logic.h" // c2f(), fmtVal()

#ifndef SETPOINT_MIN_F
#define SETPOINT_MIN_F 32.0f
#endif
#ifndef SETPOINT_MAX_F
#define SETPOINT_MAX_F 300.0f
#endif
#ifndef PID_OUTPUT_MAX
#define PID_OUTPUT_MAX 255
#endif

#ifdef ARDUINO


void mqttCallback(char *topic, byte *payload, unsigned int len)
{
  char msg[32];
  if (len == 0 || len >= sizeof(msg))
    return;
  memcpy(msg, payload, len);
  msg[len] = '\0';

  if (strcmp(topic, "sauna/ceiling_pid/set") == 0)
  {
    ceiling_pid_en = (strcmp(msg, "ON") == 0);
    g_needs_save = true;  // deferred: savePrefs() called from loop(), not callback
  }
  else if (strcmp(topic, "sauna/bench_pid/set") == 0)
  {
    bench_pid_en = (strcmp(msg, "ON") == 0);
    g_needs_save = true;
  }
  else if (strcmp(topic, "sauna/ceiling_setpoint/set") == 0)
  {
    float f = atof(msg);
    if (f >= SETPOINT_MIN_F && f <= SETPOINT_MAX_F)
    {
      Ceilingpoint = (f - 32.0f) * 5.0f / 9.0f;
      g_needs_save = true;
    }
  }
  else if (strcmp(topic, "sauna/bench_setpoint/set") == 0)
  {
    float f = atof(msg);
    if (f >= SETPOINT_MIN_F && f <= SETPOINT_MAX_F)
    {
      Benchpoint = (f - 32.0f) * 5.0f / 9.0f;
      g_needs_save = true;
    }
  }
}

void mqttPublishState()
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
           ceiling_output / (float)PID_OUTPUT_MAX * 100.0f,
           bench_output / (float)PID_OUTPUT_MAX * 100.0f,
           ceiling_pid_en ? "ON" : "OFF",
           bench_pid_en ? "ON" : "OFF",
           c2f(Ceilingpoint), c2f(Benchpoint),
           pv, pc, pw);

  mqttClient.publish("sauna/state", buf);
}

void mqttPublishDiscovery()
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
  if (!mqttClient.publish("homeassistant/sensor/sauna_esp32/" id "/config", buf, true)) \
    Serial.println("MQTT discovery publish failed for " id " (buffer too small?)");

// Sensor without device_class
#define PUB_SN(id, nm, vt, unit)                                       \
  snprintf(buf, sizeof(buf),                                           \
           "{\"name\":\"%s\",\"state_topic\":\"sauna/state\","         \
           "\"value_template\":\"%s\",\"unit_of_measurement\":\"%s\"," \
           "\"state_class\":\"measurement\","                          \
           "\"unique_id\":\"sauna_" id "\",%s}",                       \
           nm, vt, unit, dev);                                         \
  if (!mqttClient.publish("homeassistant/sensor/sauna_esp32/" id "/config", buf, true)) \
    Serial.println("MQTT discovery publish failed for " id " (buffer too small?)");

// Switch
#define PUB_SW(id, nm, vt, cmd)                                  \
  snprintf(buf, sizeof(buf),                                     \
           "{\"name\":\"%s\",\"state_topic\":\"sauna/state\","   \
           "\"value_template\":\"%s\",\"command_topic\":\"%s\"," \
           "\"unique_id\":\"sauna_" id "\",%s}",                 \
           nm, vt, cmd, dev);                                    \
  if (!mqttClient.publish("homeassistant/switch/sauna_esp32/" id "/config", buf, true)) \
    Serial.println("MQTT discovery publish failed for " id " (buffer too small?)");

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
  if (!mqttClient.publish("homeassistant/number/sauna_esp32/" id "/config", buf, true)) \
    Serial.println("MQTT discovery publish failed for " id " (buffer too small?)");

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

void mqttConnect()
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

#endif // ARDUINO
