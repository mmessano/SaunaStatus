#!/usr/bin/env bash
# scripts/update-handoff.sh
# Regenerates HANDOFF.md with current project state.
# Called automatically by .git/hooks/post-commit, or run manually.
#
# Environment variables:
#   SKIP_BUILD=1   Skip PlatformIO compilation (default: auto — skips if no
#                  C/C++ files changed in the last commit)
#   FORCE_BUILD=1  Always run PlatformIO build regardless of changed files
#
# Usage:
#   bash scripts/update-handoff.sh          # auto-detect whether to build
#   SKIP_BUILD=1 bash scripts/update-handoff.sh   # skip build
#   FORCE_BUILD=1 bash scripts/update-handoff.sh  # always build

set -uo pipefail

REPO="$(git rev-parse --show-toplevel)"
OUT="$REPO/HANDOFF.md"
DATE="$(date +%Y-%m-%d)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
LAST_COMMIT="$(git log -1 --format='%h %s')"

log() { echo "  [handoff] $*"; }

# ── decide whether to run the build ──────────────────────────────────────────

if [[ "${FORCE_BUILD:-0}" == "1" ]]; then
    RUN_BUILD=1
elif [[ "${SKIP_BUILD:-0}" == "1" ]]; then
    RUN_BUILD=0
else
    # Auto: build only if the last commit touched C/C++ source or headers
    CHANGED_CPP="$(git diff-tree --no-commit-id -r --name-only HEAD \
        | grep -E '\.(cpp|h|c|ino|py)$' | grep -v '^test/' | head -1 || true)"
    if [[ -n "$CHANGED_CPP" ]]; then
        RUN_BUILD=1
    else
        RUN_BUILD=0
    fi
fi

# ── section 1: recent git history ────────────────────────────────────────────

log "Collecting git history..."

RECENT_LOG="$(git log -10 --oneline)"

# Per-commit stat summary (files changed, insertions, deletions)
RECENT_STATS="$(git log -10 --stat --oneline | head -80)"

# ── section 2: conventions from CLAUDE.md and skills ─────────────────────────

log "Scanning CLAUDE.md and skills..."

# Extract Common Pitfalls table from CLAUDE.md
PITFALLS="$(awk '/^## Common Pitfalls/{found=1; next} found && /^## /{exit} found{print}' \
    "$REPO/CLAUDE.md" | head -40)"

# Extract Lessons Learned from CLAUDE.md
LESSONS="$(awk '/^## Lessons Learned/{found=1; next} found && /^## /{exit} found{print}' \
    "$REPO/CLAUDE.md" | head -30)"

# List project-specific skills
SKILLS_LIST="$(ls "$REPO/.claude/skills/" 2>/dev/null | sort || echo "(none)")"

# Key sensor invariants from sensor-patterns rule
SENSOR_KEY_RULES="$(grep -A2 '^## Rule' "$REPO/.claude/rules/sensor-patterns.md" \
    2>/dev/null | grep -v '^--$' | head -30 || echo "(sensor-patterns.md not found)")"

# ── section 3: TODO/FIXME/HACK scan ──────────────────────────────────────────

log "Scanning for TODO/FIXME/HACK..."

TODOS="$(grep -rn --include='*.cpp' --include='*.h' --include='*.c' --include='*.py' \
    -E '\b(TODO|FIXME|HACK|XXX)\b' \
    "$REPO/src" "$REPO/test" "$REPO/scripts" 2>/dev/null \
    | grep -v 'Binary file' \
    | head -30 || true)"

# ── section 4: build status ───────────────────────────────────────────────────

BUILD_STATUS="skipped"
BUILD_WARNINGS="(build not run)"
BUILD_NOTE=""
BUILD_LOG_FILE="$(mktemp)"
TEST_LOG_FILE="$(mktemp)"
cleanup() {
    rm -f "$BUILD_LOG_FILE" "$TEST_LOG_FILE"
}
trap cleanup EXIT

if [[ "$RUN_BUILD" == "1" ]]; then
    log "Running PlatformIO build (pio run -e lb_esp32s3 -t buildprog)..."
    if (cd "$REPO" && pio run -e lb_esp32s3 -t buildprog >"$BUILD_LOG_FILE" 2>&1); then
        BUILD_EXIT=0
    else
        BUILD_EXIT=$?
    fi

    if [[ "$BUILD_EXIT" -eq 0 ]]; then
        BUILD_STATUS="SUCCESS"
    else
        BUILD_STATUS="FAILED (exit $BUILD_EXIT)"
    fi

    # Extract warning lines (deduplicated, capped)
    BUILD_WARNINGS="$(cat "$BUILD_LOG_FILE" \
        | grep -i ': warning:' \
        | sed 's|.*/src/||' \
        | sort -u \
        | head -20 || true)"
    [[ -z "$BUILD_WARNINGS" ]] && BUILD_WARNINGS="(none)"
