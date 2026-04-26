# Autonomous Documentation Maintenance System — Implementation Plan

> Note: current repo policy has since moved the tracked checkpoint surface to `BACKLOG.md`.
> `HANDOFF.md` is now a local ignored artifact generated only on demand.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `scripts/refresh-docs.sh` — an explicitly-triggered pipeline that audits CLAUDE.md against the live codebase, appends AI-analyzed issues/next-steps to HANDOFF.md, extracts new skills from git history, verifies the repo-managed local hooks, and commits all changes after a diff + confirmation prompt.

**Architecture:** Six sequential bash steps, each gated on exit code. Steps 2–4 invoke `claude --print` with focused prompts constructed from live codebase data written to temp files. Claude outputs structured text; the script writes it to disk. Skills are output in a `===FILE:…===` / `===ENDFILE===` format parsed by an inline Python 3 block.

**Tech Stack:** Bash 5, `claude --print` (Claude Code CLI non-interactive mode), Python 3 (skill file parsing), PlatformIO, Git

---

## File Map

| File | Action |
|------|--------|
| `scripts/refresh-docs.sh` | **Create** — main pipeline script (≈250 lines) |
| `CLAUDE.md` | Modified in-place by Step 2 |
| `HANDOFF.md` | AI sections appended/replaced by Step 3 |
| `.claude/skills/<name>/SKILL.md` | Created by Step 4 (project-local skills) |
| `~/.claude/skills/learned/<name>.md` | Created by Step 4 (global learned skills) |

---

## Task 1: Script skeleton with shared utilities

**Files:**
- Create: `scripts/refresh-docs.sh`

- [ ] **Step 1: Create the script with the header, shared helpers, and entrypoint guard**

```bash
cat > scripts/refresh-docs.sh << 'SCRIPT_EOF'
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
#   5. hook verify               — install and sanity-check local no-op hooks
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
SCRIPT_EOF
chmod +x scripts/refresh-docs.sh
```

- [ ] **Step 2: Verify the script is executable and parseable**

```bash
bash -n scripts/refresh-docs.sh && echo "syntax OK"
```
Expected: `syntax OK`

- [ ] **Step 3: Commit the skeleton**

```bash
git add scripts/refresh-docs.sh
git commit -m "feat(scripts): add refresh-docs.sh skeleton with shared utilities"
```

---

## Task 2: Step 1 — delegate to update-handoff.sh

**Files:**
- Modify: `scripts/refresh-docs.sh`

- [ ] **Step 1: Append Step 1 block to the script (before the final line)**

Open `scripts/refresh-docs.sh` and append the following before the last line (the closing of the script):

```bash
# ── Step 1: base HANDOFF.md regeneration ─────────────────────────────────────

step 1 "Base HANDOFF.md regeneration (update-handoff.sh)"

SKIP_BUILD=1 bash "$REPO/scripts/update-handoff.sh" \
    || die "update-handoff.sh failed — fix it before running refresh-docs.sh"

log "HANDOFF.md regenerated."
```

- [ ] **Step 2: Run Step 1 in isolation to verify it passes**

```bash
bash -c '
  source scripts/refresh-docs.sh
' 2>&1 | head -20
```

Simpler: just run the step directly:
```bash
SKIP_BUILD=1 bash scripts/update-handoff.sh && echo "Step 1 PASS"
```
Expected: `[handoff] Done → …/HANDOFF.md` then `Step 1 PASS`

- [ ] **Step 3: Commit**

```bash
git add scripts/refresh-docs.sh
git commit -m "feat(scripts): refresh-docs step 1 — delegate to update-handoff.sh"
```

---

## Task 3: Step 2 — CLAUDE.md full audit

**Files:**
- Modify: `scripts/refresh-docs.sh`

This step collects codebase facts into a temp file, runs `claude --print` with the full CLAUDE.md + facts as the prompt, and overwrites CLAUDE.md with the output.

- [ ] **Step 1: Append Step 2 block to the script**

