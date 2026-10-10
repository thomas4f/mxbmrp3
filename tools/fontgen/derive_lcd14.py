#!/usr/bin/env python3
"""Derive the shipped LCD14 fonts from DSEG14 Classic (OFL 1.1, keshikan).

Usage: python3 tools/fontgen/derive_lcd14.py <DSEG14-Classic dir> <out dir>

Writes LCD14-Regular.ttf and LCD14-Bold.ttf from DSEG14Classic-Regular.ttf and
DSEG14Classic-Bold.ttf (DSEG release fonts-DSEG_v046.zip,
https://github.com/keshikan/DSEG/releases). Then run regen_shipped.sh.

Why a derived copy instead of the release files: DSEG draws `.` at ZERO width
(the dot sits under the previous digit, like a real LCD) and a space at a
quarter of a digit. The HUD lays numbers out at a fixed width per character, so
at HUD sizes the dot vanished into "35900" and spaced text collapsed. This gives
the dot the colon's width (0.2 em, centred) and the space a digit's width.

Why the new name: the OFL reserves "DSEG" for the original, so a Modified
Version may not present itself under it (licence condition 3). Copyright and
licence metadata are kept as they are; THIRD_PARTY_LICENSES.md credits DSEG.

fontTools (`python3 -m pip install fonttools`) does the editing; it is the
standard library for this and nothing bespoke would do it better.
"""
import sys
from pathlib import Path

from fontTools.ttLib import TTFont

DOT_ADVANCE = 200  # the colon's advance in DSEG14 (units per em = 1000)


def derive(src: Path, dst: Path, style: str) -> None:
    font = TTFont(src)
    cmap = font.getBestCmap()
    hmtx = font["hmtx"]
    glyf = font["glyf"]

    digit_advance = hmtx[cmap[ord("0")]][0]
    hmtx[cmap[ord(" ")]] = (digit_advance, 0)

    dot = cmap[ord(".")]
    glyph = glyf[dot]
    shift = DOT_ADVANCE // 2 - (glyph.xMin + glyph.xMax) // 2
    glyph.coordinates.translate((shift, 0))
    glyph.recalcBounds(glyf)
    hmtx[dot] = (DOT_ADVANCE, glyph.xMin)

    for record in font["name"].names:
        text = str(record)
        renamed = text.replace("DSEG14 Classic", "LCD14").replace("DSEG14Classic", "LCD14")
        if renamed != text:
            record.string = renamed
    font.save(dst / f"LCD14-{style}.ttf")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.splitlines()[2], file=sys.stderr)
        return 2
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    for style in ("Regular", "Bold"):
        derive(src / f"DSEG14Classic-{style}.ttf", dst, style)
    return 0


if __name__ == "__main__":
    sys.exit(main())
