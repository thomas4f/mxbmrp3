#!/usr/bin/env bash
# ============================================================================
# tests/web/lint.sh — ESLint over every .js in the repo (overlay + this suite).
# The rule set and why three rules are off live in eslint.config.mjs.
#
#   ./tests/web/lint.sh            # check (what CI and the ctest gate run)
#   ./tests/web/lint.sh --fix      # apply the fixable ones
#   ./tests/web/lint.sh <path>     # one file or directory
#
# Requires: Node.js. Shares node_modules with run.sh — first use installs.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"

command -v node >/dev/null || { echo "ERROR: Node.js not found"; exit 1; }

# Same install line as run.sh, including the browser skip: eslint does not need
# Chromium, and whichever script runs first should not fetch one.
[ -d "${HERE}/node_modules" ] || (cd "${HERE}" && \
    PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm install --no-audit --no-fund)

# From the ROOT, so the config's globs (mxbmrp3_data/... , tests/web/...) mean
# what they say. Default targets when none are given: everything the config
# covers, which is every .js in the repo bar node_modules and vendor.
cd "${ROOT}"
if [ "$#" -eq 0 ] || [ "${1}" = "--fix" ]; then
    set -- "$@" mxbmrp3_data/web tests/web
fi
exec "${HERE}/node_modules/.bin/eslint" --config "${HERE}/eslint.config.mjs" "$@"
