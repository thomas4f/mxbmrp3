// ============================================================================
// core/hud_sw_renderer.cpp  — see hud_sw_renderer.h
// ============================================================================
#include "hud_sw_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace hudsw {

// Both renderers' asset readers feed the hudassets decoders
// (render_asset_decode.h — the formats live there); this keeps the I/O.
std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

namespace {

struct Color { uint8_t r, g, b, a; };
inline Color abgr(unsigned long c) {
    uint32_t v = static_cast<uint32_t>(c);
    return { uint8_t(v & 255), uint8_t((v >> 8) & 255), uint8_t((v >> 16) & 255), uint8_t((v >> 24) & 255) };
}

// Src-over compositing. The RGB rule (c*a + dst*(1-a)) doubles as PREMULTIPLIED
// src-over when the backdrop starts transparent black: each contribution enters
// as color*alpha, which is exactly the premultiplied encoding — so a frame
// rendered over fill(0,0,0,0) is directly consumable by UpdateLayeredWindow
// (AC_SRC_ALPHA expects premultiplied). The alpha rule is standard coverage
// accumulation (aOut = aS + aD*(1-aS)); over an opaque backdrop it stays 255,
// so the companion's StretchDIBits path (which ignores alpha) is unaffected.
inline void blend(Image& im, int x, int y, Color c, float cov) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h) return;
    float a = (c.a / 255.0f) * cov;
    if (a <= 0.0f) return;
    uint8_t* p = &im.px[(size_t(y) * im.w + x) * 4];
    p[0] = uint8_t(c.r * a + p[0] * (1 - a));
    p[1] = uint8_t(c.g * a + p[1] * (1 - a));
    p[2] = uint8_t(c.b * a + p[2] * (1 - a));
    p[3] = uint8_t(255.0f * a + p[3] * (1 - a));
}

// Single-pass scanline fill of a convex quad — one pass so semi-transparent quads
// don't double-blend a diagonal seam. Handles rotated quads (map ribbon).
void fillQuad(Image& im, const float p[4][2], Color col) {
    float minY = p[0][1], maxY = p[0][1];
    for (int i = 1; i < 4; ++i) { minY = std::min(minY, p[i][1]); maxY = std::max(maxY, p[i][1]); }
    int y0 = std::max(0, (int)std::floor(minY)), y1 = std::min(im.h - 1, (int)std::ceil(maxY));
    for (int y = y0; y <= y1; ++y) {
        float sy = y + 0.5f, xL = 1e30f, xR = -1e30f;
        for (int e = 0; e < 4; ++e) {
            const float* a = p[e]; const float* b = p[(e + 1) & 3];
            if ((sy >= a[1] && sy < b[1]) || (sy >= b[1] && sy < a[1])) {
                float x = a[0] + (b[0] - a[0]) * (sy - a[1]) / (b[1] - a[1]);
                xL = std::min(xL, x); xR = std::max(xR, x);
            }
        }
        if (xR < xL) continue;
        int xi0 = std::max(0, (int)std::floor(xL + 0.5f)), xi1 = std::min(im.w - 1, (int)std::ceil(xR - 0.5f));
        for (int x = xi0; x <= xi1; ++x) blend(im, x, y, col, 1.0f);
    }
}

}  // namespace

void Image::fill(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // One 32-bit store per pixel, not four byte stores: this clear runs before EVERY
    // frame and covers the whole client, and the byte loop was measured at ~2 ms of a
    // 1080p companion paint all by itself.
    uint32_t v;
    uint8_t bytes[4] = { r, g, b, a };
    std::memcpy(&v, bytes, 4);
    uint32_t* p = reinterpret_cast<uint32_t*>(px.data());
    std::fill(p, p + px.size() / 4, v);
}

hudassets::FntFont* Renderer::fnt(const std::string& base, const std::string& root) {
    auto it = m_fnts.find(base);
    if (it != m_fnts.end()) return it->second.ok ? &it->second : nullptr;
    hudassets::FntFont fo = hudassets::decodeFnt(readFile(hudassets::fntPath(root, base)));
    auto& ref = m_fnts.emplace(base, std::move(fo)).first->second;
    return ref.ok ? &ref : nullptr;
}

hudassets::Texture* Renderer::tex(const std::string& base, bool icon, const std::string& root) {
    auto it = m_texs.find(base);
    if (it != m_texs.end()) return it->second.ok ? &it->second : nullptr;
    // Path resolution (renderName's inverse) lives with the shared decoders —
    // see hudassets::spritePath for the mapping and its ownership note.
    hudassets::Texture t = hudassets::decodeTga(readFile(hudassets::spritePath(root, base, icon)));
    auto& ref = m_texs.emplace(base, std::move(t)).first->second;
    return ref.ok ? &ref : nullptr;
}

