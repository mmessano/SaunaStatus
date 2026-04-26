#include <unity.h>
#include "sauna_logic.h"

void setUp(void) {}
void tearDown(void) {}

// Helper: run the full 3-tier load sequence from a clean SaunaConfig.
// fleet  → models a successfully parsed /config.json (or empty layer if parse failed)
// nvs    → models NVS keys present (or empty layer if no keys exist)
static SaunaConfig runLoadSequence(const ConfigLayer &fleet, const ConfigLayer &nvs)
{
    SaunaConfig cfg;          // Layer 1: build-flag / compiled defaults
    mergeConfigLayer(cfg, fleet); // Layer 2: LittleFS /config.json
    mergeConfigLayer(cfg, nvs);   // Layer 3: NVS Preferences
    return cfg;
}

static FleetRuntimeConfig defaultFleetRuntime(void)
{
    FleetRuntimeConfig runtime;
    runtime.sauna.ceiling_setpoint_f = 160.0f;
    runtime.sauna.bench_setpoint_f = 120.0f;
    runtime.sauna.ceiling_pid_en = false;
    runtime.sauna.bench_pid_en = false;
    runtime.sensor_read_interval_ms = 5000UL;
    runtime.serial_log_interval_ms = 10000UL;
    strncpy(runtime.static_ip_str, "192.168.1.201", sizeof(runtime.static_ip_str) - 1);
    runtime.static_ip_str[sizeof(runtime.static_ip_str) - 1] = '\0';
    strncpy(runtime.device_name, "ESP32-S3", sizeof(runtime.device_name) - 1);
    runtime.device_name[sizeof(runtime.device_name) - 1] = '\0';
    return runtime;
}

static void assertDefaultFleetRuntime(const FleetRuntimeConfig &runtime)
{
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, runtime.sauna.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, runtime.sauna.bench_setpoint_f);
    TEST_ASSERT_FALSE(runtime.sauna.ceiling_pid_en);
    TEST_ASSERT_FALSE(runtime.sauna.bench_pid_en);
    TEST_ASSERT_EQUAL_UINT32(5000UL, runtime.sensor_read_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(10000UL, runtime.serial_log_interval_ms);
    TEST_ASSERT_EQUAL_STRING("192.168.1.201", runtime.static_ip_str);
    TEST_ASSERT_EQUAL_STRING("ESP32-S3", runtime.device_name);
}

// =============================================================================
// Scenario 1 — Default config loads correctly when no saved config exists
// =============================================================================

// All four fields are at their compile-time defaults out of the box.
void test_sauna_config_defaults(void)
{
    SaunaConfig cfg;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
    TEST_ASSERT_FALSE(cfg.ceiling_pid_en);
    TEST_ASSERT_FALSE(cfg.bench_pid_en);
}

// Applying two empty layers (no file, no NVS) leaves defaults untouched.
void test_no_layers_all_defaults(void)
{
    ConfigLayer fleet; // all has_* = false (file missing / parse error)
    ConfigLayer nvs;   // all has_* = false (no NVS keys written yet)
    SaunaConfig cfg = runLoadSequence(fleet, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
    TEST_ASSERT_FALSE(cfg.ceiling_pid_en);
    TEST_ASSERT_FALSE(cfg.bench_pid_en);
}

// An empty fleet layer (JSON file not found) is a no-op regardless of existing state.
void test_empty_fleet_layer_is_noop(void)
{
    SaunaConfig cfg;
    cfg.ceiling_setpoint_f = 175.0f; // simulate a value set by a previous layer

    ConfigLayer fleet; // no has_* set
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 175.0f, cfg.ceiling_setpoint_f);
}

// =============================================================================
// Scenario 2 — Device-level (NVS) overrides take precedence over fleet defaults
// =============================================================================

