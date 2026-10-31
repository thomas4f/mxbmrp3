#!/usr/bin/env bash
# ============================================================================
# tests/integration/run_perf_ledger.sh
# The perf LEDGER: exact, repeatable cost counters for the plugin, gated
# against a committed baseline (perf_ledger_baseline.txt). Where run_perf.sh
# times the hot paths (and those timings move ~25% between identical runs on a
# shared VM), this counts what a change actually alters: primitives handed to
# the engine and the engine time they cost (an estimate priced by
# tools/probetheme), heap allocations per frame and per callback, rebuilds per
# HUD, JSON snapshot builds, net heap growth, and a hash of the frames - all on
# a synthetic busy tape (make_perf_tape.cpp: 50 riders, every stream) replayed
# on a virtual clock (perf_vclock.h), so the same commit gives the same numbers
# on any host. See perf_ledger_driver.cpp for the profiles and what is counted.
#
# Usage: run_perf_ledger.sh [--update] [--repeats N] [--tape FILE[.gz]] [--dll FILE] [--sites]
#   --update     rewrite the baseline (commit it with the change that moved it)
#   --repeats N  runs per profile (default 2; counters must agree across them,
#                timings are reported as their median)
#   --tape FILE  replay another tape (e.g. a real capture); compares against
#                nothing, just prints the ledger
#   --dll FILE   measure another build of the test DLL (A/B against a base)
#   --sites      then one more run per profile that records every allocating
#                call stack, printed per callback/Draw by perf_alloc_sites.py:
#                where to look when an allocation counter is not zero
#
# Requires: mingw-w64 (posix), wine64, a native g++, python3.
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="${HERE}/build"
BASELINE="${HERE}/perf_ledger_baseline.txt"
UPDATE=""; REPEATS=2; TAPE_IN=""; DLL=""; SITES=""
while [ $# -gt 0 ]; do
    case "$1" in
        --update) UPDATE="--update" ;;
        --repeats) REPEATS="$2"; shift ;;
        --tape) TAPE_IN="$2"; shift ;;
        --dll) DLL="$2"; shift ;;
        --sites) SITES=1 ;;
        *) echo "unknown argument: $1"; exit 2 ;;
    esac
    shift
done

. "${HERE}/wine_env.sh"
mxb_wine_env
mxb_wine_no_crash_debugger
command -v wine >/dev/null || { echo "ERROR: wine not found"; exit 3; }
command -v g++ >/dev/null || { echo "ERROR: native g++ not found"; exit 3; }
command -v python3 >/dev/null || { echo "ERROR: python3 not found"; exit 3; }
TIMEOUT="${MXBMRP3_PERF_TIMEOUT:-300}"
WORK=/tmp/mxbledger_work
SAVE=/tmp/mxbledger            # the driver's hardcoded Z:\tmp\mxbledger\
rm -rf "${WORK}"; mkdir -p "${WORK}"

echo "== Building plugin DLL + ledger driver + tape generator =="
DLL_NAME=mxbmrp3_test.dlo
if [ -z "${DLL}" ]; then
    "${HERE}/build.sh" || { echo "ERROR: plugin build failed"; exit 1; }
else
    # Run beside the test DLL (the plugin finds its plugins/ data relative to
    # the working directory), under a name of its own.
    DLL_NAME=mxbmrp3_ledger_ab.dlo
    cp "${DLL}" "${BUILD}/${DLL_NAME}" || exit 1
fi
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ \
    -I "${HERE}/harness" -I "${HERE}/../../mxbmrp3" -I "${HERE}/../../mxbmrp3/vendor" \
    "${HERE}/perf_ledger_driver.cpp" -o "${BUILD}/perf_ledger_driver.exe" -lws2_32 \
    || { echo "ERROR: ledger driver build failed"; exit 1; }

TAPE="${WORK}/perf_busy.tape"
if [ -n "${TAPE_IN}" ]; then
    case "${TAPE_IN}" in
        *.gz) gunzip -c "${TAPE_IN}" > "${TAPE}" || exit 1 ;;
        *) cp "${TAPE_IN}" "${TAPE}" || exit 1 ;;
    esac
