# SaunaStatus Modular Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extract sensor reading, WebSocket/HTTP, MQTT, and InfluxDB logic from `src/main.cpp` into separate focused modules, add unit tests for the extracted pure functions, and update `data/index.html` with an inline login form and pre-auth trend chart.

**Architecture:** `globals.h` provides `extern` declarations for all globals defined in `main.cpp`. Each new module (sensors, web, mqtt, influx) includes `globals.h` plus only what it needs. Pure functions testable natively (`stoveReading()`, `buildJson()`) are placed inline in their respective headers so native test builds avoid Arduino-specific headers. All existing behavior is preserved verbatim.

**Tech Stack:** C++14, ESP32 Arduino framework, PlatformIO, Unity test framework, LittleFS, HTML/CSS/JS

**Spec:** `docs/superpowers/specs/2026-03-21-modular-refactor-design.md`

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/globals.h` | **Create** | `extern` declarations for all shared globals; `#ifdef ARDUINO` guards for hardware objects |
| `src/sensors.h` | **Create** | `stoveReading()` inline; `#ifdef ARDUINO`-guarded declarations for `readSensors()`, `checkOverheat()` |
| `src/sensors.cpp` | **Create** | `readSensors()`, `checkOverheat()` (verbatim from main.cpp) |
| `src/web.h` | **Create** | `buildJson()` inline; `#ifdef ARDUINO`-guarded declarations for all HTTP + WebSocket handlers |
| `src/web.cpp` | **Create** | All `handle*` functions and `webSocketEvent()` (verbatim from main.cpp) |
| `src/mqtt.h` | **Create** | Declarations for `mqttConnect()`, `mqttCallback()`, `mqttPublishState()`, `mqttPublishDiscovery()` |
| `src/mqtt.cpp` | **Create** | MQTT functions (verbatim from main.cpp) |
| `src/influx.h` | **Create** | Declarations for `writeInflux()`, `logAccessEvent()` (new signature with `client_ip`) |
| `src/influx.cpp` | **Create** | `writeInflux()` (verbatim), `logAccessEvent()` (new `client_ip` param) |
| `src/main.cpp` | **Modify** | Remove extracted functions; add `#include` for new modules; keep globals, PID, motor, setup, loop |
| `src/auth.h` | **Modify** | Update `logAccessEvent()` call sites to pass `server.client().remoteIP().toString().c_str()` |
| `test/test_sensor_module/test_main.cpp` | **Create** | Unity tests for `stoveReading()` |
| `test/test_sensor_module/test_globals.cpp` | **Create** | Stub global definitions for sensor module tests |
| `test/test_web_module/test_main.cpp` | **Create** | Unity tests for `buildJson()` |
| `test/test_web_module/test_globals.cpp` | **Create** | Stub global definitions + `millis()` stub for web module tests |
| `data/index.html` | **Modify** | Inline login form; trend chart pre-auth; auth-gated action sections |
| `CLAUDE.md` | **Modify** | Update test count from 81 to actual; document new files |

---

## Task 0: Verify Baseline

**Files:** none modified

- [ ] **Step 1: Run native tests and confirm 113 pass**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -10
```

Expected output: `113 test cases: 113 succeeded`

- [ ] **Step 2: Confirm firmware compiles clean**

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `[SUCCESS]` with no new errors (existing `-Wno-parentheses` suppresses known warnings)

- [ ] **Step 3: Commit baseline marker**

```bash
git commit --allow-empty -m "chore: begin modular refactor — baseline 113 tests passing"
```

---

## Task 1: Create `globals.h`

**Files:**
- Create: `src/globals.h`

`globals.h` is the single source of truth for `extern` declarations of all globals defined in `main.cpp`. Hardware objects are wrapped in `#ifdef ARDUINO` guards so native test builds compile cleanly. `String` (Arduino type) globals are also guarded.

- [ ] **Step 1: Create `src/globals.h`**

```cpp
// src/globals.h
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

// Auth
extern AuthSession  g_auth_sessions[];
extern AuthUserStore g_auth_users;

// ─── Arduino-only globals (hardware objects + Arduino String type) ─────────────
#ifdef ARDUINO
#include <Arduino.h>
#include <CheapStepper.h>
#include <Adafruit_MAX31865.h>
#include <DHT.h>
#include <Adafruit_INA260.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <InfluxDbClient.h>

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
extern String g_device_name;
extern String g_static_ip_str;
extern String g_db_url;
extern String g_db_key;
#endif // ARDUINO
```

- [ ] **Step 2: Confirm `globals.h` compiles in the Arduino environment**

