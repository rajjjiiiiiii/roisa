#!/usr/bin/env bash
# finish-feature.sh — Merges paired feature branches back to main and
# python-port, pushes both, and deletes the local feature branches.
#
# Usage:  ./scripts/finish-feature.sh <feature-name>
# Example: ./scripts/finish-feature.sh add-wl-presets
#
# Requires both branches to exist:
#   feature/<name>     (C++ — merged into main)
#   feature/<name>-py  (Python — merged into python-port)

set -euo pipefail
FEATURE="${1:?Usage: $0 <feature-name>}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

# ── C++ side ──────────────────────────────────────────────────────────────────
if ! git show-ref --verify --quiet "refs/heads/feature/$FEATURE"; then
    echo "ERROR: branch feature/$FEATURE not found" >&2
    exit 1
fi

echo "→ Merging C++ branch into main…"
git checkout main
git merge --no-ff "feature/$FEATURE" \
    -m "$(printf 'Merge feature/%s into main\n\nCo-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>' "$FEATURE")"
git push origin main
git branch -d "feature/$FEATURE"
echo "✓  main updated and pushed."

# ── Python side ───────────────────────────────────────────────────────────────
if ! git show-ref --verify --quiet "refs/heads/feature/${FEATURE}-py"; then
    echo "WARNING: branch feature/${FEATURE}-py not found — python-port not updated." >&2
    git checkout main
    exit 0
fi

echo "→ Merging Python branch into python-port…"
git checkout python-port
git merge --no-ff "feature/${FEATURE}-py" \
    -m "$(printf 'Merge feature/%s-py into python-port\n\nCo-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>' "$FEATURE")"
git push origin python-port
git branch -d "feature/${FEATURE}-py"
echo "✓  python-port updated and pushed."

git checkout main
echo ""
echo "Done — both C++ (main) and Python (python-port) are up to date."