// Fleet overrides defaults.
void test_fleet_config_overrides_defaults(void)
{
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

// Full three-tier chain: NVS wins over fleet on all four fields.
void test_full_three_tier_nvs_wins_all(void)
{
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 170.0f;
    fleet.bench_setpoint_f   = 130.0f;
    fleet.ceiling_pid_en     = true;
    fleet.bench_pid_en       = true;
    fleet.has_ceiling_sp     = true;
    fleet.has_bench_sp       = true;
    fleet.has_ceiling_en     = true;
    fleet.has_bench_en       = true;

    ConfigLayer nvs;
    nvs.ceiling_setpoint_f = 185.0f;
    nvs.bench_setpoint_f   = 145.0f;
    nvs.ceiling_pid_en     = false;
    nvs.bench_pid_en       = false;
    nvs.has_ceiling_sp     = true;
    nvs.has_bench_sp       = true;
    nvs.has_ceiling_en     = true;
    nvs.has_bench_en       = true;

    SaunaConfig cfg = runLoadSequence(fleet, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 185.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 145.0f, cfg.bench_setpoint_f);
    TEST_ASSERT_FALSE(cfg.ceiling_pid_en);
    TEST_ASSERT_FALSE(cfg.bench_pid_en);
}

// NVS overrides ceiling setpoint only; fleet bench and PID flags are preserved.
void test_nvs_partial_override_preserves_fleet_fields(void)
{
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 170.0f;
    fleet.bench_setpoint_f   = 130.0f;
    fleet.ceiling_pid_en     = true;
    fleet.bench_pid_en       = true;
    fleet.has_ceiling_sp     = true;
    fleet.has_bench_sp       = true;
    fleet.has_ceiling_en     = true;
    fleet.has_bench_en       = true;

    ConfigLayer nvs;
    nvs.ceiling_setpoint_f = 190.0f;
    nvs.has_ceiling_sp     = true;
    // bench_sp, ceiling_en, bench_en all absent from NVS

    SaunaConfig cfg = runLoadSequence(fleet, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 190.0f, cfg.ceiling_setpoint_f); // NVS wins
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 130.0f, cfg.bench_setpoint_f);   // fleet preserved
    TEST_ASSERT_TRUE(cfg.ceiling_pid_en);  // fleet PID preserved (no NVS key)
    TEST_ASSERT_TRUE(cfg.bench_pid_en);    // fleet PID preserved
}

// NVS wins over fleet setpoint.
void test_nvs_wins_over_fleet(void)
{
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

// NVS missing key leaves fleet value intact.
void test_nvs_missing_key_preserves_fleet(void)
{
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 175.0f;
    fleet.has_ceiling_sp     = true;
    mergeConfigLayer(cfg, fleet);

    ConfigLayer nvs;
    nvs.has_ceiling_sp = false;
    mergeConfigLayer(cfg, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 175.0f, cfg.ceiling_setpoint_f);
}

// NVS can disable a PID that fleet enabled.
void test_nvs_can_disable_fleet_pid(void)
{
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

// =============================================================================
// Scenario 3 — Corrupted / missing SPIFFS config falls back gracefully
// =============================================================================

// Corrupted fleet JSON (parse error → empty ConfigLayer): compiled defaults win.
void test_corrupted_fleet_preserves_defaults(void)
{
    ConfigLayer bad_fleet; // all has_* = false — simulates a parse error
    ConfigLayer nvs;       // also absent

    SaunaConfig cfg = runLoadSequence(bad_fleet, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
    TEST_ASSERT_FALSE(cfg.ceiling_pid_en);
    TEST_ASSERT_FALSE(cfg.bench_pid_en);
}

// Corrupted fleet: NVS Layer 3 still applies on top of compiled defaults.
void test_corrupted_fleet_nvs_still_applied(void)
{
    ConfigLayer bad_fleet; // parse error → skipped

    ConfigLayer nvs;
    nvs.ceiling_setpoint_f = 155.0f;
    nvs.has_ceiling_sp     = true;

    SaunaConfig cfg = runLoadSequence(bad_fleet, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 155.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f); // default (no NVS key)
}

// Fleet JSON parsed but contains one bad value and one valid value;
// invalid field is rejected per-field — valid field still applied.
void test_out_of_range_fleet_rejected_per_field(void)
{
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 500.0f; // too high — rejected
    fleet.bench_setpoint_f   = 125.0f; // valid
    fleet.has_ceiling_sp     = true;
    fleet.has_bench_sp       = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f); // default retained
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 125.0f, cfg.bench_setpoint_f);   // accepted
}

// Values above 300 °F are rejected.
void test_out_of_range_rejected(void)
{
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 9999.0f;
    fleet.has_ceiling_sp     = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f);
}