Add `#include "globals.h"` temporarily at the top of `main.cpp` (right after existing includes), then:

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -E "error:|warning:|SUCCESS"
```

Expected: `[SUCCESS]`. Fix any missing externs or include-order issues before proceeding.

- [ ] **Step 3: Remove the temporary include from `main.cpp`** (will be added back properly in Task 8)

- [ ] **Step 4: Commit**

```bash
git add src/globals.h
git commit -m "feat(refactor): add globals.h with extern declarations and ARDUINO guards"
```

---

## Task 2: Write Failing Tests — `test_sensor_module`

**Files:**
- Create: `test/test_sensor_module/test_main.cpp`
- Create: `test/test_sensor_module/test_globals.cpp`

`stoveReading()` will be inline in `sensors.h`. These tests are written before `sensors.h` exists so they will fail to compile — that is expected (red phase of TDD). They test `stoveReading()` in isolation using stub globals.

- [ ] **Step 1: Create `test/test_sensor_module/test_globals.cpp`**

```cpp
// test/test_sensor_module/test_globals.cpp
// Stub definitions for globals used by stoveReading()
float ceiling_temp = __builtin_nanf("");
float ceiling_hum  = __builtin_nanf("");
float bench_temp   = __builtin_nanf("");
float bench_hum    = __builtin_nanf("");
float stove_temp   = __builtin_nanf("");
```

- [ ] **Step 2: Create `test/test_sensor_module/test_main.cpp`**

```cpp
// test/test_sensor_module/test_main.cpp
#include <unity.h>
#include <cmath>
#include "sensors.h"   // stoveReading() is inline here

void setUp(void) {
    // Reset all globals to NAN before each test
    stove_temp   = std::numeric_limits<float>::quiet_NaN();
    ceiling_temp = std::numeric_limits<float>::quiet_NaN();
    bench_temp   = std::numeric_limits<float>::quiet_NaN();
}
void tearDown(void) {}

// stove valid -> returns stove_temp
void test_stove_valid_returns_stove(void) {
    stove_temp   = 95.0f;
    ceiling_temp = 70.0f;
    bench_temp   = 65.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 95.0f, stoveReading());
}

// stove NaN, both air valid -> returns average
void test_stove_nan_both_air_valid_returns_average(void) {
    stove_temp   = std::numeric_limits<float>::quiet_NaN();
    ceiling_temp = 70.0f;
    bench_temp   = 60.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 65.0f, stoveReading());
}

// stove NaN, only ceiling valid -> returns NaN (both air required)
void test_stove_nan_only_ceiling_returns_nan(void) {
    stove_temp   = std::numeric_limits<float>::quiet_NaN();
    ceiling_temp = 70.0f;
    bench_temp   = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_TRUE(std::isnan(stoveReading()));
}

// stove NaN, only bench valid -> returns NaN
void test_stove_nan_only_bench_returns_nan(void) {
    stove_temp   = std::numeric_limits<float>::quiet_NaN();
    ceiling_temp = std::numeric_limits<float>::quiet_NaN();
    bench_temp   = 60.0f;
    TEST_ASSERT_TRUE(std::isnan(stoveReading()));
}

// stove NaN, both air NaN -> returns NaN
void test_stove_nan_all_nan_returns_nan(void) {
    stove_temp   = std::numeric_limits<float>::quiet_NaN();
    ceiling_temp = std::numeric_limits<float>::quiet_NaN();
    bench_temp   = std::numeric_limits<float>::quiet_NaN();
    TEST_ASSERT_TRUE(std::isnan(stoveReading()));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_stove_valid_returns_stove);
    RUN_TEST(test_stove_nan_both_air_valid_returns_average);
    RUN_TEST(test_stove_nan_only_ceiling_returns_nan);
    RUN_TEST(test_stove_nan_only_bench_returns_nan);
    RUN_TEST(test_stove_nan_all_nan_returns_nan);
    return UNITY_END();
}
```

- [ ] **Step 3: Run tests — confirm they FAIL (sensors.h doesn't exist yet)**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native --filter test_sensor_module 2>&1 | tail -15
```

Expected: Compile error — `sensors.h: No such file or directory`. This confirms the red phase.

- [ ] **Step 4: Commit failing tests**

```bash
git add test/test_sensor_module/
git commit -m "test(sensors): add failing tests for stoveReading() — red phase"
```

---

## Task 3: Create `sensors.h/.cpp` — Make Sensor Tests Pass

**Files:**
- Create: `src/sensors.h`
- Create: `src/sensors.cpp`

`stoveReading()` is inline in `sensors.h` (6 lines, pure, no hardware dep). `readSensors()` and `checkOverheat()` are Arduino-only and declared inside `#ifdef ARDUINO`.

- [ ] **Step 1: Create `src/sensors.h`**

```cpp
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
```

- [ ] **Step 2: Create `src/sensors.cpp`**

Copy the sensor-read block from `main.cpp`'s `loop()` into `readSensors()`, and move `checkOverheat()` and helper code verbatim. At the top of the file, include what's needed:

```cpp
// src/sensors.cpp
#include "sensors.h"
#include <Arduino.h>
```

Then paste these functions verbatim from `main.cpp` (do NOT modify logic):
- `checkOverheat()` from line 378 (remove the `static` keyword — now externally linked)
- `readSensors()` — this is new: it wraps the sensor read block from `loop()` (~lines 1677–1730). Extract it as a named function. The block reads DHT ceiling/bench, MAX31865 stove, and INA260. **Do not include the PID compute block** — that stays in `loop()`.

