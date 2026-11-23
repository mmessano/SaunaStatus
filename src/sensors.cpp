// src/sensors.cpp
#include "sensors.h"
#include <Arduino.h>

// These defines are also in main.cpp; duplicated here so sensors.cpp is
// self-contained as a separate translation unit.
#ifndef RREF
#define RREF 4300.0
#endif
#ifndef RNOMINAL
#define RNOMINAL 1000.0
#endif
#ifndef TEMP_LIMIT_C
#define TEMP_LIMIT_C 120.0f
#endif

// Opens both vents fully when air temps reach TEMP_LIMIT_C.
// State decision is delegated to tickOverheat() (sauna_logic.h, natively tested):
// clears only when BOTH temps are valid and below
// TEMP_LIMIT_C - OVERHEAT_CLEAR_HYSTERESIS_C; a NaN sensor holds the alarm.
// Vents are driven only on the rising edge.
// Returns true while alarm is active — callers must skip PID computation.
bool checkOverheat()
{
  const bool was_active = overheat_alarm;
  OverheatGuard guard;
  guard.triggered = was_active;
  overheat_alarm = tickOverheat(guard, ceiling_temp, bench_temp, TEMP_LIMIT_C);

  if (overheat_alarm && !was_active)
  {
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
  else if (!overheat_alarm && was_active)
  {
    Serial.printf("Overheat cleared (both < %.0f °C) — resuming normal control.\n",
                  (float)(TEMP_LIMIT_C - OVERHEAT_CLEAR_HYSTERESIS_C));
  }
  return overheat_alarm;
}

// Reads all sensors: DHT21 ceiling+bench, MAX31865 stove, INA260 power.
// Updates globals directly. Applies || rule for last_ok_ms timestamps.
void readSensors()
{
    // Read all sensors
    ceiling_hum = dhtCeiling.readHumidity();
    ceiling_temp = dhtCeiling.readTemperature();
    if (!isnan(ceiling_temp) || !isnan(ceiling_hum))
      ceiling_last_ok_ms = millis();

    bench_hum = dhtBench.readHumidity();
    bench_temp = dhtBench.readTemperature();
    if (!isnan(bench_temp) || !isnan(bench_hum))
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

    // INA260 power monitor
    if (ina260_ok)
    {
      pwr_bus_V      = ina260.readBusVoltage();
      pwr_current_mA = ina260.readCurrent();
      pwr_mW         = ina260.readPower();
    }
}
