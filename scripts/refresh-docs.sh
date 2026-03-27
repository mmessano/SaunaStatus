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
