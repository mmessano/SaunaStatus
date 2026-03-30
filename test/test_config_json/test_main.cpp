// test/test_config_json/test_main.cpp
// RED phase: tests for buildConfigJson() pure serializer function.
// Must be added to src/sauna_logic.h (pure C++, no Arduino deps).
// Run: pio test -e native -f test_config_json
//
// buildConfigJson(const SaunaConfig& cfg, char* buf, size_t len)
// Output format: {"csp_f":<float>,"bsp_f":<float>,"cen":<0|1>,"ben":<0|1>}
//   csp_f — ceiling setpoint in °F (one decimal place)
//   bsp_f — bench setpoint in °F (one decimal place)
//   cen   — ceiling PID enabled flag (0 or 1)
//   ben   — bench PID enabled flag (0 or 1)
// Setpoints stored internally in °C; the serializer converts to °F at the boundary.
#include <unity.h>
#include "sauna_logic.h"
#include <cstring>
#include <cstdio>

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Structure and key presence
// =============================================================================

void test_config_json_starts_with_brace(void) {
    SaunaConfig cfg{};
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_CHAR('{', buf[0]);
}

void test_config_json_ends_with_brace(void) {
    SaunaConfig cfg{};
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    size_t len = strlen(buf);
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_CHAR('}', buf[len - 1]);
}

void test_config_json_contains_all_keys(void) {
    SaunaConfig cfg{};
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"csp_f\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bsp_f\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cen\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ben\""));
}

// =============================================================================
// Default values
// =============================================================================

// Default SaunaConfig: ceiling=160°F, bench=120°F, both PIDs disabled
void test_config_json_default_ceiling_setpoint(void) {
    SaunaConfig cfg{};  // ceiling_setpoint_f = 160.0f by default
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"csp_f\":160.0"), buf);
}

void test_config_json_default_bench_setpoint(void) {
    SaunaConfig cfg{};  // bench_setpoint_f = 120.0f by default
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"bsp_f\":120.0"), buf);
}

void test_config_json_default_ceiling_pid_disabled(void) {
    SaunaConfig cfg{};  // ceiling_pid_en = false by default
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cen\":0"));
}

void test_config_json_default_bench_pid_disabled(void) {
    SaunaConfig cfg{};  // bench_pid_en = false by default
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ben\":0"));
}

// =============================================================================
// PID enabled flags
// =============================================================================

void test_config_json_ceiling_pid_enabled(void) {
    SaunaConfig cfg{};
    cfg.ceiling_pid_en = true;
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cen\":1"));
}

void test_config_json_bench_pid_enabled(void) {
    SaunaConfig cfg{};
    cfg.bench_pid_en = true;
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ben\":1"));
}

void test_config_json_both_pids_enabled(void) {
    SaunaConfig cfg{};
    cfg.ceiling_pid_en = true;
    cfg.bench_pid_en   = true;
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"cen\":1"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"ben\":1"));
}

// =============================================================================
// Setpoint conversion and round-trip (°C stored, °F output)
// =============================================================================

// SaunaConfig stores setpoints in °F directly (not °C) — verify passthrough
void test_config_json_custom_ceiling_setpoint(void) {
    SaunaConfig cfg{};
    cfg.ceiling_setpoint_f = 180.0f;
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"csp_f\":180.0"), buf);
}

void test_config_json_custom_bench_setpoint(void) {
    SaunaConfig cfg{};
    cfg.bench_setpoint_f = 140.0f;
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL_MESSAGE(strstr(buf, "\"bsp_f\":140.0"), buf);
}

// Minimum valid setpoint (32°F = 0°C)
void test_config_json_min_setpoint(void) {
    SaunaConfig cfg{};
    cfg.ceiling_setpoint_f = 32.0f;
    cfg.bench_setpoint_f   = 32.0f;
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"csp_f\":32.0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bsp_f\":32.0"));
}

// Maximum valid setpoint (300°F)
void test_config_json_max_setpoint(void) {
    SaunaConfig cfg{};
    cfg.ceiling_setpoint_f = 300.0f;
    cfg.bench_setpoint_f   = 300.0f;
    char buf[128];
    buildConfigJson(cfg, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"csp_f\":300.0"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"bsp_f\":300.0"));
}

// =============================================================================
// Buffer safety
// =============================================================================

// Output must fit in a 64-byte buffer (minimum reasonable size)
void test_config_json_fits_in_64_bytes(void) {
    SaunaConfig cfg{};
    cfg.ceiling_setpoint_f = 300.0f;
    cfg.bench_setpoint_f   = 300.0f;
    cfg.ceiling_pid_en     = true;
    cfg.bench_pid_en       = true;
    char buf[64];
    buildConfigJson(cfg, buf, sizeof(buf));
    size_t len = strlen(buf);
    TEST_ASSERT_LESS_THAN_MESSAGE(64, (int)(len + 1), "output + null must fit in 64 bytes");
    TEST_ASSERT_EQUAL_CHAR('}', buf[len - 1]);  // not truncated
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_config_json_starts_with_brace);
    RUN_TEST(test_config_json_ends_with_brace);
    RUN_TEST(test_config_json_contains_all_keys);
    RUN_TEST(test_config_json_default_ceiling_setpoint);
    RUN_TEST(test_config_json_default_bench_setpoint);
    RUN_TEST(test_config_json_default_ceiling_pid_disabled);
    RUN_TEST(test_config_json_default_bench_pid_disabled);
    RUN_TEST(test_config_json_ceiling_pid_enabled);
    RUN_TEST(test_config_json_bench_pid_enabled);
    RUN_TEST(test_config_json_both_pids_enabled);
    RUN_TEST(test_config_json_custom_ceiling_setpoint);
    RUN_TEST(test_config_json_custom_bench_setpoint);
    RUN_TEST(test_config_json_min_setpoint);
    RUN_TEST(test_config_json_max_setpoint);
    RUN_TEST(test_config_json_fits_in_64_bytes);
    return UNITY_END();
}
