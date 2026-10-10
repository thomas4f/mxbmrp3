#!/usr/bin/env python3
"""Derive the shipped LCD7 font from DSEG7 Classic (OFL 1.1, keshikan).

Usage: python3 tools/fontgen/derive_lcd7.py <DSEG7-Classic dir> <out dir>

Writes LCD7-BoldItalic.ttf from DSEG7Classic-BoldItalic.ttf (DSEG release
fonts-DSEG_v046.zip, https://github.com/keshikan/DSEG/releases). Then run
regen_shipped.sh.

The sibling of derive_lcd14.py, whose docstring has the why: the dot gets the
colon's width and the space a digit's width, and the result is renamed because
the OFL reserves "DSEG" for the original. DSEG7 also lacks two characters the
HUD prints, so they are added in its own segment shapes:
  ","  the dot with a short tail below the baseline
  "?"  a "2" without its bottom segment (top, upper right, middle, lower left),
       the usual seven-segment question mark
  "+"  the "-" crossed by a vertical segment (gaps)
  "/"  one segment slanted across the cell (lap counts, "3/10")
  "%"  the "/" between two small hollow boxes (Telemetry and Rumble values)
The sample text the font carries ("DSEG.7 12:34") is renamed with it.
"""
import math
import sys
from array import array
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables import ttProgram
from fontTools.ttLib.tables._g_l_y_f import Glyph, GlyphCoordinates

DOT_ADVANCE = 200    # the colon's advance in DSEG7 (units per em = 1000)
COMMA_TAIL = 150     # how far the comma's tail reaches below the baseline
SEGMENT = 124        # a segment's thickness, as DSEG7 draws its "-"


def vertical_segment(centre_x: float, bottom: int, top: int, slant: float):
    """A pointed vertical segment like DSEG7's, leaning with the face.
    centre_x is the segment's centre line at y = 0."""
    half = SEGMENT // 2

    def at(dx: int, y: int):
        return (round(centre_x + dx + slant * y), y)

    points = [at(0, top), at(half, top - half), at(half, bottom + half),
              at(0, bottom), at(-half, bottom + half), at(-half, top - half)]
    return points, [1] * len(points)


def add_glyph(font: TTFont, char: str, name: str, glyph, advance: int) -> None:
    order = font.getGlyphOrder() + [name]
    font.setGlyphOrder(order)
    font["glyf"].glyphOrder = order
    font["glyf"][name] = glyph
    font["hmtx"][name] = (advance, glyph.xMin)
    for table in font["cmap"].tables:
        if table.isUnicode():
            table.cmap[ord(char)] = name


def glyph_from_contours(parts, glyf):
    """A simple glyph made of (points, on-curve flags) contours."""
    glyph = Glyph()
    glyph.numberOfContours = len(parts)
    glyph.coordinates = GlyphCoordinates()
    glyph.flags = array("B")
    glyph.endPtsOfContours = []
    glyph.program = ttProgram.Program()
    glyph.program.fromBytecode(b"")
    for points, flags in parts:
        glyph.coordinates.extend(points)
        glyph.flags.extend(flags)
        glyph.endPtsOfContours.append(len(glyph.coordinates) - 1)
    glyph.recalcBounds(glyf)
    return glyph


def split_contours(glyph):
    """The glyph's contours as (points, flags) pairs."""
    parts, start = [], 0
    for end in glyph.endPtsOfContours:
        parts.append((list(glyph.coordinates[start:end + 1]),
                      list(glyph.flags[start:end + 1])))
        start = end + 1
    return parts


def derive(src: Path, dst: Path) -> None:
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

    # Comma: the (now centred) dot plus a tail hanging below it, leaning left
    # like the face's slant.
    dot_parts = split_contours(glyph)
    left, right = glyph.xMin, glyph.xMax
    width = right - left
    tail = [(left + width // 4, 0), (left + width * 3 // 4, 0),
            (left + width // 3, -COMMA_TAIL), (left - width // 6, -COMMA_TAIL)]
    comma = glyph_from_contours(dot_parts + [(tail, [1] * len(tail))], glyf)
    add_glyph(font, ",", "comma", comma, DOT_ADVANCE)

    # Question mark: "2" minus its bottom segment, the one contour lying
    # wholly within the segment's thickness above the baseline.
    two = split_contours(glyf[cmap[ord("2")]])
    kept = [c for c in two if max(y for _, y in c[0]) > 200]
    assert len(kept) == len(two) - 1, "expected exactly one bottom segment in '2'"
    question = glyph_from_contours(kept, glyf)
    add_glyph(font, "?", "question", question, digit_advance)

    # Plus: the hyphen crossed by one vertical segment of the same length.
    slant = -math.tan(math.radians(font["post"].italicAngle))
    (minus,) = split_contours(glyf[cmap[ord("-")]])
    xs = [x for x, _ in minus[0]]
    ys = [y for _, y in minus[0]]
    middle_y = (min(ys) + max(ys)) // 2
    centre_x = (min(xs) + max(xs)) / 2 - slant * middle_y
    reach = (max(xs) - min(xs)) // 2
    plus = glyph_from_contours([
        minus,
        vertical_segment(centre_x, middle_y - reach, middle_y + reach, slant),
    ], glyf)
    add_glyph(font, "+", "plus", plus, digit_advance)

    # Slash: one segment's width, slanted from the cell's lower left to its
    # upper right.
    low, high = 200, 616
    slash_points = [(low, 0), (low + SEGMENT, 0), (high, 1000), (high - SEGMENT, 1000)]
    slash = glyph_from_contours([(slash_points, [1] * 4)], glyf)
    add_glyph(font, "/", "slash", slash, digit_advance)

    # Percent: the slash between a small box at the upper left and one at the
    # lower right, each a stroke thinner than a segment so the holes stay open.
    def box(x0: int, y0: int, size: int):
        inset = SEGMENT * 2 // 3

        def at(x: int, y: int):
            return (round(x + slant * y), y)

        outer = [at(x0, y0), at(x0, y0 + size), at(x0 + size, y0 + size), at(x0 + size, y0)]
        inner = [at(x0 + inset, y0 + inset), at(x0 + size - inset, y0 + inset),
                 at(x0 + size - inset, y0 + size - inset), at(x0 + inset, y0 + size - inset)]
        return [(outer, [1] * 4), (inner, [1] * 4)]

    percent = glyph_from_contours(
        [(slash_points, [1] * 4)] + box(60, 700, 300) + box(456, 0, 300), glyf)
    add_glyph(font, "%", "percent", percent, digit_advance)

    for record in font["name"].names:
        text = str(record)
        renamed = (text.replace("DSEG7 Classic", "LCD7").replace("DSEG7Classic", "LCD7")
                   .replace("DSEG.7 12:34", "LCD.7 12:34"))
        if renamed != text:
            record.string = renamed
    font.save(dst / "LCD7-BoldItalic.ttf")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.splitlines()[2], file=sys.stderr)
        return 2
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    derive(src / "DSEG7Classic-BoldItalic.ttf", dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
