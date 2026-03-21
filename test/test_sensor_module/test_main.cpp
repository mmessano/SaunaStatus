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