The sensor read block in `loop()` currently looks like (locate it with `grep -n "dhtCeiling\|dhtBench\|stove_thermo" src/main.cpp`):

```cpp
void readSensors() {
    // DHT ceiling
    float ct = dhtCeiling.readTemperature();
    float ch = dhtCeiling.readHumidity();
    if (!isnan(ct) || !isnan(ch)) {
        ceiling_temp = ct;
        ceiling_hum  = ch;
        ceiling_last_ok_ms = millis();
    } else {
        ceiling_temp = NAN;
        ceiling_hum  = NAN;
    }

    // DHT bench
    float bt = dhtBench.readTemperature();
    float bh = dhtBench.readHumidity();
    if (!isnan(bt) || !isnan(bh)) {
        bench_temp = bt;
        bench_hum  = bh;
        bench_last_ok_ms = millis();
    } else {
        bench_temp = NAN;
        bench_hum  = NAN;
    }

    // MAX31865 stove
    float st = stove_thermo.temperature(RNOMINAL, RREF);
    uint8_t fault = stove_thermo.readFault();
    if (fault || st < -200.0f || st > 900.0f) {
        stove_thermo.clearFault();
        if (fault) Serial.printf("Stove fault: 0x%02X\n", fault);
        stove_temp = NAN;
    } else {
        stove_temp = st;
    }

    // INA260 power monitor (optional)
    if (ina260_ok) {
        pwr_bus_V       = ina260.readBusVoltage() / 1000.0f;
        pwr_current_mA  = ina260.readCurrent();
        pwr_mW          = ina260.readPower();
    }
}
```

**IMPORTANT:** Copy the actual sensor-read block exactly from `main.cpp` — do not use the skeleton above as the canonical version. The skeleton is illustrative; the source file is authoritative.

- [ ] **Step 3: Run sensor module tests — confirm they PASS**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native --filter test_sensor_module 2>&1 | tail -10
```

Expected: `5 test cases: 5 succeeded`. If any fail, debug before proceeding.

- [ ] **Step 4: Run ALL native tests — confirm 113 still pass**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -10
```

Expected: `118 test cases: 118 succeeded` (113 baseline + 5 new)

- [ ] **Step 5: Compile Arduino firmware — confirm no errors**

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -E "error:|SUCCESS"
```

Note: `sensors.cpp` is new but `main.cpp` hasn't been modified yet — sensors.h is compiled but `readSensors()` / `checkOverheat()` are not called yet. That's fine.

- [ ] **Step 6: Commit**

```bash
git add src/sensors.h src/sensors.cpp test/test_sensor_module/
git commit -m "feat(sensors): extract sensors module with stoveReading() and readSensors()"
```

---

## Task 4: Write Failing Tests — `test_web_module`

**Files:**
- Create: `test/test_web_module/test_main.cpp`
- Create: `test/test_web_module/test_globals.cpp`

`buildJson()` will be inline in `web.h`. These tests are written before `web.h` exists — they will fail to compile (red phase).

- [ ] **Step 1: Create `test/test_web_module/test_globals.cpp`**

This file defines all globals that `buildJson()` reads, plus a `millis()` stub.

```cpp
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
```

- [ ] **Step 2: Create `test/test_web_module/test_main.cpp`**

```cpp
// test/test_web_module/test_main.cpp
#include <unity.h>
#include <cstring>
#include <cmath>
#include "web.h"   // buildJson() is inline here

static char buf[512];

void setUp(void) {
    // Reset globals to safe defaults before each test
    ceiling_temp     = std::numeric_limits<float>::quiet_NaN();
    ceiling_hum      = std::numeric_limits<float>::quiet_NaN();
    bench_temp       = std::numeric_limits<float>::quiet_NaN();
    bench_hum        = std::numeric_limits<float>::quiet_NaN();
    stove_temp       = std::numeric_limits<float>::quiet_NaN();
    pwr_bus_V        = std::numeric_limits<float>::quiet_NaN();
    pwr_current_mA   = std::numeric_limits<float>::quiet_NaN();
    pwr_mW           = std::numeric_limits<float>::quiet_NaN();
    ceiling_last_ok_ms = 0;
    bench_last_ok_ms   = 0;
    ina260_ok        = false;
    overheat_alarm   = false;
    outflow_pos = 0; outflow_dir = 0;
    inflow_pos  = 0; inflow_dir  = 0;
    ceiling_output = 0; bench_output = 0;
    ceiling_pid_en = false; bench_pid_en = false;
    c_cons_mode = false; b_cons_mode = false;
}
void tearDown(void) {}

// buildJson produces valid JSON
void test_buildjson_is_valid_json(void) {
    buildJson(buf, sizeof(buf));
    TEST_ASSERT_EQUAL('{', buf[0]);
    TEST_ASSERT_EQUAL('}', buf[strlen(buf) - 1]);
}

