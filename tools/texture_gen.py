#!/usr/bin/env python3
"""Render SVG texture sources to the 32-bit TGA textures the plugin loads.

The sibling of icon_gen.py for art that is SHADED rather than a glyph: it keeps
the SVG's own colour and alpha (gradients, sheens, inner shadows), where icon_gen
deliberately keeps only the alpha shape and repaints it one flat tint. That is
why this exists rather than a flag on icon_gen: the two contracts are opposites,
and a shaded tile pushed through the icon path comes out as a white square.

Sources are greyscale on purpose. A texture is drawn with a vertex colour the
game multiplies in, so white takes the caller's tint in full, grey a darker
tint, and the shading survives whatever colour the tile is given (the badge
tiles take the tier's metal). Same renderer as the icons (cairosvg, pinned in
tools/requirements.txt), same TGA encoding (icon_gen.write_tga), so the output
is byte-reproducible for a fixed renderer version.

    python3 tools/texture_gen.py assets/textures/badge_*.svg -o mxbmrp3_data/textures

Output names follow the plugin's texture convention, <base>_<variant>.tga, so
the source is named that way already (badge_1.svg -> badge_1.tga).
"""
import argparse
import os
import sys
from io import BytesIO

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_gen import render_svg, write_tga  # noqa: E402


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("svgs", nargs="+", help="SVG sources, named <base>_<variant>.svg")
    ap.add_argument("-o", "--out", required=True, help="output directory")
    ap.add_argument("--size", type=int, default=128, help="output edge in pixels (default 128)")
    args = ap.parse_args(argv)

    from PIL import Image
    os.makedirs(args.out, exist_ok=True)
    for svg in args.svgs:
        stem = os.path.splitext(os.path.basename(svg))[0]
        image = Image.open(BytesIO(render_svg(svg, args.size))).convert("RGBA")
        path = os.path.join(args.out, stem + ".tga")
        write_tga(image, path)
        print(f"  ok  {svg} -> {os.path.basename(path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
