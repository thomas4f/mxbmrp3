#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_test_headers.sh
# Every test file must carry a header comment that explains what it pins.
#
# WHY THIS EXISTS. TESTING.md's catalogue is a CENSUS - one line per test, so a
# reader can pick which file to open and a new test cannot land undocumented.
# It used to carry the explanation too, in entries that restated the test's own
# header; those copies were deleted, because the copy is the half that rots (the
# catalogue named `kShippedPacks` for a while after the code renamed it
# `kPublishedPacks`, while the test's own header stayed correct).
#
# That leaves the header as the ONLY place a test explains itself, which is the
# right home - it is re-read exactly when someone opens the file - but it is now
# load-bearing, and nothing checked it. A test landing with a four-line header
# would leave its "why" nowhere at all, and the census would still pass, because
# the census only checks that the name is listed.
#
# THE FLOOR IS A FLOOR, not a style rule: 400 bytes, where the thinnest header
# in the tree today is 510 and the median is 1,194. It cannot be met by a
# file-name banner alone, and it does not ask a small test to pad. If a test
# genuinely needs less than that, the honest fix is to say so here in the
# exemption list rather than to write filler.
#
# No --self-test: the assertion is a byte count, and the failure this class of
# lint actually has is the vacuous pass (scan nothing, report success). That is
# guarded directly by MIN_SCANNED below, which is the same guard check_docs.py
# and check_icon_reproducibility.sh use, rather than a second script to maintain.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
cd "${ROOT}"

FLOOR=400
MIN_SCANNED=150      # 195 files today; a big drop means the layout moved.

# Deliberate exemptions: path <TAB> reason. Empty today, and that is the point -
# a file listed here is a decision someone made, not a file that slipped through.
EXEMPT=""

scanned=0
fail=0

header_bytes() {
    awk '
        /^[[:space:]]*\/\// { n += length($0) + 1; next }
        /^[[:space:]]*$/    { if (n > 0) exit; next }
                            { exit }
        END                 { print n + 0 }
    ' "$1"
}

while IFS= read -r f; do
    scanned=$((scanned + 1))
    case "${EXEMPT}" in *"${f}"*) continue ;; esac
    n="$(header_bytes "${f}")"
    if [ "${n}" -lt "${FLOOR}" ]; then
        printf '  %-52s header is %s bytes (floor %s)\n' "${f}" "${n}" "${FLOOR}"
        fail=1
    fi
done < <(find tests/unit tests/integration/tests -name '*.cpp' | sort)

if [ "${scanned}" -lt "${MIN_SCANNED}" ]; then
    echo "ERROR: scanned only ${scanned} test files (expected ${MIN_SCANNED}+)."
    echo "       The layout moved and this check would pass without checking anything."
    exit 1
fi

if [ "${fail}" -ne 0 ]; then
    echo
    echo "FAIL: a test file's header is the only place it explains itself."
    echo
    echo "TESTING.md's catalogue is a census - it names the test, it does not"
    echo "describe it. Say in the header what the test pins and why it exists"
    echo "(the bug it would have caught, the trap the obvious implementation"
    echo "falls into). See CLAUDE.md -> 'Bug lore belongs in the regression test'."
    exit 1
fi

echo "OK: all ${scanned} test files carry a header of at least ${FLOOR} bytes."
