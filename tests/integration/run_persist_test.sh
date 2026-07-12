#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_persist_test.sh
# Settings persistence round-trip test. Perturbs a settings file (flips every
# boolean toggle — modelling a user changing settings in the plugin), then
# Startup loads it and Shutdown re-saves from live state. Asserts every changed
# value survived — i.e. user changes actually persist across a restart.
#
# Targets the highest-severity, least-covered area: a setting applied in memory
# but never written to disk (the hudOrder "third hardcoded list" trap) reverts
# silently on restart. See CLAUDE.md "Adding a New HUD" step 6.
#
# Requires: mingw-w64 (posix), wine64, python3.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"

export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"
SAVE=/tmp/mxbsave   # loader.exe's hardcoded Z:\tmp\mxbsave\
INI="${SAVE}/mxbmrp3/mxbmrp3_settings.ini"
# Wall-clock cap per loader run (startup+shutdown). Aborts a hang instead of burning CI
# minutes. Override: MXBMRP3_PERSIST_TIMEOUT=120 ./run_persist_test.sh
PERSIST_TIMEOUT="${MXBMRP3_PERSIST_TIMEOUT:-60}"

command -v wine >/dev/null    || { echo "ERROR: wine not found";    exit 1; }
command -v python3 >/dev/null || { echo "ERROR: python3 not found"; exit 1; }

echo "== Building plugin DLL + loader host =="
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O1 "${HERE}/loader.cpp" -o "${BUILD}/loader.exe" \
    || { echo "ERROR: loader host build failed"; exit 1; }
[ -d "${WINEPREFIX}" ] || wineboot -i >/dev/null 2>&1
wineserver -k 2>/dev/null || true

echo "== Capturing default settings.ini =="
rm -rf "${SAVE}"; mkdir -p "${SAVE}"
( cd "${BUILD}" && timeout "${PERSIST_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
wineserver -w
[ -s "${INI}" ] || { echo "ERROR: no baseline settings.ini"; exit 1; }

echo "== Perturbing every boolean toggle =="
python3 "${HERE}/persist_gen.py" "${INI}" "${INI}" /tmp/persist_expect.txt

echo "== Startup (load) -> Shutdown (re-save from live state) =="
( cd "${BUILD}" && timeout "${PERSIST_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
wineserver -w

echo "== Checking every changed setting survived =="
python3 "${HERE}/persist_check.py" /tmp/persist_expect.txt "${INI}"
rc=$?
[ $rc -eq 0 ] && echo "== PERSIST TEST PASS ==" || echo "== PERSIST TEST FAIL (settings did not persist) =="
exit $rc
