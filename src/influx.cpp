// src/influx.cpp
#include "influx.h"
#include "sensors.h"
#include <Arduino.h>

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
  if (ina260_ok)
  {
    if (!isnan(pwr_bus_V))
      status.addField("bus_voltage_V", pwr_bus_V);
    if (!isnan(pwr_current_mA))
      status.addField("current_mA", pwr_current_mA);
    if (!isnan(pwr_mW))
      status.addField("power_mW", pwr_mW);
  }
  bool ok = influxClient.writePoint(status);

  // sauna_control — PID controller state and motor output
  control.clearFields();
  control.addField("outflow_pos", (int)outflow_pos);
  control.addField("inflow_pos", (int)inflow_pos);
  control.addField("ceiling_setpoint", Ceilingpoint);
  control.addField("ceiling_pid_out", ceiling_output);
  control.addField("bench_setpoint", Benchpoint);
  control.addField("bench_pid_out", bench_output);
  ok &= influxClient.writePoint(control);

  return ok;
}

void logAccessEvent(const char *event,
                    const char *username,
                    const char *auth_source,
                    const char *client_ip) {
    AuthLogEvent ev = authBuildLogEvent(event, username, client_ip, auth_source);
    webaccess.clearFields();
    webaccess.clearTags();
    webaccess.addTag("device",   g_device_name);
    webaccess.addTag("event",    ev.event);
    webaccess.addTag("username", ev.username);
    webaccess.addField("client_ip",   ev.client_ip);
    webaccess.addField("auth_source", ev.auth_source);
    influxClient.writePoint(webaccess);  // fire-and-forget
}
