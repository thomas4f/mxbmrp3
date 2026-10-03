#!/usr/bin/env bash
# ============================================================================
# tools/fontgen/test_outlines2ttf.sh — regression test for outlines2ttf.py.
# Draws a synthetic glyph sheet with PyMuPDF (a bar for '1', a ring for '0', a
# smaller caption shape that must NOT be taken for a glyph), converts it with
# outlines2ttf.py, rasterises the result with mxbmrp3_fontgen, and asserts:
#   * the .ttf maps exactly the drawn characters (plus space), the ring has two
#     contours, and --base fills the rest of CP1252 from the given font;
#   * the .fnt has ink for '0' and '1', the ring's counter is hollow, an undrawn
#     character is blank but keeps a digit's advance (no --base) and has ink
#     (--base), so "1/24" can never collapse into "124".
# No game engine or Wine needed. Exits 3 (CTest SKIP) when PyMuPDF/fontTools are
# not installed: `python3 -m pip install -r tools/requirements.txt`.
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${HERE}/../.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "${TMP}"' EXIT

if ! python3 -c "import pymupdf, fontTools" 2>/dev/null; then
    echo "SKIPPED: outlines2ttf needs pymupdf + fonttools (python3 -m pip install -r tools/requirements.txt)"
    exit 3
fi

"${HERE}/build.sh" >/dev/null

# The synthetic sheet: page space is y-down, like the .ai files Illustrator saves.
python3 - "${TMP}/sheet.pdf" <<'PY'
import sys, pymupdf
doc = pymupdf.open(); page = doc.new_page(width=400, height=300)
sh = page.new_shape()
# '1': a solid bar, 40x200, at x=50 (flat bottom at y=250 = the baseline)
sh.draw_rect(pymupdf.Rect(50, 50, 90, 250)); sh.finish(fill=(0, 0, 0), color=None)
# '0': a ring drawn as one path with two subpaths — outer 120x200 and a hole —
# with the hole wound the SAME way as the outer (what a naive tool exports), so
# the winding normalisation has work to do.
sh.draw_rect(pymupdf.Rect(130, 50, 250, 250))
sh.draw_rect(pymupdf.Rect(160, 90, 220, 210))
sh.finish(fill=(0, 0, 0), color=None, even_odd=True)
# A caption: shorter than the glyphs, so it must be ignored.
sh.draw_rect(pymupdf.Rect(50, 10, 300, 30)); sh.finish(fill=(0, 0, 0), color=None)
sh.commit(); doc.save(sys.argv[1])
PY

BASE="${ROOT}/mxbmrp3_data/web/fonts/RobotoMono-Regular.ttf"
python3 "${HERE}/outlines2ttf.py" "${TMP}/sheet.pdf" --chars 10 -o "${TMP}/Bare.ttf" 2>/dev/null
python3 "${HERE}/outlines2ttf.py" "${TMP}/sheet.pdf" --chars 10 --base "${BASE}" -o "${TMP}/Filled.ttf" 2>/dev/null
"${HERE}/mxbmrp3_fontgen" "${TMP}/Bare.ttf" "${TMP}/Bare.fnt" 2>/dev/null
"${HERE}/mxbmrp3_fontgen" "${TMP}/Filled.ttf" "${TMP}/Filled.fnt" 2>/dev/null

python3 - "${TMP}" <<'PY'
import struct, sys, zlib
from fontTools.ttLib import TTFont
tmp = sys.argv[1]
fails = []

bare = TTFont(f"{tmp}/Bare.ttf"); cm = bare.getBestCmap()
if set(cm) != {ord("0"), ord("1"), 32}:
    fails.append(f"bare cmap: expected exactly '0','1',space; got {sorted(cm)}")
ring = bare["glyf"][cm[ord("0")]]
if ring.numberOfContours != 2:
    fails.append(f"ring glyph: expected 2 contours, got {ring.numberOfContours}")
