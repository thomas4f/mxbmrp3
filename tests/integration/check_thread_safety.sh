#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_thread_safety.sh
# Clang Thread Safety Analysis over the plugin sources (compile check, no link).
#
# THE INVARIANT (CLAUDE.md): a mutex-guarded member is guarded at EVERY access
# site — including private helpers called from already-locked-*looking* code
# (the RecordsHud crash class). Members are annotated MXB_GUARDED_BY(mutex) and
# helpers MXB_REQUIRES(mutex) (see core/thread_safety.h); this check runs
# clang's -Wthread-safety as an ERROR over every TU, so an unlocked access to
# an annotated member fails CI instead of racing in the field.
#
# Neither shipping (MSVC) nor test (mingw g++) builds run this analysis — the
# annotations are no-ops there — so clang front-end-parses the same TUs with
# the mingw target headers (-fsyntax-only: no codegen, no link, fast).
# Deliberate exceptions carry MXB_NO_TSA with a comment saying why.
#
# Two passes:
#   1. The test-build TU set (same as the cross-build) under GAME_MXBIKES.
#   2. discord_manager.cpp alone — it's compiled out of test builds
#      (MXBMRP3_TEST_BUILD), so pass 1 never sees its mutexes.
#
#   ./tests/integration/check_thread_safety.sh
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "${HERE}/../../mxbmrp3" && pwd)"
CXX="${CXX:-clang++}"

if ! command -v "${CXX%% *}" >/dev/null; then
    echo "ERROR: clang (${CXX}) not found — thread-safety analysis needs clang." >&2
    exit 1
fi

# MXB_TSA_TARGET is a test seam: point it at a triple with no sysroot to prove the
# SKIP branch below is actually reachable (it was, once, dead code).
TARGET="--target=${MXB_TSA_TARGET:-x86_64-w64-mingw32}"
WARN="-Wno-everything -Wthread-safety -Werror=thread-safety"
INCS="-I${HERE}/shim -I${SRC}"
# MXB_REPO_DATA_DIR mirrors mxbmrp3/CMakeLists.txt: the test-build sources this
# gate parses under -DMXBMRP3_TEST_BUILD reference it, so without it the gate
# fails to COMPILE rather than reporting a thread-safety verdict - which is a
# red gate that says nothing about what it exists to check.
BASE="-std=c++17 -fsyntax-only ${TARGET} ${WARN} -DNOMINMAX -DNDEBUG -DGAME_MXBIKES -DMXBMRP3_ALLOW_NO_ANALYTICS -DMXB_REPO_DATA_DIR=\"\\\"${SRC}/../mxbmrp3_data\\\"\" ${INCS}"

# PREFLIGHT: clang alone isn't enough — it cross-parses with the mingw TARGET,
# whose C++ headers come from the mingw-w64 sysroot. Without that sysroot every
# TU dies on `'vector' file not found` and the analysis reports a wall of
# failures that look exactly like real violations. Probe once, up front, so a
# missing toolchain says so instead of accusing the code.
if ! echo '#include <vector>
int main() { return 0; }' | ${CXX} -std=c++17 -fsyntax-only ${TARGET} -x c++ - >/dev/null 2>&1; then
    # Exit 3 + a SKIP: line, not 1: this is "prerequisite unavailable on this
    # machine", which is what CTest's SKIP_RETURN_CODE is for.
    # Failing here would paint a red build on any clang-without-mingw box for a
    # toolchain gap, while its sibling gates correctly report SKIP. mxb_gate's
    # TOOLS column already declares clang++ and mingw, so the common case is
    # caught up front without spawning clang; this covers the narrower one where
    # the mingw BINARY is present but its C++ headers are not.
    echo "SKIP: mingw-w64 sysroot not found — clang can parse for ${TARGET#--target=}," >&2
    echo "      but its C++ headers are missing, so no TU would compile. Install with:" >&2
    echo "  sudo apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64" >&2
    exit 3
fi

mapfile -t SHARED < <(cd "${SRC}" && find core handlers hud diagnostics -name '*.cpp' | grep -vE 'discord_manager' | sort)

JOBS="$(nproc 2>/dev/null || echo 4)"
rc=0

echo "==> thread-safety analysis: test-build TU set ($(( ${#SHARED[@]} + 1 )) TUs)"
printf '%s\n' "${SHARED[@]}" "vendor/piboso/mxb_api.cpp" \
    | xargs -P "${JOBS}" -I{} sh -c \
        "cd '${SRC}' && ${CXX} ${BASE} -DMXBMRP3_TEST_BUILD '{}' || { echo 'THREAD-SAFETY: {}' >&2; exit 1; }" || rc=1

echo "==> thread-safety analysis: discord_manager.cpp (compiled out of test builds)"
( cd "${SRC}" && ${CXX} ${BASE} core/discord_manager.cpp ) || { echo 'THREAD-SAFETY: core/discord_manager.cpp' >&2; rc=1; }

# A raw std::mutex / std::lock_guard / std::unique_lock is INVISIBLE to the
# analysis — new synchronization must use the annotated Mutex/MutexLock/CvLock
# wrappers or it silently dodges every check above. thread_safety.h itself (the
# wrappers' implementation) is the only legitimate site; anything else needs a
# `// tsa-exempt: <reason>` annotation on the line.
echo "==> raw std::mutex lint (unannotated sync primitives dodge the analysis)"
raw=$(grep -rn "std::mutex\|std::lock_guard\|std::unique_lock\|std::recursive_mutex" \
        "${SRC}/core" "${SRC}/hud" "${SRC}/handlers" "${SRC}/diagnostics" \
        --include='*.cpp' --include='*.h' \
      | grep -v "thread_safety.h:" | grep -v "tsa-exempt:" | grep -v "^\s*//" \
      | grep -vE ':[0-9]+:\s*//')
if [ -n "${raw}" ]; then
    echo "${raw}" >&2
    echo "THREAD-SAFETY: raw std sync primitive outside thread_safety.h (use Mutex/MutexLock/CvLock, or annotate '// tsa-exempt: <reason>')" >&2
    rc=1
fi

if [ $rc -ne 0 ]; then
    cat >&2 <<'EOF'

THREAD-SAFETY ANALYSIS FAILED.
An annotated mutex-guarded member is accessed without its mutex (or a lock
contract is violated). Hold the mutex (MutexLock), annotate the called-under-
lock helper MXB_REQUIRES(mutex), or — for a deliberate, explained exception —
MXB_NO_TSA with a comment. See mxbmrp3/core/thread_safety.h.
EOF
    exit 1
fi
echo "Thread-safety analysis clean."
