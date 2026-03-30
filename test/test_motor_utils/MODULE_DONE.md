STATUS: PASS
ITERATIONS: 1
FUNCTIONS_ADDED:
- motorClampCCW(int current_target, int steps) → int
- motorPosToPercent(int pos, int max_steps) → int
- motorPercentToSteps(int pct, int max_steps) → int

NOTES:
- All three functions added as inline to src/motor_logic.h immediately after motorClampCW.
- motorClampCCW: uses min(steps, max(0, current_target)) via ternary; negative target handled by the <= 0 guard returning 0.
- motorPosToPercent: early returns for 0/negative pos and pos >= max_steps avoid integer division edge cases cleanly.
- motorPercentToSteps: early returns for pct <= 0 and pct >= 100 ensure max_steps is returned exactly for 100% (no rounding loss from integer division).
- Roundtrip test (768 → pct → steps) passes with 0 rounding error because 768/1024 = 75% and 75*1024/100 = 768 exactly.
