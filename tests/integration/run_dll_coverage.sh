#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_dll_coverage.sh — REPORT-ONLY line coverage of the real
# plugin DLL as the integration suite exercises it (gcov + gcovr).
#
#   ./tests/integration/run_dll_coverage.sh               # whole suite
#   ./tests/integration/run_dll_coverage.sh race sessions # run_tests.sh filter
#
# WHAT IT MEASURES. The mingw test DLL built with --coverage (build/cross-cov),
# loaded under Wine by the unmodified run_tests.sh. Only the DLL is instrumented:
# harness/test code is ~100% covered by construction and would inflate the total.
# gcov writes each .gcda to its object's absolute path; under Wine "/home/..."
# resolves against the current drive, Z:, which IS /, so no GCOV_PREFIX needed.
#
# WHAT IT DOESN'T. It is the cross-build (Discord/analytics compiled out, no
# SEH, no MSVC paths), so it is not the shipping binary's coverage. A line hit is
# not a line asserted: render code runs under every test and is mostly unchecked
# - tests/integration/API_COVERAGE.md stays the behavioral manifest. A test that
# crashes or times out writes no .gcda. No floor, no failure on the percentage:
# exit status is run_tests.sh's, or gcovr's if it breaks.
#
# EVALUATED. gcovr with the cross gcov (--gcov-executable) does the collection,
# filtering, HTML and per-directory summaries (via --add-tracefile); nothing here
# parses coverage data. OpenCppCoverage would need real Windows; llvm-cov would
# need a clang cross-toolchain this project doesn't otherwise use.
# Opt-in CTest gate `dll-coverage` (MXBMRP3_DLL_COVERAGE=1), like `codeql`.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD="${ROOT}/build/cross-cov"
OUT="${BUILD}/coverage"
DLL="${HERE}/build/mxbmrp3_test.dlo"

# The plain and the instrumented tree link the same DLL path; removing it on
# both sides makes whichever builds next relink instead of trusting a newer file.
rm -f "${DLL}"
trap 'rm -f "${DLL}"' EXIT
# Stale counts from an earlier run would merge into this one's.
[ -d "${BUILD}" ] && find "${BUILD}" -name '*.gcda' -delete

started=${SECONDS}
MXBMRP3_COVERAGE=1 "${HERE}/run_tests.sh" "$@"
rc=$?
echo "== suite exit ${rc} after $(( (SECONDS - started) / 60 )) min =="

# A build failure or an all-crash run leaves nothing to report on.
if [ -z "$(find "${BUILD}" -name '*.gcda' -print -quit 2>/dev/null)" ]; then
    echo "dll-coverage: no .gcda written - nothing to report" >&2
    exit 1
fi
mkdir -p "${OUT}"
GCOVR=(gcovr --root "${ROOT}" --gcov-executable x86_64-w64-mingw32-gcov)
"${GCOVR[@]}" --filter "${ROOT}/mxbmrp3/" \
    --exclude "${ROOT}/mxbmrp3/vendor/(?!piboso/)" \
    --json "${OUT}/dll.json" --txt "${OUT}/dll.txt" \
    --html-nested "${OUT}/index.html" --print-summary "${BUILD}" \
    || { echo "dll-coverage: gcovr failed" >&2; exit 1; }

echo
echo "DLL line coverage by directory (report-only; full: ${OUT}/index.html)"
for d in core hud handlers game vendor/piboso; do
    printf '%-15s ' "${d}/"
    "${GCOVR[@]}" --add-tracefile "${OUT}/dll.json" \
        --filter "${ROOT}/mxbmrp3/${d}/" --print-summary --txt /dev/null 2>/dev/null | grep '^lines:'
done
exit ${rc}
