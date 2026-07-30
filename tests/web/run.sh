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

# Resolve a browser BEFORE reaching for the network. Playwright's `install` step
# pins an exact build and downloads it from cdn.playwright.dev, which is blocked
# in sandboxed environments (403 "host not permitted") — and is pure waste on an
# image that already ships Chromium under PLAYWRIGHT_BROWSERS_PATH. Discovering
# what is already here first makes the common cases (CI image, dev box, sandbox)
# all work without anyone having to hand-set MXB_CHROMIUM, which is what this
# used to require. MXB_CHROMIUM (see playwright.config.js) still wins when set
# explicitly.
if [ -z "${MXB_CHROMIUM:-}" ]; then
    for cand in \
        "${PLAYWRIGHT_BROWSERS_PATH:-/opt/pw-browsers}"/chromium-*/chrome-linux/chrome \
        "${PLAYWRIGHT_BROWSERS_PATH:-/opt/pw-browsers}"/chromium/chrome-linux/chrome \
        "$(command -v chromium 2>/dev/null || true)" \
        "$(command -v chromium-browser 2>/dev/null || true)" \
        "$(command -v google-chrome 2>/dev/null || true)"
    do
        if [ -n "${cand}" ] && [ -x "${cand}" ]; then
            export MXB_CHROMIUM="${cand}"
            echo "Using preinstalled browser: ${MXB_CHROMIUM}"
            break
        fi
    done
fi

# Still nothing — try the download, but treat a failure as "prerequisite
# unavailable" (exit 3) rather than a test failure. CTest maps 3 to SKIPPED
# (SKIP_RETURN_CODE, set by mxb_gate in CMakeLists.txt):
# a machine with no browser and no route to fetch one hasn't found a bug, and
# reporting it as one buries real failures.
if [ -z "${MXB_CHROMIUM:-}" ]; then
    if ! npx playwright install chromium; then
        echo "SKIP: no usable Chromium found and the pinned build could not be downloaded." >&2
        echo "      Set MXB_CHROMIUM=/path/to/chrome to use an existing browser." >&2
        exit 3
    fi
fi

exec npx playwright test "$@"