// buildJson contains all 23 required keys
void test_buildjson_contains_all_keys(void) {
    buildJson(buf, sizeof(buf));
    const char *keys[] = {
        "\"clt\"", "\"clh\"", "\"d5t\"", "\"d5h\"", "\"tct\"",
        "\"ofs\"", "\"ofd\"", "\"ifs\"", "\"ifd\"",
        "\"csp\"", "\"cop\"", "\"ctm\"", "\"cen\"",
        "\"bsp\"", "\"bop\"", "\"btm\"", "\"ben\"",
        "\"pvolt\"", "\"pcurr\"", "\"pmw\"",
        "\"oa\"", "\"cst\"", "\"bst\""
    };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, keys[i]), keys[i]);
    }
}

// Stale ceiling (last_ok_ms=0, threshold non-zero) -> clt/clh null, cst=1
void test_stale_ceiling_gives_null(void) {
    ceiling_temp = 70.0f;
    ceiling_hum  = 50.0f;
    ceiling_last_ok_ms = 0;     // never read -> always stale
    bench_last_ok_ms   = 5000;  // fresh
    bench_temp = 65.0f;
    bench_hum  = 45.0f;
    buildJson(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"clt\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":1"));
    // bench unaffected
    TEST_ASSERT_NULL(strstr(buf, "\"d5t\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":0"));
}

// Stale bench -> d5t/d5h null, bst=1, ceiling unaffected
void test_stale_bench_gives_null(void) {
    bench_temp = 65.0f;
    bench_hum  = 45.0f;
    bench_last_ok_ms   = 0;     // never read -> always stale
    ceiling_last_ok_ms = 5000;  // fresh
    ceiling_temp = 70.0f;
    ceiling_hum  = 50.0f;
    buildJson(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d5t\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":1"));
    TEST_ASSERT_NULL(strstr(buf, "\"clt\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":0"));
}

// NaN stove -> tct:null
void test_nan_stove_gives_null(void) {
    stove_temp = std::numeric_limits<float>::quiet_NaN();
    buildJson(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"tct\":null"));
}

// INA260 absent -> pvolt/pcurr/pmw null
void test_ina260_absent_gives_null_power(void) {
    ina260_ok = false;
    pwr_bus_V = 12.0f;  // value set but ina260_ok=false — must still be null
    buildJson(buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"pvolt\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"pcurr\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"pmw\":null"));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_buildjson_is_valid_json);
    RUN_TEST(test_buildjson_contains_all_keys);
    RUN_TEST(test_stale_ceiling_gives_null);
    RUN_TEST(test_stale_bench_gives_null);
    RUN_TEST(test_nan_stove_gives_null);
    RUN_TEST(test_ina260_absent_gives_null_power);
    return UNITY_END();
}
```

- [ ] **Step 3: Run tests — confirm they FAIL**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native --filter test_web_module 2>&1 | tail -10
```

Expected: Compile error — `web.h: No such file or directory`. Red phase confirmed.

- [ ] **Step 4: Commit failing tests**

```bash
git add test/test_web_module/
git commit -m "test(web): add failing tests for buildJson() — red phase"
```

---

## Task 5: Create `web.h/.cpp` — Make Web Tests Pass

**Files:**
- Create: `src/web.h`
- Create: `src/web.cpp`

`buildJson()` is inline in `web.h` (34 lines, no Arduino-specific types). All HTTP/WebSocket handlers go in `web.cpp`.

- [ ] **Step 1: Create `src/web.h`**

```cpp
// src/web.h
#pragma once

#include <math.h>
#include "globals.h"
#include "sauna_logic.h"

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
#include <WebSocketsServer.h>

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
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
```

**Important:** Check the current `buildJson()` in `main.cpp` at line 412. The `pwr_*` fields in the original may already null-guard on `ina260_ok` inside `buildJsonFull()`, or they may not. Compare the inline version above to the original and use the original's logic verbatim. The version above adds explicit NAN masking for `ina260_ok` — verify this matches what the test expects.

- [ ] **Step 2: Create `src/web.cpp`**

```cpp
// src/web.cpp
#include "web.h"
#include "influx.h"
#include "auth.h"
#include "ota_logic.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
// Add any other headers that the handler functions require
```

Then paste these functions verbatim from `main.cpp` (removing `static` where needed):
- `handleRoot()` — line 487
- `handleDeleteMeasurement()` — line 500 (keep `static`, used only by handleDeleteStatus/Control)
- `handleDeleteStatus()` — line 523
- `handleDeleteControl()` — line 524
- `handleMotorCmd()` — line 526
- `handlePidToggle()` — line 647
- `handleSetpoint()` — line 658
- `handleHistory()` — line 680
- `handleLog()` — line 722
- `webSocketEvent()` — line 738
- `handleOtaStatus()` — line 969
- `handleOtaUpdate()` — line 990
- `handleConfigPage()` — line 1133
- `handleConfigGet()` — line 1147
- `handleConfigSave()` — line 1166
- `handleAuthLoginPage()` — line 1327
- `handleAuthLogin()` — line 1336
- `handleAuthLogout()` — line 1372
- `handleAuthStatus()` — line 1384
- `handleUsersGet()` — line 1396
- And remaining user management handlers

**Do NOT modify the function bodies** — copy verbatim. Fix only what is needed for compilation (e.g., forward declarations, includes).

- [ ] **Step 3: Run web module tests — confirm they PASS**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native --filter test_web_module 2>&1 | tail -10
```

Expected: `6 test cases: 6 succeeded`. If any fail, debug before proceeding.

- [ ] **Step 4: Run ALL native tests**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -10
```

Expected: `124 test cases: 124 succeeded` (118 + 6 new)

- [ ] **Step 5: Compile firmware**

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -E "error:|SUCCESS"
```

- [ ] **Step 6: Commit**

```bash
git add src/web.h src/web.cpp test/test_web_module/
git commit -m "feat(web): extract web module; buildJson() inline in web.h for native tests"
```

---

## Task 6: Create `influx.h/.cpp`

**Files:**
- Create: `src/influx.h`
- Create: `src/influx.cpp`

`writeInflux()` is moved verbatim. `logAccessEvent()` gains a `client_ip` parameter to remove its dependency on the HTTP server object.

- [ ] **Step 1: Create `src/influx.h`**

```cpp
// src/influx.h
#pragma once

#include "globals.h"
#include "auth_logic.h"  // for AuthLogEvent

#ifdef ARDUINO

// Writes sauna_status and sauna_control measurements to InfluxDB.
// NaN-valued fields are omitted. Called every INFLUX_WRITE_INTERVAL_MS.
bool writeInflux();

// Writes a login/logout/failure event to sauna_webaccess measurement.
// client_ip must be provided by the caller (not extracted internally).
// Callers: server.client().remoteIP().toString().c_str()
void logAccessEvent(const char *event,
                    const char *username,
                    const char *auth_source,
                    const char *client_ip);

#endif // ARDUINO
```

- [ ] **Step 2: Create `src/influx.cpp`**

```cpp
// src/influx.cpp
#include "influx.h"
#include <Arduino.h>
```

Then paste `writeInflux()` verbatim from `main.cpp` (line 447). Remove the `static` keyword.

For `logAccessEvent()`, paste from `auth.h` (line 195) but:
- Add the `client_ip` parameter
- Replace `String ip = server.client().remoteIP().toString();` with using the provided `client_ip` directly
- Replace `authBuildLogEvent(event, username, ip.c_str(), auth_source)` with `authBuildLogEvent(event, username, client_ip, auth_source)`

```cpp
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
    influxClient.writePoint(webaccess);
}
```

- [ ] **Step 3: Compile firmware**

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -E "error:|SUCCESS"
```

