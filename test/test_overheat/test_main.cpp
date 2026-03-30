// test/test_overheat/test_main.cpp
// RED phase: tests for OverheatGuard struct and tickOverheat() state machine.
// These must be added to src/sauna_logic.h (pure C++, no Arduino deps).
// Run: pio test -e native -f test_overheat
//
// Semantics:
//   - Triggers when ceiling_c >= threshold OR bench_c >= threshold (NaN ignored for triggering)
//   - Clears only when BOTH temps are valid (not NaN) AND both are below threshold
//   - If both temps are NaN: retain current triggered state (sensor failure keeps alarm latched)
//   - threshold_c is passed in (matches TEMP_LIMIT_C define, currently 120.0f)
#include <unity.h>
#include "sauna_logic.h"
#include <limits>
#include <cmath>

static const float NaN = std::numeric_limits<float>::quiet_NaN();
static const float THRESH = 120.0f;

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Initial / idle state
// =============================================================================

// Both temps well below threshold — not triggered
void test_no_alarm_when_cool(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 80.0f, 70.0f, THRESH);
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(g.triggered);
}

// Both temps exactly at threshold − 1: not triggered
void test_just_below_threshold_not_triggered(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 119.9f, 119.9f, THRESH);
    TEST_ASSERT_FALSE(result);
}

// =============================================================================
// Trigger conditions
// =============================================================================

// Ceiling hits exactly at threshold — triggers
void test_ceiling_at_threshold_triggers(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, THRESH, 70.0f, THRESH);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g.triggered);
}

// Bench hits exactly at threshold — triggers
void test_bench_at_threshold_triggers(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 70.0f, THRESH, THRESH);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g.triggered);
}

// Ceiling exceeds threshold, bench is fine — triggers
void test_ceiling_over_threshold_triggers(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 150.0f, 50.0f, THRESH);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g.triggered);
}

// Bench exceeds threshold, ceiling is fine — triggers
void test_bench_over_threshold_triggers(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 50.0f, 150.0f, THRESH);
    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_TRUE(g.triggered);
}

// Both over threshold — triggers
void test_both_over_threshold_triggers(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 200.0f, 200.0f, THRESH);
    TEST_ASSERT_TRUE(result);
}

// =============================================================================
// Clearance conditions
// =============================================================================

// Alarm triggered then both temps drop below threshold — clears
void test_alarm_clears_when_both_cool_down(void) {
    OverheatGuard g{};
    tickOverheat(g, 150.0f, 50.0f, THRESH);  // trigger
    TEST_ASSERT_TRUE(g.triggered);
    bool result = tickOverheat(g, 80.0f, 70.0f, THRESH);  // cool
    TEST_ASSERT_FALSE(result);
    TEST_ASSERT_FALSE(g.triggered);
}

// Alarm active, only ceiling cooled — does NOT clear (bench still hot)
void test_alarm_does_not_clear_if_only_ceiling_cools(void) {
    OverheatGuard g{};
    tickOverheat(g, 150.0f, 150.0f, THRESH);
    // ceiling cools, bench still hot
    bool result = tickOverheat(g, 80.0f, 150.0f, THRESH);
    TEST_ASSERT_TRUE(result);  // still triggered
}

// Alarm active, only bench cooled — does NOT clear (ceiling still hot)
void test_alarm_does_not_clear_if_only_bench_cools(void) {
    OverheatGuard g{};
    tickOverheat(g, 150.0f, 150.0f, THRESH);
    // bench cools, ceiling still hot
    bool result = tickOverheat(g, 150.0f, 80.0f, THRESH);
    TEST_ASSERT_TRUE(result);
}

// =============================================================================
// NaN handling
// =============================================================================

// NaN ceiling, valid bench below threshold — does NOT trigger (NaN ignored for trigger)
void test_nan_ceiling_valid_bench_cool_no_trigger(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, NaN, 70.0f, THRESH);
    TEST_ASSERT_FALSE(result);
}

// NaN bench, valid ceiling below threshold — does NOT trigger
void test_nan_bench_valid_ceiling_cool_no_trigger(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 70.0f, NaN, THRESH);
    TEST_ASSERT_FALSE(result);
}

