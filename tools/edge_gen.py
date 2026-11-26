#!/usr/bin/env python3
"""Generate the shared slant-edge textures (mxbmrp3_data/textures/slant_edge_N.tga).

The game draws a quad's edges hard, so a leaning solid quad (a deck meter's
block, bar or column) steps along its slanted sides. DeckHud::addRectPx
draws it instead as an upright core plus one thin wedge per slanted side,
textured with this picture: a right triangle filling the bottom-right half,
white, its diagonal an alpha RAMP rather than a hard line, so the game's
texture filtering turns the stretched wedge into a smooth edge.

The ramp must span about one screen pixel across the wedge, and a wedge's
width is its lean (1.4 px on a meter strip, 10 on an Inputs column), so four
variants carry ramps of 1, 1/2, 1/4 and 1/8 of the width; the deck picks the
one nearest a pixel. Each texel is the ramp averaged over 8x8 samples.

    python3 tools/edge_gen.py -o mxbmrp3_data/textures
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_gen import write_tga  # noqa: E402

SIZE = 64
RAMPS = (1.0, 0.5, 0.25, 0.125)   # variant N's ramp, as a share of the width
SS = 8


def render(ramp):
    import numpy as np
    from PIL import Image
    s = (np.arange(SIZE * SS) + 0.5) / (SIZE * SS)
    u, v = np.meshgrid(s, s)
    # Covered right of the diagonal from the bottom-left corner to the top-right
    a = np.clip((u - (1.0 - v)) / ramp + 0.5, 0.0, 1.0)
    a = a.reshape(SIZE, SS, SIZE, SS).mean(axis=(1, 3))
    rgba = np.full((SIZE, SIZE, 4), 255, dtype=np.uint8)
    rgba[..., 3] = (a * 255.0 + 0.5).astype(np.uint8)
    return Image.fromarray(rgba, mode="RGBA")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-o", "--out", required=True, help="output directory")
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    for n, ramp in enumerate(RAMPS, 1):
        path = os.path.join(args.out, f"slant_edge_{n}.tga")
        write_tga(render(ramp), path)
        print(f"  ok  {os.path.basename(path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
