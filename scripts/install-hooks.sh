#!/usr/bin/env bash
set -euo pipefail

REPO="$(git rev-parse --show-toplevel)"
HOOKS_DIR="$(git rev-parse --git-path hooks)"
PRE_COMMIT_HOOK="$HOOKS_DIR/pre-commit"
POST_COMMIT_HOOK="$HOOKS_DIR/post-commit"

mkdir -p "$HOOKS_DIR"

cat > "$PRE_COMMIT_HOOK" <<'EOF'
#!/usr/bin/env bash
# Checkpoint artifacts are generated explicitly, not during commit.
exit 0
EOF
chmod +x "$PRE_COMMIT_HOOK"

cat > "$POST_COMMIT_HOOK" <<'EOF'
#!/usr/bin/env bash
# Reserved for local automation. Keep as a no-op so commits do not dirty the tree.
exit 0
EOF
chmod +x "$POST_COMMIT_HOOK"

echo "Installed hooks in $HOOKS_DIR"
echo "  pre-commit -> no-op"
echo "  post-commit -> no-op"
