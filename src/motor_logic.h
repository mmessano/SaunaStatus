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
