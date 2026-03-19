#include <unity.h>
#include "sauna_logic.h"
#include <cmath>
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

// fmtVal NaN -> "null"
void test_fmtVal_nan(void) {
    char buf[16];
    fmtVal(buf, sizeof(buf), std::numeric_limits<float>::quiet_NaN());
    TEST_ASSERT_EQUAL_STRING("null", buf);
}

// fmtVal valid value
void test_fmtVal_valid(void) {
    char buf[16];
    fmtVal(buf, sizeof(buf), 72.5f);
    TEST_ASSERT_EQUAL_STRING("72.5", buf);
}

// c2f conversion
void test_c2f(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.0f, c2f(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 212.0f, c2f(100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, c2f(71.111f));
}

// f2c conversion
void test_f2c(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, f2c(32.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, f2c(212.0f));
}

// c2f/f2c round trip
void test_c2f_f2c_roundtrip(void) {
    float orig = 75.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, orig, f2c(c2f(orig)));
}

// ceiling NaN -> clt:null in JSON
void test_ceiling_nan_gives_null_in_json(void) {
    SensorValues sv;
    sv.ceiling_temp = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_hum  = std::numeric_limits<float>::quiet_NaN();
    sv.bench_temp   = 25.0f;
    sv.bench_hum    = 50.0f;
    sv.stove_temp   = 80.0f;
    sv.ceiling_last_ok_ms = 0;
    sv.bench_last_ok_ms   = 1000;
    sv.stale_threshold_ms = 0; // disable stale

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 0, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"clt\":null"));
}

// bench NaN independent of ceiling
void test_bench_nan_independent(void) {
    SensorValues sv;
    sv.ceiling_temp = 70.0f;
    sv.ceiling_hum  = 45.0f;
    sv.bench_temp   = std::numeric_limits<float>::quiet_NaN();
    sv.bench_hum    = std::numeric_limits<float>::quiet_NaN();
    sv.stove_temp   = 80.0f;
    sv.ceiling_last_ok_ms = 1000;
    sv.bench_last_ok_ms   = 0;
    sv.stale_threshold_ms = 0; // disable stale

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 0, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d5t\":null"));
    // ceiling should NOT be null
    TEST_ASSERT_NULL(strstr(buf, "\"clt\":null"));
}

// both sensors NaN simultaneously
void test_both_sensors_nan(void) {
    SensorValues sv;
    sv.ceiling_temp = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_hum  = std::numeric_limits<float>::quiet_NaN();
    sv.bench_temp   = std::numeric_limits<float>::quiet_NaN();
    sv.bench_hum    = std::numeric_limits<float>::quiet_NaN();
    sv.stove_temp   = std::numeric_limits<float>::quiet_NaN();
    sv.ceiling_last_ok_ms = 0;
    sv.bench_last_ok_ms   = 0;
    sv.stale_threshold_ms = 0;

    MotorState ms{};
    PIDState ps{};

    char buf[512];
    buildJsonFull(sv, ms, ps, 0, buf, sizeof(buf));

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"clt\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d5t\":null"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"tct\":null"));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_fmtVal_nan);
    RUN_TEST(test_fmtVal_valid);
    RUN_TEST(test_c2f);
    RUN_TEST(test_f2c);
    RUN_TEST(test_c2f_f2c_roundtrip);
    RUN_TEST(test_ceiling_nan_gives_null_in_json);
    RUN_TEST(test_bench_nan_independent);
    RUN_TEST(test_both_sensors_nan);
    return UNITY_END();
}