else
    if [[ "${SKIP_BUILD:-0}" == "1" ]]; then
        BUILD_NOTE="SKIP_BUILD=1 was set."
    else
        BUILD_NOTE="No C/C++ source files changed in last commit — build skipped. Run FORCE_BUILD=1 bash scripts/update-handoff.sh to override."
    fi
fi

# ── section 5: test status ───────────────────────────────────────────────────

log "Running native tests..."
if (cd "$REPO" && pio test -e native >"$TEST_LOG_FILE" 2>&1); then
    TEST_EXIT=0
else
    TEST_EXIT=$?
fi
TEST_PASS="$(grep -cE '^(PASS|OK)' "$TEST_LOG_FILE" || true)"
TEST_FAIL="$(grep -cE '^(FAIL|ERROR)' "$TEST_LOG_FILE" || true)"
# PlatformIO test summary: "X Tests Y Failures Z Ignored" or "[PASSED]"/"[FAILED]"
TEST_SUMMARY_LINE="$(grep -E '([0-9]+ Tests|PASSED|FAILED|passed|failed)' "$TEST_LOG_FILE" | tail -3 || true)"
TEST_FAILURES="$(grep -E '(:FAIL|:ERROR)' "$TEST_LOG_FILE" | head -10 || true)"

if [[ "$TEST_EXIT" -eq 0 ]]; then
    TEST_STATUS="ALL PASSED"
else
    TEST_STATUS="FAILURES DETECTED"
fi

# ── section 6: working tree state ────────────────────────────────────────────

# Modified/staged files (exclude compile_commands.json)
DIRTY="$(git status --short | grep -v '^??' | grep -v 'compile_commands' || true)"
UNTRACKED="$(git status --short | grep '^??' | grep -v 'compile_commands' || true)"

# ── write HANDOFF.md ──────────────────────────────────────────────────────────

log "Writing $OUT..."

