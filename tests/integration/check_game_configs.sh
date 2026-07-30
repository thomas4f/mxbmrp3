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

# Same flags as the cross-build (mxbmrp3/CMakeLists.txt), minus the game define
# (added per-config below).
BASE_DEFS="-DNOMINMAX -DNDEBUG -DMXBMRP3_TEST_BUILD -DMXBMRP3_ALLOW_NO_ANALYTICS"
INCS="-I${HERE}/shim -I${SRC}"
# The warning bar, matching the cross-build's. This script is the ONLY place the
# GP Bikes and KRP #if branches are ever compiled, so leaving it at -w meant the
# tree-wide bar stopped exactly where the blind spot starts: the
# `#if GAME_SECTOR_COUNT >= 4` blocks that the -Werror cleanup nearly broke are
# invisible to every other gate. The PiBoSo export TU keeps the same narrow
# -Wno-unused-parameter exemption it has in mxbmrp3/CMakeLists.txt, for the same
# reason (signatures are the game's, and the names document the contract) —
# applied per-file below rather than to everything, so the shared sources are
# held to the full bar.
FLAGS="-std=c++17 -m64 -O1 -Wall -Wextra -Werror -Wno-unknown-pragmas -fsyntax-only ${BASE_DEFS} ${INCS}"

# The shared sources every game compiles (matches the cross-build's glob). Exclude
# discord_manager (GAME_HAS_DISCORD=0 in the test build — the cross-build drops it too).
# The per-game API export TU is added per-config below (gpb_api / krp_api).
mapfile -t SHARED < <(cd "${SRC}" && find core handlers hud diagnostics -name '*.cpp' | grep -vE 'discord_manager' | sort)

# game define : matching API export TU
CONFIGS=(
    "GAME_GPBIKES:vendor/piboso/gpb_api.cpp"
    "GAME_KRP:vendor/piboso/krp_api.cpp"
)

JOBS="$(nproc 2>/dev/null || echo 4)"
# --- self-test: prove the bar BITES before trusting a green run -------------
# A clean exit proves "no warnings were found". It does NOT prove "a warning
# would be caught" — a typo in FLAGS, a dropped -Werror, or the per-file
# exemption widening to every TU all produce the identical silent success, and
# this script is the only place the GPB/KRP branches are compiled at all, so
# nothing downstream would notice. One throwaway compile per run closes that.
#
# This is the "one test for a gate" CLAUDE.md calls normal, not a lint checking
# a lint: it asserts this script's own contract, in this script, with no second
# implementation to maintain. Same idiom as run_perf.sh's analyzer-contract
# check and 12e800db's skip-idiom self-test.
# ONE copy of the exemption rule, evaluated with $f set to the path being
# compiled. Both canaries AND the real loop below go through it, so a change to
# the scoping is exercised by the self-test instead of bypassing it. The first
# cut of this canary compiled with ${CXX} ${FLAGS} directly, which meant it
# could never test the very dispatch its own error message told you to check.
EXEMPT_RULE='case "$f" in vendor/piboso/*) x="-Wno-unused-parameter";; *) x="";; esac'

selftest_dir="$(mktemp -d)"
trap 'rm -rf "${selftest_dir}"' EXIT

# TWO canaries, because a canary only catches regressions in the diagnostic it
# actually trips. The first cut used an unused VARIABLE alone, which -Wall
# catches and -Wno-unused-parameter does not silence — so widening the exemption
# left the canary rejected, the self-test declaring "bar live", and the run
# green with a weakened bar. The exact failure this canary exists to prevent.
printf 'void mxb_canary_var() { int unusedCanary = 0; }\n' \
    > "${selftest_dir}/canary_var.cpp"
printf 'void mxb_canary_param(int unusedCanaryParam) { }\n' \
    > "${selftest_dir}/canary_param.cpp"

# $1 = canary file, $2 = what a clean compile would mean
check_canary() {
    f="$1"; eval "${EXEMPT_RULE}"
    if ${CXX} ${FLAGS} ${x} -DGAME_GPBIKES "$f" 2>/dev/null; then
        echo "ERROR: self-test compiled a deliberately-warning TU cleanly." >&2
        echo "       $2" >&2
        echo "       A green run here would mean nothing. Check FLAGS" >&2
        echo "       (-Wall -Wextra -Werror) and EXEMPT_RULE's scoping." >&2
        exit 1
    fi
}
# Catches -Werror or -Wall going missing from FLAGS.
check_canary "${selftest_dir}/canary_var.cpp" \
    "An unused VARIABLE was accepted: the base warning bar is not live."
# Non-piboso path, so EXEMPT_RULE must NOT exempt it. Catches the exemption
# widening -- whether by adding -Wno-unused-parameter to FLAGS or by loosening
# EXEMPT_RULE's pattern.
check_canary "${selftest_dir}/canary_param.cpp" \
    "An unused PARAMETER was accepted at a non-piboso path: the exemption has widened."

rc=0
for cfg in "${CONFIGS[@]}"; do
    game="${cfg%%:*}"
    api="${cfg##*:}"
    echo "==> syntax-check as ${game} ($(( ${#SHARED[@]} + 1 )) TUs)"
    printf '%s\n' "${SHARED[@]}" "${api}" \
        | xargs -P "${JOBS}" -I{} sh -c \
            "cd '${SRC}' && f='{}'; ${EXEMPT_RULE}; \
             ${CXX} ${FLAGS} \$x -D${game} \"\$f\" || { echo 'FAILED: ${game} {}' >&2; exit 1; }"
    if [ $? -ne 0 ]; then
        echo "ERROR: ${game} config has compile errors (see above)." >&2
        rc=1
    fi
done

if [ ${rc} -eq 0 ]; then
    echo "All non-MXB game configs syntax-check clean."
fi
exit ${rc}
