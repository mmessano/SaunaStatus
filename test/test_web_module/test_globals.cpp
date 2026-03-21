// test/test_web_module/test_globals.cpp
#include <cmath>

// millis() stub — returns a fixed value for deterministic tests
unsigned long millis() { return 5000UL; }

// Sensor globals
float ceiling_temp    = __builtin_nanf("");
float ceiling_hum     = __builtin_nanf("");
float bench_temp      = __builtin_nanf("");
float bench_hum       = __builtin_nanf("");
float stove_temp      = __builtin_nanf("");
float pwr_bus_V       = __builtin_nanf("");
float pwr_current_mA  = __builtin_nanf("");
float pwr_mW          = __builtin_nanf("");
unsigned long ceiling_last_ok_ms = 0;
unsigned long bench_last_ok_ms   = 0;
bool ina260_ok = false;

// Motor globals
unsigned short outflow_pos  = 0;
int outflow_dir             = 0;
int outflow_target          = 0;
int outflow_max_steps       = 1024;
unsigned short inflow_pos   = 0;
int inflow_dir              = 0;
int inflow_target           = 0;
int inflow_max_steps        = 1024;

// PID globals
bool c_cons_mode    = false;
bool b_cons_mode    = false;
bool ceiling_pid_en = false;
bool bench_pid_en   = false;
bool overheat_alarm = false;
float Ceilingpoint  = 71.1f;  // ~160°F in °C
float Benchpoint    = 48.9f;  // ~120°F in °C
float ceiling_output = 0.0f;
float bench_output   = 0.0f;