```bash
# ── Step 2: CLAUDE.md full audit ─────────────────────────────────────────────

step 2 "CLAUDE.md full audit (claude --print)"

# Collect codebase facts
log "Collecting codebase facts..."
LINE_COUNTS="$(wc -l "$REPO"/src/*.cpp "$REPO"/src/*.h 2>/dev/null | sort -rn)"
API_ROUTES="$(grep -n 'server\.on(' "$REPO/src/web.cpp" 2>/dev/null | head -60 || true)"
NVS_KEYS="$(grep -rn 'prefs\.put\|prefs\.get\|prefs\.isKey' \
    "$REPO/src/main.cpp" "$REPO/src/web.cpp" "$REPO/src/auth.h" 2>/dev/null | head -80 || true)"
DEFINES="$(grep -rhn '#define\|constexpr' "$REPO/src/"*.h 2>/dev/null \
    | grep -v '^\s*//' | grep -v '#ifndef\|#ifdef\|#endif' | head -80 || true)"
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

### Actual API route registrations (src/web.cpp server.on calls)
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
```

- [ ] **Step 2: Test the prompt construction (dry run — don't send to Claude)**

```bash
# Just verify the context collection runs without error
cd "$(git rev-parse --show-toplevel)"
wc -l src/*.cpp src/*.h 2>/dev/null | sort -rn | head -5
grep -n 'server\.on(' src/web.cpp 2>/dev/null | head -5
grep -rn 'prefs\.put\|prefs\.get' src/main.cpp src/web.cpp src/auth.h 2>/dev/null | head -5
echo "Context collection OK"
```
Expected: line counts, route registrations, NVS keys printed, then `Context collection OK`

- [ ] **Step 3: Verify script still parses**

```bash
bash -n scripts/refresh-docs.sh && echo "syntax OK"
```
Expected: `syntax OK`

- [ ] **Step 4: Commit**

```bash
git add scripts/refresh-docs.sh
git commit -m "feat(scripts): refresh-docs step 2 — CLAUDE.md full audit via claude --print"
```

---

## Task 4: Step 3 — HANDOFF.md AI extension

**Files:**
- Modify: `scripts/refresh-docs.sh`

Step 3 appends two AI-generated sections to HANDOFF.md under `<!-- REFRESH-AI:START -->` / `<!-- REFRESH-AI:END -->` markers. On re-runs, the script strips the old markers and re-appends.

- [ ] **Step 1: Append Step 3 block to the script**

```bash
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
for f in $RECENT_PLANS; do
    PLAN_CONTENT+="### $(basename "$f")"$'\n'
    PLAN_CONTENT+="$(tail -50 "$f")"$'\n\n'
done

EXTEND_PROMPT_FILE="$TMPDIR_REFRESH/extend_prompt.txt"
cat > "$EXTEND_PROMPT_FILE" <<PROMPT_EOF
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
PROMPT_EOF

log "Running claude --print for HANDOFF.md extension (this may take 30-60s)..."
AI_SECTIONS="$(claude --print "$(cat "$EXTEND_PROMPT_FILE")")" \
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
```

- [ ] **Step 2: Verify strip logic works correctly**

```bash
# Test: if AI section already exists, stripping leaves only base content
TEST_FILE="$(mktemp)"
printf "# Base\n\nsome content\n\n<!-- REFRESH-AI:START -->\nold ai\n<!-- REFRESH-AI:END -->\n" > "$TEST_FILE"
sed '/<!-- REFRESH-AI:START -->/,$ d' "$TEST_FILE"
rm "$TEST_FILE"
```
Expected output:
```
# Base

some content

```

- [ ] **Step 3: Verify script syntax**

```bash
bash -n scripts/refresh-docs.sh && echo "syntax OK"
```

- [ ] **Step 4: Commit**

```bash
git add scripts/refresh-docs.sh
git commit -m "feat(scripts): refresh-docs step 3 — AI-extended HANDOFF.md sections"
```

---

## Task 5: Step 4 — skill extraction

**Files:**
- Modify: `scripts/refresh-docs.sh`

Claude outputs skill files in `===FILE: <path>===` / `===ENDFILE===` delimited format. An inline Python 3 block parses the output and writes each file.

- [ ] **Step 1: Append Step 4 block to the script**

```bash
# ── Step 4: skill extraction ──────────────────────────────────────────────────

step 4 "Skill extraction from git history (claude --print)"

# Collect existing skill names to avoid duplication
EXISTING_PROJECT_SKILLS="$(ls "$REPO/.claude/skills/" 2>/dev/null | sort || echo "(none)")"
EXISTING_GLOBAL_SKILLS="$(ls ~/.claude/skills/learned/ 2>/dev/null | sort || echo "(none)")"

# Recent git history (stats only for overview; patches for last 15 commits)
GIT_OVERVIEW="$(git log -60 --no-merges --stat --oneline | head -150)"
GIT_PATCHES="$(git log -15 --no-merges -p --stat | head -600)"

SKILLS_PROMPT_FILE="$TMPDIR_REFRESH/skills_prompt.txt"
cat > "$SKILLS_PROMPT_FILE" <<PROMPT_EOF
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

## Recent diffs (last 15 commits — look for patterns here)
\`\`\`
${GIT_PATCHES}
\`\`\`

## Instructions

- Identify 2-5 patterns worth capturing as skills
- Focus on: security hardening workflows, testing patterns, config layering, anything that bit this project
- Do NOT output anything outside the ===FILE: …=== blocks
- If you find no new patterns worth capturing, output exactly: NO_NEW_SKILLS
PROMPT_EOF

log "Running claude --print for skill extraction (this may take 60-90s)..."
SKILLS_OUTPUT="$(claude --print "$(cat "$SKILLS_PROMPT_FILE")")" \
    || die "claude --print failed for skill extraction"

if [[ -z "$SKILLS_OUTPUT" ]]; then
    die "Claude returned empty output for skill extraction"
fi

if echo "$SKILLS_OUTPUT" | grep -q "^NO_NEW_SKILLS"; then
    log "No new skills to extract."
else
    # Parse and write skill files using Python
    log "Writing skill files..."
    python3 - "$TMPDIR_REFRESH" "$REPO" <<'PYEOF'
import sys, os, re

tmpdir = sys.argv[1]
repo   = sys.argv[2]

# Read Claude output from stdin (we'll pass it via env var set below)
import subprocess
skills_output = os.environ.get("SKILLS_OUTPUT", "")

pattern = re.compile(
    r'===FILE:\s*(.+?)===\n(.*?)===ENDFILE===',
    re.DOTALL
)

written = 0
for m in pattern.finditer(skills_output):
    raw_path = m.group(1).strip()
    content  = m.group(2)

    # Expand ~ and make absolute
    path = os.path.expanduser(raw_path)
    if not os.path.isabs(path):
        path = os.path.join(repo, path)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(content)
    print(f"  [refresh]   wrote: {raw_path}")
    written += 1

if written == 0:
    print("  [refresh] WARNING: skill output present but no ===FILE: blocks found", file=sys.stderr)
    sys.exit(1)
else:
    print(f"  [refresh] {written} skill file(s) written.")
PYEOF
    # Pass SKILLS_OUTPUT as env var to the Python block
    export SKILLS_OUTPUT
    python3 - "$TMPDIR_REFRESH" "$REPO" <<'PYEOF'
import sys, os, re
tmpdir = sys.argv[1]
repo   = sys.argv[2]
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
```

> **Note on the duplicate Python block:** The first `python3 - … <<'PYEOF'` block in the `else` branch above is orphaned (it runs before `SKILLS_OUTPUT` is exported). Remove it — only the second invocation (after `export SKILLS_OUTPUT`) is correct. The final script should have only one Python invocation.

- [ ] **Step 2: Test the Python parser independently**

```bash
python3 - /tmp /tmp <<'PYEOF'
import sys, os, re
tmpdir = sys.argv[1]
repo   = sys.argv[2]
# Simulate Claude output
os.environ["SKILLS_OUTPUT"] = """===FILE: .claude/skills/test-skill/SKILL.md===
---
name: test-skill
description: A test skill
---
# Test
===ENDFILE==="""
skills_output = os.environ["SKILLS_OUTPUT"]
pattern = re.compile(r'===FILE:\s*(.+?)===\n(.*?)===ENDFILE===', re.DOTALL)
written = 0
for m in pattern.finditer(skills_output):
    raw_path = m.group(1).strip()
    content  = m.group(2)
    path = os.path.expanduser(raw_path)
    if not os.path.isabs(path):
        path = os.path.join(repo, path)
    print(f"Would write: {raw_path}")
    print(f"Content preview: {content[:40]!r}")
    written += 1
print(f"{written} file(s) would be written.")
PYEOF
```
Expected:
```
Would write: .claude/skills/test-skill/SKILL.md
Content preview: '---\nname: test-skill\ndescription: A test'
1 file(s) would be written.
```

- [ ] **Step 3: Fix the duplicate Python block** — edit `scripts/refresh-docs.sh` so only the second Python invocation (after `export SKILLS_OUTPUT`) remains inside the `else` branch. The final `else` block should look like:

```bash
else
    log "Writing skill files..."
    export SKILLS_OUTPUT
    python3 - "$TMPDIR_REFRESH" "$REPO" <<'PYEOF'
import sys, os, re
tmpdir = sys.argv[1]
repo   = sys.argv[2]
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
```

- [ ] **Step 4: Verify script syntax**

```bash
bash -n scripts/refresh-docs.sh && echo "syntax OK"
```

- [ ] **Step 5: Commit**

```bash
git add scripts/refresh-docs.sh
git commit -m "feat(scripts): refresh-docs step 4 — skill extraction via claude --print"
```

---

## Task 6: Steps 5 + 6 — hook verify, diff + confirm + commit

**Files:**
- Modify: `scripts/refresh-docs.sh`

- [ ] **Step 1: Append Step 5 (hook verify) block**

```bash
# ── Step 5: hook verification ─────────────────────────────────────────────────

step 5 "Local hook installation verification"

log "Installing repo-managed hooks..."
HOOK_OUT="$(bash "$REPO/scripts/install-hooks.sh" 2>&1)" \
    && HOOK_EXIT=0 || HOOK_EXIT=$?

if [[ "$HOOK_EXIT" -eq 0 ]]; then
    log "Hook PASS — install-hooks.sh refreshed local no-op hooks."
else
    warn "Hook FAIL (exit $HOOK_EXIT) — hook is broken but continuing refresh."
    warn "Hook output:"
    echo "$HOOK_OUT" | sed 's/^/    /' >&2
fi
```

- [ ] **Step 2: Append Step 6 (diff + confirm + commit) block**

```bash
# ── Step 6: diff + confirm + commit ──────────────────────────────────────────

step 6 "Diff + confirm + commit"

# Collect modified and new files
MODIFIED_FILES="$(git diff --name-only)"
NEW_FILES="$(git ls-files --others --exclude-standard \
    | grep -v 'compile_commands\.json' || true)"

# Also check global skills dir for new files
NEW_GLOBAL_SKILLS="$(ls -1 ~/.claude/skills/learned/*.md 2>/dev/null \
    | xargs -I{} basename {} 2>/dev/null \
    | while read -r f; do
        git -C "$REPO" ls-files --error-unmatch "$f" 2>/dev/null || echo "~/.claude/skills/learned/$f"
      done || true)"

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

if [[ -n "$NEW_GLOBAL_SKILLS" ]]; then
    echo "New global skills (outside repo):"
    echo "$NEW_GLOBAL_SKILLS" | sed 's/^/  /'
    echo ""
fi

if [[ -z "$MODIFIED_FILES" && -z "$NEW_FILES" ]]; then
    log "No changes detected. Nothing to commit."
    exit 0
fi

echo "─── git diff --stat ───────────────────────────────────────────────────────"
git diff --stat
echo ""

# Stage everything (exclude compile_commands.json)
git add CLAUDE.md HANDOFF.md 2>/dev/null || true
if [[ -n "$NEW_FILES" ]]; then
    echo "$NEW_FILES" | xargs git add -- 2>/dev/null || true
fi

printf "Commit these changes? [y/N] "
read -r CONFIRM </dev/tty

if [[ "${CONFIRM,,}" == "y" ]]; then
    git commit -m "chore(docs): refresh documentation and skills" \
        || die "git commit failed — changes are staged, commit manually"
    log "Committed: $(git log --oneline -1)"
else
    log "Skipped commit. Changes are staged — review and commit manually."
    git restore --staged . 2>/dev/null || true
fi
```

- [ ] **Step 3: Verify script syntax**

```bash
bash -n scripts/refresh-docs.sh && echo "syntax OK"
```

- [ ] **Step 4: Commit**

```bash
git add scripts/refresh-docs.sh
git commit -m "feat(scripts): refresh-docs steps 5+6 — hook verify and diff/confirm/commit"
```

---

## Task 7: End-to-end smoke test and final commit

- [ ] **Step 1: Check `claude --print` works non-interactively**

```bash
claude --print "Reply with exactly: READY" 2>&1
```
Expected: `READY`

If this fails, the claude CLI is not configured for non-interactive use. Fix before proceeding.

- [ ] **Step 2: Run the full pipeline (Steps 1–5 only, skip commit)**

To test without committing, temporarily answer `N` at the confirm prompt:

```bash
bash scripts/refresh-docs.sh
# At "Commit these changes? [y/N]" — type N
```

Expected output sequence:
```
═══ Step 1: Base HANDOFF.md regeneration (update-handoff.sh) ═══
  [handoff] ...
  [handoff] Done → .../HANDOFF.md
  [refresh] HANDOFF.md regenerated.

═══ Step 2: CLAUDE.md full audit (claude --print) ═══
  [refresh] Collecting codebase facts...
  [refresh] Running claude --print for CLAUDE.md audit (this may take 30-60s)...
  [refresh] CLAUDE.md updated.

═══ Step 3: HANDOFF.md AI extension (claude --print) ═══
  [refresh] Running claude --print for HANDOFF.md extension (this may take 30-60s)...
  [refresh] HANDOFF.md extended with AI sections.

═══ Step 4: Skill extraction from git history (claude --print) ═══
  [refresh] Running claude --print for skill extraction (this may take 60-90s)...
  [refresh]   wrote: .claude/skills/...
  [refresh] N skill file(s) written.

═══ Step 5: Local hook installation verification ═══
  [refresh] Hook PASS — install-hooks.sh refreshed local no-op hooks.

═══ Step 6: Diff + confirm + commit ═══
...
Commit these changes? [y/N] N
  [refresh] Skipped commit. Changes are staged — review and commit manually.
```

- [ ] **Step 3: Review the diff**

```bash
git diff CLAUDE.md | head -60
git diff HANDOFF.md | tail -40
ls -la .claude/skills/
ls -la ~/.claude/skills/learned/
```

Verify:
- CLAUDE.md line counts are updated (e.g. `web.cpp` now shows ~937, not 782)
- HANDOFF.md ends with `<!-- REFRESH-AI:START -->` ... `<!-- REFRESH-AI:END -->`
- At least one new skill file exists

- [ ] **Step 4: If output looks good, run again and confirm commit**

```bash
bash scripts/refresh-docs.sh
# At prompt — type y
```

- [ ] **Step 5: Verify the commit and that `HANDOFF.md` was updated explicitly by the refresh flow**

```bash
git log --oneline -3
git diff HEAD~1 --name-only
```
Expected: last commit includes `CLAUDE.md`, `HANDOFF.md`, any new skill files.

- [ ] **Step 6: Final commit of the plan doc**

```bash
git add docs/superpowers/plans/2026-03-26-doc-maintenance.md
git commit -m "docs(superpowers): add implementation plan for doc maintenance system"
```

---

## Self-Review

**Spec coverage check:**
- ✅ Step 1: delegates to `update-handoff.sh`
- ✅ Step 2: full CLAUDE.md audit (line counts, routes, NVS keys, defines, stale, undocumented)
- ✅ Step 3: HANDOFF.md AI extension with `<!-- REFRESH-AI:START/END -->` markers
- ✅ Step 4: skill extraction, project-local + global, `===FILE:===` format + Python parser
- ✅ Step 5: hook verify (WARNING not fatal)
- ✅ Step 6: diff + confirm + commit
- ✅ Error handling: each step gated, `die()` on failure, empty output guards
- ✅ Non-goals respected: no auto-commit without confirmation, no push, no source file edits

**Placeholder scan:** None found — all steps include complete bash code.

**Type/name consistency:** `SKILLS_OUTPUT` env var used in both the export and the Python heredoc — consistent. `TMPDIR_REFRESH` used throughout — consistent. `die()` / `log()` / `warn()` defined in Task 1 skeleton and used in all subsequent tasks — consistent.
