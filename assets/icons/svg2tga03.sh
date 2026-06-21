#!/bin/bash
# svg2tga.sh — Convert SVG icons to flat-filled, anti-aliased TGA (no outline).
#
# Pipeline: render high-res (supersample) -> trim -> smooth downscale (no alpha
# threshold) -> center in canvas -> force flat fill color LAST -> save 32-bit TGA.
# Forcing the fill color last sets EVERY pixel's RGB (icon + transparent padding)
# to the fill, while the alpha channel carries the anti-aliased shape. This means
# there is no darker RGB anywhere to bleed into edges -- no halos under downscale,
# and no dark fringe when a game samples the texture with bilinear filtering.
# No alpha threshold = smooth (non-jagged) edges.
#
# Requires: rsvg-convert (librsvg2-bin), convert (ImageMagick).
# ============================================================
# DEFAULTS (override via flags)
# ============================================================
SIZE=64               # Output width/height in px
COLOR="white"         # Fill color (any ImageMagick color: name, #RRGGBB, rgb(...))
OUT_DIR="tga"         # Output directory
SUPERSAMPLE=8         # Render at SIZE*SUPERSAMPLE before downscaling (higher = smoother)
RENDER_SIZE=0         # 0 = auto (SIZE*SUPERSAMPLE); >0 sets an explicit render size
FLIP=1                # 1 = flip vertically for TGA origin (matches typical game engines)
# ============================================================

usage() {
    cat << EOF
Usage: $0 [options] [file.svg ...]

  -s, --size N         Output size in px (default: $SIZE)
  -c, --color C        Fill color (default: $COLOR) e.g. white, "#ff8800", "rgb(0,128,255)"
  -o, --out DIR        Output directory (default: $OUT_DIR)
      --supersample N  Render scale factor for smoothness (default: $SUPERSAMPLE)
      --render N        Explicit render size in px (overrides --supersample)
      --no-flip        Do NOT flip vertically (use if icons come out upside-down)
  -h, --help           Show this help

With no files given, all *.svg in the current directory are processed.
EOF
}

# ---- Parse arguments ----
files=()
while [ $# -gt 0 ]; do
    case "$1" in
        -s|--size)        SIZE="$2"; shift 2 ;;
        -c|--color)       COLOR="$2"; shift 2 ;;
        -o|--out)         OUT_DIR="$2"; shift 2 ;;
        --supersample)    SUPERSAMPLE="$2"; shift 2 ;;
        --render)         RENDER_SIZE="$2"; shift 2 ;;
        --no-flip)        FLIP=0; shift ;;
        -h|--help)        usage; exit 0 ;;
        -*)               echo "Unknown option: $1" >&2; usage; exit 1 ;;
        *)                files+=("$1"); shift ;;
    esac
done

# ---- Dependency check ----
for cmd in rsvg-convert convert; do
    command -v "$cmd" >/dev/null 2>&1 || { echo "Error: '$cmd' not found." >&2; exit 1; }
done

# ---- Resolve render size ----
if [ "$RENDER_SIZE" -le 0 ]; then
    RENDER_SIZE=$(( SIZE * SUPERSAMPLE ))
fi

# ---- Collect input files ----
if [ ${#files[@]} -eq 0 ]; then
    shopt -s nullglob
    files=(*.svg)
fi
if [ ${#files[@]} -eq 0 ]; then
    echo "No SVG files to process." >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
echo "Converting ${#files[@]} file(s): size=${SIZE}px, color=${COLOR}, render=${RENDER_SIZE}px, flip=${FLIP}, out=${OUT_DIR}/"
echo ""

tmp_r="/tmp/svg2tga_r_$$.png"
tmp_f="/tmp/svg2tga_f_$$.png"
trap 'rm -f "$tmp_r" "$tmp_f"' EXIT

for svg in "${files[@]}"; do
    [ -f "$svg" ] || { echo "  skip (not found): $svg"; continue; }
    base="$(basename "$svg" .svg)"
    out="${OUT_DIR}/${base}.tga"

    # 1) Render SVG at high resolution, transparent background.
    rsvg-convert --background-color=none -w "$RENDER_SIZE" -h "$RENDER_SIZE" "$svg" -o "$tmp_r"

    # 2) Trim transparent margin, smooth-downscale (no threshold), center in square
    #    canvas, then force the fill color LAST so all RGB (incl. padding) is uniform.
    convert "$tmp_r" \
        -trim +repage \
        -resize "${SIZE}x${SIZE}" \
        -background none -gravity center -extent "${SIZE}x${SIZE}" \
        -fill "$COLOR" -colorize 100 \
        "$tmp_f"

    # 3) Save as 32-bit TGA (optionally flipped for TGA origin convention).
    if [ "$FLIP" -eq 1 ]; then
        convert "$tmp_f" -flip "$out"
    else
        convert "$tmp_f" "$out"
    fi

    echo "  ✓ $out"
done

echo ""
echo "Done -> ${OUT_DIR}/"
