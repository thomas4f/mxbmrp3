#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_fuzz.sh
# Config-robustness fuzzer. Feeds a corpus of malformed settings.ini / JSON
# config files (fuzz_gen.py) to the plugin's Startup under Wine and asserts it
# never crashes — the parse sites must degrade gracefully, not abort or fault.
# Targets the "one bad value aborts the whole settings load" and "malformed
# JSON throws past a load site" bug classes (see CLAUDE.md).
#
# Requires: mingw-w64 (posix), wine64, python3.
# Exit non-zero if the build fails or any case crashes the plugin.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"

export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"
SAVE=/tmp/mxbsave          # loader.exe's hardcoded Z:\tmp\mxbsave\
CORPUS=/tmp/fuzz_corpus
# Wall-clock cap per corpus case (a single load). A hang on one case counts as a fail
# (a malformed INI must never wedge the plugin) rather than stalling the run. Override:
# MXBMRP3_FUZZ_TIMEOUT=120 ./run_fuzz.sh
FUZZ_TIMEOUT="${MXBMRP3_FUZZ_TIMEOUT:-60}"

command -v wine >/dev/null    || { echo "ERROR: wine not found";    exit 1; }
command -v python3 >/dev/null || { echo "ERROR: python3 not found"; exit 1; }

echo "== Building plugin DLL + loader host =="
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
x86_64-w64-mingw32-g++ -std=c++17 -O1 "${HERE}/loader.cpp" -o "${BUILD}/loader.exe" \
    || { echo "ERROR: loader host build failed"; exit 1; }
[ -d "${WINEPREFIX}" ] || wineboot -i >/dev/null 2>&1
wineserver -k 2>/dev/null || true

echo "== Capturing a valid baseline settings.ini =="
rm -rf "${SAVE}"; mkdir -p "${SAVE}"
( cd "${BUILD}" && timeout "${FUZZ_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
wineserver -w
BASE="${SAVE}/mxbmrp3/mxbmrp3_settings.ini"
[ -s "${BASE}" ] || { echo "ERROR: no baseline settings.ini produced"; exit 1; }

echo "== Generating corpus =="
rm -rf "${CORPUS}"; mkdir -p "${CORPUS}"
python3 "${HERE}/fuzz_gen.py" "${BASE}" "${CORPUS}"

echo "== Fuzzing (each case: load under Wine, assert no crash) =="
pass=0; fail=0
while read -r name; do
    [ -z "${name}" ] && continue
    rm -rf "${SAVE}"; mkdir -p "${SAVE}/mxbmrp3"
    cp -r "${CORPUS}/${name}/mxbmrp3/." "${SAVE}/mxbmrp3/"
    ( cd "${BUILD}" && timeout "${FUZZ_TIMEOUT}" wine loader.exe mxbmrp3_test.dlo >/dev/null 2>&1 )
    rc=$?
    wineserver -w 2>/dev/null
    # Survival = clean exit, no crash dump, and the plugin got through Startup
    # far enough to init the HTTP server (i.e. the settings load didn't abort/fault).
    dumps=$(find "${SAVE}/mxbmrp3/crashes" -type f 2>/dev/null | wc -l)
    got_through=$(grep -c "HttpServer initialized" "${SAVE}/mxbmrp3/mxbmrp3_log.txt" 2>/dev/null || echo 0)
    if [ "${rc}" -eq 0 ] && [ "${dumps}" -eq 0 ] && [ "${got_through}" -ge 1 ]; then
        pass=$((pass+1)); printf "  [ok]   %s\n" "${name}"
    else
        fail=$((fail+1))
        printf "  [FAIL] %s  (exit=%s dumps=%s reached_init=%s)\n" "${name}" "${rc}" "${dumps}" "${got_through}"
    fi
done < "${CORPUS}/cases.txt"

echo "== $pass survived, $fail crashed/aborted =="
[ "${fail}" -eq 0 ] && { echo "== FUZZ PASS =="; exit 0; } || { echo "== FUZZ FAIL =="; exit 1; }
