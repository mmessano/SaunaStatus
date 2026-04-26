#!/usr/bin/env bash
set -euo pipefail

REPO="$(git rev-parse --show-toplevel)"

HANDOFF_USE_STAGED=1 bash "$REPO/scripts/update-handoff.sh"
git add HANDOFF.md
