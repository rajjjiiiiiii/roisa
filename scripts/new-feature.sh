#!/usr/bin/env bash
# new-feature.sh — Creates paired C++ and Python feature branches.
#
# Usage:  ./scripts/new-feature.sh <feature-name>
# Example: ./scripts/new-feature.sh add-wl-presets
#
# Creates:
#   feature/<name>     branched off main       (C++ work)
#   feature/<name>-py  branched off python-port (Python work)
# then checks out the C++ branch so you can start there.

set -euo pipefail
FEATURE="${1:?Usage: $0 <feature-name>}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

echo "→ Pulling latest main…"
git checkout main
git pull --ff-only origin main 2>/dev/null || true
git checkout -b "feature/$FEATURE"
echo "✓  C++ branch ready:    feature/$FEATURE"

echo "→ Pulling latest python-port…"
git checkout python-port
git pull --ff-only origin python-port 2>/dev/null || true
git checkout -b "feature/${FEATURE}-py"
echo "✓  Python branch ready: feature/${FEATURE}-py"

git checkout "feature/$FEATURE"
echo ""
echo "Workflow:"
echo "  1.  Implement feature in C++    (src/)    on  feature/$FEATURE"
echo "      git add … && git commit"
echo ""
echo "  2.  git checkout feature/${FEATURE}-py"
echo "      Implement the same feature in Python  (roisa/)"
echo "      git add … && git commit"
echo ""
echo "  3.  ./scripts/finish-feature.sh $FEATURE"
