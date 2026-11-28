#!/usr/bin/env bash
# ============================================================================
# tools/demo_reel/showcase.sh
# Shoot the README's showcase sheet: most HUDs, widgets and decks, the themes in
# diagonal bands (showcase.py has the layout and the why).
#
#   tools/demo_reel/showcase.sh [out.png]       (default assets/hud-showcase.png)
#   THEMES="carbon-dark modern-light retro-dark cabinet-dark"   the bands, in order
#   --no-build                                  reuse the test DLL
#
# Each theme is shot twice through make_demo.sh's still mode (the made-up race,
# 27.5 s into a 30 s window, the PB lap, a track playing): once with everything
# at offset 0 to read each panel's rect, once laid out on the grid, kept with
# its alpha. The pieces that look the same in every theme are shot once more,
# alone. About 15 minutes; needs what make_demo.sh needs plus Pillow and numpy.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
BUILD=
OUT=
for a in "$@"; do if [ "$a" = "--no-build" ]; then BUILD=--no-build; else OUT="$a"; fi; done
OUT="${OUT:-${ROOT}/assets/hud-showcase.png}"
read -r -a THEMES <<< "${THEMES:-carbon-dark modern-light retro-dark cabinet-dark}"
WORK="${ROOT}/tests/integration/build/showcase"
rm -rf "${WORK}" && mkdir -p "${WORK}"
PY="python3 ${HERE}/showcase.py"

shoot() {   # seed.ini out.png canvasHeight [extra env...]
    local seed=$1 png=$2 h=$3; shift 3
    env REEL_MEDIA=1 REEL_STILL=27.5 STILL_H="${h}" SEED_INI="${seed}" "$@" \
        "${HERE}/make_demo.sh" "${png}" 30 ${BUILD} > "${png%.png}.log" 2>&1 \
        || { echo "ERROR: shooting ${png} failed, see ${png%.png}.log" >&2; exit 1; }
    BUILD=--no-build   # the first shot builds; the rest reuse it
}

IDS=$(${PY} ids)
for t in "${THEMES[@]}"; do
    echo "==> measuring ${t}"
    ${PY} measure-ini "${t}" "${WORK}/measure-${t}.ini"
    shoot "${WORK}/measure-${t}.ini" "${WORK}/measure-${t}.png" 1080 REEL_EDGES="${IDS}"
    grep -q '^edges' "${WORK}/measure-${t}.log" || { echo "ERROR: no panel rects for ${t}" >&2; exit 1; }
done
${PY} layout "${WORK}" "${THEMES[@]}"
H_MAIN=$(python3 -c "import json;print(json.load(open('${WORK}/layout.json'))['main']['h'])")
H_SIDE=$(python3 -c "import json;print(json.load(open('${WORK}/layout.json'))['side']['h'])")
for t in "${THEMES[@]}"; do
    echo "==> shooting ${t}"
    shoot "${WORK}/seed-${t}.ini" "${WORK}/render-${t}.png" "${H_MAIN}" KEEP_ALPHA=1
done
echo "==> shooting the side column"
shoot "${WORK}/seed-side.ini" "${WORK}/render-side.png" "${H_SIDE}" KEEP_ALPHA=1
${PY} compose "${WORK}" "${OUT}" "${THEMES[@]}"
