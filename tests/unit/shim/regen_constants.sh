#!/usr/bin/env bash
# ============================================================================
# tests/unit/shim/regen_constants.sh
# Regenerate the constant blocks in this directory's shim headers from the REAL
# mingw-w64 headers, restricted to the names the plugin actually references.
#
#   ./regen_constants.sh vk       # print the VK_* block (windows.h)
#   ./regen_constants.sh xinput   # print the XINPUT_GAMEPAD_* block (Xinput.h)
#   ./regen_constants.sh --check  # verify BOTH committed blocks still match, exit 1 if not
#
# WHY A GENERATOR. These are ABI constants. Writing them by hand risks a shim
# that LIES: the unit build would compile and pass while disagreeing with the
# header the shipping build sees, and nothing would ever say so. Deriving them
# from the same headers the cross-build uses makes that class of error
# impossible, and the script FAILS rather than guessing if the plugin starts
# using a name the real header doesn't define.
#
# --check is what makes this an enforcement rather than a good intention: a
# generator nobody re-runs is a hand-maintained list with extra steps, and the
# committed shim could silently drift from the real headers while every gate
# stayed green. The cross-build job already has mingw, so the check is free
# there. (Drift is unlikely — these are ABI-frozen — but "unlikely" is the
# argument that leaves you with no check at all.)
#
# Needs mingw-w64 (the cross-build's own dependency). Exits 3 with a SKIP: line
# when it isn't installed; CTest reports that as SKIPPED (SKIP_RETURN_CODE).
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../../.." && pwd)"

WHAT="${1:-}"

# --check: regenerate both blocks and diff them against what is committed. The
# comparison ignores leading/trailing blank lines only; a changed value, a added
# or removed constant, or a reordering all fail.
if [ "${WHAT}" = "--check" ]; then
    rc=0
    for pair in "vk:windows.h:VK_" "xinput:Xinput.h:XINPUT_GAMEPAD_"; do
        what="${pair%%:*}"; rest="${pair#*:}"
        file="${HERE}/${rest%%:*}"; prefix="${rest#*:}"
        gen="$("${BASH_SOURCE[0]}" "${what}")" || { rc=$?; [ "${rc}" = 3 ] && exit 3; exit "${rc}"; }
        have="$(grep -E "^#define ${prefix}" "${file}" 2>/dev/null || true)"
        # Both sides empty must NOT read as "matches". An unreadable or emptied
        # shim would otherwise compare equal to an empty generation and report ok
        # — a check that can silently pass is not a check.
        if [ -z "${gen}" ] || [ -z "${have}" ]; then
            echo "ERROR: ${file##*/}: expected ${prefix}* defines, got none" \
                 "(generated=$(printf '%s' "${gen}" | grep -c . || true)," \
                 "committed=$(printf '%s' "${have}" | grep -c . || true))" >&2
            rc=1
            continue
        fi
        if ! diff -q <(printf '%s\n' "${gen}") <(printf '%s\n' "${have}") >/dev/null; then
            echo "DRIFT: ${file##*/} does not match the real mingw header." >&2
            diff <(printf '%s\n' "${have}") <(printf '%s\n' "${gen}") \
                 --label "committed" --label "regenerated" -u >&2 || true
            echo "Fix: ./tests/unit/shim/regen_constants.sh ${what}  # then paste the block" >&2
            rc=1
        else
            echo "ok    ${file##*/}: ${prefix}* matches mingw-w64"
        fi
    done
    exit "${rc}"
fi

case "${WHAT}" in
    vk)     HEADER_NAME=winuser.h; PREFIX=VK_ ;;
    xinput) HEADER_NAME=xinput.h;  PREFIX=XINPUT_GAMEPAD_ ;;
    *) echo "usage: $0 vk|xinput|--check" >&2; exit 2 ;;
esac

# `|| true` is load-bearing: with `set -e` and `pipefail`, find exiting 1 because
# the search directories don't exist aborts the script ON THE ASSIGNMENT, making
# the SKIP guard below dead code at exactly the moment it is needed. That is not
# theoretical — it shipped. Re-verify by running the suite with mingw removed
# from PATH (see CMakeLists.txt): this must report SKIPPED, never Failed.
# MXB_MINGW_INCLUDE_DIRS is the seam that test drives; unset it is the real paths.
MINGW_DIRS="${MXB_MINGW_INCLUDE_DIRS:-/usr/share/mingw-w64/include /usr/x86_64-w64-mingw32/include}"
# shellcheck disable=SC2086
REAL="$(find ${MINGW_DIRS} -iname "${HEADER_NAME}" 2>/dev/null | head -1 || true)"
[ -n "${REAL}" ] || { echo "SKIP: mingw-w64 ${HEADER_NAME} not found" >&2; exit 3; }

python3 - "${REAL}" "${ROOT}" "${PREFIX}" <<'PY'
import re, subprocess, sys
real, root, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
used = set(subprocess.run(
    rf"grep -rhoE '\b{prefix}[A-Z0-9_]+' mxbmrp3 --include=*.h --include=*.cpp | sort -u",
    shell=True, capture_output=True, text=True, cwd=root).stdout.split())
vals = dict(re.findall(rf'^#define\s+({prefix}[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+)',
                       open(real, encoding="utf-8", errors="ignore").read(), re.M))
# Names used as TYPES (XINPUT_STATE, XINPUT_GAMEPAD) aren't #defines; the shim
# declares those separately, so only object-like macros are generated here.
gen = sorted(used & set(vals), key=lambda k: int(vals[k], 0))
missing = sorted(n for n in used - set(vals) if n in vals)
if missing:
    print("ERROR: not in the real header: " + ", ".join(missing), file=sys.stderr)
    sys.exit(1)
width = max(len(k) for k in gen) if gen else 1
for k in gen:
    print(f"#define {k:<{width}} {vals[k]}")
PY