// Values below 32 °F are rejected.
void test_out_of_range_low_rejected(void)
{
    SaunaConfig cfg;
    ConfigLayer layer;
    layer.bench_setpoint_f = 10.0f;
    layer.has_bench_sp     = true;
    mergeConfigLayer(cfg, layer);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
}

// Exact boundary values (32.0 and 300.0) are accepted.
void test_boundary_setpoints_accepted(void)
{
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 300.0f;
    fleet.bench_setpoint_f   = 32.0f;
    fleet.has_ceiling_sp     = true;
    fleet.has_bench_sp       = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 300.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 32.0f,  cfg.bench_setpoint_f);
}

// Just outside boundaries are rejected.
void test_just_outside_boundary_rejected(void)
{
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 300.01f;
    fleet.bench_setpoint_f   = 31.99f;
    fleet.has_ceiling_sp     = true;
    fleet.has_bench_sp       = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
}

// Partial fleet config (only ceiling) does not disturb bench default.
void test_partial_fleet_only_ceiling(void)
{
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 165.0f;
    fleet.has_ceiling_sp     = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 165.0f, cfg.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, cfg.bench_setpoint_f);
}

// PID enable flag applied independently from setpoints.
void test_pid_enable_flag(void)
{
    SaunaConfig cfg;
    ConfigLayer fleet;
    fleet.ceiling_pid_en = true;
    fleet.has_ceiling_en = true;
    mergeConfigLayer(cfg, fleet);

    TEST_ASSERT_TRUE(cfg.ceiling_pid_en);
    TEST_ASSERT_FALSE(cfg.bench_pid_en); // unset — stays at default
}

void test_parse_fleet_config_json_malformed_returns_false(void)
{
    FleetConfigFile fleet;
    bool ok = parseFleetConfigJson("{\"ceiling_setpoint_f\":", fleet);

    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_FALSE(fleet.layer.has_ceiling_sp);
    TEST_ASSERT_FALSE(fleet.has_sensor_read_interval_ms);
    TEST_ASSERT_FALSE(fleet.has_device_name);
}

void test_parse_fleet_config_json_partial_valid_fields_apply(void)
{
    const char *json =
        "{"
        "\"ceiling_setpoint_f\":175.0,"
        "\"bench_pid_enabled\":true,"
        "\"sensor_read_interval_ms\":2500,"
        "\"static_ip\":\"192.168.1.55\","
        "\"device_name\":\"Sauna-A\""
        "}";
    FleetConfigFile fleet;
    bool ok = parseFleetConfigJson(json, fleet);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fleet.layer.has_ceiling_sp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 175.0f, fleet.layer.ceiling_setpoint_f);
    TEST_ASSERT_FALSE(fleet.layer.has_bench_sp);
    TEST_ASSERT_FALSE(fleet.layer.has_ceiling_en);
    TEST_ASSERT_TRUE(fleet.layer.has_bench_en);
    TEST_ASSERT_TRUE(fleet.layer.bench_pid_en);
    TEST_ASSERT_TRUE(fleet.has_sensor_read_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(2500UL, fleet.sensor_read_interval_ms);
    TEST_ASSERT_FALSE(fleet.has_serial_log_interval_ms);
    TEST_ASSERT_TRUE(fleet.has_static_ip);
    TEST_ASSERT_EQUAL_STRING("192.168.1.55", fleet.static_ip_str);
    TEST_ASSERT_TRUE(fleet.has_device_name);
    TEST_ASSERT_EQUAL_STRING("Sauna-A", fleet.device_name);
}

