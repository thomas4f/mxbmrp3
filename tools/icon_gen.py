#!/usr/bin/env python3
"""Batch-convert SVG glyphs into the 64x64 BGRA TGA icons the plugin loads.

This is the Python successor to the old ``svg2tga.sh`` ImageMagick script. It
renders each SVG at a supersampled size, recolours it to a flat tint (white by
default, so the in-game ColorConfig tint shows through), optionally adds a
contrast outline, downsamples, sharpens, and writes a TGA that is byte-compatible
with the existing icon set (uncompressed 32-bit, BGRA, bottom-left origin -
descriptor 0x08, which is what the existing icons use and the loader expects).

Two icon "roles" in this project:
  * Identity icons (hud-*.tga) sit beside HUD titles and get an in-game drop
    shadow, so they want NO outline -> run with --outline 0 (the default).
  * Status/marker icons (flag-checkered, clock, ...) are drawn over the track
    with no shadow, so they want the outline -> e.g. --outline 2.

Renderer: cairosvg ONLY (pinned in tools/requirements.txt). The .tga output is
byte-reproducible only for a fixed renderer + version; cairosvg pins cleanly via
pip, whereas rsvg-convert/system librsvg cannot be pinned (its output drifts
between librsvg releases), so it is deliberately not used. Image processing uses
Pillow. See assets/icons/README.md.

Examples
--------
Flat identity icons (no outline), smooth anti-aliased edges (the default):
    python3 tools/icon_gen.py hud-standings.svg hud-timing.svg -o mxbmrp3_data/icons

Outlined status/marker icons (2px black outline), same smooth AA edges:
    python3 tools/icon_gen.py *.svg --outline 2 -o out

Hard binary edges (legacy look), larger output, heavier supersampling:
    python3 tools/icon_gen.py icon.svg --crisp 0.8 --size 128 --supersample 8

Install deps:  python3 -m pip install -r tools/requirements.txt
"""

import argparse
import glob
import os
import struct
import sys
from io import BytesIO


# --------------------------------------------------------------------------- #
# TGA writing (pure stdlib - this is the format-compatibility-critical part).
# --------------------------------------------------------------------------- #
def encode_tga(width, height, bgra_topdown):
    """Encode an uncompressed 32-bit BGRA TGA matching the existing icon set.

    ``bgra_topdown`` is the pixel buffer in BGRA order, row-major, TOP row first
    (the natural orientation of a PIL image). The existing icons use a bottom-left
    origin (header descriptor 0x08), so the rows are written bottom-to-top here.
    """
    expected = width * height * 4
    if len(bgra_topdown) != expected:
        raise ValueError(f"buffer is {len(bgra_topdown)} bytes, expected {expected}")

    # descriptor 0x08: low nibble = 8 alpha bits, bits 4/5 = 0 -> bottom-left origin.
    header = struct.pack(
        "<BBBHHBHHHHBB",
        0,      # ID length
        0,      # colour-map type (none)
        2,      # image type: uncompressed true-colour
        0, 0, 0,  # colour-map spec (unused)
        0, 0,   # x/y origin
        width, height,
        32,     # bits per pixel
        0x08,   # image descriptor
    )

    stride = width * 4
    rows = [bgra_topdown[i * stride:(i + 1) * stride] for i in range(height)]
    rows.reverse()  # top-down buffer -> bottom-left-origin file
    return header + b"".join(rows)


def write_tga(image, path):
    """Write a PIL RGBA image to ``path`` as a BGRA bottom-origin TGA."""
    if image.mode != "RGBA":
        image = image.convert("RGBA")
    bgra = image.tobytes("raw", "BGRA")  # top-down BGRA
    with open(path, "wb") as fh:
        fh.write(encode_tga(image.width, image.height, bgra))


# --------------------------------------------------------------------------- #
# SVG rendering
# --------------------------------------------------------------------------- #
def render_svg(svg_path, px):
    """Render an SVG to RGBA PNG bytes at px*px using cairosvg.

    cairosvg is the project's canonical, pip-pinned renderer and the ONLY one
    used. rsvg-convert/system librsvg is deliberately not supported: its output
    is not pinnable and would not byte-match the shipped icon set. See
    assets/icons/README.md.
    """
    try:
        import cairosvg
    except ImportError:
        sys.exit(
            "error: cairosvg is required. Install the pinned renderer:\n"
            "       python3 -m pip install -r tools/requirements.txt"
        )
    return cairosvg.svg2png(
        url=svg_path, output_width=px, output_height=px,
        background_color="transparent",
    )


