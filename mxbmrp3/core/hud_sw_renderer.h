// ============================================================================
// core/hud_sw_renderer.h
// A pure-C++ software rasterizer for the plugin's own render primitives
// (SPluginQuad_t / SPluginString_t, in 16:9 normalized coords). No GPU, no
// windowing — it fills an RGBA8 buffer, so the companion window can present the
// exact HUD the game would draw, off the game's renderer.
//
// Colored quads are single-pass scanline convex fills; sprites/icons are blitted
// from the game's .tga (affine-mapped, so rotated sprites draw rotated). Text is
// drawn from the game's own pre-rasterized bitmap fonts (.fnt) — the exact glyph
// atlas the game samples, so the companion is pixel-faithful AND allocation-free
// per frame (the atlas is decompressed once and cached). Every font the game
// registers is a .fnt, so there is no .ttf path here. Fonts and textures are
// cached across frames (stateful Renderer). Portable: no Win32/SDL here — the
// window shell owns that (plain Win32 + GDI).
// ============================================================================
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../game/game_config.h"   // SPluginQuad_t / SPluginString_t (per game)

namespace hudsw {

// An RGBA8 image the renderer draws into (row-major, 4 bytes/pixel).
struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    // Normalized [0,1] HUD coords map into this sub-rect of the buffer: [ox, ox+ew)
    // x [oy, oy+eh). ew/eh <= 0 means "the whole image" (the default). The companion
    // sets a centered 16:9 rect so the HUD keeps its aspect (no distortion) while
    // elements positioned outside [0,1] land in the surrounding window area instead of
    // being clipped to a letterbox — matching the in-game HUD, where x/y can go
    // negative or past 1. Clipping still uses the full buffer (w/h).
    float ox = 0, oy = 0, ew = 0, eh = 0;
    float vpW() const { return ew > 0.0f ? ew : static_cast<float>(w); }
    float vpH() const { return eh > 0.0f ? eh : static_cast<float>(h); }
    float mapX(float n) const { return ox + n * vpW(); }
    float mapY(float n) const { return oy + n * vpH(); }
    void setViewport(float x, float y, float wv, float hv) { ox = x; oy = y; ew = wv; eh = hv; }
    void resize(int W, int H) { w = W; h = H; px.assign(static_cast<size_t>(W) * H * 4, 0); }
    void fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
};

// Assets a frame references, resolved to files under `assetRoot`:
//   fonts   -> <root>/fonts/<name>.fnt (game bitmap font, pixel-exact)
//   texture -> <root>/textures/<name>.tga
//   icon    -> <root>/icons/<name>.tga (tinted to the quad color)
// `fontNames`/`spriteNames` are the game's registration tables (1-based indices);
// spriteNames[i] with i >= firstIcon-1 are icons, the rest textures.
struct Frame {
    const SPluginQuad_t* quads = nullptr; int quadCount = 0;
    const SPluginString_t* strings = nullptr; int stringCount = 0;
    const std::vector<std::string>* fontNames = nullptr;    // basenames, no extension
    const std::vector<std::string>* spriteNames = nullptr;  // basenames, no extension
    int firstIcon = 1 << 30;
    std::string assetRoot;   // e.g. "plugins/mxbmrp3_data"
};

class Renderer {
public:
    // Draw `frame` into `out` (must be pre-sized). Fills the backdrop first.
    void render(Image& out, const Frame& frame, uint8_t bgR, uint8_t bgG, uint8_t bgB);

private:
    struct Tex { int w = 0, h = 0; std::vector<uint8_t> rgba; bool ok = false; };

    // PiBoSo bitmap font (.fnt): one decompressed grayscale atlas + a per-codepoint
    // glyph table. This is the game's own text asset, so drawing from it is exact.
    struct FntGlyph { bool valid = false; int x0 = 0, y0 = 0, x1 = 0, y1 = 0; int xoff = 0, adv = 0; };
    struct FntFont {
        bool ok = false;
        int cellH = 0, aw = 0, ah = 0;
        std::vector<uint8_t> atlas;   // aw*ah 8-bit coverage
        FntGlyph glyphs[256];
    };

    FntFont* fnt(const std::string& base, const std::string& root);
    Tex* tex(const std::string& base, bool icon, const std::string& root);
    void drawQuad(Image&, const SPluginQuad_t&, const Frame&);
    void drawString(Image&, const SPluginString_t&, const Frame&);
    void drawStringFnt(Image&, const SPluginString_t&, const Frame&, FntFont&);

    std::map<std::string, FntFont> m_fnts;
    std::map<std::string, Tex> m_texs;
};

}  // namespace hudsw
