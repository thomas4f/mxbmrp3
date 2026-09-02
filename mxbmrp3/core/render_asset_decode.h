// ============================================================================
// core/render_asset_decode.h
// The render-asset DECODERS and the .fnt text-layout math, shared by both HUD
// renderers — the software rasterizer (hud_sw_renderer) and the D3D11 backend
// (hud_gpu_renderer). Extracted from hud_sw_renderer.cpp verbatim when the GPU
// backend landed, so the two renderers cannot drift on what a .tga or .fnt
// means, and a string lays out identically whichever renderer draws it (same
// scale rule, same advance, same justify) — parity by construction rather than
// by a mirrored-implementation test.
//
// PURE ON PURPOSE: bytes in, structs out — no Win32, no singletons, no file
// paths (callers own I/O and caching). The unit suite compiles this directly;
// the existing hud_sw_renderer golden-frame tests pin that the extraction
// changed nothing.
// ============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hudassets {

// ---- Asset path resolution (shared by both renderers) -----------------------
// `base` arrives from AssetPath::renderName (see core/asset_path.h), which is
// the single definition of this naming and already normalised the separators
// and dropped the "mxbmrp3_data" segment. These are that mapping's INVERSE, and
// they read only what renderName's two outcomes leave behind:
//
//   a bare basename ("hud-laplog")      -> the icon flag picks the flat folder
//   a relative path ("themes/x/corner") -> resolved against the asset root
//
// Theme sprites take the second branch because they live in a per-theme
// subdirectory the icon/texture split cannot express. If this ever stops
// matching, asset_path.h is the side to change (its unit test pins what
// renderName may emit).
inline std::string spritePath(const std::string& root, const std::string& base, bool icon) {
    const std::string rel = (base.find('/') != std::string::npos)
        ? "/" + base + ".tga"
        : (icon ? "/icons/" : "/textures/") + base + ".tga";
    return root + rel;
}
inline std::string fntPath(const std::string& root, const std::string& base) {
    return root + "/fonts/" + base + ".fnt";
}

// A decoded .tga: RGBA8, row-major, top-down. Supports type 2 (raw) and 10
// (RLE), 24/32 bpp, either row origin. ok=false on anything else — including
// file-declared dimensions past kMaxDim, because these files live in
// user-overridable asset dirs and a corrupt header must not bad_alloc a
// render loop.
// ONE level of a mip chain, shared by the glyph atlas and by textures. Declared
// once on purpose: they were briefly two identical structs, which immediately
// bought two near-identical upload paths in the GL backend and would have kept
// them in step by vigilance. `px` is 8-bit coverage for a font and RGBA8 for a
// texture - the CHAIN is the same concept either way, only the pixel is not.
struct MipLevel {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
};

struct Texture {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    bool ok = false;
    // Empty unless buildTexMips() was asked for it - see there for who asks and
    // who deliberately does not.
    std::vector<MipLevel> mips;   // [0] == rgba, [n] == rgba >> n
};
constexpr int kMaxTexDim = 8192;
Texture decodeTga(const std::vector<uint8_t>& bytes);

// Build a complete mip chain for a decoded .tga. NOT called by decodeTga: it is
// opt-in per texture, because only some of them want it.
//
// WANTED for ICONS. An icon's source art is far larger than the ~20px it draws
// at, minified roughly uniformly in both axes, so a single bilinear tap
// undersamples it exactly the way it undersampled glyphs.
//
// NOT wanted for nine-slice panel art, which is why this is not simply done for
// everything. Those pieces are STRETCHED hard in one axis - a 1px-tall edge
// sprite pulled across a whole panel - and mip selection keys off the LARGER of
// the two derivatives, so the level chosen for the stretched axis blurs the
// sharp one. Isotropic minification is the case a mip chain answers; anisotropic
// stretching is the case it makes worse.
//
// PREMULTIPLIED WHILE FILTERING, which is the whole reason this is not four
// lines. Averaging straight RGBA mixes in the COLOUR of fully transparent
// texels, and a .tga's transparent surround is usually black or white - so a
// naive box filter rings every icon with a dark or bright halo that gets worse
// at each level. Colours are weighted by their own alpha here and divided back
// out afterwards, so a transparent texel contributes nothing but its
// transparency. Pinned by tests/unit/test_render_asset_decode_mips.cpp.
void buildTexMips(Texture& t);

