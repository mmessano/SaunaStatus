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

// --- New edge case tests ---

// After reconnection: sensor was stale (last_ok_ms=0), now has a fresh timestamp.
// Expect: cst:0, clt has a valid value (not null).
void test_reconnect_after_stale_restores_value(void) {
    SensorValues sv;
    sv.ceiling_temp       = 75.0f;
    sv.ceiling_hum        = 55.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 9900;   // just reconnected
    sv.bench_last_ok_ms   = 9900;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 10000UL, buf, sizeof(buf)); // diff=100 < threshold

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"cst\":0"), "ceiling should not be stale after reconnect");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "\"clt\":null"), "ceiling temp should appear (not null) after reconnect");
}

// Stale sensor with NaN value stored: both code paths lead to null.
// The stale branch forces NaN regardless; this test confirms no residual valid float leaks through.
void test_stale_with_nan_value_still_null(void) {
    SensorValues sv;
    sv.ceiling_temp       = std::numeric_limits<float>::quiet_NaN();  // NaN AND stale
    sv.ceiling_hum        = std::numeric_limits<float>::quiet_NaN();
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
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"clh\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":1"));
    // bench is valid and not stale
    TEST_ASSERT_NULL(strstr(buf, "\"d5t\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":0"));
}

// Simultaneous failure: both sensors stale — all 4 DHT fields must be null.
void test_simultaneous_both_sensors_stale_all_fields_null(void) {
    SensorValues sv;
    sv.ceiling_temp       = 71.0f;  // stored float, but stale -> must be suppressed
    sv.ceiling_hum        = 45.0f;
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 0;      // both never read -> both stale
    sv.bench_last_ok_ms   = 0;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 1UL, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"clt\":null"), "ceiling temp must be null when stale");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"clh\":null"), "ceiling hum must be null when stale");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"d5t\":null"), "bench temp must be null when stale");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"d5h\":null"), "bench hum must be null when stale");
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cst\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bst\":1"));
}

// Partial DHT failure: ceiling temp is NaN, humidity is valid, sensor not stale.
// Real DHT21 behaviour: individual fields can fail independently within one read cycle.
// Expect: clt:null (NaN temp), but clh has a numeric value (hum succeeded).
void test_partial_dht_failure_temp_nan_hum_valid(void) {
    SensorValues sv;
    sv.ceiling_temp       = std::numeric_limits<float>::quiet_NaN();  // temp failed
    sv.ceiling_hum        = 52.0f;                                    // hum OK
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 5000;   // sensor alive (humidity succeeded -> || logic updated timestamp)
    sv.bench_last_ok_ms   = 5000;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 6000UL, buf, sizeof(buf)); // diff=1000 < threshold, not stale

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"clt\":null"),  "NaN temp should be null");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "\"clh\":null"),      "valid hum should not be null");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"cst\":0"),     "sensor should not be stale (hum kept it alive)");
}

// Reconnect with partial recovery: humidity came back, temp still NaN.
// The || in main.cpp updates last_ok_ms when either reading succeeds, so sensor is not stale.
// Expect: cst:0 (not stale), clt:null (temp still NaN), clh has value.
void test_reconnect_partial_hum_ok_temp_nan(void) {
    SensorValues sv;
    sv.ceiling_temp       = std::numeric_limits<float>::quiet_NaN();  // temp still broken
    sv.ceiling_hum        = 48.0f;                                    // hum just recovered
    sv.bench_temp         = 65.0f;
    sv.bench_hum          = 40.0f;
    sv.stove_temp         = 200.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 9950;   // hum success just updated this
    sv.bench_last_ok_ms   = 9950;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 10000UL, buf, sizeof(buf)); // diff=50 < threshold

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"cst\":0"),    "sensor should not be stale after hum recovery");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"clt\":null"), "temp still NaN -> clt must be null");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf, "\"clh\":null"),     "recovered hum must not be null");
}

// Disconnect scenario: sensor was providing readings (last_ok_ms=1000),
// then goes silent — at now_ms=11001 it exceeds the 10s threshold.
// This is the real "physical disconnect" path distinct from last_ok_ms==0.
void test_disconnect_elapsed_time_becomes_stale(void) {
    SensorValues sv;
    sv.ceiling_temp       = 72.0f;  // last known value — must NOT appear in output
    sv.ceiling_hum        = 50.0f;
    sv.bench_temp         = 60.0f;
    sv.bench_hum          = 38.0f;
    sv.stove_temp         = 180.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 1000;   // was alive at t=1000
    sv.bench_last_ok_ms   = 1000;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 11001UL, buf, sizeof(buf));  // 11001-1000=10001 > 10000

    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"cst\":1"),    "ceiling should be stale after disconnect");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"clt\":null"), "stale ceiling temp must be null, not last-known value");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"clh\":null"), "stale ceiling hum must be null");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"bst\":1"),    "bench should also be stale");
}

// Broadcast timing: sensor is fresh on the first broadcast and stale on the second.
// Calls buildJsonFull twice with increasing now_ms to verify the transition happens
// at exactly the right moment (matching the 2-second sensor read cycle cadence).
void test_broadcast_timing_fresh_then_stale(void) {
    SensorValues sv;
    sv.ceiling_temp       = 73.0f;
    sv.ceiling_hum        = 46.0f;
    sv.bench_temp         = 61.0f;
    sv.bench_hum          = 39.0f;
    sv.stove_temp         = 185.0f;
    sv.pwr_bus_V          = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_current_mA     = std::numeric_limits<float>::quiet_NaN();
    sv.pwr_mW             = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 10000;  // last successful read at t=10000
    sv.bench_last_ok_ms   = 10000;
    sv.stale_threshold_ms = 10000;

    MotorState ms{};
    PIDState ps{};

    // First broadcast at t=20000: age=10000, exactly at threshold — NOT stale (strict >)
    char buf1[512];
    buildJsonFull(sv, ms, ps, 20000UL, buf1, sizeof(buf1));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf1, "\"cst\":0"), "at threshold boundary: should not be stale");
    TEST_ASSERT_NULL_MESSAGE(strstr(buf1, "\"clt\":null"),  "at threshold boundary: temp should be valid");

    // Second broadcast at t=20001: age=10001 > threshold — NOW stale
    char buf2[512];
    buildJsonFull(sv, ms, ps, 20001UL, buf2, sizeof(buf2));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf2, "\"cst\":1"),    "one ms past threshold: should be stale");
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf2, "\"clt\":null"), "one ms past threshold: temp must be null");
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
    RUN_TEST(test_reconnect_after_stale_restores_value);
    RUN_TEST(test_stale_with_nan_value_still_null);
    RUN_TEST(test_simultaneous_both_sensors_stale_all_fields_null);
    RUN_TEST(test_partial_dht_failure_temp_nan_hum_valid);
    RUN_TEST(test_reconnect_partial_hum_ok_temp_nan);
    RUN_TEST(test_disconnect_elapsed_time_becomes_stale);
    RUN_TEST(test_broadcast_timing_fresh_then_stale);
    return UNITY_END();
}