// NaN ceiling, bench OVER threshold — triggers (bench alone is enough)
void test_nan_ceiling_hot_bench_triggers(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, NaN, 150.0f, THRESH);
    TEST_ASSERT_TRUE(result);
}

// NaN bench, ceiling OVER threshold — triggers
void test_nan_bench_hot_ceiling_triggers(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, 150.0f, NaN, THRESH);
    TEST_ASSERT_TRUE(result);
}

// Both NaN while triggered — alarm stays latched (cannot clear with missing data)
void test_both_nan_while_triggered_stays_latched(void) {
    OverheatGuard g{};
    tickOverheat(g, 150.0f, 150.0f, THRESH);  // trigger
    bool result = tickOverheat(g, NaN, NaN, THRESH);  // sensor failure
    TEST_ASSERT_TRUE(result);  // still triggered — cannot clear without data
}

// Both NaN while NOT triggered — stays not triggered
void test_both_nan_while_not_triggered_stays_clear(void) {
    OverheatGuard g{};
    bool result = tickOverheat(g, NaN, NaN, THRESH);
    TEST_ASSERT_FALSE(result);
}

// Triggered with one NaN, then both valid and cool — clears
void test_triggered_then_both_valid_cool_clears(void) {
    OverheatGuard g{};
    tickOverheat(g, 150.0f, NaN, THRESH);  // trigger via ceiling
    bool result = tickOverheat(g, 80.0f, 70.0f, THRESH);  // both valid+cool
    TEST_ASSERT_FALSE(result);
}

// NaN ceiling while triggered, bench valid and cool — does NOT clear
// (One NaN means we can't confirm ceiling is below threshold)
void test_triggered_nan_ceiling_valid_bench_cool_stays_triggered(void) {
    OverheatGuard g{};
    tickOverheat(g, 150.0f, 50.0f, THRESH);
    bool result = tickOverheat(g, NaN, 70.0f, THRESH);
    TEST_ASSERT_TRUE(result);
}

// =============================================================================
// State is persistent across calls
// =============================================================================

// Multiple ticks: trigger, stay triggered, cool, confirm clear
void test_multi_tick_lifecycle(void) {
    OverheatGuard g{};
    TEST_ASSERT_FALSE(tickOverheat(g, 80.0f, 70.0f, THRESH));
    TEST_ASSERT_FALSE(tickOverheat(g, 90.0f, 80.0f, THRESH));
    TEST_ASSERT_TRUE (tickOverheat(g, 130.0f, 80.0f, THRESH));  // trigger
    TEST_ASSERT_TRUE (tickOverheat(g, 130.0f, 70.0f, THRESH));  // still hot
    TEST_ASSERT_TRUE (tickOverheat(g, 115.0f, 70.0f, THRESH));  // cooling but not below
    TEST_ASSERT_FALSE(tickOverheat(g, 90.0f,  70.0f, THRESH));  // cleared
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_no_alarm_when_cool);
    RUN_TEST(test_just_below_threshold_not_triggered);
    RUN_TEST(test_ceiling_at_threshold_triggers);
    RUN_TEST(test_bench_at_threshold_triggers);
    RUN_TEST(test_ceiling_over_threshold_triggers);
    RUN_TEST(test_bench_over_threshold_triggers);
    RUN_TEST(test_both_over_threshold_triggers);
    RUN_TEST(test_alarm_clears_when_both_cool_down);
    RUN_TEST(test_alarm_does_not_clear_if_only_ceiling_cools);
    RUN_TEST(test_alarm_does_not_clear_if_only_bench_cools);
    RUN_TEST(test_nan_ceiling_valid_bench_cool_no_trigger);
    RUN_TEST(test_nan_bench_valid_ceiling_cool_no_trigger);
    RUN_TEST(test_nan_ceiling_hot_bench_triggers);
    RUN_TEST(test_nan_bench_hot_ceiling_triggers);
    RUN_TEST(test_both_nan_while_triggered_stays_latched);
    RUN_TEST(test_both_nan_while_not_triggered_stays_clear);
    RUN_TEST(test_triggered_then_both_valid_cool_clears);
    RUN_TEST(test_triggered_nan_ceiling_valid_bench_cool_stays_triggered);
    RUN_TEST(test_multi_tick_lifecycle);
    return UNITY_END();
}
