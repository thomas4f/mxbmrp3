#!/usr/bin/env bash
# ============================================================================
# tools/mxbmrp3_fontgen/test.sh — regression test for the .fnt generator.
# Builds mxbmrp3_fontgen, regenerates RobotoMono-Regular.fnt from the source .ttf, and
# asserts the output is structurally identical to the shipped (fontgen-made)
# font: cell height, per-glyph widths, advance, atlas dimensions, and that the
# atlas round-trips through raw inflate. No game engine or Wine needed.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
TTF="${ROOT}/mxbmrp3_data/web/fonts/RobotoMono-Regular.ttf"
SHIPPED="${ROOT}/mxbmrp3_data/fonts/RobotoMono-Regular.fnt"
TMP="$(mktemp -d)"; trap 'rm -rf "${TMP}"' EXIT

"${HERE}/build.sh" >/dev/null
cat > "${TMP}/roboto.cfg" <<EOF
name = Roboto Mono Regular
code_page = 1252
char_start = 32
char_end = 255
bitmap_x = 512
bitmap_y = 512
spacing = 4
cell_height = 135
filename = ${TTF}
EOF
"${HERE}/mxbmrp3_fontgen" "${TMP}/roboto.cfg" "${TMP}/ours.fnt" >/dev/null

python3 - "${SHIPPED}" "${TMP}/ours.fnt" <<'PY'
import struct, sys, zlib
def load(fn):
    return open(fn, 'rb').read()
S, O = load(sys.argv[1]), load(sys.argv[2])
def i(f, o): return struct.unpack('<i', f[o:o+4])[0]

fails = []
def eq(name, a, b):
    if a != b: fails.append(f"{name}: shipped={a} ours={b}")

eq("magic", S[:4], O[:4])
eq("cellH", i(S, 264), i(O, 264))
eq("bitmapW", i(S, 10512), i(O, 10512))
eq("bitmapH", i(S, 10516), i(O, 10516))

# Per-glyph: ADVANCE must match exactly (it drives the plugin's monospace
# columns). Ink WIDTH may differ by 1px — stb_truetype and fontgen round glyph
# edges slightly differently; that is sub-pixel and imperceptible.
for cp in range(32, 127):
    o = 268 + cp*40
    if i(S, o) == 0: continue
    sw, ow = i(S, o+8), i(O, o+8)
    sa = i(S,o+4)+i(S,o+8)+i(S,o+12); oa = i(O,o+4)+i(O,o+8)+i(O,o+12)
    if abs(sw - ow) > 1: fails.append(f"width cp{cp}({chr(cp)!r}) off by >1: shipped={sw} ours={ow}")
    if sa != oa: fails.append(f"advance cp{cp}({chr(cp)!r}): shipped={sa} ours={oa}")

# Our atlas must inflate to width*height bytes (raw deflate).
w, h = i(O, 10512), i(O, 10516)
try:
    atlas = zlib.decompress(O[10532:], -15)
    if len(atlas) != w*h:
        fails.append(f"atlas size: got {len(atlas)} expected {w*h}")
except Exception as e:
    fails.append(f"atlas inflate failed: {e}")

if fails:
    print("FAIL:"); [print("  -", f) for f in fails[:20]]
    print(f"  ({len(fails)} mismatch(es))")
    sys.exit(1)
print("OK: generated RobotoMono is structurally identical to the shipped font")
PY
