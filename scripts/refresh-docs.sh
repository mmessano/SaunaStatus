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
#   5. hook verify               — dry-run pre-commit handoff hook
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
UPDATED_CLAUDE="$(claude --print "$(cat "$AUDIT_PROMPT_FILE")" < /dev/null)" \
    || die "claude --print failed for CLAUDE.md audit"

if [[ -z "$UPDATED_CLAUDE" ]]; then
    die "Claude returned empty output for CLAUDE.md audit"
fi

# Sanity check: output must contain key CLAUDE.md markers
if ! echo "$UPDATED_CLAUDE" | grep -q "## Project Overview"; then
    die "Claude output does not look like a valid CLAUDE.md (missing '## Project Overview')"
fi

# Strip any preamble before the first # heading (claude sometimes prefixes with prose)
UPDATED_CLAUDE="$(echo "$UPDATED_CLAUDE" | sed -n '/^# /,$p')"
if [[ -z "$UPDATED_CLAUDE" ]]; then
    die "Claude output has no top-level markdown heading — cannot write CLAUDE.md safely"
fi

echo "$UPDATED_CLAUDE" > "$REPO/CLAUDE.md"
log "CLAUDE.md updated."

# ── Step 3: HANDOFF.md AI extension ──────────────────────────────────────────

step 3 "HANDOFF.md AI extension (claude --print)"

# Strip any existing AI sections from previous refresh runs
HANDOFF_BASE="$(sed '/<!-- REFRESH-AI:START -->/,$ d' "$REPO/HANDOFF.md")"

# Collect context for the prompt
RECENT_LOG="$(git log --oneline -20)"
RECENT_SECURITY="$(git log --oneline -40 | grep 'security\|fix\|CRIT\|HIGH\|MED\|LOW' | head -15 || true)"
TODOS_FOUND="$(grep -rn --include='*.cpp' --include='*.h' \
    -E '\b(TODO|FIXME|HACK|XXX)\b' "$REPO/src" "$REPO/test" 2>/dev/null | head -30 || true)"
RECENT_PLANS="$(ls -t "$REPO/docs/superpowers/plans/"*.md 2>/dev/null | head -3 || true)"
PLAN_CONTENT=""
while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    PLAN_CONTENT+="### $(basename "$f")"$'\n'
    PLAN_CONTENT+="$(tail -50 "$f")"$'\n\n'
done <<< "$RECENT_PLANS"

EXTEND_PROMPT_FILE="$TMPDIR_REFRESH/extend_prompt.txt"
cat > "$EXTEND_PROMPT_FILE" <<EXTEND_PROMPT_EOF
You are extending HANDOFF.md for the SaunaStatus ESP32 project with AI-analyzed sections.

Output ONLY the two sections below — no explanation, no preamble.
Start your output with exactly: <!-- REFRESH-AI:START -->
End your output with exactly: <!-- REFRESH-AI:END -->

## Section 1: Open Issues (AI Analysis)

Heading: ## AI Analysis: Open Issues

Produce a prioritized list of open issues based on:
- TODOs/FIXMEs in source (listed below)
- Any security work that appears incomplete based on commit history
- Any build failures or test failures mentioned in the current HANDOFF.md
- Any items flagged as STALE in CLAUDE.md (pass through if found)

Format each issue as: **[PRIORITY] File:line** — description

## Section 2: Recommended Next Steps

Heading: ## AI Analysis: Recommended Next Steps

Based on the recent commit patterns and plan files, produce an ordered list of recommended
next actions. Be specific — reference files, functions, or commits where relevant.
Mark each item as one of: [ ] todo, [~] in-progress, [x] done

Include a timestamp: <!-- REFRESHED: ${DATE} -->

## Context