- [ ] **Step 4: Run all native tests — 124 still pass**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add src/influx.h src/influx.cpp
git commit -m "feat(influx): extract influx module; logAccessEvent gains client_ip param"
```

---

## Task 7: Create `mqtt.h/.cpp`

**Files:**
- Create: `src/mqtt.h`
- Create: `src/mqtt.cpp`

All four MQTT functions moved verbatim.

- [ ] **Step 1: Create `src/mqtt.h`**

```cpp
// src/mqtt.h
#pragma once

#include "globals.h"

#ifdef ARDUINO

// Connects to MQTT broker, subscribes to control topics, publishes HA discovery.
void mqttConnect();

// PubSubClient callback — handles incoming messages on subscribed topics.
void mqttCallback(char *topic, byte *payload, unsigned int len);

// Publishes current sauna state JSON to sauna/state.
void mqttPublishState();

// Publishes Home Assistant MQTT Discovery configs (retained).
void mqttPublishDiscovery();

#endif // ARDUINO
```

- [ ] **Step 2: Create `src/mqtt.cpp`**

```cpp
// src/mqtt.cpp
#include "mqtt.h"
#include <Arduino.h>
#include <ArduinoJson.h>
```

Paste these functions verbatim from `main.cpp`, removing `static`:
- `mqttCallback()` — line 756
- `mqttPublishState()` — line 794
- `mqttPublishDiscovery()` — line 829
- `mqttConnect()` — line 906

- [ ] **Step 3: Compile firmware**

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -E "error:|SUCCESS"
```

- [ ] **Step 4: Run all native tests — 124 still pass**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add src/mqtt.h src/mqtt.cpp
git commit -m "feat(mqtt): extract mqtt module"
```

---

## Task 8: Slim `main.cpp` — Remove Extracted Code

**Files:**
- Modify: `src/main.cpp`

Now that all extracted functions exist in their modules, remove them from `main.cpp` and add the new `#include` lines. This is the highest-risk task — do it incrementally with a compile check after each group.

- [ ] **Step 1: Add includes at the top of `main.cpp`** (after existing includes)