void test_parse_fleet_config_json_rejects_invalid_values_per_field(void)
{
    const char *json =
        "{"
        "\"ceiling_setpoint_f\":301.0,"
        "\"bench_setpoint_f\":130.0,"
        "\"ceiling_pid_enabled\":true,"
        "\"sensor_read_interval_ms\":499,"
        "\"serial_log_interval_ms\":1200,"
        "\"static_ip\":\"999.1.1.1\","
        "\"device_name\":\"Sauna-B\""
        "}";
    FleetConfigFile fleet;
    bool ok = parseFleetConfigJson(json, fleet);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(fleet.layer.has_ceiling_sp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 301.0f, fleet.layer.ceiling_setpoint_f);
    TEST_ASSERT_TRUE(fleet.layer.has_bench_sp);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 130.0f, fleet.layer.bench_setpoint_f);
    TEST_ASSERT_TRUE(fleet.layer.has_ceiling_en);
    TEST_ASSERT_TRUE(fleet.layer.ceiling_pid_en);
    TEST_ASSERT_FALSE(fleet.has_sensor_read_interval_ms);
    TEST_ASSERT_TRUE(fleet.has_serial_log_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(1200UL, fleet.serial_log_interval_ms);
    TEST_ASSERT_FALSE(fleet.has_static_ip);
    TEST_ASSERT_TRUE(fleet.has_device_name);
    TEST_ASSERT_EQUAL_STRING("Sauna-B", fleet.device_name);
}

