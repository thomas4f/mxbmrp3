#!/usr/bin/env bash
# ============================================================================
# tools/fontgen/test.sh — regression test for the .fnt generator.
# Builds mxbmrp3_fontgen, regenerates RobotoMono-Regular from the source .ttf,
# and asserts the output is structurally identical to a font PiBoSo's own
# fontgen made: cell height, per-glyph widths, advance, atlas dimensions, and
# that the atlas round-trips through raw inflate. No game engine or Wine needed.
# Then asserts regen_shipped.sh's font table still names exactly the shipped
# .fnt set and their .ttf sources -- see that check's own comment below.
# Then regenerates every shipped font into a scratch folder and requires a byte
# match, and that each advances its ten digits alike (tabular digits).
#
# THE BASELINE IS A FIXTURE, not the shipped font. It used to be
# mxbmrp3_data/fonts/RobotoMono-Regular.fnt, kept un-normalised so this
# comparison had a genuine fontgen artefact to check against — but that font is
# also the DEFAULT for Normal and Digits, so the one face that skipped the
# normalisation was the one most users read, and it advanced ~3% wider than
# LayoutMetrics::charWidthRatio reserves (long lines overran their card; Normal
# and Strong did not measure alike). The shipped font is normalised now and the
# original lives here, where nothing renders it and the proof still holds.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
TTF="${ROOT}/mxbmrp3_data/web/fonts/RobotoMono-Regular.ttf"
SHIPPED="${HERE}/testdata/RobotoMono-Regular.fontgen-baseline.fnt"
TMP="$(mktemp -d)"; trap 'rm -rf "${TMP}"' EXIT

"${HERE}/build.sh" >/dev/null
cat > "${TMP}/roboto.cfg" <<EOF
name = Roboto Mono Regular
code_page = 1252
char_start = 32
char_end = 255
bitmap_x = 512
bitmap_y = 512
spacing = 20
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
atlas = None
try:
    atlas = zlib.decompress(O[10532:], -15)
    if len(atlas) != w*h:
        fails.append(f"atlas size: got {len(atlas)} expected {w*h}")
except Exception as e:
    fails.append(f"atlas inflate failed: {e}")

# Inter-glyph padding: the games mipmap the atlas, so a neighbouring glyph too
# close to a glyph's edge bleeds across the gap at coarse mip levels (the "green
# bar to the right of the K" artifact). Guard that no ink sits within MIN_GAP px
# to the right of any glyph's ink — i.e. the packing gap stays mip-safe. This
# fails for the old 4px spacing and passes for the 20px normalised default.
MIN_GAP = 12
if atlas is not None and len(atlas) == w*h:
    worst = None
    for cp in range(32, 256):
        o = 268 + cp*40
        if i(O, o) == 0: continue
        x0, x1, y0, y1 = i(O, o+16), i(O, o+20), i(O, o+24), i(O, o+28)
        if x1 <= x0 or y1 <= y0: continue          # blank glyph (space)
        xr = min(w, x1 + MIN_GAP)
        for yy in range(y0, min(y1, h)):
            row = yy*w
            for xx in range(x1, xr):
                if atlas[row + xx] != 0:
                    d = xx - x1
                    if worst is None or d < worst[0]: worst = (d, cp, xx, yy)
    if worst is not None:
        d, cp, xx, yy = worst
        fails.append(f"inter-glyph bleed: ink {d}px right of a glyph edge "
                     f"(cp{cp}({chr(cp)!r}) at atlas ({xx},{yy})); need >= {MIN_GAP}px gap")

if fails:
    print("FAIL:"); [print("  -", f) for f in fails[:20]]
    print(f"  ({len(fails)} mismatch(es))")
    sys.exit(1)
print("OK: generated RobotoMono is structurally identical to the fontgen baseline")
PY

# ---------------------------------------------------------------------------
# THE SHIPPED SET IS THE REGEN LIST. regen_shipped.sh's NORMALISED table claims
# to be exhaustive -- that is what makes "every shipped font is normalised"
# checkable by running the script. It has silently drifted before: two fonts
# were missing from the table and got regenerated by hand instead, so a re-run
# no longer reproduced the shipped set and nothing said so. Assert the three
# sets agree, so a font added to one place has to reach the other two.
# ---------------------------------------------------------------------------
python3 - "${ROOT}" <<'PY'
import os, re, sys
root = sys.argv[1]
def stems(d, ext):
    return {f[:-len(ext)] for f in os.listdir(os.path.join(root, d)) if f.endswith(ext)}

fnt = stems("mxbmrp3_data/fonts", ".fnt")
ttf = stems("mxbmrp3_data/web/fonts", ".ttf")
src = open(os.path.join(root, "tools/fontgen/regen_shipped.sh")).read()
table = re.search(r'NORMALISED=\(\n(.*?)\n\)', src, re.S).group(1)
listed = set(re.findall(r'"[^"|]+\|([^"]+)"', table))

fails = []
for what, a, b in (("mxbmrp3_data/fonts", listed, fnt),
                   ("web/fonts sources", listed, ttf)):
    if a - b: fails.append(f"regen list vs {what}: listed but absent: {sorted(a - b)}")
    if b - a: fails.append(f"regen list vs {what}: present but unlisted: {sorted(b - a)}")
if fails:
    print("FAIL:")
    for f in fails: print("  -", f)
    sys.exit(1)
print(f"OK: {len(listed)} shipped fonts; regen list and both asset folders agree")
PY

# ---------------------------------------------------------------------------
# THE SHIPPED .fnt ARE THIS GENERATOR'S OUTPUT, byte for byte: a re-run of
# regen_shipped.sh into a scratch folder must reproduce every one, so a font
# edited by hand, or a generator change that was not followed by a regen, fails
# here. And every shipped font advances its ten digits alike (normalize's
# tabular digits), which is what keeps a running time from jiggling.
# ---------------------------------------------------------------------------
"${HERE}/regen_shipped.sh" "${TMP}" >/dev/null 2>&1
python3 - "${ROOT}/mxbmrp3_data/fonts" "${TMP}" <<'PY'
import os, struct, sys
shipped, regen = sys.argv[1], sys.argv[2]
fails = []
names = sorted(f for f in os.listdir(shipped) if f.endswith(".fnt"))
for f in names:
    data = open(os.path.join(shipped, f), "rb").read()
    if open(os.path.join(regen, f), "rb").read() != data:
        fails.append(f"{f}: regen_shipped.sh does not reproduce the committed file")
    def advance(cp):
        o = 268 + cp * 40
        return sum(struct.unpack_from("<iii", data, o + 4))
    digits = {advance(cp) for cp in range(ord("0"), ord("9") + 1)}
    if len(digits) != 1:
        fails.append(f"{f}: digit advances differ {sorted(digits)}")
if fails:
    print("FAIL:")
    for f in fails: print("  -", f)
    sys.exit(1)
print(f"OK: {len(names)} shipped fonts regenerate byte for byte, digits tabular")
PY