```cpp
#include "globals.h"
#include "sensors.h"
#include "web.h"
#include "mqtt.h"
#include "influx.h"
```

- [ ] **Step 2: Remove functions now in `influx.cpp`**

Delete from `main.cpp`:
- `writeInflux()` (line 447)
- The forward declaration of `logAccessEvent` if any (it was in auth.h; verify no separate forward decl in main.cpp)

Compile check:
```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -c "error:"
```

Expected: 0 errors.

- [ ] **Step 3: Remove functions now in `mqtt.cpp`**

Delete from `main.cpp`:
- `mqttCallback()` (line 756)
- `mqttPublishState()` (line 794)
- `mqttPublishDiscovery()` (line 829)
- `mqttConnect()` (line 906)

Also remove the QuickPID instance declarations that were between webSocketEvent and mqttCallback (lines ~750–755) if they are NOT global definitions — verify these are globals and keep them.

Compile check:
```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -c "error:"
```

- [ ] **Step 4: Remove functions now in `sensors.cpp`**

Delete from `main.cpp`:
- `stoveReading()` (line 367) — now inline in sensors.h
- `checkOverheat()` (line 378) — now in sensors.cpp
- The sensor read block in `loop()` — replace it with a call to `readSensors()`

The sensor read block is approximately lines 1677–1730. After replacement:

```cpp
// Was: inline DHT/MAX31865/INA260 read block
readSensors();
```

Compile check:
```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -c "error:"
```

- [ ] **Step 5: Remove functions now in `web.cpp`**

Delete from `main.cpp`:
- `buildJson()` (line 412) — now inline in web.h
- All `handle*` functions (lines 487–524, 526, 647, 658, 680, 722)
- `webSocketEvent()` (line 738)
- OTA handlers (lines 969–1130)
- Config handlers (lines 1133–1326)
- Auth handlers (lines 1327–end of auth handlers)

Compile check:
```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -c "error:"
```

- [ ] **Step 6: Verify OTA helpers remain in `main.cpp`**

`otaCheckBootHealth()`, `otaMarkBootSuccessful()`, `otaCheckPartialDownload()` are Arduino-specific OTA helpers. Per the spec, they are NOT extracted into a separate module — they stay in `main.cpp`. Do not remove them.

- [ ] **Step 7: Final compile check**

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `[SUCCESS]`

- [ ] **Step 8: Run all native tests**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -5
```

Expected: `124 test cases: 124 succeeded`

- [ ] **Step 9: Commit**

```bash
git add src/main.cpp
git commit -m "refactor(main): remove extracted functions; main.cpp is now thin orchestrator"
```

---

## Task 9: Update `auth.h` — Fix `logAccessEvent()` Call Sites

**Files:**
- Modify: `src/auth.h`

The `logAccessEvent()` signature changed: it now takes a `client_ip` parameter. Update all call sites in `auth.h` to pass the client IP, and remove the old inline implementation.

- [ ] **Step 1: Find all call sites in `auth.h`**

```bash
grep -n "logAccessEvent" src/auth.h src/main.cpp src/web.cpp
```

Note every call site location.

- [ ] **Step 2: Update `auth.h`**

1. Remove the `inline void logAccessEvent(...)` implementation block (lines ~195–208)
2. Add `#include "influx.h"` at the top of `auth.h` (so call sites can use the new signature)
3. Update each `logAccessEvent(event, username, auth_source)` call to:
   ```cpp
   logAccessEvent(event, username, auth_source,
                  server.client().remoteIP().toString().c_str())
   ```

- [ ] **Step 3: Compile**

```bash
/home/mmessano/.platformio/penv/bin/pio run 2>&1 | grep -E "error:|SUCCESS"
```

- [ ] **Step 4: Run all native tests — 124 still pass**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add src/auth.h
git commit -m "refactor(auth): update logAccessEvent call sites with client_ip param"
```

---

## Task 10: Full Validation

**Files:** none modified

This is the gate before touching the frontend.

- [ ] **Step 1: Clean build**

```bash
/home/mmessano/.platformio/penv/bin/pio run -t clean && /home/mmessano/.platformio/penv/bin/pio run 2>&1 | tail -5
```

Expected: `[SUCCESS]`

- [ ] **Step 2: Run complete native test suite**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -10
```

Expected: `124 test cases: 124 succeeded`

- [ ] **Step 3: Validate JSON config**

```bash
python3 -m json.tool data/config.json && echo "JSON OK"
```

- [ ] **Step 4: Check no hardcoded user paths**

```bash
grep -rn '/home/' .claude/settings.local.json .claude/settings.json 2>/dev/null \
  && echo "ERROR: hardcoded paths found" || echo "OK: no hardcoded paths"
```

- [ ] **Step 5: Commit validation marker**

```bash
git commit --allow-empty -m "chore: firmware refactor complete — 124 tests passing, firmware builds clean"
```

---

## Task 11: Frontend — Inline Login & Pre-Auth Chart

