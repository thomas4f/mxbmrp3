#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_clang_tidy.sh
# clang-tidy over the plugin sources, with ONLY the checks in mxbmrp3/.clang-tidy,
# plus clang's thread-safety analysis on the same parse (see below).
#
# WHY IT EXISTS: bugprone-use-after-move, and specifically its "use and move
# are unsequenced" case. The bug it is here for:
#
#     emitCue(key,
#             std::string("Up ") + v.positionsChanged + ".",   // reads v
#             cat, std::move(v), nowMs);                       // and guts v
#
# Call arguments are indeterminately sequenced, so whether the text sees the
# number or a moved-from empty string is the compiler's choice. gcc evaluated
# left to right and was right; MSVC evaluates right to left and shipped
# `SPOTTER SAY [position_gained] Up .` to a real player. Every other gate
# compiles with gcc/clang, where the code is correct, so a behavioural test
# passes with the bug present - and -Wunsequenced stays quiet because it is not
# UB. The fix is always the same: compute into a local first, pass the local.
#
# This replaced check_move_reads.sh, a hand-rolled grep for that one shape
# (move + member read in the same statement). Tidy is the maintained version of
# it and is strictly wider: it sees through aliases, loops and later uses too.
#
# ADDING A CHECK: add it to Checks AND WarningsAsErrors in mxbmrp3/.clang-tidy
# (game/ is header-only, reached through the TUs via HeaderFilterRegex). Suppress a reviewed false positive with
# `// NOLINT(<check>): <reason>` on the line.
#
# CLANG THREAD SAFETY ANALYSIS rides the same parse. THE INVARIANT (CLAUDE.md):
# a mutex-guarded member is guarded at EVERY access site, including private
# helpers called from already-locked-looking code (the RecordsHud crash class).
# Members are annotated MXB_GUARDED_BY(mutex), helpers MXB_REQUIRES(mutex) (see
# core/thread_safety.h), and -Werror=thread-safety turns an unlocked access into
# a failure here. Neither shipping (MSVC) nor test (mingw g++) builds run the
# analysis - the annotations are no-ops there. Deliberate exceptions carry
# MXB_NO_TSA with a comment saying why.
#
# It used to be its own gate (check_thread_safety.sh, which now keeps only the
# raw-std::mutex grep): a second clang front-end pass over the very same TUs and
# flags, ~110s of the full suite for a parse this gate already does. Folded in,
# both together cost what tidy alone did.
#
# SETUP: clang front-end with the mingw target headers and the shim, the
# test-build TU set (plus the MXB export TU) under GAME_MXBIKES, and
# discord_manager.cpp alone (it is compiled out of test builds, so the first
# pass never sees its mutexes). No compile database - the flags are the same
# for every TU, so it would add a CMake configure for nothing.
#
#   ./tests/integration/check_clang_tidy.sh
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "${HERE}/../../mxbmrp3" && pwd)"
TIDY="${CLANG_TIDY:-clang-tidy}"

# The check list, error list and header filter live in mxbmrp3/.clang-tidy, so
# an editor's clang-tidy integration sees the same rules this gate enforces.
CONFIG="${SRC}/.clang-tidy"

if ! command -v "${TIDY}" >/dev/null; then
    echo "ERROR: ${TIDY} not found." >&2
    exit 1
fi

TARGET="--target=x86_64-w64-mingw32"
# Preflight: without the mingw sysroot every TU
# dies on `'vector' file not found`, which reads like a wall of findings.
if ! echo '#include <vector>
int main() { return 0; }' | clang++ -std=c++17 -fsyntax-only ${TARGET} -x c++ - >/dev/null 2>&1; then
    echo "SKIP: mingw-w64 C++ headers not found for ${TARGET#--target=}. Install with:" >&2
    echo "  sudo apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64" >&2
    exit 3
fi

FLAGS="-std=c++17 ${TARGET} -Wno-everything -Wthread-safety -Werror=thread-safety -DNOMINMAX -DNDEBUG -DGAME_MXBIKES -DMXBMRP3_ALLOW_NO_ANALYTICS -DMXB_REPO_DATA_DIR=\"\\\"${SRC}/../mxbmrp3_data\\\"\" -I${HERE}/shim -I${SRC}"
TIDY_ARGS="--quiet --config-file=${CONFIG}"

mapfile -t SHARED < <(cd "${SRC}" && find core handlers hud diagnostics -name '*.cpp' | grep -v discord_manager | sort)
JOBS="$(nproc 2>/dev/null || echo 4)"
rc=0

echo "==> clang-tidy + thread-safety: test-build TU set ($(( ${#SHARED[@]} + 1 )) TUs)"
printf '%s\n' "${SHARED[@]}" "vendor/piboso/mxb_api.cpp" \
    | xargs -P "${JOBS}" -I{} sh -c \
        "cd '${SRC}' && ${TIDY} ${TIDY_ARGS} '{}' -- ${FLAGS} -DMXBMRP3_TEST_BUILD 2>/dev/null || { echo 'CLANG-TIDY: {}' >&2; exit 1; }" || rc=1

echo "==> clang-tidy: discord_manager.cpp (compiled out of test builds)"
( cd "${SRC}" && ${TIDY} ${TIDY_ARGS} core/discord_manager.cpp -- ${FLAGS} 2>/dev/null ) \
    || { echo 'CLANG-TIDY: core/discord_manager.cpp' >&2; rc=1; }

if [ $rc -ne 0 ]; then
    cat >&2 <<'EOF'

CLANG-TIDY FAILED (findings above).
thread-safety-analysis: an annotated mutex-guarded member is accessed without
its mutex (or a lock contract is violated). Hold the mutex (MutexLock),
annotate the called-under-lock helper MXB_REQUIRES(mutex), or - for a
deliberate, explained exception - MXB_NO_TSA with a comment. See
mxbmrp3/core/thread_safety.h.
use-after-move with "the use and move are unsequenced": a call both moves out
of a variable and reads it, so the read may see a moved-from value depending on
the compiler - gcc and MSVC disagree, and only MSVC ships. Compute into a local
first and pass the local. A reviewed false positive takes
`// NOLINT(bugprone-use-after-move): <reason>`.
EOF
    exit 1
fi
echo "clang-tidy clean."
