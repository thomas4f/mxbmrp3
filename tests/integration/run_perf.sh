#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_perf.sh
# CPU performance baseline for the plugin's hot paths (see perf_driver.cpp).
# The plugin is CPU-bound, so this headless measurement under Wine is
# representative of the cost it controls, measured against the 480fps budget.
#
# THESE NUMBERS ARE PESSIMISTIC, BY DESIGN — don't quote them as "what the
# plugin costs". The cross-build is -O1 while the shipping MSVC DLL is
# /O2 + /LTCG (mxbmrp3/CMakeLists.txt says why), so the binary measured here is
# the slower one. Measured gap: ~14% on Draw average, ~25% on the standings
# rebuild. That direction is the useful one — a pass here implies a pass on
# what ships — but it means a figure from this script understates the real
# plugin by roughly that much, on top of Wine overhead.
#
# Two drivers run against the SAME demanding scenario (a full 50-rider grid on a
# long/complex ~2400m circuit — see perf_scenario.h, a heavier superset of the
# real Farm14 capture):
#   * perf_driver     — isolated per-callback cost (Draw / TrackPos / Class / Telemetry)
#   * map_perf_driver — the realistic interleaved MAP hot loop (position update +
#                       Draw every frame; the map's per-frame rebuild is the
#                       heaviest thing the plugin does)
#
# Prints the full report every run (eyeball trends in CI logs) and applies a
# DELIBERATELY GENEROUS regression gate on the headline number (average Draw
# time) so it fails only on a gross regression, not on normal CI CPU variance,
# PLUS a p99-under-480fps-budget gate on both the general Draw and the worst
# map-ON Draw. Override the gross-regression ceiling with the first arg (us).
#
# Requires: mingw-w64 (posix), wine64.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
DRAW_MAX_US="${1:-1500}"     # gross-regression ceiling; baseline avg ~100us.
                             # Deliberately kept BELOW the 480fps budget (2083us/frame),
                             # so passing the gate proves the average Draw fits a single
                             # 480fps frame with room to spare.

. "${HERE}/wine_env.sh"
mxb_wine_env
mxb_wine_no_crash_debugger   # a crash exits instead of hanging winedbg
SAVE=/tmp/mxbperf            # perf_driver's hardcoded Z:\tmp\mxbperf\
# Wall-clock cap for the perf driver (a fixed-iteration Draw/telemetry loop). Aborts a
# hang instead of burning CI minutes. Override: MXBMRP3_PERF_TIMEOUT=300 ./run_perf.sh
PERF_TIMEOUT="${MXBMRP3_PERF_TIMEOUT:-180}"

command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 1; }

