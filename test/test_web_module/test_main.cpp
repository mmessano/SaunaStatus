// test/test_web_module/test_main.cpp
#include <unity.h>
#include <cstring>
#include <cmath>
#include <limits>
#include "web.h"   // buildJson() is inline here

static char buf[512];

// Extern declarations for globals defined in test_globals.cpp
extern float ceiling_temp;
extern float ceiling_hum;
extern float bench_temp;
extern float bench_hum;
extern float stove_temp;
extern float pwr_bus_V;
extern float pwr_current_mA;
extern float pwr_mW;
extern unsigned long ceiling_last_ok_ms;
extern unsigned long bench_last_ok_ms;
extern bool ina260_ok;
extern unsigned short outflow_pos;
extern int outflow_dir;
extern unsigned short inflow_pos;
extern int inflow_dir;
extern float ceiling_output;
extern float bench_output;
extern bool ceiling_pid_en;
extern bool bench_pid_en;
extern bool c_cons_mode;
extern bool b_cons_mode;
extern bool overheat_alarm;
extern float Ceilingpoint;
extern float Benchpoint;

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
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"clh\":null"));
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
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"d5h\":null"));
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
