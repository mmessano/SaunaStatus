# Autonomous Documentation Maintenance System — Design Spec

**Date:** 2026-03-26
**Project:** SaunaStatus (ESP32 sauna automation)
**Status:** Approved

---

## Overview

A shell script (`scripts/refresh-docs.sh`) that performs a full documentation refresh when explicitly triggered. It audits CLAUDE.md against the actual codebase, extends HANDOFF.md with open issues and next steps, extracts uncaptured lessons from git history into skill files, verifies hooks, then shows a diff summary and asks for confirmation before committing.

---

## Architecture

Six sequential steps, each gated — failure stops the pipeline and prints stderr:

```
Step 1  update-handoff.sh          Base HANDOFF.md regeneration (existing script)
Step 2  claude -p "audit-claude"   Full audit of CLAUDE.md vs codebase
Step 3  claude -p "extend-handoff" Append open issues + next steps to HANDOFF.md
Step 4  claude -p "extract-skills" Scan git log → create new skill files
Step 5  hook-verify                Dry-run post-commit hook, report PASS/FAIL
Step 6  diff + confirm             Print summary, ask y/n, git commit
```

`claude -p` prompts are heredocs inside the script. Steps 2–4 pass targeted grep/wc output as context rather than full source files, keeping prompts compact.

---

## Step 2: CLAUDE.md Full Audit

The prompt instructs Claude to perform a full semantic audit and edit CLAUDE.md in-place:

1. **Line count table** — re-measure every file in the Source File Tree and update counts
2. **Architecture descriptions** — verify each module's description matches actual exported types and key behaviors
3. **API routes** — cross-reference every route in CLAUDE.md against `handleXxx()` registrations in `web.cpp`
4. **NVS key table** — verify every listed key (`sri`, `slg`, `omx`, `imx`, etc.) exists in actual `prefs.putX()`/`prefs.getX()` calls
5. **Config defines** — verify every `#define`/`constexpr` named in CLAUDE.md still exists with the documented default value
6. **Stale references** — flag any named function, file, or constant in CLAUDE.md not found in the codebase
7. **Undocumented** — flag any public API routes, NVS keys, or `#define` thresholds in source not mentioned in CLAUDE.md

Context passed to the prompt: full CLAUDE.md content + `wc -l src/*.cpp src/*.h` output + `grep -n "server.on\|server.on\b" src/web.cpp` + `grep -n "prefs.put\|prefs.get" src/main.cpp src/web.cpp src/auth.h`.

---

## Step 3: HANDOFF.md Extension

The prompt instructs Claude to append (or replace on subsequent runs) two sections to HANDOFF.md:

**Open Issues** — sourced from:
- `TODO`/`FIXME`/`HACK` scan across all source files (file:line references)
- Build failure status from step 1's output
- Security follow-up items from `git log --grep="follow-up\|TODO\|FIXME"`
- Stale items flagged by the CLAUDE.md audit (step 2 output passed as context)

**Next Steps** — sourced from:
- Uncompleted tasks in the most recent `docs/superpowers/plans/` files
- Remaining unfixed severity levels if recent commits are all `fix(security):`
- Unchecked hardware verification checklist items already in HANDOFF.md

Sections are delimited with `<!-- OPEN-ISSUES:START -->` / `<!-- OPEN-ISSUES:END -->` and `<!-- NEXT-STEPS:START -->` / `<!-- NEXT-STEPS:END -->` markers, and stamped `<!-- REFRESHED: YYYY-MM-DD -->`, so subsequent runs replace rather than append.

---

## Step 4: Skill Extraction

The prompt scans `git log -60 --patch --no-merges` and compares against existing skill names. For each uncaptured pattern:

- **ESP32/project-specific** → `.claude/skills/<name>/SKILL.md`
- **Broadly reusable** → `~/.claude/skills/learned/<name>.md`
- Patterns that are both get written to both locations

Skill format: standard frontmatter (`name`, `description`, `type` for memory files; `name`, `description` for SKILL.md files) + checklist or reference body.

Expected new skills from the 14-commit security hardening series:
- `esp32-security-hardening` (project-local + global) — PBKDF2, HTTPS enforcement, rate limiting, CSP headers, input whitelisting, WebSocket auth pattern
- `security-remediation-workflow` (global) — CRIT→HIGH→MED→LOW triage and fix sequencing

The prompt receives the list of existing skill file paths to avoid duplication.

---

## Step 5: Hook Verification

```bash
SKIP_BUILD=1 bash scripts/update-handoff.sh
```

Checks: exit code 0, HANDOFF.md written, no `set -e` failures. Reports `PASS` or `FAIL` with stderr on failure. Only one hook exists currently (`post-commit`).

---

## Step 6: Diff + Confirm

```
=== Documentation Refresh Summary ===
Modified:  CLAUDE.md, HANDOFF.md
New files: .claude/skills/esp32-security-hardening/SKILL.md
           ~/.claude/skills/learned/security-remediation-workflow.md

<git diff --stat>

Commit these changes? [y/N]
```

- `y` → `git commit -m "chore(docs): refresh documentation and skills"`
- `N` → leave unstaged; user reviews and commits manually

---

## Error Handling

- Each step runs in a subshell; exit code checked before proceeding
- `claude -p` failures: print stderr, print which step failed, exit non-zero
- Hook verify failure: reported as WARNING (not fatal) — doc refresh is still useful even if the hook is broken
- `git commit` failure: reported with full git error; changes remain staged

---

## Files Created / Modified

| Path | Action |
|------|--------|
| `scripts/refresh-docs.sh` | New — main script |
| `CLAUDE.md` | Modified in-place by step 2 |
| `HANDOFF.md` | Extended by step 3 |
| `.claude/skills/esp32-security-hardening/SKILL.md` | New (expected) |
| `~/.claude/skills/learned/security-remediation-workflow.md` | New (expected) |
| `~/.claude/skills/learned/esp32-security-hardening.md` | New (expected) |

Actual skill files created depend on what `claude -p` finds in the git history.

---

## Non-Goals

- Does not run on every commit (explicit trigger only)
- Does not push to remote
- Does not modify `platformio.ini` or any source files
- Does not replace `update-handoff.sh` — delegates to it