else
    g++ -std=c++17 -O2 -I "${HERE}/harness" "${HERE}/make_perf_tape.cpp" -o "${WORK}/make_perf_tape" \
        || { echo "ERROR: tape generator build failed"; exit 1; }
    "${WORK}/make_perf_tape" "${TAPE}" || exit 1
fi

run_one() {   # profile, output file, [hud]
    # The save dir AND the CWD-relative discovery tree (build/plugins), which
    # the integration suite leaves populated: a pack or theme left there by
    # another gate changes what the plugin draws, so every run starts from both
    # empty, exactly like run_tests.sh.
    rm -rf "${SAVE}" "${BUILD}/plugins"; mkdir -p "${SAVE}"
    wineserver -k 2>/dev/null || true
    ( cd "${BUILD}" && MXB_LEDGER_SITES="${SITES_FILE:-}" timeout "${TIMEOUT}" wine perf_ledger_driver.exe "${DLL_NAME}" \
        "Z:${TAPE//\//\\}" "$1" ${3:-} > "$2" 2>"$2.err" )
    local rc=$?
    wineserver -w
    if [ "${rc}" -eq 124 ]; then echo "== LEDGER FAIL: $1 run TIMED OUT after ${TIMEOUT}s =="; return 1; fi
    if [ "${rc}" -ne 0 ]; then echo "== LEDGER FAIL: $1 run exited ${rc} =="; cat "$2.err"; return 1; fi
    grep -q "^LEDGER " "$2" || { echo "== LEDGER FAIL: $1 run printed no ledger =="; return 1; }
}

# The baseline is exact for one toolchain; perf_ledger.py compares this stamp.
LEDGER_TOOLCHAIN="mingw $(x86_64-w64-mingw32-g++ -dumpfullversion 2>/dev/null) / $(wine --version 2>/dev/null | tail -1)"
export LEDGER_TOOLCHAIN

specs=()
for profile in defaults all; do
    for i in $(seq 1 "${REPEATS}"); do
        echo "== ${profile}: run ${i}/${REPEATS} =="
        run_one "${profile}" "${WORK}/${profile}_${i}.txt" || exit 1
        specs+=("${profile}:${WORK}/${profile}_${i}.txt")
    done
done
# Twice, like the profiles above: its HUD rows are counters too, so a row that
# drifts between two identical runs is reported as the harness bug it is.
for i in 1 2; do
    echo "== all: per-HUD pass (BenchmarkWidget on), run ${i}/2 =="
    run_one all "${WORK}/all_hud_${i}.txt" hud || exit 1
    specs+=("all:${WORK}/all_hud_${i}.txt")
done
echo "== all: per-HUD time, real clock (reported only) =="
run_one all "${WORK}/all_hudtime.txt" hudtime || exit 1
specs+=("hudtime:${WORK}/all_hudtime.txt")

if [ -n "${SITES}" ]; then
    for profile in defaults all; do
        echo "== ${profile}: allocation sites =="
        SITES_FILE="Z:${WORK//\//\\}\\sites_${profile}.txt"
        run_one "${profile}" "${WORK}/sites_${profile}_run.txt" || exit 1
        SITES_FILE=""
        python3 "${HERE}/perf_alloc_sites.py" "${WORK}/sites_${profile}.txt" "${BUILD}/${DLL_NAME}"
    done
fi

if [ -n "${TAPE_IN}" ]; then
    # Another tape has no baseline: print the ledger for reading, gate nothing.
    python3 "${HERE}/perf_ledger.py" - "${WORK}/ledger.txt" "${specs[@]}"
    rc=$?
    echo "== Ledger for ${TAPE_IN}: ${WORK}/ledger.txt =="
    exit "${rc}"
fi
python3 "${HERE}/perf_ledger.py" "${BASELINE}" "${WORK}/ledger.txt" "${specs[@]}" ${UPDATE}