echo "== Building plugin DLL + perf drivers =="
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O2 "${HERE}/perf_driver.cpp" -o "${BUILD}/perf_driver.exe" \
    || { echo "ERROR: perf driver build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O2 "${HERE}/map_perf_driver.cpp" -o "${BUILD}/map_perf_driver.exe" \
    || { echo "ERROR: map perf driver build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O2 "${HERE}/bench_driver.cpp" -o "${BUILD}/bench_driver.exe" \
    || { echo "ERROR: bench driver build failed"; exit 1; }
[ -d "${WINEPREFIX}" ] || wineboot -i >/dev/null 2>&1
wineserver -k 2>/dev/null || true

echo "== Measuring under Wine (general hot paths) =="
rm -rf "${SAVE}"; mkdir -p "${SAVE}"
( cd "${BUILD}" && timeout "${PERF_TIMEOUT}" wine perf_driver.exe mxbmrp3_test.dlo >/tmp/perf_report.txt 2>/dev/null )
rc=$?
wineserver -w
cat /tmp/perf_report.txt

if [ "${rc}" -eq 124 ]; then echo "== PERF FAIL: driver TIMED OUT after ${PERF_TIMEOUT}s (raise MXBMRP3_PERF_TIMEOUT) =="; exit 1; fi
if [ "${rc}" -ne 0 ]; then echo "== PERF FAIL (driver exit ${rc}) =="; exit 1; fi

echo "== Measuring under Wine (interleaved MAP hot loop) =="
( cd "${BUILD}" && timeout "${PERF_TIMEOUT}" wine map_perf_driver.exe mxbmrp3_test.dlo >/tmp/mapperf_report.txt 2>/dev/null )
mrc=$?
wineserver -w
cat /tmp/mapperf_report.txt

if [ "${mrc}" -eq 124 ]; then echo "== PERF FAIL: map driver TIMED OUT after ${PERF_TIMEOUT}s (raise MXBMRP3_PERF_TIMEOUT) =="; exit 1; fi
if [ "${mrc}" -ne 0 ]; then echo "== PERF FAIL (map driver exit ${mrc}) =="; exit 1; fi

# The 480fps frame budget (2.08ms). Steady-state Draw must fit a single frame at
# this rate; the driver reports it too, but the gate below enforces it.
BUDGET_US=2083

# Parse the machine-readable summary line and apply the gates.
draw_avg=$(sed -n 's/.*draw_avg_us=\([0-9.]*\).*/\1/p' /tmp/perf_report.txt)
draw_p99=$(sed -n 's/.*draw_p99_us=\([0-9.]*\).*/\1/p' /tmp/perf_report.txt)
[ -z "${draw_avg}" ] && { echo "== PERF FAIL (no PERF line) =="; exit 1; }

# Gate 1: gross-regression ceiling on the headline average (coarse, CI-variance tolerant).
if ! awk "BEGIN{exit !(${draw_avg} < ${DRAW_MAX_US})}"; then
    echo "== PERF REGRESSION: Draw avg ${draw_avg}us exceeds ${DRAW_MAX_US}us ceiling =="
    exit 1
fi

# Gate 2: 480fps budget enforcement. Steady-state p99 Draw must fit inside one
# 480fps frame (the first cold-start frame that builds every HUD's cached
# primitives is an expected outlier, which is why this gates p99, not max).
# Fail CLOSED if the field is missing (a PERF-line drift must not silently
# disable the gate — mirrors the map gate below).
[ -z "${draw_p99}" ] && { echo "== PERF FAIL (no draw_p99_us in PERF line) =="; exit 1; }
if ! awk "BEGIN{exit !(${draw_p99} < ${BUDGET_US})}"; then
    echo "== PERF FAIL: Draw p99 ${draw_p99}us exceeds the 480fps budget (${BUDGET_US}us/frame) =="
    exit 1
fi

# Gate 3: 480fps budget enforcement on the MAP hot path. Worst map-ON Draw p99
# (full grid rebuilding on the long/complex circuit, across every view mode)
# must also fit one 480fps frame — this is the heaviest realistic per-frame cost.
map_p99=$(sed -n 's/.*mapdraw_p99_worst_us=\([0-9.]*\).*/\1/p' /tmp/mapperf_report.txt)
[ -z "${map_p99}" ] && { echo "== PERF FAIL (no MAPPERF line) =="; exit 1; }
if ! awk "BEGIN{exit !(${map_p99} < ${BUDGET_US})}"; then
    echo "== PERF FAIL: worst map Draw p99 ${map_p99}us exceeds the 480fps budget (${BUDGET_US}us/frame) =="
    exit 1
fi

# Contract check: the BenchmarkWidget report's machine-readable "BENCH ..." line
# must stay parseable by tools/benchmark_report.py (the "keep KEYS in step"
# invariant). bench_driver produces a real report headlessly; feed it to the
# analyzer and require a clean parse. Skipped only if python3 is unavailable.
if command -v python3 >/dev/null 2>&1; then
    echo "== Checking benchmark report <-> analyzer contract =="
    ( cd "${BUILD}" && timeout "${PERF_TIMEOUT}" wine bench_driver.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
    wineserver -w
    bench_report=$(ls -t "${SAVE}"/mxbmrp3/benchmarks/benchmark_*.txt 2>/dev/null | head -1)
    if [ -z "${bench_report}" ]; then echo "== PERF FAIL: bench_driver wrote no report =="; exit 1; fi
    if ! python3 "${HERE}/../../tools/benchmark_report.py" "${bench_report}" >/tmp/bench_analyzed.txt 2>&1; then
        echo "== PERF FAIL: tools/benchmark_report.py could not parse the BENCH line =="
        cat /tmp/bench_analyzed.txt; exit 1
    fi
    # The parser must actually have found the BENCH line (grep the scenario echo).
    grep -q "Scenario :" /tmp/bench_analyzed.txt || { echo "== PERF FAIL: analyzer produced no scenario (BENCH line drift?) =="; cat /tmp/bench_analyzed.txt; exit 1; }
    grep -q "Handoff by HUD" /tmp/bench_analyzed.txt || { echo "== PERF FAIL: analyzer found no per-HUD footprint (FOOTPRINT table drift?) =="; cat /tmp/bench_analyzed.txt; exit 1; }
    # ...and the STINT TOTALS tables, which are what a post-session analysis actually
    # reads (the tables above them cover only the last ~0.25s snapshot interval). Same
    # coupling, same failure mode: a column change in exportReport() silently stops
    # parse_stint() matching, and the analysis quietly loses a section.
    grep -q "Rebuild cost over the stint" /tmp/bench_analyzed.txt || { echo "== PERF FAIL: analyzer found no stint rebuild totals (STINT TOTALS table drift?) =="; cat /tmp/bench_analyzed.txt; exit 1; }
    echo "== benchmark report parsed OK by analyzer (incl. per-HUD footprint) =="
else
    echo "== (skipping benchmark analyzer contract check: python3 not found) =="
fi

echo "== PERF OK (Draw avg ${draw_avg}us < ${DRAW_MAX_US}us ceiling; Draw p99 ${draw_p99}us, worst map p99 ${map_p99}us < ${BUDGET_US}us 480fps budget) =="
exit 0
