#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_tape_bench.sh
# Benchmark the plugin against a REAL recorded session, headless. Replays a
# committed .tape.gz through tape_bench_driver (reconstructs the actual riders /
# gaps / session), profiles it with every HUD visible, and runs
# tools/benchmark_report.py on the exported report — so the per-HUD RENDER
# FOOTPRINT reflects a real field instead of a synthetic grid.
#
# Usage: run_tape_bench.sh [fixture.tape.gz] [default|max]
#   default (Max Riders / columns as shipped) vs max (uncapped — the worst case).
# Note: the slim fixtures carry no Draw/TrackCenterline/positions, so this
# measures the STANDINGS-side footprint (map/telemetry are empty). FPS in the
# report is a tight-loop artifact — read the FOOTPRINT, not the FPS.
#
# Requires: mingw-w64 (posix), wine64, python3, gzip.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
FIXTURE="${1:-${HERE}/tests/fixtures/race_farm14_24riders.tape.gz}"
MODE="${2:-max}"

. "${HERE}/wine_env.sh"
mxb_wine_env
SAVE=/tmp/mxbperf
TAPE=/tmp/tape_bench_input.tape

command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 1; }
[ -f "${FIXTURE}" ] || { echo "ERROR: fixture not found: ${FIXTURE}"; exit 1; }

echo "== Building plugin DLL + tape_bench_driver =="
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O2 -w -static -static-libgcc -static-libstdc++ \
    -I "${HERE}/harness" -I "${HERE}/../../mxbmrp3" -I "${HERE}/../../mxbmrp3/vendor" \
    "${HERE}/tape_bench_driver.cpp" -o "${BUILD}/tape_bench_driver.exe" -lws2_32 \
    || { echo "ERROR: tape_bench_driver build failed"; exit 1; }

echo "== Decompressing fixture =="
gunzip -c "${FIXTURE}" > "${TAPE}" || { echo "ERROR: gunzip failed"; exit 1; }

echo "== Replaying under Wine (${MODE} settings) =="
rm -rf "${SAVE}"; mkdir -p "${SAVE}"
wineserver -k 2>/dev/null || true
modearg=""; [ "${MODE}" = "max" ] && modearg="max"
( cd "${BUILD}" && timeout 180 wine tape_bench_driver.exe mxbmrp3_test.dlo "Z:${TAPE//\//\\}" ${modearg} )
wineserver -w

report=$(ls -t "${SAVE}"/mxbmrp3/benchmarks/benchmark_*.txt 2>/dev/null | head -1)
[ -z "${report}" ] && { echo "== FAIL: no report written =="; exit 1; }
echo "== Report: ${report} =="
if command -v python3 >/dev/null 2>&1; then
    python3 "${HERE}/../../tools/benchmark_report.py" "${report}"
    echo; echo "-- full HUD RENDER FOOTPRINT --"
    sed -n '/=== HUD RENDER FOOTPRINT/,/^$/p' "${report}"
else
    cat "${report}"
fi