bar = bare["glyf"][cm[ord("1")]]
if bar.numberOfContours != 1:
    fails.append(f"bar glyph: expected 1 contour, got {bar.numberOfContours}")
# Winding: outer clockwise (negative area), hole anticlockwise — else the ring
# fills solid under a nonzero rasteriser.
coords, ends, _ = ring.getCoordinates(bare["glyf"])
start = 0; areas = []
for e in ends:
    poly = coords[start:e + 1]; start = e + 1
    areas.append(sum(poly[i][0]*poly[(i+1)%len(poly)][1] - poly[(i+1)%len(poly)][0]*poly[i][1] for i in range(len(poly))) / 2)
areas.sort(key=abs, reverse=True)
if not (areas[0] < 0 < areas[1]):
    fails.append(f"ring winding: outer must be clockwise and the hole anticlockwise; areas={areas}")
# The '1' bar is 40 wide and '0' starts 80pt later: advance == 80 * (700/200).
adv1 = bare["hmtx"][cm[ord("1")]][0]
if adv1 != 280:
    fails.append(f"'1' advance: expected 280 (the artwork's spacing), got {adv1}")

filled = TTFont(f"{tmp}/Filled.ttf"); fcm = filled.getBestCmap()
for ch in "A/#P":
    if ord(ch) not in fcm:
        fails.append(f"--base: {ch!r} not filled from the base font")
if filled["glyf"][fcm[ord("0")]].numberOfContours != 2:
    fails.append("--base must keep the artwork's own glyphs")

def fnt(path):
    b = open(path, "rb").read()
    recs = [struct.unpack_from("<10i", b, 268 + 40*i) for i in range(256)]
    w, h = struct.unpack_from("<2i", b, 10512)
    return recs, w, zlib.decompress(b[10532:], -15)

def ink(recs, w, atlas, cp):
    v, xo, gw, rb, ax0, ax1, ay0, ay1, _, _ = recs[cp]
    return sum(1 for y in range(ay0, ay1) for x in range(ax0, ax1) if atlas[y*w + x] > 128)

def adv(recs, cp):
    v, xo, gw, rb = recs[cp][:4]
    return xo + gw + rb

recs, w, atlas = fnt(f"{tmp}/Bare.fnt")
if ink(recs, w, atlas, ord("1")) == 0: fails.append("Bare.fnt: '1' has no ink")
if ink(recs, w, atlas, ord("0")) == 0: fails.append("Bare.fnt: '0' has no ink")
# Hollow: the ring's centre pixel is clear.
v, xo, gw, rb, ax0, ax1, ay0, ay1, _, _ = recs[ord("0")]
if atlas[((ay0 + ay1)//2)*w + (ax0 + ax1)//2] > 32:
    fails.append("Bare.fnt: the ring's counter is filled — winding normalisation failed")
if ink(recs, w, atlas, ord("/")) != 0: fails.append("Bare.fnt: '/' should be blank without --base")
if adv(recs, ord("/")) == 0: fails.append("Bare.fnt: an undrawn character must keep a digit's advance")
lo, hi = sorted((adv(recs, ord("1")), adv(recs, ord("0"))))
if not (lo <= adv(recs, ord("/")) <= hi): fails.append("Bare.fnt: undrawn advance should be a digit's (between the drawn ones)")

recs, w, atlas = fnt(f"{tmp}/Filled.fnt")
if ink(recs, w, atlas, ord("/")) == 0: fails.append("Filled.fnt: '/' has no ink despite --base")
if ink(recs, w, atlas, ord("P")) == 0: fails.append("Filled.fnt: 'P' has no ink despite --base")
if ink(recs, w, atlas, ord("0")) == 0: fails.append("Filled.fnt: '0' lost its ink")

if fails:
    print("FAIL:"); [print("  -", f) for f in fails]; sys.exit(1)
print("OK: outlines2ttf builds a font from a sheet, fontgen rasterises it, winding and fill behave")
PY