# --------------------------------------------------------------------------- #
# Image pipeline
# --------------------------------------------------------------------------- #
def _hex_to_rgb(s):
    s = s.lstrip("#")
    if len(s) != 6:
        raise argparse.ArgumentTypeError("color must be RRGGBB hex")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def _fit_alpha(alpha, bbox, args, Image, ImageFilter):
    """Crop (to a shared bbox), smooth-downscale and centre an L-mode alpha mask
    into the square output canvas.

    Everything happens on the ALPHA channel alone - no colour is present yet - so
    nothing can bleed into the edges. Optional --crisp adds unsharp + a hard cut.
    """
    a = alpha.crop(bbox) if bbox else alpha
    w, h = a.size
    scale = min(args.size / w, args.size / h)
    new_size = (max(1, round(w * scale)), max(1, round(h * scale)))
    a = a.resize(new_size, Image.LANCZOS)
    if args.crisp > 0:
        amount = int(round((1.0 + 2.0 * args.crisp) * 100))  # unsharp percent
        a = a.filter(ImageFilter.UnsharpMask(radius=2.5, percent=amount, threshold=2))
    canvas = Image.new("L", (args.size, args.size), 0)
    canvas.paste(a, ((args.size - new_size[0]) // 2, (args.size - new_size[1]) // 2))
    if args.crisp > 0:
        cut = round(args.crisp * 255)
        canvas = canvas.point(lambda v: 255 if v >= cut else 0)
    return canvas


def process_svg(svg_path, args, fill_rgb, outline_rgb):
    from PIL import Image, ImageFilter, ImageOps

    render_px = args.size * args.supersample
    png_bytes = render_svg(svg_path, render_px)
    src = Image.open(BytesIO(png_bytes)).convert("RGBA")

    # Use ONLY the rendered alpha shape; colour is applied LAST to a uniform layer
    # so transparent/edge pixels carry the fill colour too. There is no darker RGB
    # anywhere to bleed under downscale or in-game bilinear sampling (the halo /
    # dark-fringe problem the legacy svg2tga03.sh documents).
    glyph_alpha = src.getchannel("A")

    if args.outline > 0:
        # Dilate the alpha to form the outline halo. Square dilation (MaxFilter)
        # approximates the legacy Disk morphology closely once downsampled. The
        # outline defines the outer extent, so both masks share its bbox to stay
        # aligned through the identical crop/resize.
        radius = max(1, round(args.outline * args.supersample))
        # Pad by the dilation radius first so the outline is never clipped when the
        # glyph reaches the viewBox edge: rsvg/cairosvg render the viewBox flush to
        # the canvas, so MaxFilter would otherwise have no room to grow outward and
        # the outline would be cut off on that side. Both masks live in the padded
        # space and share the dilated bbox, so they stay aligned through crop/resize.
        padded = ImageOps.expand(glyph_alpha, border=radius, fill=0)
        outline_alpha = padded.filter(ImageFilter.MaxFilter(radius * 2 + 1))
        bbox = outline_alpha.getbbox()
        oa = _fit_alpha(outline_alpha, bbox, args, Image, ImageFilter)
        ga = _fit_alpha(padded, bbox, args, Image, ImageFilter)
        # Two uniform-colour layers composited: each is a single flat colour, so
        # the only soft edge is outline-vs-transparent (a real edge, no fringe).
        out = Image.new("RGBA", (args.size, args.size), outline_rgb + (0,))
        out.putalpha(oa)
        glyph = Image.new("RGBA", (args.size, args.size), fill_rgb + (0,))
        glyph.putalpha(ga)
        out.alpha_composite(glyph)
        return out

    ga = _fit_alpha(glyph_alpha, glyph_alpha.getbbox(), args, Image, ImageFilter)
    out = Image.new("RGBA", (args.size, args.size), fill_rgb + (0,))
    out.putalpha(ga)
    return out


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def main(argv=None):
    p = argparse.ArgumentParser(
        description="Convert SVG glyphs to plugin-compatible TGA icons.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("inputs", nargs="*", default=["*.svg"],
                   help="SVG files or globs (default: *.svg in the current dir)")
    p.add_argument("-o", "--out-dir", default="tga",
                   help="output directory for .tga files")
    p.add_argument("--png-dir", default=None,
                   help="also write upright .png previews to this dir")
    p.add_argument("--size", type=int, default=64,
                   help="output edge length in pixels")
    p.add_argument("--supersample", type=int, default=8,
                   help="render at size*supersample, then downscale (edge cleanliness)")
    p.add_argument("--outline", type=float, default=0.0,
                   help="outline thickness in OUTPUT pixels (0 = no outline / flat)")
    p.add_argument("--crisp", type=float, default=0.0,
                   help="edge sharpness 0..1 (0=smooth anti-aliased edges, the "
                        "default and what the whole shipped set uses; >0 adds "
                        "unsharp + a hard alpha threshold for binary edges)")
    p.add_argument("--color", type=_hex_to_rgb, default=_hex_to_rgb("ffffff"),
                   help="flat fill colour RRGGBB (white lets the in-game tint show)")
    p.add_argument("--outline-color", type=_hex_to_rgb, default=_hex_to_rgb("000000"),
                   help="outline colour RRGGBB (used only when --outline > 0)")
    args = p.parse_args(argv)

    if args.size < 1 or args.supersample < 1:
        sys.exit("error: --size and --supersample must be >= 1")
    if not 0.0 <= args.crisp <= 1.0:
        sys.exit("error: --crisp must be between 0 and 1")

    # Expand inputs (globs + explicit paths), de-duplicated, sorted.
    files = []
    for pattern in args.inputs:
        files.extend(glob.glob(pattern))
    files = sorted(set(f for f in files if f.lower().endswith(".svg")))
    if not files:
        sys.exit("error: no SVG files matched")

    try:
        import PIL  # noqa: F401
    except ImportError:
        sys.exit("error: Pillow is required ('python3 -m pip install -r tools/requirements.txt')")

    os.makedirs(args.out_dir, exist_ok=True)
    if args.png_dir:
        os.makedirs(args.png_dir, exist_ok=True)

    print(f"{len(files)} SVG(s) -> {args.size}px, supersample x{args.supersample}, "
          f"outline={args.outline}px, crisp={args.crisp}")

    ok = 0
    for svg in files:
        base = os.path.splitext(os.path.basename(svg))[0]
        try:
            img = process_svg(svg, args, args.color, args.outline_color)
            write_tga(img, os.path.join(args.out_dir, base + ".tga"))
            if args.png_dir:
                img.save(os.path.join(args.png_dir, base + ".png"))
            print(f"  ok  {svg} -> {base}.tga")
            ok += 1
        except Exception as exc:  # one bad SVG shouldn't abort the batch
            print(f"  FAIL {svg}: {exc}")

    print(f"done: {ok}/{len(files)} converted -> {args.out_dir}")
    return 0 if ok == len(files) else 1


if __name__ == "__main__":
    raise SystemExit(main())
