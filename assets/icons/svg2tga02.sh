#!/bin/bash
# Batch convert SVGs to 64x64 TGA icons with crisp edges (outline / no-outline / both)
# ============================================================
# TWEAKABLE SETTINGS
# ============================================================
RENDER_SIZE=512           # Initial render size (higher = better quality, slower)
OUTPUT_SIZE=64            # Final output size
OUTLINE_MODE="outline"    # outline | no-outline | both (override with --outline / --no-outline / --both)
OUTLINE_THICKNESS=16      # Outline thickness at RENDER_SIZE (8 at 512 = 2px at 64)
UNSHARP_RADIUS=2.5        # Sharpening radius (higher = wider sharpening)
UNSHARP_AMOUNT=2.0        # Sharpening strength (higher = more crisp, 1.0-3.0 recommended)
UNSHARP_THRESHOLD=0.02    # Lower = sharpen more aggressively
ALPHA_THRESHOLD=50
                          # 0 = smooth anti-aliased edges, 100 = hard aliased edges
                          # Recommended: 0 for smooth, 50 for sharp, 80 for very sharp

# Output directories
OUTLINE_TGA_DIR="tga"             # Outlined TGA output
OUTLINE_PNG_DIR="png"             # Outlined PNG output
PLAIN_TGA_DIR="tga_no_outline"    # No-outline TGA output
PLAIN_PNG_DIR="png_no_outline"    # No-outline PNG output
# ============================================================

# Parse command-line arguments (override settings above)
for arg in "$@"; do
    case "$arg" in
        --no-outline) OUTLINE_MODE="no-outline" ;;
        --outline)    OUTLINE_MODE="outline" ;;
        --both)       OUTLINE_MODE="both" ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: $0 [--outline | --no-outline | --both]"
            exit 1
            ;;
    esac
done

# Build an outlined image from a white-fill silhouette
# build_outlined <white_png> <out_png>
build_outlined() {
    convert "$1" \
        -alpha extract -morphology Dilate Disk:$OUTLINE_THICKNESS \
        -background black -alpha shape \
        "$1" -compose Over -composite \
        "$2"
}

# Run the finishing pipeline (trim/resize/sharpen/threshold) and write PNG + flipped TGA
# finish_variant <input_png> <tga_dir> <png_dir> <base>
finish_variant() {
    local input_png="$1"
    local tga_dir="$2"
    local png_dir="$3"
    local base="$4"
    local final="/tmp/temp_final_$$.png"

    # Trim excess space, resize to fit within OUTPUT_SIZE, then center in canvas
    convert "$input_png" \
        -trim +repage \
        -resize ${OUTPUT_SIZE}x${OUTPUT_SIZE} \
        -unsharp 0x${UNSHARP_RADIUS}+${UNSHARP_AMOUNT}+${UNSHARP_THRESHOLD} \
        -gravity center -background none -extent ${OUTPUT_SIZE}x${OUTPUT_SIZE} \
        -colorspace sRGB \
        "$final"

    # Apply alpha threshold for sharper edges (if threshold > 0)
    if [ "$ALPHA_THRESHOLD" -gt 0 ]; then
        convert "$final" \
            -channel A -threshold ${ALPHA_THRESHOLD}% +channel \
            /tmp/temp_thresholded_$$.png
        mv /tmp/temp_thresholded_$$.png "$final"
    fi

    # Save as PNG (normal orientation)
    convert "$final" "${png_dir}/${base}.png"
    # Save as TGA (flip vertically to fix TGA coordinate system)
    convert "$final" -flip "${tga_dir}/${base}.tga"

    rm -f "$final"

    echo "  ✓ Created: ${tga_dir}/${base}.tga"
    echo "  ✓ Created: ${png_dir}/${base}.png"
}

# Determine which variants are active for this run
do_outline=0
do_plain=0
case "$OUTLINE_MODE" in
    outline)    do_outline=1 ;;
    no-outline) do_plain=1 ;;
    both)       do_outline=1; do_plain=1 ;;
esac

# Create only the output directories we need
[ "$do_outline" -eq 1 ] && mkdir -p "$OUTLINE_TGA_DIR" "$OUTLINE_PNG_DIR"
[ "$do_plain" -eq 1 ]   && mkdir -p "$PLAIN_TGA_DIR" "$PLAIN_PNG_DIR"

# Check for SVG files
shopt -s nullglob
svg_files=(*.svg)
if [ ${#svg_files[@]} -eq 0 ]; then
    echo "No SVG files found in current directory"
    exit 1
fi
echo "Found ${#svg_files[@]} SVG file(s)"
echo "Settings: ${RENDER_SIZE}→${OUTPUT_SIZE}px, mode=${OUTLINE_MODE}, outline=${OUTLINE_THICKNESS}px@${RENDER_SIZE}, unsharp=${UNSHARP_RADIUS}x${UNSHARP_AMOUNT}"
echo ""

# Process each SVG
for svg in *.svg; do
    # Get base filename without extension
    base="${svg%.svg}"

    echo "Processing: $svg"

    # Convert SVG to high-res PNG
    rsvg-convert --background-color=none -w $RENDER_SIZE -h $RENDER_SIZE "$svg" -o /tmp/temp_$$.png

    # Force white fill (in case SVG doesn't specify fill color)
    convert /tmp/temp_$$.png \
        \( +clone -alpha extract -negate \) \
        -alpha off -compose copy_opacity -composite \
        -background white -alpha remove -alpha copy \
        /tmp/temp_white_$$.png

    # Outlined variant
    if [ "$do_outline" -eq 1 ]; then
        build_outlined /tmp/temp_white_$$.png /tmp/temp_outlined_$$.png
        finish_variant /tmp/temp_outlined_$$.png "$OUTLINE_TGA_DIR" "$OUTLINE_PNG_DIR" "$base"
    fi

    # No-outline variant
    if [ "$do_plain" -eq 1 ]; then
        finish_variant /tmp/temp_white_$$.png "$PLAIN_TGA_DIR" "$PLAIN_PNG_DIR" "$base"
    fi

    # Clean up temp files
    rm -f /tmp/temp_$$.png /tmp/temp_white_$$.png /tmp/temp_outlined_$$.png
done
echo ""
echo "Done! Processed ${#svg_files[@]} file(s)"
[ "$do_outline" -eq 1 ] && echo "  Outlined    → ./${OUTLINE_TGA_DIR}/ + ./${OUTLINE_PNG_DIR}/"
[ "$do_plain" -eq 1 ]   && echo "  No outline  → ./${PLAIN_TGA_DIR}/ + ./${PLAIN_PNG_DIR}/"
