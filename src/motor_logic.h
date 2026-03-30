// src/motor_logic.h
// Pure motor clamping logic — no Arduino/hardware dependencies.
// Included by both web.cpp (Arduino) and native unit tests.
#pragma once

// Returns the actual number of CW steps to move from current_target toward
// max_steps, clamped so that (current_target + actual) <= max_steps.
// Returns 0 if target is already at or beyond max_steps.
//
// Returns the actual CW step count, clamped so target never exceeds max_steps.
// Mirrors the ccw floor-at-zero pattern already present in handleMotorCmd.
inline int motorClampCW(int current_target, int steps, int max_steps) {
    if (current_target >= max_steps) return 0;
    int room = max_steps - current_target;
    return steps < room ? steps : room;
}

// Returns the actual number of CCW steps allowed from current_target toward 0,
// clamped so that (current_target - actual) >= 0.
// Returns 0 if current_target <= 0.
inline int motorClampCCW(int current_target, int steps) {
    if (current_target <= 0) return 0;
    return steps < current_target ? steps : current_target;
}

// Maps position (0..max_steps) to percentage 0..100.
// Returns 0 if max_steps == 0 (divide-by-zero guard).
// Clamps: pos < 0 → 0, pos > max_steps → 100.
inline int motorPosToPercent(int pos, int max_steps) {
    if (max_steps == 0) return 0;
    if (pos <= 0) return 0;
    if (pos >= max_steps) return 100;
    return (pos * 100) / max_steps;
}

// Maps percentage 0..100 to step count 0..max_steps.
// Clamps pct to [0, 100] before computing.
inline int motorPercentToSteps(int pct, int max_steps) {
    if (pct <= 0) return 0;
    if (pct >= 100) return max_steps;
    return (pct * max_steps) / 100;
}