void test_apply_fleet_config_file_preserves_defaults_when_layer_empty(void)
{
    FleetRuntimeConfig runtime = defaultFleetRuntime();
    FleetConfigFile fleet;

    applyFleetConfigFile(runtime, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f, runtime.sauna.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, runtime.sauna.bench_setpoint_f);
    TEST_ASSERT_FALSE(runtime.sauna.ceiling_pid_en);
    TEST_ASSERT_FALSE(runtime.sauna.bench_pid_en);
    TEST_ASSERT_EQUAL_UINT32(5000UL, runtime.sensor_read_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(10000UL, runtime.serial_log_interval_ms);
    TEST_ASSERT_EQUAL_STRING("192.168.1.201", runtime.static_ip_str);
    TEST_ASSERT_EQUAL_STRING("ESP32-S3", runtime.device_name);
}

void test_apply_fleet_config_file_updates_only_valid_parsed_fields(void)
{
    FleetRuntimeConfig runtime = defaultFleetRuntime();
    FleetConfigFile fleet;
    bool ok = parseFleetConfigJson(
        "{"
        "\"ceiling_setpoint_f\":180.0,"
        "\"bench_setpoint_f\":10.0,"
        "\"ceiling_pid_enabled\":true,"
        "\"sensor_read_interval_ms\":3000,"
        "\"static_ip\":\"192.168.1.88\""
        "}",
        fleet);

    TEST_ASSERT_TRUE(ok);
    applyFleetConfigFile(runtime, fleet);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, runtime.sauna.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 120.0f, runtime.sauna.bench_setpoint_f);
    TEST_ASSERT_TRUE(runtime.sauna.ceiling_pid_en);
    TEST_ASSERT_FALSE(runtime.sauna.bench_pid_en);
    TEST_ASSERT_EQUAL_UINT32(3000UL, runtime.sensor_read_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(10000UL, runtime.serial_log_interval_ms);
    TEST_ASSERT_EQUAL_STRING("192.168.1.88", runtime.static_ip_str);
    TEST_ASSERT_EQUAL_STRING("ESP32-S3", runtime.device_name);
}

void test_load_fleet_config_runtime_skips_when_littlefs_unavailable(void)
{
    FleetRuntimeConfig runtime = defaultFleetRuntime();
    FleetConfigLoadStatus status = loadFleetConfigRuntime(
        runtime, false, true,
        "{\"ceiling_setpoint_f\":180.0,\"device_name\":\"Sauna-X\"}");

    TEST_ASSERT_EQUAL(FLEET_CONFIG_SKIPPED_LITTLEFS_UNAVAILABLE, status);
    assertDefaultFleetRuntime(runtime);
}

void test_load_fleet_config_runtime_skips_when_file_missing(void)
{
    FleetRuntimeConfig runtime = defaultFleetRuntime();
    FleetConfigLoadStatus status = loadFleetConfigRuntime(runtime, true, false, nullptr);

    TEST_ASSERT_EQUAL(FLEET_CONFIG_SKIPPED_FILE_MISSING, status);
    assertDefaultFleetRuntime(runtime);
}

void test_load_fleet_config_runtime_reports_parse_error(void)
{
    FleetRuntimeConfig runtime = defaultFleetRuntime();
    FleetConfigLoadStatus status = loadFleetConfigRuntime(
        runtime, true, true, "{\"ceiling_setpoint_f\":");

    TEST_ASSERT_EQUAL(FLEET_CONFIG_PARSE_ERROR, status);
    assertDefaultFleetRuntime(runtime);
}

void test_load_fleet_config_runtime_applies_valid_file(void)
{
    FleetRuntimeConfig runtime = defaultFleetRuntime();
    FleetConfigLoadStatus status = loadFleetConfigRuntime(
        runtime, true, true,
        "{"
        "\"ceiling_setpoint_f\":180.0,"
        "\"bench_setpoint_f\":130.0,"
        "\"ceiling_pid_enabled\":true,"
        "\"bench_pid_enabled\":true,"
        "\"sensor_read_interval_ms\":3000,"
        "\"serial_log_interval_ms\":12000,"
        "\"static_ip\":\"192.168.1.88\","
        "\"device_name\":\"Sauna-X\""
        "}");

    TEST_ASSERT_EQUAL(FLEET_CONFIG_APPLIED, status);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 180.0f, runtime.sauna.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 130.0f, runtime.sauna.bench_setpoint_f);
    TEST_ASSERT_TRUE(runtime.sauna.ceiling_pid_en);
    TEST_ASSERT_TRUE(runtime.sauna.bench_pid_en);
    TEST_ASSERT_EQUAL_UINT32(3000UL, runtime.sensor_read_interval_ms);
    TEST_ASSERT_EQUAL_UINT32(12000UL, runtime.serial_log_interval_ms);
    TEST_ASSERT_EQUAL_STRING("192.168.1.88", runtime.static_ip_str);
    TEST_ASSERT_EQUAL_STRING("Sauna-X", runtime.device_name);
}

// =============================================================================
// Scenario 4 — Config values survive simulated power cycles
// =============================================================================

// Running the same layer sequence twice produces identical results (idempotent).
void test_power_cycle_idempotent(void)
{
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 168.0f;
    fleet.bench_setpoint_f   = 125.0f;
    fleet.ceiling_pid_en     = true;
    fleet.has_ceiling_sp     = true;
    fleet.has_bench_sp       = true;
    fleet.has_ceiling_en     = true;

    ConfigLayer nvs;
    nvs.ceiling_setpoint_f = 182.0f;
    nvs.has_ceiling_sp     = true;

    SaunaConfig boot1 = runLoadSequence(fleet, nvs);
    SaunaConfig boot2 = runLoadSequence(fleet, nvs);

    TEST_ASSERT_FLOAT_WITHIN(0.01f, boot1.ceiling_setpoint_f, boot2.ceiling_setpoint_f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, boot1.bench_setpoint_f,   boot2.bench_setpoint_f);
    TEST_ASSERT_EQUAL(boot1.ceiling_pid_en, boot2.ceiling_pid_en);
    TEST_ASSERT_EQUAL(boot1.bench_pid_en,   boot2.bench_pid_en);
}

