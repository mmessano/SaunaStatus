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
  - \`pio test -e native\`
  - \`pio run -e lb_esp32s3 -t buildprog\`
- Current focus:
  - P1 hardware validation on a real board
  - P2 documentation drift reduction for routes, config keys, and constants
<!-- CHECKPOINT:END -->

EOF

printf '%s\n' "$REST_OF_FILE" >> "$BACKLOG"

echo "Updated Session Checkpoint in $BACKLOG"
