#!/usr/bin/env bash
# scripts/refresh-docs.sh
# Full documentation refresh pipeline. Run explicitly — NOT on every commit.
#
# Usage:
#   bash scripts/refresh-docs.sh
#
# Steps:
#   1. update-handoff.sh         — base HANDOFF.md regeneration
#   2. claude --print audit      — full CLAUDE.md semantic audit
#   3. claude --print extend     — AI open-issues + next-steps in HANDOFF.md
#   4. claude --print skills     — extract uncaptured patterns → new skill files
#   5. hook verify               — dry-run post-commit hook
#   6. diff + confirm + commit

set -uo pipefail

REPO="$(git rev-parse --show-toplevel)"
DATE="$(date +%Y-%m-%d)"
TMPDIR_REFRESH="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_REFRESH"' EXIT

# ── helpers ──────────────────────────────────────────────────────────────────

step()  { echo ""; echo "═══ Step $1: $2 ═══"; }
log()   { echo "  [refresh] $*"; }
warn()  { echo "  [refresh] WARNING: $*" >&2; }
die()   { echo "  [refresh] FAILED: $*" >&2; exit 1; }

run_step() {
    local num="$1" name="$2"; shift 2
    step "$num" "$name"
    "$@" || die "Step $num ($name) exited with code $?"
}

# ── verify claude CLI is available ───────────────────────────────────────────

if ! command -v claude &>/dev/null; then
    die "claude CLI not found. Install Claude Code and ensure it is on PATH."
fi

cd "$REPO"

# ── Step 1: base HANDOFF.md regeneration ─────────────────────────────────────

step 1 "Base HANDOFF.md regeneration (update-handoff.sh)"

SKIP_BUILD=1 bash "$REPO/scripts/update-handoff.sh" \
    || die "update-handoff.sh failed — fix it before running refresh-docs.sh"

log "HANDOFF.md regenerated."

# ── Step 2: CLAUDE.md full audit ─────────────────────────────────────────────

step 2 "CLAUDE.md full audit (claude --print)"

# Collect codebase facts
log "Collecting codebase facts..."
LINE_COUNTS="$(wc -l "$REPO"/src/*.cpp "$REPO"/src/*.h 2>/dev/null | sort -rn)"
API_ROUTES="$(grep -n 'server\.on(' "$REPO/src/main.cpp" 2>/dev/null | head -60 || true)"
NVS_KEYS="$(grep -rn 'prefs\.put\|prefs\.get\|prefs\.isKey' \
    "$REPO/src/main.cpp" "$REPO/src/web.cpp" "$REPO/src/auth.h" 2>/dev/null | head -80 || true)"
DEFINES="$(grep -rhn '#define\|constexpr' "$REPO/src/"*.h 2>/dev/null \
    | grep -v '^\s*//' | grep -v '#ifndef\|#ifdef\|#endif' | head -80 || true)"
[[ -z "$DEFINES" ]] && warn "No #define/constexpr found in src/*.h — audit may be incomplete"
CURRENT_CLAUDE="$(cat "$REPO/CLAUDE.md")"

AUDIT_PROMPT_FILE="$TMPDIR_REFRESH/audit_prompt.txt"
cat > "$AUDIT_PROMPT_FILE" <<PROMPT_EOF
You are performing a full semantic audit of CLAUDE.md for the SaunaStatus ESP32 project.

Your job: update CLAUDE.md so it accurately reflects the current codebase.
Output ONLY the complete updated CLAUDE.md content — no explanation, no markdown fences around it.

## Audit checklist

1. LINE COUNT TABLE ("Source File Tree" section):
   Update every line count to match the actual wc -l output below.

2. ARCHITECTURE DESCRIPTIONS:
   Verify each module description matches reality. Correct any that are outdated.

3. API ROUTES:
   Cross-reference every route mentioned in CLAUDE.md against the actual server.on() registrations.
   Flag undocumented routes. Remove routes that no longer exist.

4. NVS KEY TABLE:
   Verify every NVS key listed in CLAUDE.md appears in actual prefs.put/get calls.
   Flag keys in code not in CLAUDE.md. Remove keys in CLAUDE.md not in code.

5. CONFIG DEFINES:
   Verify every #define / constexpr named in CLAUDE.md exists with the documented default value.
   Flag undocumented defines. Correct wrong default values.

6. STALE REFERENCES:
   Flag any function, file, or constant named in CLAUDE.md not found in the codebase data below.
   Mark with a <!-- STALE: reason --> HTML comment so the maintainer can review.

7. UNDOCUMENTED:
   Add a section "## Undocumented Items" at the END (before the final section) listing anything
   found in codebase data that is not documented in CLAUDE.md. Use a checklist format.

## Codebase data

### Actual line counts
\`\`\`
${LINE_COUNTS}
\`\`\`

### Actual API route registrations (src/main.cpp server.on calls)
\`\`\`
${API_ROUTES}
\`\`\`

### Actual NVS key operations (prefs.put/get/isKey)
\`\`\`
${NVS_KEYS}
\`\`\`

### Actual #define / constexpr declarations (src/*.h)
\`\`\`
${DEFINES}
\`\`\`

## Current CLAUDE.md

${CURRENT_CLAUDE}
PROMPT_EOF

log "Running claude --print for CLAUDE.md audit (this may take 30-60s)..."
UPDATED_CLAUDE="$(claude --print "$(cat "$AUDIT_PROMPT_FILE")")" \
    || die "claude --print failed for CLAUDE.md audit"

if [[ -z "$UPDATED_CLAUDE" ]]; then
    die "Claude returned empty output for CLAUDE.md audit"
fi

# Sanity check: output must contain key CLAUDE.md markers
if ! echo "$UPDATED_CLAUDE" | grep -q "## Project Overview"; then
    die "Claude output does not look like a valid CLAUDE.md (missing '## Project Overview')"
fi

echo "$UPDATED_CLAUDE" > "$REPO/CLAUDE.md"
log "CLAUDE.md updated."
