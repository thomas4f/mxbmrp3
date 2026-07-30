#!/usr/bin/env bash
# ============================================================================
# tests/integration/make_positions_tape.sh
# Regenerate the SYNTHETIC "fuller" fixture (positions + map + telemetry) that
# run_tape_bench.sh can replay to profile the map/telemetry HUDs — the committed
# real captures are slim (standings-only). We can't record a real session
# headless, so make_positions_tape.cpp drives a synthetic 22-rider race through
# PluginHost with the plugin's OWN recorder on: the tape's FORMAT is real (same
# recorder as an in-game capture), the data is synthetic.
#
# Output: tests/integration/tests/fixtures/synthetic_positions_22riders.tape.gz
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
OUT="${1:-${HERE}/tests/fixtures/synthetic_positions_22riders.tape.gz}"
FRAMES="${2:-600}"; RIDERS="${3:-22}"

. "${HERE}/wine_env.sh"
mxb_wine_env
command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 1; }

echo "== Building DLL + generator =="
"${HERE}/build.sh" || exit 1
x86_64-w64-mingw32-g++ -std=c++17 -O2 -w -static -static-libgcc -static-libstdc++ \
    -I "${HERE}/harness" -I "${HERE}/../../mxbmrp3" -I "${HERE}/../../mxbmrp3/vendor" \
    "${HERE}/make_positions_tape.cpp" -o "${BUILD}/make_positions_tape.exe" -lws2_32 || exit 1

echo "== Recording ${FRAMES} frames x ${RIDERS} riders under Wine =="
rm -rf /tmp/mxbperf; mkdir -p /tmp/mxbperf; wineserver -k 2>/dev/null || true
( cd "${BUILD}" && timeout 120 wine make_positions_tape.exe mxbmrp3_test.dlo "Z:\\tmp\\positions.tape" "${FRAMES}" "${RIDERS}" )
wineserver -w
[ -f /tmp/positions.tape ] || { echo "ERROR: no tape produced"; exit 1; }
gzip -9 -c /tmp/positions.tape > "${OUT}"
echo "== Wrote ${OUT} ($(du -h "${OUT}" | cut -f1)) =="
