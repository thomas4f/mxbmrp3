#!/usr/bin/env python3
"""Generate DIAGNOSTIC themes whose only variable is sprite resolution.

WHY THIS EXISTS. A theme costs ~26 extra quads per panel, and enabling one measured
+1606us of frame time against +12us of plugin CPU -- so essentially the whole cost
is the engine drawing our primitives, and we do not know WHICH property of them we
are paying for. There are four candidates and the theme-on/off comparison moves all
four at once:

    quad COUNT        1 flat background quad becomes 27 slice quads
    TEXTURED vs flat  m_iSprite 0 (fill with m_ulColor) becomes a real sprite
    texture SWITCHES  those 27 quads come from up to 27 distinct sprites
    texture SIZE      the sprites have some resolution, and nothing measures it

The first three are separable with the in-plugin render probe alone ([Advanced]
renderProbeQuads / renderProbeType / renderProbeSprite -- see the run matrix in
tools/probetheme/README.md). The fourth is not: pinning two EXISTING sprites
of different size compares two pictures that differ in alpha coverage and detail as
well as in resolution, so the difference is not attributable. This generates themes
that are byte-for-byte the same design at different resolutions, so it is.

WHAT IT DRAWS, and why it is deliberately ugly. High-frequency NOISE, not a flat
colour. A solid texture samples the same texel everywhere and sits in the texture
cache whatever its resolution, so a flat probe would measure nothing and report
"resolution is free" -- the wrong answer, confidently. Noise defeats the cache,
which is the condition under which resolution can cost anything at all. These are
diagnostic instruments; they are not meant to be looked at.

The noise is SEEDED FROM THE STEM NAME ONLY, not from the resolution, so the same
slice is the same design in every generated theme -- the pair really does differ in
one variable. Alpha is fully opaque so every generated quad covers the same pixels;
a varying alpha would turn a resolution test into a blending test.

ONE THEME AT A TIME, which is a real constraint and not tidiness: HudManager
registers EVERY discovered theme's sprites with the engine at DrawInit, not just the
selected one, so an unselected 1024px probe theme may still be resident while you
measure. Install one, measure, delete it, install the next.

    probetheme.py --out <themes-dir> --size 16
    probetheme.py --out <themes-dir> --size 1024

Cheap sizes are 5MB-ish; 1024 is ~113MB of texture across the 27 slices, which is
the point of going there. Nothing here is committed as art -- run it when you need
it and delete the folder afterwards. It writes the same 27 stems and the same
uncompressed 32-bit top-origin TGA that tools/themeslice emits, so what
discovery sees is an ordinary theme.
"""

import argparse
import os
import struct

# The 27 stems, exactly as AssetManager's discovery walks them. Kept as literals
# rather than imported from themeslice so this tool has no dependency on the one it
# sits beside; the theme that matters is the one the plugin accepts, and its
# requirement (the whole frame_ set, card_ and button_ optional) is stated there.
SETS = ("frame", "card", "button")
PARTS = ("corner_tl", "corner_tr", "corner_bl", "corner_br",
         "edge_top", "edge_bottom", "edge_left", "edge_right", "center")


def noise_rows(stem, size):
    """Deterministic high-frequency noise, seeded by NAME so it is resolution-independent.

    A hand-rolled LCG rather than `random`: it must produce the same design for the
    same stem across Python versions and platforms, since two themes generated on
    different machines are supposed to be comparable. `random`'s stream is not
    contractually stable across versions; this is fifteen bytes and is.
    """
    seed = 2166136261
    for ch in stem:                      # FNV-1a over the stem, for a per-slice seed
        seed = ((seed ^ ord(ch)) * 16777619) & 0xFFFFFFFF
    rows = []
    for _y in range(size):
        row = []
        for _x in range(size):
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            v = (seed >> 16) & 0xFF
            # Full opacity, and colour varying per texel: the cost we are hunting is
            # the fetch, so adjacent texels must differ in every channel.
            row.append((v, (v * 7) & 0xFF, (v * 13) & 0xFF, 255))
        rows.append(row)
    return rows


def write_tga(path, rows):
    """Uncompressed 32-bit BGRA, top-origin -- the same shape themeslice.py writes."""
    h, w = len(rows), len(rows[0])
    body = bytearray()
    for row in rows:
        for (r, g, b, a) in row:
            body += bytes((b, g, r, a))
    with open(path, "wb") as f:
        f.write(struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, w, h, 32, 0x20))
        f.write(bytes(body))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True,
                    help="themes directory to write into (the plugin's mxbmrp3_data/themes "
                         "or the synced Documents one)")
    ap.add_argument("--size", type=int, required=True,
                    help="edge length in pixels of EVERY slice (e.g. 16, 256, 1024)")
    ap.add_argument("--name", default=None,
                    help="theme folder name (default: _probe_<size>)")
    args = ap.parse_args()

    if args.size < 2 or args.size > 4096:
        ap.error("--size must be between 2 and 4096")

    name = args.name or f"_probe_{args.size}"
    outdir = os.path.join(args.out, name)
    os.makedirs(outdir, exist_ok=True)

    total = 0
    for s in SETS:
        for part in PARTS:
            stem = f"{s}_{part}"
            path = os.path.join(outdir, stem + ".tga")
            write_tga(path, noise_rows(stem, args.size))
            total += os.path.getsize(path)

    # No .ini. Every theme key is optional and the defaults apply, which is what a
    # controlled pair wants: two probe themes that state nothing cannot disagree
    # about border widths, and a geometry difference would land in the measurement
    # as if it were a resolution difference.
    print(f"{name}: 27 slices at {args.size}x{args.size}, {total / 1048576.0:.1f} MB")
    print(f"  -> {outdir}")
    print("  Install ONE probe theme at a time: every discovered theme's sprites are")
    print("  registered at DrawInit, so an unselected one is not necessarily free.")


if __name__ == "__main__":
    main()
