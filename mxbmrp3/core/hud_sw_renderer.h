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
#include "render_asset_decode.h"   // shared .tga/.fnt decode + .fnt text layout

namespace hudsw {

// One file's bytes, or empty on any failure. The one I/O helper both renderers
// share (the decoders in render_asset_decode.h are deliberately I/O-free —
// callers own I/O and caching, so the file read lives here, once).
std::vector<uint8_t> readFile(const std::string& path);

// FNV-1a over a frame's content: quads, strings, and the font/sprite
// registration names (a lap time changing without any quad moving still has to
// redraw). This is the identity behind the unchanged-frame skip in BOTH window
// threads (companion + overlay) — one definition so the two cannot drift on
// what "changed" means. Callers mix their own extras in (client size, `have`).
constexpr uint64_t kFrameHashSeed = 1469598103934665603ULL;  // FNV-1a offset basis
inline uint64_t frameContentHash(const std::vector<SPluginQuad_t>& quads,
                                 const std::vector<SPluginString_t>& strings,
                                 const std::vector<std::string>& fontNames,
                                 const std::vector<std::string>& spriteNames) {
    uint64_t h = kFrameHashSeed;
    auto mix = [&h](const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    };
    mix(quads.data(), quads.size() * sizeof(SPluginQuad_t));
    mix(strings.data(), strings.size() * sizeof(SPluginString_t));
    for (const std::string& fn : fontNames) mix(fn.data(), fn.size());
    for (const std::string& sn : spriteNames) mix(sn.data(), sn.size());
    return h;
}

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
    // bgA=255 (the companion window): opaque backdrop, output alpha stays 255.
    // bgA=0 with black: the buffer comes out as PREMULTIPLIED RGBA with real
    // coverage in the alpha channel, ready for UpdateLayeredWindow.
    //
    // NOTHING IN THE PLUGIN ASKS FOR THAT SECOND FORM ANY MORE - it existed for
    // the in-game overlay window, which is deleted. It is kept rather than
    // removed because it costs a defaulted parameter and no branch: blend() does
    // not test it, so the two forms differ only in what fill() writes first. A
    // unit test still covers it, which is the only thing holding it up; delete
    // both together if a future reader wants the parameter gone.
    void render(Image& out, const Frame& frame, uint8_t bgR, uint8_t bgG, uint8_t bgB,
                uint8_t bgA = 255);

    // Drop every decoded .tga so the next frame re-reads them from disk. This is
    // what makes a theme's ART live-reloadable HERE and nowhere else: the GAME is
    // handed sprite indices once at init and cannot be told about a changed file,
    // but this renderer opens the .tga itself, so invalidating is the whole job.
    // Fonts are deliberately kept -- a .fnt is not something anyone iterates on.
    void dropTextureCache() { m_texs.clear(); }

private:
    // Decode (the .fnt/.tga formats + the text-layout math) lives in
    // render_asset_decode.h, SHARED with the D3D11 backend so the two renderers
    // cannot drift; this class keeps only the path resolution, the caches, and
    // the actual rasterization.
    hudassets::FntFont* fnt(const std::string& base, const std::string& root);
    hudassets::Texture* tex(const std::string& base, bool icon, const std::string& root);
    void drawQuad(Image&, const SPluginQuad_t&, const Frame&);
    void drawString(Image&, const SPluginString_t&, const Frame&);
    void drawStringFnt(Image&, const SPluginString_t&, hudassets::FntFont&);

    std::map<std::string, hudassets::FntFont> m_fnts;
    std::map<std::string, hudassets::Texture> m_texs;
};

}  // namespace hudsw
