#!/usr/bin/env bash
# ============================================================================
# tools/mxbmrp3_fontgen/regen_shipped.sh — regenerate the shipped bitmap fonts.
# Rebuilds the NORMALISED shipped fonts (mxbmrp3_data/fonts/*.fnt) from the
# source .ttf under mxbmrp3_data/web/fonts — consistent size/width/vertical position,
# at the normalised 135px cell (high atlas resolution so text stays crisp when a
# HUD draws it larger than the cell — high-DPI, scaled-up widgets like the speedo).
# RobotoMono-Regular.fnt is intentionally left untouched: it is the original
# fontgen output kept as the reference and as test.sh's baseline (regenerate it at
# 135px via test.sh's cfg if the reference resolution ever changes again).
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
WEB="${ROOT}/mxbmrp3_data/web/fonts"
OUT="${ROOT}/mxbmrp3_data/fonts"
TMP="$(mktemp -d)"; trap 'rm -rf "${TMP}"' EXIT

"${HERE}/build.sh" >/dev/null

# name|file   (name is the display name stored in the .fnt)
NORMALISED=(
  "Audiowide Regular|Audiowide-Regular"
  "Enter Sansman Italic|EnterSansman-Italic"
  "Fuzzy Bubbles Regular|FuzzyBubbles-Regular"
  "Roboto Mono Bold|RobotoMono-Bold"
  "Tiny5 Regular|Tiny5-Regular"
)
for entry in "${NORMALISED[@]}"; do
    name="${entry%%|*}"; file="${entry##*|}"
    cat > "${TMP}/f.cfg" <<EOF
name = ${name}
code_page = 1252
char_start = 32
char_end = 255
bitmap_x = 512
bitmap_y = 512
normalize = 1
filename = ${WEB}/${file}.ttf
EOF
    "${HERE}/mxbmrp3_fontgen" "${TMP}/f.cfg" "${OUT}/${file}.fnt"
done
echo "regenerated the normalised fonts in ${OUT} (RobotoMono-Regular left as-is)"