// PiBoSo .fnt bitmap font (layout reverse-engineered, confirmed with PiBoSo's
// fontgen 1.02): a per-codepoint glyph table + one DEFLATE-compressed 8-bit
// coverage atlas. The same kMaxDim guard applies to the atlas header.
struct FntGlyph {
    bool valid = false;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;   // atlas rect
    int xoff = 0, adv = 0;
};
struct FntFont {
    bool ok = false;
    int cellH = 0, aw = 0, ah = 0;
    // aw*ah 8-bit coverage. NON-EMPTY EXACTLY WHEN `mips` IS EMPTY: a successful
    // buildFntMips moves this into mips[0] rather than copying 4 MB of shipped
    // atlas per font per backend. Read it only as the no-chain fallback.
    std::vector<uint8_t> atlas;
    // WHY A MIP CHAIN, when the atlas is only ever drawn as flat 2D text.
    //
    // The shipped cell is 135px, deliberately, so a scaled-up widget stays crisp
    // (see tools/fontgen). Ordinary HUD text is ~20px. So EVERY normal string
    // MINIFIES the atlas about 7x, and a plain bilinear tap reads 4 texels out of
    // a footprint of roughly 49. What gets dropped is exactly the partial-coverage
    // texels at a stroke's edge, so strokes thin out and their edges break up -
    // reported from the field as text that "is not smooth any more", against the
    // game's own renderer drawing the same .fnt beside it.
    //
    // The game mipmaps it. That is not inference: fontgen widened the inter-glyph
    // padding from 4px to 20px specifically because the games "render the whole
    // atlas down its mip chain" and 4px bled neighbours in at levels 2-3. So the
    // packing needed to mipmap this atlas safely is ALREADY in the shipped fonts,
    // and we were the only renderer not using it.
    //
    // The chain is complete, down to 1x1. GL 1.1 has no GL_TEXTURE_MAX_LEVEL to
    // declare a partial chain with, and a texture whose chain stops early is
    // INCOMPLETE - it silently samples as white. Levels below the ones we sample
    // cost a third of the atlas once and remove that whole failure mode.
    std::vector<MipLevel> mips;   // [0] == atlas, [n] == atlas >> n
    FntGlyph glyphs[256];
};

// Box-filter the atlas down to 1x1, each level max(1, floor(prev/2)) - the
// dimensions GL and D3D11 both REQUIRE of a mip chain, not merely a convention.
// Odd axes sample the clamped last row/column so the edge behaves.
void buildFntMips(FntFont& f);
FntFont decodeFnt(const std::vector<uint8_t>& bytes);

// ---- Text layout (the game's own metrics) ----------------------------------
// The atlas cell height maps 1:1 to the string's normalized size, so
// scale = normSize * viewportH / cellH gives the exact on-screen metrics the
// game uses (the advance ratio already matches MONOSPACE_CHAR_WIDTH_RATIO).

// Total advance width of `text` at `scale`, for justify offsets.
inline float measureFnt(const FntFont& f, const char* text, float scale) {
    float total = 0;
    for (const char* c = text; *c; ++c) total += f.glyphs[(unsigned char)*c].adv * scale;
    return total;
}

// One glyph's placement: destination rect in float pixels + its atlas rect.
struct GlyphQuad {
    float dx0, dy0, dx1, dy1;
    int ax0, ay0, ax1, ay1;
};

// Walk `text` from (penX, top), emitting a GlyphQuad per drawable glyph.
// Header-only template so both renderers inline their own consumption.
template <class Emit>
inline void layoutFnt(const FntFont& f, const char* text, float penX, float top,
                      float scale, Emit&& emit) {
    for (const char* c = text; *c; ++c) {
        const FntGlyph& g = f.glyphs[(unsigned char)*c];
        int gw = g.x1 - g.x0, gh = g.y1 - g.y0;
        if (g.valid && gw > 0 && gh > 0) {
            float dx0 = penX + g.xoff * scale;
            emit(GlyphQuad{ dx0, top, dx0 + gw * scale, top + gh * scale,
                            g.x0, g.y0, g.x1, g.y1 });
        }
        penX += g.adv * scale;
    }
}

}  // namespace hudassets