void Renderer::drawQuad(Image& im, const SPluginQuad_t& q, const Frame& fr) {
    if (q.m_iSprite == 0) {
        float p[4][2];
        for (int i = 0; i < 4; ++i) { p[i][0] = im.mapX(q.m_aafPos[i][0]); p[i][1] = im.mapY(q.m_aafPos[i][1]); }
        fillQuad(im, p, abgr(q.m_ulColor));
        return;
    }
    const auto& names = *fr.spriteNames;
    int idx = q.m_iSprite - 1;
    if (idx < 0 || idx >= (int)names.size()) return;
    bool icon = q.m_iSprite >= fr.firstIcon;
    hudassets::Texture* t = tex(names[idx], icon, fr.assetRoot);
    if (!t) return;
    Color tint = abgr(q.m_ulColor);
    // Affine sprite blit: map each destination pixel back into texture UV space via
    // the quad's edge basis, so ROTATED sprites (map rider arrows rotate to heading)
    // draw rotated — not axis-aligned-and-stretched into their bounding box. The
    // quad corners are TL, BL, BR, TR; U runs TL->TR, V runs TL->BL.
    float p0x = im.mapX(q.m_aafPos[0][0]), p0y = im.mapY(q.m_aafPos[0][1]);   // TL (u=0,v=0)
    float ux = im.mapX(q.m_aafPos[3][0]) - p0x, uy = im.mapY(q.m_aafPos[3][1]) - p0y;  // U edge
    float vx = im.mapX(q.m_aafPos[1][0]) - p0x, vy = im.mapY(q.m_aafPos[1][1]) - p0y;  // V edge
    float det = ux * vy - uy * vx;
    if (std::abs(det) < 1e-6f) return;
    float inv = 1.0f / det;
    float minx = p0x, maxx = p0x, miny = p0y, maxy = p0y;
    for (int i = 1; i < 4; ++i) {
        float X = im.mapX(q.m_aafPos[i][0]), Y = im.mapY(q.m_aafPos[i][1]);
        minx = std::min(minx, X); maxx = std::max(maxx, X);
        miny = std::min(miny, Y); maxy = std::max(maxy, Y);
    }
    int X0 = std::max(0, (int)std::floor(minx)), X1 = std::min(im.w - 1, (int)std::ceil(maxx));
    int Y0 = std::max(0, (int)std::floor(miny)), Y1 = std::min(im.h - 1, (int)std::ceil(maxy));
    for (int y = Y0; y <= Y1; ++y)
        for (int x = X0; x <= X1; ++x) {
            float rx = x + 0.5f - p0x, ry = y + 0.5f - p0y;
            float u = (rx * vy - ry * vx) * inv;
            float v = (ux * ry - uy * rx) * inv;
            if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) continue;
            int sx = std::min(t->w - 1, std::max(0, (int)(u * t->w)));
            int sy = std::min(t->h - 1, std::max(0, (int)(v * t->h)));
            const uint8_t* s = &t->rgba[(size_t(sy) * t->w + sx) * 4];
            // Modulate the texel by the quad color (RGB and alpha), the same as the
            // game's texture stage: a white icon takes the color; a colored texture
            // keeps its own color when the quad color is white; and the quad color's
            // alpha (which carries the HUD opacity) fades textures too — the game
            // relies on this (a white-with-opacity quad lets a texture show through).
            Color texel{ uint8_t(s[0] * tint.r / 255), uint8_t(s[1] * tint.g / 255),
                         uint8_t(s[2] * tint.b / 255), tint.a };
            blend(im, x, y, texel, s[3] / 255.0f);
        }
}

// Bilinear coverage sample of a grayscale atlas (float pixel coords, clamped).
static inline float sampleAtlas(const uint8_t* a, int aw, int ah, float fx, float fy) {
    fx -= 0.5f; fy -= 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - x0, ty = fy - y0;
    auto at = [&](int x, int y) -> float {
        x = std::min(aw - 1, std::max(0, x)); y = std::min(ah - 1, std::max(0, y));
        return a[size_t(y) * aw + x];
    };
    float top = at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx;
    float bot = at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx;
    return (top * (1 - ty) + bot * ty) / 255.0f;
}

// Pick the mip level for a minification factor, and how far we are between it
// and the next - the CPU's version of what a GPU's LOD selection does.
//
// `texelsPerPixel` is 1/scale: how many level-0 texels one output pixel covers.
// A level halves the atlas, so log2 of that is the level whose texels are about
// pixel-sized, which is exactly the level that is not undersampled.
static inline void pickMip(const hudassets::FntFont& f, float texelsPerPixel,
                           int& lo, int& hi, float& t) {
    lo = hi = 0; t = 0.0f;
    const int last = int(f.mips.size()) - 1;
    if (last <= 0 || !(texelsPerPixel > 1.0f)) return;   // magnifying: level 0
    const float lod = std::log2(texelsPerPixel);
    const int base = int(std::floor(lod));
    lo = base < 0 ? 0 : (base > last ? last : base);
    hi = lo + 1 > last ? last : lo + 1;
    t = lod - float(base);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
}