**Files:**
- Modify: `data/index.html`

No server-side changes needed. `/history` is already unauthenticated. The changes are purely in the HTML/JS.

**What to change:**
1. Add a login panel (shown when unauthenticated)
2. Move `loadHistory()` / chart initialization to fire on page load (before auth check)
3. Sensor readings (WebSocket) remain visible pre-auth
4. Wrap action sections in a `div.auth-required` that is hidden until authenticated
5. On login success: add `body.authenticated` class, hide login panel
6. On logout: remove `body.authenticated` class, show login panel

- [ ] **Step 1: Read the current `data/index.html` in full before editing**

```bash
wc -l data/index.html
```

Read the file carefully — understand the current auth flow (`authFetch`, token check at top, redirect to `/auth/login`).

- [ ] **Step 2: Add login panel HTML**

Find the `<body>` tag. Immediately after it, add a login panel div:

```html
<!-- Login panel — shown when unauthenticated -->
<div id="login-panel" style="display:none; position:fixed; top:0; left:0; right:0; bottom:0;
     background:rgba(0,0,0,0.85); z-index:1000; display:flex; align-items:center; justify-content:center;">
  <div style="background:#1e1e2e; border-radius:12px; padding:2rem; width:320px; box-shadow:0 8px 32px rgba(0,0,0,0.5);">
    <h2 style="color:#cdd6f4; margin:0 0 1.5rem; text-align:center;">Sauna Status</h2>
    <input id="login-user" type="text" placeholder="Username"
           style="width:100%; padding:0.6rem; margin-bottom:0.75rem; border-radius:6px; border:1px solid #45475a; background:#313244; color:#cdd6f4; box-sizing:border-box;">
    <input id="login-pass" type="password" placeholder="Password"
           style="width:100%; padding:0.6rem; margin-bottom:1rem; border-radius:6px; border:1px solid #45475a; background:#313244; color:#cdd6f4; box-sizing:border-box;">
    <button onclick="doLogin()"
            style="width:100%; padding:0.75rem; background:#89b4fa; border:none; border-radius:6px; color:#1e1e2e; font-weight:bold; cursor:pointer;">
      Sign In
    </button>
    <p id="login-error" style="color:#f38ba8; font-size:0.85rem; margin:0.75rem 0 0; display:none;"></p>
  </div>
</div>
```

- [ ] **Step 3: Wrap action sections in `auth-required`**

Identify the action card sections (motor controls, PID controls, setpoints, logging buttons, user management). Wrap each with `class="auth-required"`:

```html
<div class="auth-required" style="display:none;">
  <!-- Motor control card content -->
</div>
```

Do this for each action section. Sensor reading cards stay outside — they are visible pre-auth.

- [ ] **Step 4: Add CSS for authenticated state**

Inside `<style>`:

```css
body.authenticated .auth-required { display: block !important; }
#login-panel { display: flex; }
body.authenticated #login-panel { display: none !important; }
```

- [ ] **Step 5: Update JavaScript auth flow**

Find the existing `DOMContentLoaded` listener and the `authFetch` / token-check logic. Make these changes:

**Remove:** The redirect to `/auth/login` on missing/invalid token.
**Add:** Show the login panel instead.

**Add `doLogin()` function:**

```javascript
async function doLogin() {
    const user = document.getElementById('login-user').value.trim();
    const pass = document.getElementById('login-pass').value;
    const err  = document.getElementById('login-error');
    err.style.display = 'none';
    try {
        const r = await fetch('/auth/login', {
            method: 'POST',
            headers: {'Content-Type':'application/json'},
            body: JSON.stringify({username: user, password: pass})
        });
        if (!r.ok) { err.textContent = 'Invalid username or password.'; err.style.display='block'; return; }
        const d = await r.json();
        localStorage.setItem('token', d.token);
        localStorage.setItem('username', d.username);
        localStorage.setItem('role', d.role);
        document.body.classList.add('authenticated');
        connect(); // start WebSocket
    } catch(e) {
        err.textContent = 'Connection error. Try again.'; err.style.display='block';
    }
}
```

**Update `logout()` function:**

```javascript
async function logout() {
    await authFetch('/auth/logout', {method:'POST'}).catch(()=>{});
    localStorage.removeItem('token');
    localStorage.removeItem('username');
    localStorage.removeItem('role');
    document.body.classList.remove('authenticated');
    // close WebSocket
    if (ws) { ws.close(); ws = null; }
}
```

**Update page load token check:**

```javascript
document.addEventListener('DOMContentLoaded', () => {
    loadHistory(); // always load chart — /history is unauthenticated
    const token = localStorage.getItem('token');
    if (token) {
        // Validate existing token
        fetch('/auth/status', {headers: {'Authorization': 'Bearer ' + token}})
            .then(r => r.ok ? r.json() : Promise.reject())
            .then(() => {
                document.body.classList.add('authenticated');
                connect();
            })
            .catch(() => {
                localStorage.removeItem('token');
                // login panel shown via CSS (body not .authenticated)
            });
    }
    // else: login panel shown via CSS
});
```

