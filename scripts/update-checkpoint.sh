#!/usr/bin/env bash
# scripts/update-checkpoint.sh
# Refreshes the tracked Session Checkpoint block in BACKLOG.md.

set -euo pipefail

REPO="$(git rev-parse --show-toplevel)"
BACKLOG="$REPO/BACKLOG.md"
DATE="$(date +%Y-%m-%d)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
LAST_COMMIT="$(git log -1 --format='%h %s')"
REST_OF_FILE="$(awk '
    /^## P1$/ { keep=1 }
    keep { print }
' "$BACKLOG")"

cat > "$BACKLOG" <<EOF
# Backlog

## Session Checkpoint

<!-- CHECKPOINT:START -->
- Refreshed: ${DATE}
- Branch: \`${BRANCH}\`
- Latest commit: \`${LAST_COMMIT}\`
- Tracked checkpoint command: \`bash scripts/update-checkpoint.sh\`
- Local handoff artifact: \`bash scripts/update-handoff.sh\` writes ignored \`HANDOFF.md\`
- Validation baseline:
  - \`pio test -e native\`  (320 native unit tests)
  - \`pio run -e lb_esp32s3 -t buildprog\`  (firmware compile only, no flash)
  - \`python3 scripts/verify_doc_drift.py\`  (routes + constants + NVS keys vs docs)
  - \`cd tools/ui-test && ./node_modules/.bin/playwright test\`  (44 UI integration tests, desktop + mobile)
- Current focus:
  - P1 hardware validation on a real board — the entire UI lane (BACKLOG.md "UI / Design") and the drift-checker extension are shipped; what is left needs live hardware in front of a person.
<!-- CHECKPOINT:END -->

EOF

printf '%s\n' "$REST_OF_FILE" >> "$BACKLOG"

echo "Updated Session Checkpoint in $BACKLOG"
