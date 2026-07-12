#!/usr/bin/env bash
# ============================================================================
# tests/web/run.sh
# Convenience runner for the Playwright overlay tests. Installs deps on first
# use, ensures the pinned Chromium is available (a no-op when it's already in
# PLAYWRIGHT_BROWSERS_PATH, e.g. a preinstalled CI image), then runs the suite.
#
#   ./tests/web/run.sh                 # run all
#   ./tests/web/run.sh --headed        # watch it drive the overlay
#   ./tests/web/run.sh -g "race phase" # filter by title
#
# Requires: Node.js. See README.md.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${HERE}"

command -v node >/dev/null || { echo "ERROR: Node.js not found"; exit 1; }

# Install deps once. Skip the auto browser download — we resolve Chromium below.
[ -d node_modules ] || PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm install --no-audit --no-fund

# Fetch the pinned Chromium if it isn't already present (idempotent: prints
# "is already installed" and exits fast when PLAYWRIGHT_BROWSERS_PATH has it).
npx playwright install chromium

exec npx playwright test "$@"
