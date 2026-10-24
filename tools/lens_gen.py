#!/usr/bin/env python3
"""Generate the helmet lens textures (mxbmrp3_data/textures/helmet_lens_N.tga).

The Visor/Goggles tint (Settings > Helmet) used to be one flat colour over the
screen. A real lens is not flat: its tint deepens toward the frame, where the
foam and the strap sit, and the middle you look through stays clearest. This
texture is that profile: white, with the falloff in the alpha channel only, so
the quad colour is the tint and the vertex alpha scales it. Smooth, no noise:
it should read as the lens, not as an effect. One variant per mode:

  1  Goggles: an oval (it is stretched over the whole screen), deepening
     toward the frame all round.
  2  Visor: a shade band, deepest across the brow and clear lower down, like
     a visor's tinted strip.

The plugin draws it at the tint opacity times LENS_EDGE_GAIN
(hud/helmet_overlay_hud.cpp), so CENTRE below must be 1 / that gain: the clear
part then shows exactly the opacity the player set, and the deep part up to
the gain times it.

    python3 tools/lens_gen.py -o mxbmrp3_data/textures
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_gen import write_tga  # noqa: E402

SIZE = 256
# Alpha in the middle, as a share of the edge's (1 / LENS_EDGE_GAIN).
CENTRE = 0.4
# Goggles: where the deepening starts and where it is full, in half-widths
# from the centre (edge midpoints are 1.0, corners 1.41), and how it ramps.
R0 = 0.30
R1 = 1.25
GAMMA = 1.3
# Visor: full at the top edge, fading out by this share of the height down.
VISOR_FADE = 0.55
VISOR_GAMMA = 1.6


def smoothstep(e0, e1, x):
    import numpy as np
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def render(variant):
    import numpy as np
    from PIL import Image
    c = (np.arange(SIZE) + 0.5) / SIZE * 2.0 - 1.0
    x, y = np.meshgrid(c, c)
    if variant == 1:
        ramp = smoothstep(R0, R1, np.hypot(x, y)) ** GAMMA
    else:
        down = (y + 1.0) / 2.0   # 0 at the top edge, 1 at the bottom (image rows run down)
        ramp = smoothstep(VISOR_FADE, 0.0, down) ** VISOR_GAMMA
    a = CENTRE + (1.0 - CENTRE) * ramp
    rgba = np.full((SIZE, SIZE, 4), 255, dtype=np.uint8)
    rgba[..., 3] = (np.clip(a, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    return Image.fromarray(rgba, mode="RGBA")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-o", "--out", required=True, help="output directory")
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    for variant in (1, 2):
        path = os.path.join(args.out, f"helmet_lens_{variant}.tga")
        write_tga(render(variant), path)
        print(f"  ok  {os.path.basename(path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