// NVS setpoint persists across cycles even when fleet config is updated between reboots.
void test_power_cycle_nvs_beats_changed_fleet(void)
{
    // Boot 1: fleet = 170, NVS = 182 → NVS wins
    ConfigLayer fleet_v1;
    fleet_v1.ceiling_setpoint_f = 170.0f;
    fleet_v1.has_ceiling_sp     = true;

    ConfigLayer nvs;
    nvs.ceiling_setpoint_f = 182.0f;
    nvs.has_ceiling_sp     = true;

    SaunaConfig boot1 = runLoadSequence(fleet_v1, nvs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 182.0f, boot1.ceiling_setpoint_f);

    // Boot 2: fleet updated to 175, NVS unchanged → NVS still wins
    ConfigLayer fleet_v2;
    fleet_v2.ceiling_setpoint_f = 175.0f;
    fleet_v2.has_ceiling_sp     = true;

    SaunaConfig boot2 = runLoadSequence(fleet_v2, nvs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 182.0f, boot2.ceiling_setpoint_f);
}

// After NVS key is cleared (factory reset), fleet value is restored on next boot.
void test_power_cycle_nvs_cleared_restores_fleet(void)
{
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 170.0f;
    fleet.has_ceiling_sp     = true;

    // Boot 1: NVS key present → 185 wins
    ConfigLayer nvs_present;
    nvs_present.ceiling_setpoint_f = 185.0f;
    nvs_present.has_ceiling_sp     = true;
    SaunaConfig boot1 = runLoadSequence(fleet, nvs_present);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 185.0f, boot1.ceiling_setpoint_f);

    // Boot 2: NVS key cleared → fleet 170 is restored
    ConfigLayer nvs_cleared; // has_ceiling_sp = false
    SaunaConfig boot2 = runLoadSequence(fleet, nvs_cleared);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 170.0f, boot2.ceiling_setpoint_f);
}

// PID enable persists consistently across multiple power cycles.
void test_power_cycle_pid_enable_persists(void)
{
    ConfigLayer fleet;
    fleet.ceiling_pid_en = false;
    fleet.has_ceiling_en = true;

    ConfigLayer nvs;
    nvs.ceiling_pid_en = true;
    nvs.has_ceiling_en = true;

    SaunaConfig boot1 = runLoadSequence(fleet, nvs);
    SaunaConfig boot2 = runLoadSequence(fleet, nvs);
    SaunaConfig boot3 = runLoadSequence(fleet, nvs);

    TEST_ASSERT_TRUE(boot1.ceiling_pid_en);
    TEST_ASSERT_TRUE(boot2.ceiling_pid_en);
    TEST_ASSERT_TRUE(boot3.ceiling_pid_en);
}

// NVS setpoint update (user change at runtime) is reflected on next boot.
void test_power_cycle_nvs_update_reflected(void)
{
    ConfigLayer fleet;
    fleet.ceiling_setpoint_f = 160.0f;
    fleet.has_ceiling_sp     = true;

    // Boot 1: user sets NVS to 175
    ConfigLayer nvs_v1;
    nvs_v1.ceiling_setpoint_f = 175.0f;
    nvs_v1.has_ceiling_sp     = true;
    SaunaConfig boot1 = runLoadSequence(fleet, nvs_v1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 175.0f, boot1.ceiling_setpoint_f);

    // Boot 2: user changes NVS to 190
    ConfigLayer nvs_v2;
    nvs_v2.ceiling_setpoint_f = 190.0f;
    nvs_v2.has_ceiling_sp     = true;
    SaunaConfig boot2 = runLoadSequence(fleet, nvs_v2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 190.0f, boot2.ceiling_setpoint_f);
}

