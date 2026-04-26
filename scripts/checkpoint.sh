#!/usr/bin/env bash
# scripts/checkpoint.sh
# Refreshes tracked checkpoint state and, unless disabled, a local-only handoff snapshot.

set -euo pipefail

REPO="$(git rev-parse --show-toplevel)"
WRITE_HANDOFF=1

for arg in "$@"; do
    case "$arg" in
        --skip-handoff)
            WRITE_HANDOFF=0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            echo "Usage: bash scripts/checkpoint.sh [--skip-handoff]" >&2
            exit 1
            ;;
    esac
done

bash "$REPO/scripts/update-checkpoint.sh"

if [[ "$WRITE_HANDOFF" == "1" ]]; then
    bash "$REPO/scripts/update-handoff.sh"
    echo "Local handoff artifact refreshed: $REPO/HANDOFF.md"
else
    echo "Skipped local HANDOFF.md generation."
fi
