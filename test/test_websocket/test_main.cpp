#include <unity.h>
#include "sauna_logic.h"
#include <cstring>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

static void make_full_json(char *buf, size_t len) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = 12.0f;
    sv.pwr_current_mA     = 500.0f;
    sv.pwr_mW             = 6000.0f;
    sv.ceiling_last_ok_ms = 1000;
    sv.bench_last_ok_ms   = 1000;
    sv.stale_threshold_ms = 10000;

    MotorState ms;
    ms.outflow_pos = 50;
    ms.outflow_dir = 1;
    ms.inflow_pos  = 25;
    ms.inflow_dir  = -1;

    PIDState ps;
    ps.ceiling_output = 128.0f;
    ps.bench_output   = 64.0f;
    ps.c_cons_mode    = true;
    ps.b_cons_mode    = false;
    ps.ceiling_pid_en = true;
    ps.bench_pid_en   = false;
    ps.Ceilingpoint   = 71.0f;
    ps.Benchpoint     = 60.0f;
    ps.overheat_alarm = false;

    buildJsonFull(sv, ms, ps, 2000UL, buf, len);
}

// JSON contains all 23 required keys
void test_json_contains_all_keys(void) {
    char buf[512];
    make_full_json(buf, sizeof(buf));

    const char *keys[] = {
        "\"clt\"", "\"clh\"", "\"d5t\"", "\"d5h\"", "\"tct\"",
        "\"ofs\"", "\"ofd\"", "\"ifs\"", "\"ifd\"",
        "\"csp\"", "\"cop\"", "\"ctm\"", "\"cen\"",
        "\"bsp\"", "\"bop\"", "\"btm\"", "\"ben\"",
        "\"pvolt\"", "\"pcurr\"", "\"pmw\"", "\"oa\"",
        "\"cst\"", "\"bst\""
    };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, keys[i]), keys[i]);
    }
}

// JSON starts with { and ends with }
void test_json_braces(void) {
    char buf[512];
    make_full_json(buf, sizeof(buf));
    TEST_ASSERT_TRUE(buf[0] == '{');
    size_t len = strlen(buf);
    TEST_ASSERT_TRUE(buf[len - 1] == '}');
}

// stale ceiling -> clt:null + cst:1, bench still valid
void test_stale_ceiling_gives_null(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 0;      // never read -> stale
    sv.bench_last_ok_ms   = 5000;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 6000UL, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"clt\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":1"));
    // bench should be valid (not null)
    TEST_ASSERT_NULL(strstr(buf, "\"d5t\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":0"));
}

// stale bench -> d5t:null + bst:1, ceiling valid
void test_stale_bench_gives_null(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 5000;
    sv.bench_last_ok_ms   = 0;      // never read -> stale
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 6000UL, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d5t\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":1"));
    TEST_ASSERT_NULL(strstr(buf, "\"clt\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":0"));
}

// fresh readings not stale
void test_fresh_readings_not_stale(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 9000;
    sv.bench_last_ok_ms   = 9000;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 10000UL, buf, sizeof(buf));

    // 10000 - 9000 = 1000 < 10000 threshold -> not stale
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":0"));
}

// never-read sensor is stale (last_ok_ms == 0)
void test_never_read_sensor_stale(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 0;  // never read
    sv.bench_last_ok_ms   = 0;  // never read
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 1UL, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":1"));
}

// stale disabled (threshold=0) -> never stale
void test_stale_disabled_threshold_zero(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 0;  // would be stale normally
    sv.bench_last_ok_ms   = 0;
    sv.stale_threshold_ms = 0;  // disabled

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 999999UL, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":0"));
}

// overheat alarm in JSON
void test_overheat_alarm_in_json(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 1000;
    sv.bench_last_ok_ms   = 1000;
    sv.stale_threshold_ms = 0;

    MotorState ms{};
    PIDState ps;
    ps.overheat_alarm = true;

    char buf[512];
    buildJsonFull(sv, ms, ps, 0UL, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"oa\":1"));
}

// motor positions in JSON
void test_motor_positions_in_json(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 1000;
    sv.bench_last_ok_ms   = 1000;
    sv.stale_threshold_ms = 0;

    MotorState ms;
    ms.outflow_pos = 75;
    ms.outflow_dir = 1;
    ms.inflow_pos  = 33;
    ms.inflow_dir  = -1;

    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 0UL, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ofs\":75"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ofd\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ifs\":33"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ifd\":-1"));
}

// Staleness boundary: exactly at threshold = NOT stale
void test_staleness_exactly_at_threshold_not_stale(void) {
    // now_ms - last_ok_ms == threshold_ms -> NOT stale (uses strict >)
    TEST_ASSERT_FALSE(isSensorStale(1000UL, 11000UL, 10000UL)); // diff == threshold
}

// Staleness boundary: 1ms over threshold = stale
void test_staleness_one_over_threshold_is_stale(void) {
    TEST_ASSERT_TRUE(isSensorStale(1000UL, 11001UL, 10000UL)); // diff > threshold
}

// threshold=0 never stale
void test_staleness_threshold_zero_never_stale(void) {
    TEST_ASSERT_FALSE(isSensorStale(0UL, 999999UL, 0UL));
    TEST_ASSERT_FALSE(isSensorStale(1000UL, 999999UL, 0UL));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_json_contains_all_keys);
    RUN_TEST(test_json_braces);
    RUN_TEST(test_stale_ceiling_gives_null);
    RUN_TEST(test_stale_bench_gives_null);
    RUN_TEST(test_fresh_readings_not_stale);
    RUN_TEST(test_never_read_sensor_stale);
    RUN_TEST(test_stale_disabled_threshold_zero);
    RUN_TEST(test_overheat_alarm_in_json);
    RUN_TEST(test_motor_positions_in_json);
    RUN_TEST(test_staleness_exactly_at_threshold_not_stale);
    RUN_TEST(test_staleness_one_over_threshold_is_stale);
    RUN_TEST(test_staleness_threshold_zero_never_stale);
    return UNITY_END();
}