// Fleet update does not overwrite per-device NVS calibration on reboot.
void test_power_cycle_fleet_update_does_not_clobber_nvs(void)
{
    ConfigLayer nvs;
    nvs.bench_setpoint_f = 138.0f;
    nvs.has_bench_sp     = true;

    // Boot 1: fleet v1
    ConfigLayer fleet_v1;
    fleet_v1.bench_setpoint_f = 120.0f;
    fleet_v1.has_bench_sp     = true;
    SaunaConfig boot1 = runLoadSequence(fleet_v1, nvs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 138.0f, boot1.bench_setpoint_f);

    // Boot 2: fleet upgraded to 125 — NVS 138 still wins
    ConfigLayer fleet_v2;
    fleet_v2.bench_setpoint_f = 125.0f;
    fleet_v2.has_bench_sp     = true;
    SaunaConfig boot2 = runLoadSequence(fleet_v2, nvs);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 138.0f, boot2.bench_setpoint_f);
}

// =============================================================================
// Runner
// =============================================================================

int main(int argc, char **argv)
{
    UNITY_BEGIN();

    // Scenario 1: defaults when no config saved
    RUN_TEST(test_sauna_config_defaults);
    RUN_TEST(test_no_layers_all_defaults);
    RUN_TEST(test_empty_fleet_layer_is_noop);

    // Scenario 2: device-level NVS overrides
    RUN_TEST(test_fleet_config_overrides_defaults);
    RUN_TEST(test_full_three_tier_nvs_wins_all);
    RUN_TEST(test_nvs_partial_override_preserves_fleet_fields);
    RUN_TEST(test_nvs_wins_over_fleet);
    RUN_TEST(test_nvs_missing_key_preserves_fleet);
    RUN_TEST(test_nvs_can_disable_fleet_pid);

    // Scenario 3: corrupted / missing SPIFFS config
    RUN_TEST(test_corrupted_fleet_preserves_defaults);
    RUN_TEST(test_corrupted_fleet_nvs_still_applied);
    RUN_TEST(test_out_of_range_fleet_rejected_per_field);
    RUN_TEST(test_out_of_range_rejected);
    RUN_TEST(test_out_of_range_low_rejected);
    RUN_TEST(test_boundary_setpoints_accepted);
    RUN_TEST(test_just_outside_boundary_rejected);
    RUN_TEST(test_partial_fleet_only_ceiling);
    RUN_TEST(test_pid_enable_flag);
    RUN_TEST(test_parse_fleet_config_json_malformed_returns_false);
    RUN_TEST(test_parse_fleet_config_json_partial_valid_fields_apply);
    RUN_TEST(test_parse_fleet_config_json_rejects_invalid_values_per_field);
    RUN_TEST(test_apply_fleet_config_file_preserves_defaults_when_layer_empty);
    RUN_TEST(test_apply_fleet_config_file_updates_only_valid_parsed_fields);
    RUN_TEST(test_load_fleet_config_runtime_skips_when_littlefs_unavailable);
    RUN_TEST(test_load_fleet_config_runtime_skips_when_file_missing);
    RUN_TEST(test_load_fleet_config_runtime_reports_parse_error);
    RUN_TEST(test_load_fleet_config_runtime_applies_valid_file);

    // Scenario 4: power cycle persistence
    RUN_TEST(test_power_cycle_idempotent);
    RUN_TEST(test_power_cycle_nvs_beats_changed_fleet);
    RUN_TEST(test_power_cycle_nvs_cleared_restores_fleet);
    RUN_TEST(test_power_cycle_pid_enable_persists);
    RUN_TEST(test_power_cycle_nvs_update_reflected);
    RUN_TEST(test_power_cycle_fleet_update_does_not_clobber_nvs);

    return UNITY_END();
}
