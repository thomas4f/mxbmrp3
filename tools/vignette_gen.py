#!/usr/bin/env python3
"""Generate the impact vignette textures (mxbmrp3_data/textures/impact_vignette_N.tga).

Procedural rather than painted or SVG: the look is noise (radial streaks, smoke)
that cairosvg cannot render (no filter support), and a fixed seed per variant
keeps the output byte-reproducible for a given numpy. Each texture is black with
the effect in the alpha channel only, so the plugin's vertex alpha scales it and
the centre is always fully clear. The HUD (hud/impact_vignette_hud.h) picks one
variant per hit, so every variant must read as the same effect, just different.

    python3 tools/vignette_gen.py -o mxbmrp3_data/textures

Variant recipes live in VARIANTS below: r0/r1 bound the ramp (in half-widths
from the centre; edge midpoints are 1.0, corners 1.41), gamma steepens it,
streak/smoke are how far the noise pushes the edge in and out.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from icon_gen import write_tga  # noqa: E402

SIZE = 512

VARIANTS = [
    # Radial streaks, like a rider's eyes smearing the edges of the frame.
    {"seed": 11, "r0": 0.42, "r1": 1.15, "gamma": 1.5, "streak": 0.10, "smoke": 0.08},
    # Smoke: blotchy, uneven, heavier in places.
    {"seed": 23, "r0": 0.40, "r1": 1.15, "gamma": 1.4, "streak": 0.04, "smoke": 0.20},
    # Tunnel: tighter and deeper, streaks and smoke together.
    {"seed": 37, "r0": 0.34, "r1": 1.05, "gamma": 1.7, "streak": 0.07, "smoke": 0.13},
]


def smoothstep(e0, e1, x):
    import numpy as np
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def angular_streaks(rng, theta):
    """Sum of sines in the angle at INTEGER frequencies, so it has no seam."""
    import numpy as np
    out = np.zeros_like(theta)
    for _ in range(48):
        f = rng.integers(5, 90)
        out += rng.uniform(0.3, 1.0) / (f ** 0.6) * np.sin(f * theta + rng.uniform(0, 2 * np.pi))
    return out / np.abs(out).max()


def smoke(rng, size):
    """Fractal value noise in -1..1: random grids upsampled bicubically, summed."""
    import numpy as np
    from PIL import Image
    out = np.zeros((size, size))
    amp, total = 1.0, 0.0
    for cells in (4, 8, 16, 32, 64):
        grid = rng.random((cells, cells)).astype(np.float32)
        img = Image.fromarray(grid, mode="F").resize((size, size), Image.BICUBIC)
        out += amp * np.asarray(img)
        total += amp
        amp *= 0.55
    out /= total
    return np.clip((out - out.mean()) / (out.std() * 2.5), -1.0, 1.0)


def render(v):
    import numpy as np
    from PIL import Image
    rng = np.random.default_rng(v["seed"])
    c = (np.arange(SIZE) + 0.5) / SIZE * 2.0 - 1.0
    x, y = np.meshgrid(c, c)
    d = np.hypot(x, y)
    theta = np.arctan2(y, x)
    # Noise moves the edge, scaled by distance so the centre stays clean.
    edge = d + d * (v["streak"] * angular_streaks(rng, theta) + v["smoke"] * smoke(rng, SIZE))
    a = smoothstep(v["r0"], v["r1"], edge) ** v["gamma"]
    alpha = (np.clip(a, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    rgba = np.zeros((SIZE, SIZE, 4), dtype=np.uint8)
    rgba[..., 3] = alpha
    return Image.fromarray(rgba, mode="RGBA")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-o", "--out", required=True, help="output directory")
    args = ap.parse_args(argv)
    os.makedirs(args.out, exist_ok=True)
    for i, v in enumerate(VARIANTS, start=1):
        path = os.path.join(args.out, f"impact_vignette_{i}.tga")
        write_tga(render(v), path)
        print(f"  ok  {os.path.basename(path)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
