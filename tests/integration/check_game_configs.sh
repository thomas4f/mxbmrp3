#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_game_configs.sh
# Syntax-check the shared plugin sources under the NON-MXB game configs
# (GP Bikes, Kart Racing Pro) so a compile error that only surfaces when a
# GAME_HAS_* feature macro is off is caught in CI — not in a manual MSVC
# release build.
#
# Why this exists: the cross-build (build.sh) and the whole Wine suite compile
# ONLY the MX Bikes config (-DGAME_MXBIKES), where every GAME_HAS_* is at its
# most-featured. A regression like "an #include gated on GAME_HAS_RECORDS_PROVIDER
# whose type is used unconditionally" builds fine on MXB but breaks GPB/KRP
# (SettingsHud left incomplete -> MSVC C2027). CI never built those configs, so
# it slipped to a release build. This closes that gap cheaply: -fsyntax-only over
# the same shared TUs the MSVC build compiles, under each non-MXB game define.
#
# It's a COMPILE check, not a link/run — no per-game API export or libs needed;
# the point is the preprocessor + type-checking of the shared code, which is
# where this class of bug lives. Fast (no codegen), parallel, no Wine.
#
#   ./tests/integration/check_game_configs.sh
# ============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "${HERE}/../../mxbmrp3" && pwd)"
CXX="${CXX:-x86_64-w64-mingw32-g++}"

if ! command -v "${CXX%% *}" >/dev/null; then
    echo "ERROR: mingw-w64 (${CXX}) not found." >&2
    exit 1
fi

# Same flags as the Makefile, minus the game define (added per-config below).
BASE_DEFS="-DNOMINMAX -DNDEBUG -DMXBMRP3_TEST_BUILD -DMXBMRP3_ALLOW_NO_ANALYTICS"
INCS="-I${HERE}/shim -I${SRC}"
FLAGS="-std=c++17 -m64 -O1 -w -fsyntax-only ${BASE_DEFS} ${INCS}"

# The shared sources every game compiles (matches the Makefile's find set). Exclude
# discord_manager (GAME_HAS_DISCORD=0 in the test build — the Makefile excludes it too).
# The per-game API export TU is added per-config below (gpb_api / krp_api).
mapfile -t SHARED < <(cd "${SRC}" && find core handlers hud diagnostics -name '*.cpp' | grep -vE 'discord_manager' | sort)

# game define : matching API export TU
CONFIGS=(
    "GAME_GPBIKES:vendor/piboso/gpb_api.cpp"
    "GAME_KRP:vendor/piboso/krp_api.cpp"
)

JOBS="$(nproc 2>/dev/null || echo 4)"
rc=0
for cfg in "${CONFIGS[@]}"; do
    game="${cfg%%:*}"
    api="${cfg##*:}"
    echo "==> syntax-check as ${game} ($(( ${#SHARED[@]} + 1 )) TUs)"
    printf '%s\n' "${SHARED[@]}" "${api}" \
        | xargs -P "${JOBS}" -I{} sh -c \
            "cd '${SRC}' && ${CXX} ${FLAGS} -D${game} '{}' || { echo 'FAILED: ${game} {}' >&2; exit 1; }"
    if [ $? -ne 0 ]; then
        echo "ERROR: ${game} config has compile errors (see above)." >&2
        rc=1
    fi
done

if [ ${rc} -eq 0 ]; then
    echo "All non-MXB game configs syntax-check clean."
fi
exit ${rc}
