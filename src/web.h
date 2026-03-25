// src/web.h
#pragma once

#include <math.h>
#include "globals.h"    // single source of truth for all extern globals

// Default stale threshold if not set by build flags (matches main.cpp default)
#ifndef STALE_THRESHOLD_MS
#define STALE_THRESHOLD_MS 10000UL
#endif

#ifndef ARDUINO
// millis() is Arduino-provided at runtime; native tests stub it in test_globals.cpp
extern unsigned long millis();
#endif

// ─── Natively testable ─────────────────────────────────────────────────────────

// Assembles SensorValues/MotorState/PIDState from globals and serializes to JSON.
// Calls buildJsonFull() from sauna_logic.h. millis() is Arduino-provided at
// runtime; native tests stub it in test_globals.cpp.
inline void buildJson(char *buf, size_t len) {
    SensorValues sv;
    sv.ceiling_temp       = ceiling_temp;
    sv.ceiling_hum        = ceiling_hum;
    sv.bench_temp         = bench_temp;
    sv.bench_hum          = bench_hum;
    sv.stove_temp         = stove_temp;
    sv.pwr_bus_V          = ina260_ok ? pwr_bus_V       : NAN;
    sv.pwr_current_mA     = ina260_ok ? pwr_current_mA  : NAN;
    sv.pwr_mW             = ina260_ok ? pwr_mW          : NAN;
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

// ─── Arduino-only handlers ─────────────────────────────────────────────────────
#ifdef ARDUINO
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void wsBroadcastAuthenticated(const char *payload);
void wsCheckAuthTimeouts();
void handleRoot();
void handleLog();
void handleDeleteStatus();
void handleDeleteControl();
void handleHistory();
void handleMotorCmd();
void handlePidToggle();
void handleSetpoint();
void handleOtaStatus();
void handleOtaUpdate();
void handleConfigPage();
void handleConfigGet();
void handleConfigSave();
void handleAuthLoginPage();
void handleAuthLogin();
void handleAuthLogout();
void handleAuthStatus();
void handleUsersGet();
void handleUsersCreate();
void handleUsersDelete();
void handleUsersChangePassword();
#endif // ARDUINO
