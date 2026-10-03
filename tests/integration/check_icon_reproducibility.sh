#!/usr/bin/env bash
# ============================================================================
# tests/integration/check_icon_reproducibility.sh
# The shipped mxbmrp3_data/icons/*.tga are GENERATED build output that is also
# committed, so the two can drift apart silently. This regenerates the whole set
# from assets/icons/*.svg under the pinned renderer and requires every byte to
# match what is committed.
#
# It catches two drifts that nothing else did:
#   1. A cairosvg bump that moves pixels. The pin in tools/requirements.txt
#      exists because edge anti-aliasing is only stable for a fixed renderer
#      version, and the rule "do not bump it without regenerating (and
#      reviewing) the whole set" was prose someone had to remember. It is a
#      check now: a bump that changes any icon turns this red, and the fix is to
#      regenerate and commit the new .tga in the same PR.
#   2. An .svg edited without regenerating its .tga - the more likely one.
#      check_docs.py compares the two sets by NAME; nothing compared content.
#
# THE TWO RENDER RULES come from assets/icons/README.md: hud-* are flat identity
# icons, everything else gets --outline 2. That this is uniform across the whole
# set is not an assumption - regenerating all 208 icons under exactly these two
# rules reproduces all 208 committed files byte-for-byte. An icon needing other
# flags would fail here, which is the point: the set has one recipe.
#
# No --self-test: what this asserts is `cmp`, and a lint whose own correctness
# needs a lint is the machinery CMakeLists.txt's header warns about. The
# must-catch half was verified by hand instead - flip one byte of a committed
# .tga and this reports it.
#
# Requires: python3 with cairosvg + Pillow (pip install -r tools/requirements.txt,
# or ./tools/install_deps.sh analytics, which installs that file).
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
cd "${ROOT}"

SRC_DIR="assets/icons"
SHIPPED_DIR="mxbmrp3_data/icons"

command -v python3 >/dev/null || { echo "ERROR: python3 not found"; exit 1; }
python3 -c "import cairosvg, PIL" 2>/dev/null || {
    echo "ERROR: the pinned renderer is not installed (need cairosvg + Pillow)."
    echo "       pip install -r tools/requirements.txt"
    exit 1
}

OUT="$(mktemp -d)"
trap 'rm -rf "${OUT}"' EXIT

# Split by the two render rules. `find` rather than a glob so an empty set is an
# error below instead of a literal '*.svg' reaching icon_gen.py.
mapfile -t FLAT < <(find "${SRC_DIR}" -maxdepth 1 -name 'hud-*.svg' | sort)
mapfile -t OUTLINED < <(find "${SRC_DIR}" -maxdepth 1 -name '*.svg' ! -name 'hud-*' | sort)

if [ "${#FLAT[@]}" -lt 10 ] || [ "${#OUTLINED[@]}" -lt 10 ]; then
    echo "ERROR: found ${#FLAT[@]} flat and ${#OUTLINED[@]} outlined SVGs under ${SRC_DIR}"
    echo "       - the folder moved and this check would pass vacuously."
    exit 1
fi

# Rasterising 200+ icons at 8x supersample is the whole cost of this gate, and
# the work is per-file independent, so fan it out. icon_gen.py takes any number
# of inputs, so each worker does a chunk in one interpreter start.
JOBS="$(command -v nproc >/dev/null && nproc || echo 4)"
gen() {
    local outline="$1"; shift
    printf '%s\0' "$@" \
        | xargs -0 -P "${JOBS}" -n 16 python3 tools/icon_gen.py \
              --outline "${outline}" -o "${OUT}" >/dev/null
}
gen 0 "${FLAT[@]}"
gen 2 "${OUTLINED[@]}"

fail=0
report() { echo "  $1"; fail=1; }

for svg in "${FLAT[@]}" "${OUTLINED[@]}"; do
    name="$(basename "${svg}" .svg)"
    regenerated="${OUT}/${name}.tga"
    shipped="${SHIPPED_DIR}/${name}.tga"
    if [ ! -f "${regenerated}" ]; then
        report "${name}: icon_gen.py produced no .tga"
    elif [ ! -f "${shipped}" ]; then
        report "${name}: has a source SVG but no shipped ${shipped}"
    elif ! cmp -s "${regenerated}" "${shipped}"; then
        report "${name}: shipped .tga differs from a fresh render of ${svg}"
    fi
done

# ...and the other direction: a shipped icon whose source is gone can no longer
# be regenerated or reviewed, and nothing else in the tree would notice.
for tga in "${SHIPPED_DIR}"/*.tga; do
    name="$(basename "${tga}" .tga)"
    [ -f "${SRC_DIR}/${name}.svg" ] || report "${name}: shipped .tga has no ${SRC_DIR}/${name}.svg"
done

if [ "${fail}" -ne 0 ]; then
    echo
    echo "FAIL: mxbmrp3_data/icons is out of step with assets/icons."
    echo
    echo "If you changed an SVG, regenerate and commit its .tga:"
    echo "  python3 tools/icon_gen.py '${SRC_DIR}/hud-<name>.svg' -o ${SHIPPED_DIR}"
    echo "  python3 tools/icon_gen.py '${SRC_DIR}/<name>.svg' --outline 2 -o ${SHIPPED_DIR}"
    echo
    echo "If you bumped cairosvg, the new renderer moves pixels: regenerate the"
    echo "WHOLE set in the same PR and eyeball the diff before committing it."
    echo "See assets/icons/README.md -> Verify a regeneration."
    exit 1
fi

echo "OK: all $(( ${#FLAT[@]} + ${#OUTLINED[@]} )) shipped icons reproduce byte-for-byte from their SVG sources."
