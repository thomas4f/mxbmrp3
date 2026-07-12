#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_fuzz_callbacks.sh
# Crash-grade boundary fuzzer. Runs callback_fuzzer under Wine: it hammers every
# plugin data callback with adversarial sizes, counts, element sizes and random
# bytes, then must exit cleanly. A fault here would crash the HOST GAME in
# production, so the bar is: nothing crosses the boundary uncaught.
#
# This fuzzer found (and this repo fixes) an unbounded read in TrackCenterline:
# an implausible segment count drove an out-of-bounds read. See README.
#
# Requires: mingw-w64 (posix), wine64.
# Exit non-zero if the build fails, the fuzzer faults, or it doesn't complete.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
ITERS="${1:-100000}"

export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"
SAVE=/tmp/mxbfuzz          # callback_fuzzer's hardcoded Z:\tmp\mxbfuzz\
# Wall-clock cap for the whole fuzz run. Scales with the iteration count (arg 1), so
# raise it if you pass a much larger ITERS. A hang leaves rc==124 (treated as a fail).
# Override: MXBMRP3_CALLBACK_FUZZ_TIMEOUT=600 ./run_fuzz_callbacks.sh 1000000
CALLBACK_FUZZ_TIMEOUT="${MXBMRP3_CALLBACK_FUZZ_TIMEOUT:-300}"

command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 1; }

echo "== Building plugin DLL + callback fuzzer =="
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O1 "${HERE}/callback_fuzzer.cpp" -o "${BUILD}/callback_fuzzer.exe" \
    || { echo "ERROR: fuzzer build failed"; exit 1; }
[ -d "${WINEPREFIX}" ] || wineboot -i >/dev/null 2>&1
wineserver -k 2>/dev/null || true

echo "== Fuzzing ${ITERS} iterations under Wine =="
rm -rf "${SAVE}"; mkdir -p "${SAVE}"
( cd "${BUILD}" && timeout "${CALLBACK_FUZZ_TIMEOUT}" wine callback_fuzzer.exe mxbmrp3_test.dlo "${ITERS}" 2>/tmp/callback_fuzz_err.txt )
rc=$?
wineserver -w

dumps=$(find "${SAVE}/mxbmrp3/crashes" -type f 2>/dev/null | wc -l)
faulted=$(grep -ciE "unhandled exception|page fault" /tmp/callback_fuzz_err.txt || true)

# rc==0 means the fuzzer ran every iteration and returned (survived). A crash
# leaves rc!=0 / a dump / a Wine fault line; a hang leaves rc==124 (timeout).
if [ "${rc}" -eq 0 ] && [ "${dumps}" -eq 0 ] && [ "${faulted}" -eq 0 ]; then
    echo "== CALLBACK FUZZ PASS (${ITERS} iterations, no crash) =="
    exit 0
fi
if [ "${rc}" -eq 124 ]; then
    echo "== CALLBACK FUZZ FAIL: TIMED OUT after ${CALLBACK_FUZZ_TIMEOUT}s (a hang, or ITERS too large — raise MXBMRP3_CALLBACK_FUZZ_TIMEOUT) =="
else
    echo "== CALLBACK FUZZ FAIL (exit=${rc} dumps=${dumps} faults=${faulted}) =="
fi
grep -iE "page fault|unhandled|\+0x" /tmp/callback_fuzz_err.txt | head -5
exit 1
