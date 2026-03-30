#!/usr/bin/env bash
# scripts/parallel-tdd.sh
# Launch 4 parallel TDD agents, one per independent feature module.
# Each agent: reads its test file, implements the missing functions, runs tests
# in a loop until green (or 10 iterations), writes MODULE_DONE.md.
#
# Usage: bash scripts/parallel-tdd.sh
# Requires: claude CLI in PATH with sufficient credits/quota
set -euo pipefail

PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_DIR="$PROJ_DIR/.parallel-tdd"
mkdir -p "$LOG_DIR"

# Timestamp helper
ts() { date +"%H:%M:%S"; }

# ─── Prompt: motor_utils ─────────────────────────────────────────────────────
cat > "$LOG_DIR/prompt_motor_utils.txt" << 'PROMPT'
You are a TDD implementation agent for the SaunaStatus ESP32 firmware project.

## Your sole task
Make all tests in `test/test_motor_utils/test_main.cpp` pass by adding the
required functions to `src/motor_logic.h`.

## Working directory
/home/mmessano/Documents/PlatformIO/Projects/SaunaStatus

## Step 1 — Read the test file
Read `test/test_motor_utils/test_main.cpp` fully. Understand what each
test expects. Do NOT start writing code until you have read the tests.

## Step 2 — Implement (in `src/motor_logic.h` only)
Add these three inline functions immediately after the existing `motorClampCW`:

### motorClampCCW(int current_target, int steps) → int
CCW movement decreases position. Clamp so position never goes below 0.
- Returns actual CCW steps allowed: min(steps, max(0, current_target))
- Returns 0 if current_target <= 0

### motorPosToPercent(int pos, int max_steps) → int
Map pos (0..max_steps) to percentage 0..100.
- Returns 0 if max_steps == 0 (divide-by-zero guard)
- Clamp: pos < 0 → 0, pos > max_steps → 100
- Integer arithmetic: (pos * 100) / max_steps, then clamp

### motorPercentToSteps(int pct, int max_steps) → int
Map percentage 0..100 to step count 0..max_steps.
- Clamp pct to [0, 100] before computing
- Integer arithmetic: (pct * max_steps) / 100

## Step 3 — Run tests
Run: `pio test -e native -f test_motor_utils`
Check the output. If tests fail, fix the implementation and re-run.
Do NOT modify the test file.

## Step 4 — Iterate
Repeat Step 3 up to 10 times until all tests pass or you have exhausted
your attempts. On each iteration, read the failure message carefully and
fix the specific failing assertion.

## Step 5 — Write MODULE_DONE.md
Write a file at `test/test_motor_utils/MODULE_DONE.md` containing:
- STATUS: PASS or FAIL
- ITERATIONS: how many pio test runs were needed
- FUNCTIONS_ADDED: bullet list of functions you added
- NOTES: any edge cases or decisions worth flagging for merge review

## Constraints
- ONLY modify `src/motor_logic.h`
- Do NOT modify test files
- Do NOT modify platformio.ini
- Do NOT modify any other source files
PROMPT

# ─── Prompt: overheat ────────────────────────────────────────────────────────
cat > "$LOG_DIR/prompt_overheat.txt" << 'PROMPT'
You are a TDD implementation agent for the SaunaStatus ESP32 firmware project.

## Your sole task
Make all tests in `test/test_overheat/test_main.cpp` pass by adding the
required struct and function to `src/sauna_logic.h`.

## Working directory
/home/mmessano/Documents/PlatformIO/Projects/SaunaStatus

## Step 1 — Read the test file
Read `test/test_overheat/test_main.cpp` fully. Understand every test case
before writing any code.

Also read `src/sauna_logic.h` to understand the existing struct layout and
where to insert your additions (add AFTER the existing PIDState struct, BEFORE
the buildJsonFull function).

## Step 2 — Implement (in `src/sauna_logic.h` only)

### OverheatGuard struct
```cpp
struct OverheatGuard {
    bool triggered = false;
};
```

### tickOverheat function
```cpp
inline bool tickOverheat(OverheatGuard& guard,
                         float ceiling_c, float bench_c,
                         float threshold_c)
```

Semantics (derived from the tests — read them to confirm):
- Triggers when ceiling_c >= threshold_c OR bench_c >= threshold_c (NaN ignored)
- Clears only when BOTH ceiling_c AND bench_c are valid (not NaN) AND both < threshold_c
- If both inputs are NaN: retain current guard.triggered state unchanged
- Returns guard.triggered after the update

## Step 3 — Run tests
Run: `pio test -e native -f test_overheat`
If tests fail, diagnose carefully. The NaN handling is tricky — use
`std::isnan()` (already included in sauna_logic.h via <cmath>).

## Step 4 — Iterate
Repeat up to 10 times until all tests pass. Fix the specific failing assertion
each iteration.

## Step 5 — Write MODULE_DONE.md
Write `test/test_overheat/MODULE_DONE.md` with:
- STATUS: PASS or FAIL
- ITERATIONS: count
- FUNCTIONS_ADDED: what you added
- NOTES: any NaN edge cases, state retention decisions for merge review

## Constraints
- ONLY modify `src/sauna_logic.h`
- Do NOT modify test files
- Do NOT modify any other source files
PROMPT

# ─── Prompt: config_json ─────────────────────────────────────────────────────
cat > "$LOG_DIR/prompt_config_json.txt" << 'PROMPT'
You are a TDD implementation agent for the SaunaStatus ESP32 firmware project.

## Your sole task
Make all tests in `test/test_config_json/test_main.cpp` pass by adding the
required function to `src/sauna_logic.h`.

## Working directory
/home/mmessano/Documents/PlatformIO/Projects/SaunaStatus

