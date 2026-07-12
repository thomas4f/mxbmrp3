// ============================================================================
// core/hud_sw_renderer.cpp  — see hud_sw_renderer.h
// ============================================================================
#include "hud_sw_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "../vendor/miniz/miniz.h"

namespace hudsw {
namespace {

// PiBoSo .fnt binary layout (reverse-engineered, confirmed with PiBoSo's fontgen 1.02):
//   [0]   "FNT\0" magic
//   [4]   font name, null-terminated (buffer runs to 264)
//   [264] int32 cell/line height in px
//   [268] 256 glyph records x 40 bytes, indexed absolutely by codepoint 0..255
//         each: int32[10] = { valid, xoffset, width, rightBearing,
//                             atlasX0, atlasX1, atlasY0, atlasY1, 0, 0 }
//         advance = xoffset + width + rightBearing
//   [10508] int32 0
//   [10512] int32 bitmap width
//   [10516] int32 bitmap height
//   [10520] int32 payload byte count
//   [10524] int32 compression type (2 = raw DEFLATE)
//   [10528] int32 0
//   [10532] raw DEFLATE stream -> width*height 8-bit grayscale atlas
constexpr int FNT_HEIGHT_OFF = 264;
constexpr int FNT_TABLE_OFF = 268;
constexpr int FNT_REC_STRIDE = 40;
constexpr int FNT_BITMAP_HDR = FNT_TABLE_OFF + 256 * FNT_REC_STRIDE;  // 10508

struct Color { uint8_t r, g, b, a; };
inline Color abgr(unsigned long c) {
    uint32_t v = static_cast<uint32_t>(c);
    return { uint8_t(v & 255), uint8_t((v >> 8) & 255), uint8_t((v >> 16) & 255), uint8_t((v >> 24) & 255) };
}

inline void blend(Image& im, int x, int y, Color c, float cov) {
    if (x < 0 || y < 0 || x >= im.w || y >= im.h) return;
    float a = (c.a / 255.0f) * cov;
    if (a <= 0.0f) return;
    uint8_t* p = &im.px[(size_t(y) * im.w + x) * 4];
    p[0] = uint8_t(c.r * a + p[0] * (1 - a));
    p[1] = uint8_t(c.g * a + p[1] * (1 - a));
    p[2] = uint8_t(c.b * a + p[2] * (1 - a));
    p[3] = uint8_t(std::min(255.0f, c.a * a + p[3] * (1 - a)));
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
    for (size_t i = 0; i < px.size(); i += 4) { px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = a; }
}

Renderer::FntFont* Renderer::fnt(const std::string& base, const std::string& root) {
    auto it = m_fnts.find(base);
    if (it != m_fnts.end()) return it->second.ok ? &it->second : nullptr;
    FntFont fo;
    std::ifstream in(root + "/fonts/" + base + ".fnt", std::ios::binary);
    if (in) {
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto i32 = [&](size_t o) -> int {
            int32_t v; std::memcpy(&v, &d[o], 4); return v;
        };
        if (d.size() > size_t(FNT_BITMAP_HDR) + 24 && std::memcmp(d.data(), "FNT\0", 4) == 0) {
            fo.cellH = i32(FNT_HEIGHT_OFF);
            for (int cp = 0; cp < 256; ++cp) {
                size_t o = FNT_TABLE_OFF + size_t(cp) * FNT_REC_STRIDE;
                FntGlyph& g = fo.glyphs[cp];
                g.valid = i32(o) != 0;
                g.xoff = i32(o + 4);
                int width = i32(o + 8), rb = i32(o + 12);
                g.adv = g.xoff + width + rb;
                g.x0 = i32(o + 16); g.x1 = i32(o + 20);
                g.y0 = i32(o + 24); g.y1 = i32(o + 28);
            }
            fo.aw = i32(FNT_BITMAP_HDR + 4);
            fo.ah = i32(FNT_BITMAP_HDR + 8);
            int ctype = i32(FNT_BITMAP_HDR + 16);
            size_t need = size_t(fo.aw) * fo.ah;
            if (fo.aw > 0 && fo.ah > 0 && ctype == 2 && fo.cellH > 0) {
                fo.atlas.assign(need, 0);
                const uint8_t* src = &d[FNT_BITMAP_HDR + 24];
                size_t srcLen = d.size() - (FNT_BITMAP_HDR + 24);
                // Raw DEFLATE (no zlib header) -> flags 0.
                size_t got = tinfl_decompress_mem_to_mem(fo.atlas.data(), need, src, srcLen, 0);
                if (got == need) fo.ok = true;
            }
        }
    }
    auto& ref = m_fnts.emplace(base, std::move(fo)).first->second;
    return ref.ok ? &ref : nullptr;
}

Renderer::Tex* Renderer::tex(const std::string& base, bool icon, const std::string& root) {
    auto it = m_texs.find(base);
    if (it != m_texs.end()) return it->second.ok ? &it->second : nullptr;
    Tex t;
    std::ifstream f(root + (icon ? "/icons/" : "/textures/") + base + ".tga", std::ios::binary);
    if (f) {
        std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (d.size() >= 18) {
            int idLen = d[0], imgType = d[2], bpp = d[16], desc = d[17];
            t.w = d[12] | (d[13] << 8); t.h = d[14] | (d[15] << 8);
            int bpx = bpp / 8;
            if ((imgType == 2 || imgType == 10) && (bpp == 24 || bpp == 32) && t.w > 0 && t.h > 0) {
                size_t o = 18 + idLen, px = size_t(t.w) * t.h;
                t.rgba.assign(px * 4, 0);
                auto put = [&](size_t i, const uint8_t* s) { t.rgba[i] = s[2]; t.rgba[i + 1] = s[1]; t.rgba[i + 2] = s[0]; t.rgba[i + 3] = bpx == 4 ? s[3] : 255; };
                if (imgType == 2) { for (size_t p = 0; p < px && o + bpx <= d.size(); ++p, o += bpx) put(p * 4, &d[o]); }
                else {
                    size_t p = 0;
                    while (p < px && o < d.size()) {
                        int hdr = d[o++]; int cnt = (hdr & 0x7f) + 1;
                        if (hdr & 0x80) { if (o + bpx > d.size()) break; for (int k = 0; k < cnt && p < px; ++k, ++p) put(p * 4, &d[o]); o += bpx; }
                        else { for (int k = 0; k < cnt && p < px && o + bpx <= d.size(); ++k, ++p, o += bpx) put(p * 4, &d[o]); }
                    }
                }
                if (!(desc & 0x20))  // bottom-origin -> flip
                    for (int y = 0; y < t.h / 2; ++y)
                        std::swap_ranges(&t.rgba[size_t(y) * t.w * 4], &t.rgba[size_t(y) * t.w * 4 + t.w * 4], &t.rgba[size_t(t.h - 1 - y) * t.w * 4]);
                t.ok = true;
            }
        }
    }
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
    Tex* t = tex(names[idx], icon, fr.assetRoot);
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

// Draw from the game's own bitmap font. The atlas cell height maps 1:1 to the
// string's normalized size, so scale = size*imgH / cellH gives the exact on-screen
// metrics the game uses (advance ratio already matches MONOSPACE_CHAR_WIDTH_RATIO).
void Renderer::drawStringFnt(Image& im, const SPluginString_t& s, const Frame& fr, FntFont& f) {
    const char* text = s.m_szString;
    float scale = s.m_fSize * im.vpH() / float(f.cellH);

    float total = 0;
    for (const char* c = text; *c; ++c) total += f.glyphs[(unsigned char)*c].adv * scale;
    float penX = im.mapX(s.m_afPos[0]);
    if (s.m_iJustify == 1) penX -= total / 2; else if (s.m_iJustify == 2) penX -= total;
    float top = im.mapY(s.m_afPos[1]);
    Color col = abgr(s.m_ulColor);

    for (const char* c = text; *c; ++c) {
        const FntGlyph& g = f.glyphs[(unsigned char)*c];
        int gw = g.x1 - g.x0, gh = g.y1 - g.y0;
        if (g.valid && gw > 0 && gh > 0) {
            float dx0 = penX + g.xoff * scale;
            int X0 = (int)std::floor(dx0), X1 = (int)std::ceil(dx0 + gw * scale);
            int Y0 = (int)std::floor(top), Y1 = (int)std::ceil(top + gh * scale);
            for (int y = Y0; y < Y1; ++y) {
                float sy = g.y0 + (y + 0.5f - top) / scale;
                for (int x = X0; x < X1; ++x) {
                    float sx = g.x0 + (x + 0.5f - dx0) / scale;
                    float cov = sampleAtlas(f.atlas.data(), f.aw, f.ah, sx, sy);
                    if (cov > 0.0f) blend(im, x, y, col, cov);
                }
            }
        }
        penX += g.adv * scale;
    }
}

void Renderer::drawString(Image& im, const SPluginString_t& s, const Frame& fr) {
    if (s.m_szString[0] == '\0') return;
    int idx = s.m_iFont - 1;
    if (idx < 0 || idx >= (int)fr.fontNames->size()) return;
    // Every font the game registers is a .fnt bitmap font (pixel-exact,
    // allocation-free). A missing/corrupt .fnt simply renders no text.
    if (FntFont* bf = fnt((*fr.fontNames)[idx], fr.assetRoot)) drawStringFnt(im, s, fr, *bf);
}

void Renderer::render(Image& out, const Frame& fr, uint8_t bgR, uint8_t bgG, uint8_t bgB) {
    out.fill(bgR, bgG, bgB, 255);
    if (!fr.fontNames || !fr.spriteNames) return;
    for (int i = 0; i < fr.quadCount; ++i) drawQuad(out, fr.quads[i], fr);
    for (int i = 0; i < fr.stringCount; ++i) drawString(out, fr.strings[i], fr);
}

}  // namespace hudsw
