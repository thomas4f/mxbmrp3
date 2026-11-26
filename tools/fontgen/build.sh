#!/usr/bin/env bash
# ============================================================================
# tools/fontgen/build.sh — build the portable PiBoSo .fnt generator.
# Compiles the vendored miniz (C) and links it with mxbmrp3_fontgen.cpp (C++17).
# Output: tools/fontgen/mxbmrp3_fontgen
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
MINIZ="${ROOT}/mxbmrp3/vendor/miniz"
OUT="${HERE}/mxbmrp3_fontgen"
CXX="${CXX:-g++}"
CC="${CC:-gcc}"
TMP="$(mktemp -d)"; trap 'rm -rf "${TMP}"' EXIT

# miniz amalgamation must be compiled as C (it is not valid C++).
"${CC}" -O2 -c "${MINIZ}/miniz.c"       -o "${TMP}/miniz.o"
"${CC}" -O2 -c "${MINIZ}/miniz_tdef.c"  -o "${TMP}/miniz_tdef.o"
"${CC}" -O2 -c "${MINIZ}/miniz_tinfl.c" -o "${TMP}/miniz_tinfl.o"

# -ffp-contract=off: the shipped .fnt are byte-exact output of this build, so
# the float maths must round the same on every machine. Where the compiler may
# fuse a*b+c into one FMA (the GitHub runner's g++ does), stb_truetype's
# coverage rounds differently and a few atlas pixels move: on 21 Nov 2026 that
# was 2/1/7 pixels in ChakraPetch-SemiBoldItalic/Inter-Regular/Tomorrow-
# SemiBoldItalic, reproduced exactly with -march=x86-64-v3 and gone with this.
"${CXX}" -std=c++17 -O2 -ffp-contract=off -Wall "${HERE}/mxbmrp3_fontgen.cpp" \
    "${TMP}/miniz.o" "${TMP}/miniz_tdef.o" "${TMP}/miniz_tinfl.o" \
    -lm -o "${OUT}"
echo "built ${OUT}"
