#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_tests.sh
# One runner for every headless integration test. Cross-compiles the plugin to a
# Windows DLL (mingw-w64), then builds and runs each self-contained doctest in
# tests/integration/tests/ under Wine — the test loads the DLL, drives the real PiBoSo
# callbacks via PluginHost, and asserts the plugin's own /api/state JSON with
# nlohmann::json. No game, no Windows, no per-test asserter or runner script.
#
# Each tests/*.cpp is its own doctest binary (its own plugin lifecycle + HTTP
# port), so they run in isolated processes with a clean save dir each. Pass a
# filter to run a subset:
#
#   ./tests/integration/run_tests.sh                 # build + run all
#   ./tests/integration/run_tests.sh race sessions   # only those (basename match)
#
# Requires: mingw-w64 (posix threads), wine64. See TESTING.md.
# Exit non-zero if the build fails or any test binary exits non-zero.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD="${HERE}/build"
TESTS_DIR="${HERE}/tests"

export WINEPREFIX="${WINEPREFIX:-$HOME/.wineprefix-mxbmrp3}"
export WINEARCH=win64
export WINEDEBUG="${WINEDEBUG:--all}"
# Save/settings tree; Z:\tmp\mxbmrp3-tests\ (in the tests) maps here. Wiped per
# run so no test inherits another's (or a prior run's) settings.ini.
SAVE_ROOT=/tmp/mxbmrp3-tests

# Per-test wall-clock cap. A healthy doctest here runs in ~1-10s; this bound exists
# only to abort a HUNG test (a deadlock, an infinite loop) instead of letting it burn
# CI minutes until the job-level timeout. 120s leaves generous headroom over the
# slowest legitimate test on a cold Wine prefix. Override for a genuinely slower run
# (or to tighten the bound locally): MXBMRP3_TEST_TIMEOUT=60 ./run_tests.sh
PER_TEST_TIMEOUT="${MXBMRP3_TEST_TIMEOUT:-120}"

command -v x86_64-w64-mingw32-g++ >/dev/null || { echo "ERROR: mingw-w64 not found"; exit 1; }
command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 1; }

CXX=x86_64-w64-mingw32-g++
# ccache-wrap the compile (matching the Makefile) so the heavy doctest.h +
# nlohmann/json.hpp parse is cached by content — an unchanged test recompiles in
# milliseconds instead of ~6s, and the CI cache persists it across runs. ccache
# only caches the compile-to-object step, so we split compile (-c) from link.
CCACHE="$(command -v ccache || true)"
# doctest.h + harness headers; nlohmann/json.hpp lives under mxbmrp3/vendor.
INCS=(-I"${HERE}/harness" -I"${ROOT}/mxbmrp3/vendor")
CXXFLAGS=(-std=c++17 -O1)
# Static link: the tests use std::string/std::thread, whose mingw runtime DLLs
# aren't on Wine's search path. -lws2_32 for the HTTP GET.
LDFLAGS=(-static -static-libgcc -static-libstdc++)
LIBS=(-lws2_32)

echo "== Building plugin DLL (per-file 'CXX ...' progress below) =="
# Stream the build instead of swallowing it (>/dev/null): the ~118-TU mingw
# compile is the slow part of a run, and its per-file "CXX ..." lines are the main
# sign of life. Run it directly (no pipe) so nothing block-buffers the output.
"${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }

# Which tests to run (basename filter; default all).
mapfile -t ALL < <(cd "${TESTS_DIR}" && ls *.cpp 2>/dev/null | sort)
SELECTED=()
if [ "$#" -eq 0 ]; then
    SELECTED=("${ALL[@]}")
else
    for want in "$@"; do
        for t in "${ALL[@]}"; do
            [[ "${t}" == "${want}" || "${t}" == "${want}.cpp" || "${t%_test.cpp}" == "${want}" ]] && SELECTED+=("${t}")
        done
    done
fi
[ "${#SELECTED[@]}" -eq 0 ] && { echo "ERROR: no tests matched"; exit 1; }

# Initialize the Wine prefix and wait for it to settle before the first test, so
# a fresh CI prefix isn't still booting when the first `wine test.exe` runs.
wineboot -i >/dev/null 2>&1 || true
wineserver -w 2>/dev/null || true
echo "wine: $(wine --version 2>/dev/null)  prefix: ${WINEPREFIX}"
rm -rf "${SAVE_ROOT}"

# Decompress committed callback-tape fixtures so replay tests can read them at
# Z:\tmp\mxbmrp3-tests\fixtures\<name> (they're stored gzipped to keep the repo small).
if compgen -G "${TESTS_DIR}/fixtures/"'*.gz' >/dev/null 2>&1; then
    mkdir -p "${SAVE_ROOT}/fixtures"
    for gz in "${TESTS_DIR}/fixtures/"*.gz; do
        gunzip -c "${gz}" > "${SAVE_ROOT}/fixtures/$(basename "${gz%.gz}")"
    done
fi

rc=0
total=${#SELECTED[@]}
i=0
for src in "${SELECTED[@]}"; do
    i=$((i + 1))
    name="${src%.cpp}"
    exe="${BUILD}/${name}.exe"
    obj="${BUILD}/${name}.o"
    echo
    echo "== [${i}/${total}] ${name} (cap ${PER_TEST_TIMEOUT}s) =="
    # Compile (ccache-cached) then link, so an unchanged test is a cache hit.
    if ! ${CCACHE} "${CXX}" "${CXXFLAGS[@]}" "${INCS[@]}" -c "${TESTS_DIR}/${src}" -o "${obj}" \
       || ! "${CXX}" "${LDFLAGS[@]}" "${obj}" -o "${exe}" "${LIBS[@]}"; then
        echo "FAIL: ${name} failed to compile"; rc=1; continue
    fi
    # Clean, pre-created save dir for this test (Z:\tmp\mxbmrp3-tests\<short>\).
    mkdir -p "${SAVE_ROOT}/${name%_test}"
    # Free :8080 from any lingering process (the server disables SO_REUSEADDR).
    wineserver -k 2>/dev/null || true
    started=${SECONDS}
    ( cd "${BUILD}" && timeout "${PER_TEST_TIMEOUT}" wine "${name}.exe" mxbmrp3_test.dlo 2>"/tmp/${name}.trace.txt" )
    ec=$?
    elapsed=$((SECONDS - started))
    wineserver -w 2>/dev/null || true
    if [ ${ec} -eq 124 ]; then
        # `timeout` kills with 124 — a hang, not a normal assertion failure. Call it out
        # explicitly (and how to lift the cap) so it reads as "aborted", not "slow test".
        echo "FAIL: ${name} TIMED OUT after ${PER_TEST_TIMEOUT}s (aborted; raise MXBMRP3_TEST_TIMEOUT to allow longer)"
        echo "--- ${name} stderr (tail) ---"; tail -30 "/tmp/${name}.trace.txt" 2>/dev/null; echo "---"
        rc=1
    elif [ ${ec} -ne 0 ]; then
        echo "FAIL: ${name} exited ${ec} (after ${elapsed}s)"
        # Always surface the driver/wine stderr on failure so CI shows *why*
        # (a missing doctest banner above means wine never started the exe).
        echo "--- ${name} stderr (tail) ---"; tail -30 "/tmp/${name}.trace.txt" 2>/dev/null; echo "---"
        rc=1
    else
        echo "-- ${name} passed (${elapsed}s)"
    fi
done

echo
[ ${rc} -eq 0 ] && echo "== ALL INTEGRATION TESTS PASS ==" || echo "== INTEGRATION TESTS FAILED =="
exit ${rc}
