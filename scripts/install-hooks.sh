#!/usr/bin/env bash
set -euo pipefail

REPO="$(git rev-parse --show-toplevel)"
HOOKS_DIR="$(git rev-parse --git-path hooks)"
PRE_COMMIT_HOOK="$HOOKS_DIR/pre-commit"
POST_COMMIT_HOOK="$HOOKS_DIR/post-commit"

mkdir -p "$HOOKS_DIR"

cat > "$PRE_COMMIT_HOOK" <<'EOF'
#!/usr/bin/env bash
exec bash "$(git rev-parse --show-toplevel)/scripts/pre-commit-handoff.sh"
EOF
chmod +x "$PRE_COMMIT_HOOK"

cat > "$POST_COMMIT_HOOK" <<'EOF'
#!/usr/bin/env bash
# HANDOFF.md is generated and staged by the pre-commit hook.
exit 0
EOF
chmod +x "$POST_COMMIT_HOOK"

echo "Installed hooks in $HOOKS_DIR"
echo "  pre-commit -> scripts/pre-commit-handoff.sh"
echo "  post-commit -> no-op"
