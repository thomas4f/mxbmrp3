#!/usr/bin/env bash
# ============================================================================
# tests/integration/make_spotter_demo_tape.sh
# Regenerate the committed SPOTTER DEMO fixture — a ~3 minute synthetic race
# WEEKEND (quali + timed race) whose motion-derived events fire every
# MX Bikes-triggerable spotter cue (see make_spotter_demo_tape.cpp for the
# cast and timeline). Meant for humans:
#
#   wine mxbmrp3_replay.exe mxbmrp3.dlo spotter_demo_weekend.tape --speed 1
#
# with the spotter enabled hears the whole feature from a fixture, no game
# needed. Runs in REAL TIME (~3 min) — the recorder stamps wall-clock time
# and the tape is only worth committing with real pacing.
#
# Output: tests/integration/tests/fixtures/spotter_demo_weekend.tape.gz
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
OUT="${1:-${HERE}/tests/fixtures/spotter_demo_weekend.tape.gz}"

. "${HERE}/wine_env.sh"
mxb_wine_env
command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 1; }

echo "== Building DLL + generator =="
"${HERE}/build.sh" || exit 1
x86_64-w64-mingw32-g++ -std=c++17 -O2 -w -static -static-libgcc -static-libstdc++ \
    -I "${HERE}/harness" -I "${HERE}/../../mxbmrp3" -I "${HERE}/../../mxbmrp3/vendor" \
    "${HERE}/make_spotter_demo_tape.cpp" -o "${BUILD}/make_spotter_demo_tape.exe" -lws2_32 || exit 1

echo "== Recording the ~2 minute scripted session under Wine (real time) =="
# The recorder only finalizes (and flushes) on a clean stop, and wineserver
# is shared per-prefix — do NOT run this concurrently with the test suite or
# anything else that resets Wine, or the recording dies mid-session and only
# a stdio-buffer-sized prefix of the tape reaches disk.
rm -rf /tmp/mxbdemo /tmp/spotter_demo.tape; mkdir -p /tmp/mxbdemo
wineserver -k 2>/dev/null || true
( cd "${BUILD}" && timeout 240 wine make_spotter_demo_tape.exe mxbmrp3_test.dlo "Z:\\tmp\\spotter_demo.tape" ) \
    || { echo "ERROR: generator failed"; exit 1; }
wineserver -w
[ -f /tmp/spotter_demo.tape ] || { echo "ERROR: no tape produced"; exit 1; }
# A healthy ~3min recording is ~300KB+; a truncated one is a few KB.
SIZE=$(stat -c%s /tmp/spotter_demo.tape)
[ "${SIZE}" -ge 50000 ] || { echo "ERROR: tape truncated (${SIZE} bytes) - was something else using Wine?"; exit 1; }
gzip -9 -c /tmp/spotter_demo.tape > "${OUT}"
echo "== Wrote ${OUT} ($(du -h "${OUT}" | cut -f1)) =="
