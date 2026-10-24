#!/usr/bin/env python3
"""Generate the shared glow texture (mxbmrp3_data/textures/glow_1.tga).

A soft round blob: white, with the falloff in the alpha channel only, so the
quad colour tints it (a lit segment's green, amber or red) and the vertex alpha
scales it. Gaussian, shifted so it reaches exactly zero at the inscribed circle
and stays zero to the corners - a stretched quad then never shows an edge. HUDs
draw it through BaseHud::addGlow, behind the few elements that are lights, while
Appearance > Glow is on; overlapping halos merge into one band of light.

    python3 tools/glow_gen.py -o mxbmrp3_data/textures
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_gen import write_tga  # noqa: E402

SIZE = 64
# Width of the falloff, in radii: smaller is a tighter, brighter core.
SIGMA = 0.42


def render():
    import numpy as np
    from PIL import Image
    c = (np.arange(SIZE) + 0.5) / SIZE * 2.0 - 1.0
    x, y = np.meshgrid(c, c)
    r2 = x * x + y * y
    edge = np.exp(-1.0 / (2.0 * SIGMA * SIGMA))
    a = (np.exp(-r2 / (2.0 * SIGMA * SIGMA)) - edge) / (1.0 - edge)
    alpha = (np.clip(a, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    rgba = np.full((SIZE, SIZE, 4), 255, dtype=np.uint8)
    rgba[..., 3] = alpha
    return Image.fromarray(rgba, mode="RGBA")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-o", "--out", required=True, help="output directory")
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    path = os.path.join(args.out, "glow_1.tga")
    write_tga(render(), path)
    print(f"  ok  {os.path.basename(path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
