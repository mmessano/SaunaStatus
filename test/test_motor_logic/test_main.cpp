#include <unity.h>
#include "motor_logic.h"

void setUp(void) {}
void tearDown(void) {}

// ── Normal operation ────────────────────────────────────────────────────────

// Plenty of room — all requested steps are allowed
void test_cw_normal_move_within_range(void) {
    TEST_ASSERT_EQUAL(100, motorClampCW(0, 100, 1024));
}

// Steps exactly equal to max from zero — full range allowed, no off-by-one
void test_cw_full_range_from_zero(void) {
    TEST_ASSERT_EQUAL(1024, motorClampCW(0, 1024, 1024));
}

// Steps exactly fill remaining room — no truncation, no extra clamp
void test_cw_steps_exactly_fill_remaining_room(void) {
    TEST_ASSERT_EQUAL(24, motorClampCW(1000, 24, 1024));
}

// ── Edge cases: clamping at/near max ───────────────────────────────────────

// Near the top — must clamp to remaining room, not the full request
// Without the fix: returns 100 (unclamped). With the fix: returns 24.
void test_cw_clamps_to_remaining_room(void) {
    TEST_ASSERT_EQUAL(24, motorClampCW(1000, 100, 1024));
}

// Already at max — no further movement allowed
void test_cw_at_max_returns_zero(void) {
    TEST_ASSERT_EQUAL(0, motorClampCW(1024, 100, 1024));
}

// One step at the limit — still returns 0 (no movement past max)
void test_cw_one_step_at_max(void) {
    TEST_ASSERT_EQUAL(0, motorClampCW(1024, 1, 1024));
}

// ── Edge case: corrupted/overrun target ─────────────────────────────────────

// Target somehow already over max (e.g. calibration changed after manual move).
// Must return 0, not a negative or wrap-around value.
void test_cw_over_max_returns_zero(void) {
    TEST_ASSERT_EQUAL(0, motorClampCW(1200, 50, 1024));
}

// ── Edge case: large step request ──────────────────────────────────────────

// Huge step request (e.g. from a misconfigured HTTP call) is clamped to room
void test_cw_large_steps_clamped_to_room(void) {
    TEST_ASSERT_EQUAL(512, motorClampCW(512, 9999, 1024));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_cw_normal_move_within_range);
    RUN_TEST(test_cw_full_range_from_zero);
    RUN_TEST(test_cw_steps_exactly_fill_remaining_room);
    RUN_TEST(test_cw_clamps_to_remaining_room);
    RUN_TEST(test_cw_at_max_returns_zero);
    RUN_TEST(test_cw_one_step_at_max);
    RUN_TEST(test_cw_over_max_returns_zero);
    RUN_TEST(test_cw_large_steps_clamped_to_room);
    return UNITY_END();
}