- [ ] **Step 6: Verify `loadHistory()` is called unconditionally**

Find `loadHistory()`. Confirm it is called in the `DOMContentLoaded` handler (step 5) regardless of auth state. Remove any existing auth guard on `loadHistory()`.

- [ ] **Step 7: Enter password with Enter key support**

Add `onkeydown` to the password field:

```html
<input id="login-pass" ... onkeydown="if(event.key==='Enter') doLogin()">
```

- [ ] **Step 8: Build filesystem and validate**

```bash
python3 -m json.tool data/config.json && echo "JSON OK"
```

Open `data/index.html` in a browser (file://) and verify:
- Login panel appears on load
- After entering credentials and clicking Sign In, the overlay hides and action cards appear
- The trend chart area is visible before login (may show loading/empty if no live server)

- [ ] **Step 9: Commit**

```bash
git add data/index.html
git commit -m "feat(ui): inline login form on landing page; trend chart visible pre-auth"
```

---

## Task 12: Update `CLAUDE.md`

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update test count**

Find the two places in `CLAUDE.md` that reference test counts:
- `Total: 8 + 9 + 12 + 35 + 17 = **81 tests**` — update to reflect actual count
- The section-by-section counts — verify each suite's count with `pio test -e native -v` if needed

Update to: `Total: 113 existing + 5 (test_sensor_module) + 6 (test_web_module) = **124 tests** across 7 suites`

- [ ] **Step 2: Add new files to Architecture section**

In the `### Firmware: src/main.cpp` section and surrounding architecture docs, add entries for the new modules:

```markdown
### `src/sensors.h/.cpp`
Sensor I/O: reads DHT21 ceiling/bench, MAX31865 stove, INA260 power. Updates globals.
- `stoveReading()` — inline in header; returns stove_temp or ceiling/bench average fallback
- `readSensors()` — reads all hardware sensors; applies || rule for last_ok_ms
- `checkOverheat()` — rising-edge state machine; drives vents open on alarm onset

### `src/web.h/.cpp`
All HTTP handlers and WebSocket event handler.
- `buildJson()` — inline in header; assembles globals into 23-field WebSocket JSON
- All `handle*()` functions — see HTTP Server table

### `src/mqtt.h/.cpp`
MQTT connection, publish, and subscription handling.

### `src/influx.h/.cpp`
InfluxDB writes. `logAccessEvent()` accepts `client_ip` as a parameter.

### `src/globals.h`
Single source of `extern` declarations for all globals defined in `main.cpp`.
Hardware objects wrapped in `#ifdef ARDUINO` guards for native test compatibility.
```

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -m "docs(claude-md): update test count to 124; document new module files"
```

---

## Task 13: Final Validation & Summary

- [ ] **Step 1: Full clean build**

```bash
/home/mmessano/.platformio/penv/bin/pio run -t clean && /home/mmessano/.platformio/penv/bin/pio run 2>&1 | tail -5
```

- [ ] **Step 2: Full test suite**

```bash
/home/mmessano/.platformio/penv/bin/pio test -e native 2>&1 | tail -10
```

Expected: `124 test cases: 124 succeeded`

- [ ] **Step 3: Report edge cases caught by new tests**

Review the test output and note which tests caught real issues during implementation (e.g., a test that failed because a global wasn't initialized, or an `ina260_ok` guard that was missing). Include in the commit message.

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
feat: complete modular refactor of main.cpp

Extracts sensor reading, HTTP/WebSocket, MQTT, and InfluxDB logic
into separate focused modules. Adds 11 new unit tests covering
stoveReading() fallback edge cases and buildJson() schema correctness.
Updates index.html with inline login and pre-auth trend chart.

New files: globals.h, sensors.h/.cpp, web.h/.cpp, mqtt.h/.cpp, influx.h/.cpp
New tests: test_sensor_module (5), test_web_module (6)
Result: 124 tests passing, firmware builds clean

Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
EOF
)"
```

---

## Edge Cases to Watch

| Area | Risk | Mitigation |
|---|---|---|
| `buildJson()` inline vs original | NaN masking for `pwr_*` fields may differ | Compare inline version to main.cpp:412 before committing |
| `globals.h` include order | Arduino SDK headers must not pollute native builds | Verify all hardware externs are inside `#ifdef ARDUINO` |
| `stoveReading()` isnan usage | `std::isnan` vs `isnan` may differ between Arduino and native | Use `std::isnan` in header; use `isnan` in sensors.cpp (Arduino env) |
| `readSensors()` extraction | Sensor read block in loop() may have setup dependencies | Copy the block exactly; do not optimize |
| `handleDeleteMeasurement()` | It's `static` in main.cpp and used only by two wrappers | Keep `static` in web.cpp |
| `logAccessEvent()` call sites | All callers must pass the new `client_ip` arg | `grep -rn logAccessEvent src/` before and after to verify count |