// Draw from the game's own bitmap font. Metrics (scale rule, advance, justify)
// come from the SHARED layout in render_asset_decode.h so the D3D11 backend
// places every glyph identically; only the rasterization (the per-pixel
// bilinear atlas sampling) lives here.
//
// TRILINEAR, not bilinear-on-one-level. Blending the two neighbouring levels
// costs a second tap and removes the visible seam where a level changes - which
// on a HUD is not hypothetical, because scale tracks the user's size slider and
// the viewport, so a resize or a drag would otherwise step the text's weight.
void Renderer::drawStringFnt(Image& im, const SPluginString_t& s, hudassets::FntFont& f) {
    const char* text = s.m_szString;
    float scale = s.m_fSize * im.vpH() / float(f.cellH);

    float penX = im.mapX(s.m_afPos[0]);
    float total = hudassets::measureFnt(f, text, scale);
    if (s.m_iJustify == 1) penX -= total / 2; else if (s.m_iJustify == 2) penX -= total;
    float top = im.mapY(s.m_afPos[1]);
    Color col = abgr(s.m_ulColor);

    // One output pixel covers 1/scale level-0 texels. The shipped 135px cell
    // against ~20px HUD text puts this near 7, i.e. mip level ~2-3.
    int mipLo = 0, mipHi = 0; float mipT = 0.0f;
    pickMip(f, scale > 0.0f ? 1.0f / scale : 1.0f, mipLo, mipHi, mipT);
    const hudassets::MipLevel* mLo = f.mips.empty() ? nullptr : &f.mips[mipLo];
    const hudassets::MipLevel* mHi = f.mips.empty() ? nullptr : &f.mips[mipHi];

    hudassets::layoutFnt(f, text, penX, top, scale, [&](const hudassets::GlyphQuad& gq) {
        int X0 = (int)std::floor(gq.dx0), X1 = (int)std::ceil(gq.dx1);
        int Y0 = (int)std::floor(gq.dy0), Y1 = (int)std::ceil(gq.dy1);
        for (int y = Y0; y < Y1; ++y) {
            float sy = gq.ay0 + (y + 0.5f - gq.dy0) / scale;
            for (int x = X0; x < X1; ++x) {
                float sx = gq.ax0 + (x + 0.5f - gq.dx0) / scale;
                float cov;
                if (!mLo) {
                    cov = sampleAtlas(f.atlas.data(), f.aw, f.ah, sx, sy);
                } else {
                    // Atlas coords are in LEVEL 0 texels; a level n texel is 2^n
                    // of them, so the same point is sx * (levelW / aw). Using the
                    // level's own ratio rather than 1/2^n keeps it exact where a
                    // dimension rounded up on an odd halve.
                    const float rxL = float(mLo->w) / float(f.aw), ryL = float(mLo->h) / float(f.ah);
                    cov = sampleAtlas(mLo->px.data(), mLo->w, mLo->h, sx * rxL, sy * ryL);
                    if (mipHi != mipLo && mipT > 0.0f) {
                        const float rxH = float(mHi->w) / float(f.aw), ryH = float(mHi->h) / float(f.ah);
                        const float hi = sampleAtlas(mHi->px.data(), mHi->w, mHi->h, sx * rxH, sy * ryH);
                        cov = cov * (1.0f - mipT) + hi * mipT;
                    }
                }
                if (cov > 0.0f) blend(im, x, y, col, cov);
            }
        }
    });
}

void Renderer::drawString(Image& im, const SPluginString_t& s, const Frame& fr) {
    if (s.m_szString[0] == '\0') return;
    int idx = s.m_iFont - 1;
    if (idx < 0 || idx >= (int)fr.fontNames->size()) return;
    // Every font the game registers is a .fnt bitmap font (pixel-exact,
    // allocation-free). A missing/corrupt .fnt simply renders no text.
    if (hudassets::FntFont* bf = fnt((*fr.fontNames)[idx], fr.assetRoot)) drawStringFnt(im, s, *bf);
}

void Renderer::render(Image& out, const Frame& fr, uint8_t bgR, uint8_t bgG, uint8_t bgB,
                      uint8_t bgA) {
    out.fill(bgR, bgG, bgB, bgA);
    if (!fr.fontNames || !fr.spriteNames) return;
    for (int i = 0; i < fr.quadCount; ++i) drawQuad(out, fr.quads[i], fr);
    for (int i = 0; i < fr.stringCount; ++i) drawString(out, fr.strings[i], fr);
}

}  // namespace hudsw