{
cat <<HEADER
# HANDOFF.md — Auto-generated Project State

> **Generated**: ${DATE}
> **Branch**: ${BRANCH}
> **Latest commit**: ${LAST_COMMIT}
>
> Auto-generated by \`scripts/update-handoff.sh\` after every commit.
> Do not edit by hand — it will be overwritten on the next commit.
> To regenerate manually: \`bash scripts/update-handoff.sh\`

---

## Project Overview

ESP32-S3 sauna automation controller (LB-ESP32S3-N16R8, board \`lolin_s3\`).
Monitors temperature/humidity via PT1000 stove sensor (MAX31865) and dual DHT21
(ceiling/bench), monitors power via INA260, and controls two stepper-driven damper
vents with dual QuickPID controllers. Integrates with InfluxDB, MQTT (Home Assistant
MQTT Discovery), and provides a local WebSocket/HTTP REST interface with Bearer token auth.

| Path | Purpose |
|------|---------|
| \`src/\` | Firmware C++ source |
| \`include/\` | PlatformIO placeholder (project headers live in \`src/\`) |
| \`data/\` | LittleFS web assets (index.html, login.html, config.html, config.json) |
| \`test/\` | Native unit tests (Unity framework, no device required) |
| \`docs/kicad/\` | KiCad schematic + PCB |
| \`.claude/skills/\` | Project-specific Claude skills |
| \`scripts/\` | Utility scripts |

**Active PlatformIO environment**: \`lb_esp32s3\` (default)
**Device IP**: 192.168.1.200 (static)
**Firmware version**: see \`platformio.ini\` → \`FIRMWARE_VERSION\`

---

## Recent Changes

### Last 10 Commits

\`\`\`
${RECENT_LOG}
\`\`\`

### Files Changed Per Commit

\`\`\`
${RECENT_STATS}
\`\`\`

---

## Current State

### Build — \`pio run -e lb_esp32s3 -t buildprog\`

**Status**: ${BUILD_STATUS}

HEADER

if [[ -n "$BUILD_NOTE" ]]; then
    echo "> $BUILD_NOTE"
    echo ""
fi

if [[ "$RUN_BUILD" == "1" ]]; then
cat <<BUILD_SECTION
**Compiler warnings** (deduplicated):

\`\`\`
${BUILD_WARNINGS}
\`\`\`

BUILD_SECTION
fi

cat <<TESTS_SECTION
### Tests — \`pio test -e native\`

**Status**: ${TEST_STATUS}

\`\`\`
${TEST_SUMMARY_LINE}
\`\`\`

TESTS_SECTION

if [[ -n "$TEST_FAILURES" ]]; then
cat <<FAIL_SECTION
**Failing tests:**

\`\`\`
${TEST_FAILURES}
\`\`\`

FAIL_SECTION
fi

cat <<TREE_HDR
### Working Tree

TREE_HDR

if [[ -n "$DIRTY" ]]; then
cat <<DIRTY_SECTION
**Modified/staged files:**

\`\`\`
${DIRTY}
\`\`\`

DIRTY_SECTION
fi

if [[ -n "$UNTRACKED" ]]; then
cat <<UNTRACKED_SECTION
**Untracked files:**

\`\`\`
${UNTRACKED}
\`\`\`

UNTRACKED_SECTION
fi

if [[ -z "$DIRTY" && -z "$UNTRACKED" ]]; then
    echo "(clean working tree)"
    echo ""
fi

cat <<SKILLS_HDR
### Project Skills (\`.claude/skills/\`)

SKILLS_HDR
echo "$SKILLS_LIST" | sed 's/^/- /'
echo ""

cat <<'OPEN_ISSUES'

---

## Open Issues

### TODO / FIXME / HACK in Source

OPEN_ISSUES

if [[ -n "$TODOS" ]]; then
cat <<TODO_BLOCK
\`\`\`
${TODOS}
\`\`\`

TODO_BLOCK
else
    echo "(none found in src/, test/, scripts/)"
    echo ""
fi

cat <<'GOTCHAS_HDR'

---

## Known Gotchas

From `CLAUDE.md` → Common Pitfalls:

GOTCHAS_HDR

echo "$PITFALLS"

cat <<'SENSOR_RULES_HDR'

### Sensor Pattern Rules (Critical)

SENSOR_RULES_HDR

echo "$SENSOR_KEY_RULES"

cat <<'LESSONS_HDR'

### Lessons Learned

LESSONS_HDR

echo "$LESSONS"

cat <<'NEXT_HDR'

---

## Next Steps

NEXT_HDR

if [[ -n "$TODOS" ]]; then
    echo "**From source TODOs/FIXMEs:**"
    echo ""
    # Print file:line and the annotation text
    echo "$TODOS" | while IFS= read -r line; do
        FILE_LINE="$(echo "$line" | cut -d: -f1,2)"
        ANNOTATION="$(echo "$line" | grep -oE '(TODO|FIXME|HACK|XXX)[^$]*' | head -1)"
        echo "- \`${FILE_LINE}\`: ${ANNOTATION}"
    done
    echo ""
fi

if [[ "$BUILD_STATUS" == "FAILED"* ]]; then
    echo "- **Fix build failure** before any further work"
    echo ""
fi

if [[ "$TEST_STATUS" == "FAILURES DETECTED" ]]; then
    echo "- **Fix failing tests** (see Current State → Tests above)"
    echo ""
fi

cat <<'NEXT_STATIC'
**Hardware verification checklist** (pending physical testing):

- [ ] DHT21 ceiling sensor (GPIO8) — temperature and humidity
- [ ] DHT21 bench sensor (GPIO9) — temperature and humidity
- [ ] MAX31865 PT1000 stove sensor (SPI, CS=GPIO42)
- [ ] Outflow stepper motor (GPIO4–7 → ULN2003 → 28BYJ-48)
- [ ] Inflow stepper motor (GPIO15–18 → ULN2003 → 28BYJ-48)
- [ ] INA260 power monitor (I2C, SDA=GPIO1, SCL=GPIO2)
- [ ] PCB footprint row spacing — measure LB board before ordering PCBs
- [ ] GPIO35–38 OPI flash conflict — verify before assigning to any peripheral

**Quick commands:**
```bash
pio run -e lb_esp32s3 -t buildprog  # compile-only (safe, no upload)
pio run -t upload        # firmware only
pio run -t uploadfs      # filesystem only (web UI + config.json)
pio test -e native       # all native unit tests (no device needed)
pio device monitor       # serial monitor (use UART connector, NOT USB-C OTG)

# Force HANDOFF.md rebuild including PlatformIO build:
FORCE_BUILD=1 bash scripts/update-handoff.sh
```

NEXT_STATIC

} > "$OUT"

log "Done → $OUT"
