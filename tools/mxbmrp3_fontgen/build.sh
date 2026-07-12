#!/usr/bin/env bash
# ============================================================================
# tools/mxbmrp3_fontgen/build.sh — build the portable PiBoSo .fnt generator.
# Compiles the vendored miniz (C) and links it with mxbmrp3_fontgen.cpp (C++17).
# Output: tools/mxbmrp3_fontgen/mxbmrp3_fontgen
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

"${CXX}" -std=c++17 -O2 -Wall "${HERE}/mxbmrp3_fontgen.cpp" \
    "${TMP}/miniz.o" "${TMP}/miniz_tdef.o" "${TMP}/miniz_tinfl.o" \
    -lm -o "${OUT}"
echo "built ${OUT}"