## Step 1 — Read the test file
Read `test/test_config_json/test_main.cpp` fully. Note the exact JSON format
expected, key names, and value formatting before writing any code.

Also read `src/sauna_logic.h` to understand SaunaConfig and where to add the
new function (add AFTER mergeConfigLayer, BEFORE SensorValues).

## Step 2 — Implement (in `src/sauna_logic.h` only)

### buildConfigJson function
```cpp
inline void buildConfigJson(const SaunaConfig& cfg, char* buf, size_t len)
```

Output format: `{"csp_f":160.0,"bsp_f":120.0,"cen":0,"ben":0}`
- csp_f — cfg.ceiling_setpoint_f (already in °F, one decimal place via %.1f)
- bsp_f — cfg.bench_setpoint_f (already in °F, one decimal place via %.1f)
- cen   — cfg.ceiling_pid_en as int (0 or 1)
- ben   — cfg.bench_pid_en as int (0 or 1)

Use snprintf with the buf and len arguments.

## Step 3 — Run tests
Run: `pio test -e native -f test_config_json`
Check the exact string assertions — the test does strstr() checks so the
format must exactly match the expected substrings.

## Step 4 — Iterate
Repeat up to 10 times until all tests pass.

## Step 5 — Write MODULE_DONE.md
Write `test/test_config_json/MODULE_DONE.md` with:
- STATUS: PASS or FAIL
- ITERATIONS: count
- FUNCTIONS_ADDED: what you added
- NOTES: format decisions for merge review

## Constraints
- ONLY modify `src/sauna_logic.h`
- Do NOT modify test files
- Do NOT modify any other source files
PROMPT

# ─── Prompt: version_utils ───────────────────────────────────────────────────
cat > "$LOG_DIR/prompt_version_utils.txt" << 'PROMPT'
You are a TDD implementation agent for the SaunaStatus ESP32 firmware project.

## Your sole task
Make all tests in `test/test_version_utils/test_main.cpp` pass by adding
three functions to `src/ota_logic.h`.

## Working directory
/home/mmessano/Documents/PlatformIO/Projects/SaunaStatus

## Step 1 — Read the test file
Read `test/test_version_utils/test_main.cpp` fully. Note every test case —
especially the invalid-version and both-invalid edge cases.

Also read `src/ota_logic.h` to understand the existing FirmwareVersion struct,
parseVersion, compareVersion, and isUpdateAvailable (add after isUpdateAvailable).

## Step 2 — Implement (in `src/ota_logic.h` only)

### formatVersion
```cpp
inline void formatVersion(const FirmwareVersion& v, char* buf, size_t len)
```
- If v.valid: snprintf(buf, len, "%d.%d.%d", v.major, v.minor, v.patch)
- If !v.valid: snprintf(buf, len, "")  (empty string, null-terminated)

### isDowngrade
```cpp
inline bool isDowngrade(const FirmwareVersion& current,
                        const FirmwareVersion& manifest)
```
- Returns false if either is invalid
- Returns true if manifest < current (compareVersion(manifest, current) < 0)
- Returns false if equal or manifest is newer

### isSameVersion
```cpp
inline bool isSameVersion(const FirmwareVersion& a, const FirmwareVersion& b)
```
- Returns false if either is invalid (including both invalid)
- Returns true only when both valid AND compareVersion(a,b) == 0

## Step 3 — Run tests
Run: `pio test -e native -f test_version_utils`
Pay close attention to the both-invalid edge case in isSameVersion —
two invalid versions are NOT considered equal.

## Step 4 — Iterate
Repeat up to 10 times until all tests pass.

## Step 5 — Write MODULE_DONE.md
Write `test/test_version_utils/MODULE_DONE.md` with:
- STATUS: PASS or FAIL
- ITERATIONS: count
- FUNCTIONS_ADDED: what you added
- NOTES: any edge case decisions for merge review

## Constraints
- ONLY modify `src/ota_logic.h`
- Do NOT modify test files
- Do NOT modify any other source files
PROMPT

# ─── Launch agents in parallel ───────────────────────────────────────────────

run_agent() {
    local name="$1"
    local prompt_file="$LOG_DIR/prompt_${name}.txt"
    local log_file="$LOG_DIR/${name}.log"
    echo "[$(ts)] Starting agent: $name"
    claude \
        --print \
        --allowedTools "Read,Edit,Write,Bash,Glob,Grep" \
        --max-turns 25 \
        --dangerously-skip-permissions \
        < "$prompt_file" > "$log_file" 2>&1
    local exit_code=$?
    if [ $exit_code -eq 0 ]; then
        echo "[$(ts)] Agent $name DONE (success)"
    else
        echo "[$(ts)] Agent $name DONE (exit $exit_code — check $log_file)"
    fi
}

echo "=== Parallel TDD agents starting ==="
echo "Logs: $LOG_DIR/"
echo ""

run_agent motor_utils   &
PID_MOTOR=$!

run_agent overheat      &
PID_OVERHEAT=$!

run_agent config_json   &
PID_CONFIG=$!

run_agent version_utils &
PID_VERSION=$!

# Wait for all agents
wait $PID_MOTOR
wait $PID_OVERHEAT
wait $PID_CONFIG
wait $PID_VERSION

echo ""
echo "=== All agents complete. Running full test suite ==="
cd "$PROJ_DIR"
pio test -e native 2>&1 | tee "$LOG_DIR/full_suite.log"

echo ""
echo "=== MODULE_DONE summaries ==="
for module in test_motor_utils test_overheat test_config_json test_version_utils; do
    f="$PROJ_DIR/test/$module/MODULE_DONE.md"
    if [ -f "$f" ]; then
        echo ""
        echo "--- $module ---"
        cat "$f"
    else
        echo ""
        echo "--- $module --- (MODULE_DONE.md not written)"
    fi
done
