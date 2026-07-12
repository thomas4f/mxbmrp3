#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_perf.sh
# CPU performance baseline for the plugin's hot paths (see perf_driver.cpp).
# The plugin is CPU-bound, so this headless measurement under Wine is
# representative of the cost it controls, measured against the 240fps budget.
#
# Prints the full report every run (eyeball trends in CI logs) and applies a
# DELIBERATELY GENEROUS regression gate on the headline number (average Draw
# time) so it fails only on a gross regression, not on normal CI CPU variance.
# Override the gate with the first arg (microseconds).
#
# Requires: mingw-w64 (posix), wine64.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
DRAW_MAX_US="${1:-1500}"     # gross-regression ceiling; baseline avg ~100us

export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"
SAVE=/tmp/mxbperf            # perf_driver's hardcoded Z:\tmp\mxbperf\
# Wall-clock cap for the perf driver (a fixed-iteration Draw/telemetry loop). Aborts a
# hang instead of burning CI minutes. Override: MXBMRP3_PERF_TIMEOUT=300 ./run_perf.sh
PERF_TIMEOUT="${MXBMRP3_PERF_TIMEOUT:-180}"

command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 1; }

echo "== Building plugin DLL + perf driver =="
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O2 "${HERE}/perf_driver.cpp" -o "${BUILD}/perf_driver.exe" \
    || { echo "ERROR: perf driver build failed"; exit 1; }
[ -d "${WINEPREFIX}" ] || wineboot -i >/dev/null 2>&1
wineserver -k 2>/dev/null || true

echo "== Measuring under Wine =="
rm -rf "${SAVE}"; mkdir -p "${SAVE}"
( cd "${BUILD}" && timeout "${PERF_TIMEOUT}" wine perf_driver.exe mxbmrp3_test.dlo >/tmp/perf_report.txt 2>/dev/null )
rc=$?
wineserver -w
cat /tmp/perf_report.txt

if [ "${rc}" -eq 124 ]; then echo "== PERF FAIL: driver TIMED OUT after ${PERF_TIMEOUT}s (raise MXBMRP3_PERF_TIMEOUT) =="; exit 1; fi
if [ "${rc}" -ne 0 ]; then echo "== PERF FAIL (driver exit ${rc}) =="; exit 1; fi

# Parse the machine-readable summary line and apply the coarse gate.
draw_avg=$(sed -n 's/.*draw_avg_us=\([0-9.]*\).*/\1/p' /tmp/perf_report.txt)
[ -z "${draw_avg}" ] && { echo "== PERF FAIL (no PERF line) =="; exit 1; }
if awk "BEGIN{exit !(${draw_avg} < ${DRAW_MAX_US})}"; then
    echo "== PERF OK (Draw avg ${draw_avg}us < ${DRAW_MAX_US}us ceiling) =="
    exit 0
else
    echo "== PERF REGRESSION: Draw avg ${draw_avg}us exceeds ${DRAW_MAX_US}us ceiling =="
    exit 1
fi
