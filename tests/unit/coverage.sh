#!/usr/bin/env bash
# ============================================================================
# tests/unit/coverage.sh
# Line coverage for the pure-logic unit layer, via gcovr.
#
#   ./tests/unit/coverage.sh            # report only
#   ./tests/unit/coverage.sh 95         # ... and fail below a 95% floor
#   ./tests/unit/coverage.sh 95 <build> # use an existing CMake build tree
#
# WHAT IT MEASURES, AND WHAT IT DELIBERATELY DOESN'T. This reports coverage of
# the PRODUCTION code linked into the unit-test binary — the header-only helpers
# plus the few .cpp TUs in that build. It is explicitly NOT a whole-project
# number, and must not be quoted as one: most of the plugin is only reachable
# through the PiBoSo DLL boundary, where a line percentage would be both
# expensive to obtain (mingw + Wine + gcov plumbing) and misleading (a large
# share of those lines are render calls with no headless observable). The honest
# coverage artifact for that surface is tests/integration/API_COVERAGE.md, which
# tracks each export's status by hand and marks the gaps.
#
# So: this closes the "no coverage instrumentation anywhere" gap for the layer
# where the measurement is cheap and meaningful, and says nothing about the rest.
#
# The floor is a RATCHET: raise it when coverage improves, never lower it to make
# a red build green.
#
# This replaced tools/coverage_report.py, a hand-written gcov-JSON parser that
# did exactly what gcovr's --filter/--exclude/--fail-under-line do.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"

MIN="${1:-}"
BUILD="${2:-${ROOT}/build/coverage}"

if ! command -v gcovr >/dev/null 2>&1; then
    echo "SKIP: gcovr not installed (pip install gcovr)"
    exit 3
fi

# Configure only a fresh tree — an existing one re-generates itself from the
# build rule when CMakeLists.txt changes, and this script is called BY ctest from
# inside that same tree.
[[ -f "${BUILD}/CMakeCache.txt" ]] || cmake -S "${ROOT}" -B "${BUILD}" >/dev/null
cmake --build "${BUILD}" --target unit_tests_cov -j"$(nproc)"

# Stale .gcda from an earlier run would merge into this one's counts.
find "${BUILD}" -name '*.gcda' -delete
"${BUILD}/tests/unit/unit_tests_cov"

echo
echo "Unit-layer line coverage — production code linked into tests/unit."
echo "(Layer-scoped by design; the DLL-boundary surface is tracked in"
echo " tests/integration/API_COVERAGE.md, not here.)"
echo

# Only production sources count. The test TUs themselves, doctest, vendored
# libraries and system headers are noise in a coverage number — a test file is
# ~100% covered by construction and would inflate the total for free.
GCOVR=(gcovr --root "${ROOT}"
       --filter "${ROOT}/mxbmrp3/"
       --exclude "${ROOT}/mxbmrp3/vendor/"
       --sort uncovered-percent
       --txt --print-summary)
[[ -n "${MIN}" ]] && GCOVR+=(--fail-under-line "${MIN}")

rc=0
"${GCOVR[@]}" "${BUILD}" || rc=$?
# gcovr encodes a threshold miss as a BIT (2 = line, 4 = branch); any other
# nonzero is the tool itself failing, and must not be reported as low coverage.
if (( rc & 2 )); then
    echo
    echo "COVERAGE FAIL: below the ${MIN}% floor. Add tests for the"
    echo "least-covered files above. Do not lower the floor to go green."
    exit 1
elif (( rc != 0 )); then
    echo "coverage: gcovr failed (exit ${rc})" >&2
    exit 1
fi