### Recent commits
\`\`\`
${RECENT_LOG}
\`\`\`

### Security-related commits
\`\`\`
${RECENT_SECURITY}
\`\`\`

### TODOs/FIXMEs in source
\`\`\`
${TODOS_FOUND:-"(none found)"}
\`\`\`

### Recent plan files
${PLAN_CONTENT:-"(none)"}

### Current HANDOFF.md (for context — do not reproduce)
$(echo "$HANDOFF_BASE" | tail -80)
EXTEND_PROMPT_EOF

log "Running claude --print for HANDOFF.md extension (this may take 30-60s)..."
AI_SECTIONS="$(claude --print "$(cat "$EXTEND_PROMPT_FILE")" < /dev/null)" \
    || die "claude --print failed for HANDOFF.md extension"

if [[ -z "$AI_SECTIONS" ]]; then
    die "Claude returned empty output for HANDOFF.md extension"
fi

if ! echo "$AI_SECTIONS" | grep -q "REFRESH-AI:START"; then
    die "Claude output missing <!-- REFRESH-AI:START --> marker"
fi

# Write: base content + AI sections
{
    echo "$HANDOFF_BASE"
    echo ""
    echo "$AI_SECTIONS"
} > "$REPO/HANDOFF.md"

log "HANDOFF.md extended with AI sections."

# ── Step 4: skill extraction ──────────────────────────────────────────────────

step 4 "Skill extraction from git history (claude --print)"

# Collect existing skill names to avoid duplication
EXISTING_PROJECT_SKILLS="$(ls "$REPO/.claude/skills/" 2>/dev/null | sort || echo "(none)")"
EXISTING_GLOBAL_SKILLS="$(ls ~/.claude/skills/learned/ 2>/dev/null | sort || echo "(none)")"

# Recent git history
GIT_OVERVIEW="$(git log -60 --no-merges --stat --oneline | head -150)"
GIT_PATCHES="$(git log -15 --no-merges -p --stat | head -600)"

SKILLS_PROMPT_FILE="$TMPDIR_REFRESH/skills_prompt.txt"
cat > "$SKILLS_PROMPT_FILE" <<SKILLS_PROMPT_EOF
You are extracting reusable development patterns from git history for the SaunaStatus ESP32 project.

## Your task

Scan the git history below and identify patterns NOT already covered by existing skills.
For each new pattern, produce a skill file.

## Output format

For EACH new skill, output a block in this exact format (no deviation):

===FILE: <relative-or-absolute-path>===
<complete file content>
===ENDFILE===

Path rules:
- Project-specific (ESP32/SaunaStatus patterns): .claude/skills/<skill-name>/SKILL.md
- Broadly reusable patterns: ~/.claude/skills/learned/<skill-name>.md
- A pattern that is BOTH: output TWO blocks — one for each path

## Skill file format

For .claude/skills/<name>/SKILL.md:
\`\`\`
---
name: <skill-name>
description: <one-line description of when to invoke this skill>
---

# <Title>

## When to Use
<1-2 sentences>

## Pattern / Rules
<numbered checklist>

## Example
<concrete code or command example if applicable>
\`\`\`

For ~/.claude/skills/learned/<name>.md:
\`\`\`
---
name: <skill-name>
description: <one-line description>
type: feedback
---

<body — rule first, then **Why:** line, then **How to apply:** line>
\`\`\`

## Existing skills (DO NOT duplicate these)

### Project skills (.claude/skills/)
${EXISTING_PROJECT_SKILLS}

### Global learned skills (~/.claude/skills/learned/)
${EXISTING_GLOBAL_SKILLS}

## Git history overview (last 60 commits)
\`\`\`
${GIT_OVERVIEW}
\`\`\`

## Recent diffs (last 15 commits)
\`\`\`
${GIT_PATCHES}
\`\`\`

## Instructions

- Identify 2-5 patterns worth capturing as skills
- Focus on: security hardening workflows, testing patterns, config layering, anything that bit this project
- Do NOT output anything outside the ===FILE: …=== blocks
- If you find no new patterns worth capturing, output exactly: NO_NEW_SKILLS
SKILLS_PROMPT_EOF

log "Running claude --print for skill extraction (this may take 60-90s)..."
SKILLS_OUTPUT="$(claude --print "$(cat "$SKILLS_PROMPT_FILE")" < /dev/null)" \
    || die "claude --print failed for skill extraction"

if [[ -z "$SKILLS_OUTPUT" ]]; then
    die "Claude returned empty output for skill extraction"
fi

if echo "$SKILLS_OUTPUT" | grep -qx "NO_NEW_SKILLS"; then
    log "No new skills to extract."
else
    log "Writing skill files..."
    export SKILLS_OUTPUT
    python3 - "$REPO" <<'PYEOF'
import sys, os, re

repo = sys.argv[1]
skills_output = os.environ.get("SKILLS_OUTPUT", "")
pattern = re.compile(r'===FILE:\s*(.+?)===\n(.*?)===ENDFILE===', re.DOTALL)
written = 0

for m in pattern.finditer(skills_output):
    raw_path = m.group(1).strip()
    content  = m.group(2)
    path = os.path.expanduser(raw_path)
    if not os.path.isabs(path):
        path = os.path.join(repo, path)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)
    print(f"  [refresh]   wrote: {raw_path}")
    written += 1

if written == 0:
    print("  [refresh] WARNING: no ===FILE: blocks found in skill output", file=sys.stderr)
    sys.exit(1)
print(f"  [refresh] {written} skill file(s) written.")
PYEOF
fi

# ── Step 5: hook verification ─────────────────────────────────────────────────

step 5 "Pre-commit hook verification"

log "Running pre-commit hook in dry-run mode (SKIP_BUILD=1)..."
# Save AI-extended HANDOFF.md — update-handoff.sh unconditionally overwrites it
cp "$REPO/HANDOFF.md" "$TMPDIR_REFRESH/handoff_backup.md"
HOOK_OUT="$(SKIP_BUILD=1 bash "$REPO/scripts/pre-commit-handoff.sh" 2>&1)" \
    && HOOK_EXIT=0 || HOOK_EXIT=$?
# Restore the AI-extended version
mv "$TMPDIR_REFRESH/handoff_backup.md" "$REPO/HANDOFF.md"

if [[ "$HOOK_EXIT" -eq 0 ]]; then
    log "Hook PASS — pre-commit handoff hook exits 0."
else
    warn "Hook FAIL (exit $HOOK_EXIT) — hook is broken but continuing refresh."
    echo "$HOOK_OUT" | sed 's/^/    /' >&2
fi

# ── Step 6: diff + confirm + commit ──────────────────────────────────────────

step 6 "Diff + confirm + commit"

# Collect modified and new files
MODIFIED_FILES="$(git diff --name-only)"
NEW_FILES="$(git ls-files --others --exclude-standard \
    | grep -v 'compile_commands\.json' || true)"

echo ""
echo "╔════════════════════════════════════════╗"
echo "║   Documentation Refresh Summary        ║"
echo "╚════════════════════════════════════════╝"
echo ""

if [[ -n "$MODIFIED_FILES" ]]; then
    echo "Modified files:"
    echo "$MODIFIED_FILES" | sed 's/^/  /'
    echo ""
fi

if [[ -n "$NEW_FILES" ]]; then
    echo "New files (untracked):"
    echo "$NEW_FILES" | sed 's/^/  /'
    echo ""
fi

if [[ -z "$MODIFIED_FILES" && -z "$NEW_FILES" ]]; then
    log "No changes detected. Nothing to commit."
    exit 0
fi

echo "─── git diff --stat ───────────────────────────────────────────────────────"
git diff --stat
echo ""

# Stage repo files (exclude compile_commands.json)
git add CLAUDE.md HANDOFF.md 2>/dev/null || true
if [[ -n "$NEW_FILES" ]]; then
    while IFS= read -r nf; do
        [[ -z "$nf" ]] && continue
        git add -- "$nf" 2>/dev/null || true
    done <<< "$NEW_FILES"
fi

CONFIRM=""
printf "Commit these changes? [y/N] "
read -r CONFIRM </dev/tty || true

if [[ "${CONFIRM,,}" == "y" ]]; then
    git commit -m "chore(docs): refresh documentation and skills" \
        || die "git commit failed — changes are staged, commit manually"
    log "Committed: $(git log --oneline -1)"
else
    log "Skipped commit. Changes are staged — review and commit manually."
    git restore --staged . 2>/dev/null || true
fi
