// test/test_motor_utils/test_main.cpp
// RED phase: tests for motorClampCCW, motorPosToPercent, motorPercentToSteps.
// All three functions must be added to src/motor_logic.h.
// Run: pio test -e native -f test_motor_utils
#include <unity.h>
#include "motor_logic.h"

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// motorClampCCW: clamp CCW step request so position never goes below 0
// Signature: int motorClampCCW(int current_target, int steps)
// - Returns the actual CCW steps allowed (current_target - actual >= 0)
// - Returns 0 if already at or below 0
// =============================================================================

// Normal move — plenty of room below
void test_ccw_normal_move_within_range(void) {
    TEST_ASSERT_EQUAL(50, motorClampCCW(200, 50));
}

// Full descent from arbitrary position to exactly 0
void test_ccw_steps_exactly_reach_zero(void) {
    TEST_ASSERT_EQUAL(100, motorClampCCW(100, 100));
}

// Steps would overshoot floor — clamp to remaining room
void test_ccw_clamps_to_remaining_room(void) {
    TEST_ASSERT_EQUAL(30, motorClampCCW(30, 100));
}

// Already at zero — no movement allowed
void test_ccw_at_zero_returns_zero(void) {
    TEST_ASSERT_EQUAL(0, motorClampCCW(0, 50));
}

// One step above zero — only 1 step allowed
void test_ccw_one_step_above_zero(void) {
    TEST_ASSERT_EQUAL(1, motorClampCCW(1, 50));
}

// Corrupted/negative target — must return 0, not negative
void test_ccw_negative_target_returns_zero(void) {
    TEST_ASSERT_EQUAL(0, motorClampCCW(-10, 50));
}

// Large step request clamped to available room
void test_ccw_large_steps_clamped(void) {
    TEST_ASSERT_EQUAL(512, motorClampCCW(512, 9999));
}

// Zero steps requested — returns 0 regardless of position
void test_ccw_zero_steps_requested(void) {
    TEST_ASSERT_EQUAL(0, motorClampCCW(500, 0));
}

// =============================================================================
// motorPosToPercent: map position (0..max_steps) to percentage (0..100)
// Signature: int motorPosToPercent(int pos, int max_steps)
// - Returns 0 if max_steps == 0 (guard against divide-by-zero)
// - Clamps: pos < 0 → 0, pos > max_steps → 100
// - Rounds toward nearest integer (standard integer arithmetic acceptable)
// =============================================================================

void test_pos_to_pct_zero(void) {
    TEST_ASSERT_EQUAL(0, motorPosToPercent(0, 1024));
}

void test_pos_to_pct_full(void) {
    TEST_ASSERT_EQUAL(100, motorPosToPercent(1024, 1024));
}

void test_pos_to_pct_half(void) {
    TEST_ASSERT_EQUAL(50, motorPosToPercent(512, 1024));
}

void test_pos_to_pct_quarter(void) {
    TEST_ASSERT_EQUAL(25, motorPosToPercent(256, 1024));
}

void test_pos_to_pct_zero_max_returns_zero(void) {
    // Guard: divide-by-zero must not happen
    TEST_ASSERT_EQUAL(0, motorPosToPercent(100, 0));
}

void test_pos_to_pct_over_max_clamps_to_100(void) {
    TEST_ASSERT_EQUAL(100, motorPosToPercent(2000, 1024));
}

void test_pos_to_pct_negative_clamps_to_zero(void) {
    TEST_ASSERT_EQUAL(0, motorPosToPercent(-1, 1024));
}

// =============================================================================
// motorPercentToSteps: map percentage (0..100) to step count (0..max_steps)
// Signature: int motorPercentToSteps(int pct, int max_steps)
// - pct < 0 → 0 steps; pct > 100 → max_steps
// - Round toward nearest integer (standard integer arithmetic acceptable)
// =============================================================================

void test_pct_to_steps_zero(void) {
    TEST_ASSERT_EQUAL(0, motorPercentToSteps(0, 1024));
}

void test_pct_to_steps_full(void) {
    TEST_ASSERT_EQUAL(1024, motorPercentToSteps(100, 1024));
}

void test_pct_to_steps_half(void) {
    TEST_ASSERT_EQUAL(512, motorPercentToSteps(50, 1024));
}

void test_pct_to_steps_quarter(void) {
    TEST_ASSERT_EQUAL(256, motorPercentToSteps(25, 1024));
}

void test_pct_to_steps_over_100_clamps(void) {
    TEST_ASSERT_EQUAL(1024, motorPercentToSteps(101, 1024));
}

void test_pct_to_steps_negative_clamps_to_zero(void) {
    TEST_ASSERT_EQUAL(0, motorPercentToSteps(-5, 1024));
}

void test_pct_to_steps_zero_max(void) {
    TEST_ASSERT_EQUAL(0, motorPercentToSteps(50, 0));
}

// Roundtrip: pos → pct → steps should return original pos (within rounding)
void test_roundtrip_pos_pct_steps(void) {
    int original = 768;
    int max = 1024;
    int pct   = motorPosToPercent(original, max);
    int steps = motorPercentToSteps(pct, max);
    // Allow ±10 steps rounding error (integer division)
    TEST_ASSERT_INT_WITHIN(10, original, steps);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    // motorClampCCW
    RUN_TEST(test_ccw_normal_move_within_range);
    RUN_TEST(test_ccw_steps_exactly_reach_zero);
    RUN_TEST(test_ccw_clamps_to_remaining_room);
    RUN_TEST(test_ccw_at_zero_returns_zero);
    RUN_TEST(test_ccw_one_step_above_zero);
    RUN_TEST(test_ccw_negative_target_returns_zero);
    RUN_TEST(test_ccw_large_steps_clamped);
    RUN_TEST(test_ccw_zero_steps_requested);
    // motorPosToPercent
    RUN_TEST(test_pos_to_pct_zero);
    RUN_TEST(test_pos_to_pct_full);
    RUN_TEST(test_pos_to_pct_half);
    RUN_TEST(test_pos_to_pct_quarter);
    RUN_TEST(test_pos_to_pct_zero_max_returns_zero);
    RUN_TEST(test_pos_to_pct_over_max_clamps_to_100);
    RUN_TEST(test_pos_to_pct_negative_clamps_to_zero);
    // motorPercentToSteps
    RUN_TEST(test_pct_to_steps_zero);
    RUN_TEST(test_pct_to_steps_full);
    RUN_TEST(test_pct_to_steps_half);
    RUN_TEST(test_pct_to_steps_quarter);
    RUN_TEST(test_pct_to_steps_over_100_clamps);
    RUN_TEST(test_pct_to_steps_negative_clamps_to_zero);
    RUN_TEST(test_pct_to_steps_zero_max);
    RUN_TEST(test_roundtrip_pos_pct_steps);
    return UNITY_END();
}
