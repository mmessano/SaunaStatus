#include <unity.h>
#include "sauna_logic.h"

void setUp(void) {}
void tearDown(void) {}

// SaunaConfig defaults: 160/120/false/false
void test_sauna_config_defaults(void) {
    SaunaConfig cfg;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
    TEST_ASSERT_FALSE(cfg.ceiling_pid_en);
    TEST_ASSERT_FALSE(cfg.bench_pid_en);
}

// Fleet config overrides defaults
void test_fleet_config_overrides_defaults(void) {
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 170.0f;
    fleet.bench_setpoint_f   = 130.0f;
    fleet.has_ceiling_sp     = true;
    fleet.has_bench_sp       = true;

    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 170.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 130.0f, cfg.bench_setpoint_f);
}

// Out-of-range values rejected (9999 F stays at default)
void test_out_of_range_rejected(void) {
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 9999.0f;
    fleet.has_ceiling_sp     = true;

    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f);
}

// Out-of-range low rejected (e.g. 10 F)
void test_out_of_range_low_rejected(void) {
    SaunaConfig cfg;
    ConfigLayer layer;
    layer.bench_setpoint_f = 10.0f;
    layer.has_bench_sp     = true;

    mergeConfigLayer(cfg, layer);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
}

// NVS wins over fleet: apply fleet first, then NVS layer
void test_nvs_wins_over_fleet(void) {
    SaunaConfig cfg;

    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 170.0f;
    fleet.has_ceiling_sp     = true;
    mergeConfigLayer(cfg, fleet);

    ConfigLayer nvs;
    nvs.ceiling_setpoint_f = 180.0f;
    nvs.has_ceiling_sp     = true;
    mergeConfigLayer(cfg, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, cfg.ceiling_setpoint_f);
}

// NVS missing key preserves fleet value
void test_nvs_missing_key_preserves_fleet(void) {
    SaunaConfig cfg;

    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 175.0f;
    fleet.has_ceiling_sp     = true;
    mergeConfigLayer(cfg, fleet);

    // NVS layer has no ceiling key
    ConfigLayer nvs;
    nvs.has_ceiling_sp = false;
    mergeConfigLayer(cfg, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 175.0f, cfg.ceiling_setpoint_f);
}

// Partial fleet config (only ceiling)
void test_partial_fleet_only_ceiling(void) {
    SaunaConfig cfg;

    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 165.0f;
    fleet.has_ceiling_sp     = true;
    // bench not set
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 165.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f); // unchanged default
}

// PID enable flag
void test_pid_enable_flag(void) {
    SaunaConfig cfg;

    ConfigLayer fleet;
    fleet.ceiling_pid_en = true;
    fleet.has_ceiling_en = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_TRUE(cfg.ceiling_pid_en);
    TEST_ASSERT_FALSE(cfg.bench_pid_en);
}

// NVS can disable fleet-enabled PID
void test_nvs_can_disable_fleet_pid(void) {
    SaunaConfig cfg;

    ConfigLayer fleet;
    fleet.ceiling_pid_en = true;
    fleet.has_ceiling_en = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_TRUE(cfg.ceiling_pid_en);

    ConfigLayer nvs;
    nvs.ceiling_pid_en = false;
    nvs.has_ceiling_en = true;
    mergeConfigLayer(cfg, nvs);

    TEST_ASSERT_FALSE(cfg.ceiling_pid_en);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_sauna_config_defaults);
    RUN_TEST(test_fleet_config_overrides_defaults);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_out_of_range_low_rejected);
    RUN_TEST(test_nvs_wins_over_fleet);
    RUN_TEST(test_nvs_missing_key_preserves_fleet);
    RUN_TEST(test_partial_fleet_only_ceiling);
    RUN_TEST(test_pid_enable_flag);
    RUN_TEST(test_nvs_can_disable_fleet_pid);
    return UNITY_END();
}
