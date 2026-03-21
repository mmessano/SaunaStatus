// src/sensors.h
#pragma once

#include <math.h>
#include "globals.h"

// ─── Natively testable ─────────────────────────────────────────────────────────

// Returns stove_temp if valid; (ceiling_temp + bench_temp)/2 if both air sensors
// are non-NAN; NAN otherwise. Verbatim logic from main.cpp:367.
inline float stoveReading() {
    if (!std::isnan(stove_temp))
        return stove_temp;
    if (!std::isnan(ceiling_temp) && !std::isnan(bench_temp))
        return (ceiling_temp + bench_temp) / 2.0f;
    return NAN;
}

// ─── Arduino-only ──────────────────────────────────────────────────────────────
#ifdef ARDUINO

// Reads all sensors (DHT21 ceiling+bench, MAX31865 stove, INA260 power).
// Updates globals directly. Applies || rule for last_ok_ms timestamps.
// Hardware-dependent: not natively testable.
void readSensors();

// Evaluates ceiling/bench temps against TEMP_LIMIT_C; drives vents fully open
// on alarm onset (rising edge only). Returns true while alarm is active.
// Callers use return value only to suppress PID — motor drive is inside.
bool checkOverheat();

#endif // ARDUINO
