// ============================================================================
// core/render_asset_decode.cpp  — see render_asset_decode.h
// Bodies extracted verbatim from hud_sw_renderer.cpp (the .fnt offsets and the
// TGA branch structure are unchanged); that file's golden-frame unit tests pin
// the extraction.
// ============================================================================
#include "render_asset_decode.h"

#include <algorithm>
#include <cstring>

#include "../vendor/miniz/miniz.h"

namespace hudassets {

// .fnt binary layout:
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
namespace {
constexpr int FNT_HEIGHT_OFF = 264;
constexpr int FNT_TABLE_OFF = 268;
constexpr int FNT_REC_STRIDE = 40;
constexpr int FNT_BITMAP_HDR = FNT_TABLE_OFF + 256 * FNT_REC_STRIDE;  // 10508
}  // namespace

// Build the complete mip chain for a decoded coverage atlas - see FntFont::mips
// for why this exists at all.
//
// A straight 2x2 box filter, on COVERAGE rather than colour, which is why no
// gamma handling appears here: the atlas byte is alpha, already linear, and
// averaging it is exactly averaging how much ink covers the texel. (Doing this
// to an sRGB colour texture without decoding first is the classic mip bug; it is
// simply not the situation here.)
//
// Dimensions round UP on the halve, so an odd axis still shrinks and the chain
// terminates at 1x1 rather than stalling on a 1xN. The extra source row/column
// an odd axis asks for is clamped to the last one.
void buildFntMips(FntFont& f) {
    f.mips.clear();
    if (!f.ok || f.aw <= 0 || f.ah <= 0) return;
    // MOVED, not copied. Level 0 IS the atlas, and a 2048^2 shipped font is 4 MB
    // of it - copied, that is 4 MB per font per BACKEND for a second copy nobody
    // reads. The invariant this creates is worth stating because it is not
    // obvious: `atlas` is non-empty exactly when `mips` is EMPTY. Every consumer
    // already branches that way, because each has an if-no-chain fallback that
    // reads `atlas` - and that branch is reachable only when this function
    // returned early, before the move.
    f.mips.push_back(MipLevel{ f.aw, f.ah, std::move(f.atlas) });
    while (f.mips.back().w > 1 || f.mips.back().h > 1) {
        const MipLevel& s = f.mips.back();
        MipLevel d;
        // FLOOR, not round-up. Both APIs define level i as max(1, floor(w >> i))
        // and CHECK it: a chain that disagrees is an incomplete GL texture (it
        // samples as white) and a failed D3D11 CreateTexture2D. This rounded UP
        // at first, reasoning only about the chain terminating - which it does
        // either way - and the unit test pinned the wrong rule with it. Shipped
        // art is power-of-two so nothing broke; a user's NPOT icon would have.
        d.w = s.w > 1 ? s.w / 2 : 1;
        d.h = s.h > 1 ? s.h / 2 : 1;
        d.px.assign(size_t(d.w) * d.h, 0);
        for (int y = 0; y < d.h; ++y) {
            const int sy0 = y * 2 < s.h ? y * 2 : s.h - 1;
            const int sy1 = y * 2 + 1 < s.h ? y * 2 + 1 : s.h - 1;
            for (int x = 0; x < d.w; ++x) {
                const int sx0 = x * 2 < s.w ? x * 2 : s.w - 1;
                const int sx1 = x * 2 + 1 < s.w ? x * 2 + 1 : s.w - 1;
                const unsigned sum =
                    unsigned(s.px[size_t(sy0) * s.w + sx0]) + s.px[size_t(sy0) * s.w + sx1] +
                    s.px[size_t(sy1) * s.w + sx0] + s.px[size_t(sy1) * s.w + sx1];
                d.px[size_t(y) * d.w + x] = static_cast<uint8_t>((sum + 2) / 4);
            }
        }
        f.mips.push_back(std::move(d));
    }
}

// See buildTexMips in the header for WHY this premultiplies, and for why icons
// get a chain and nine-slice panel art deliberately does not.
void buildTexMips(Texture& t) {
    t.mips.clear();
    if (!t.ok || t.w <= 0 || t.h <= 0) return;
    t.mips.push_back(MipLevel{ t.w, t.h, t.rgba });
    while (t.mips.back().w > 1 || t.mips.back().h > 1) {
        const MipLevel& s = t.mips.back();
        MipLevel d;
        d.w = s.w > 1 ? s.w / 2 : 1;      // floor; see buildFntMips
        d.h = s.h > 1 ? s.h / 2 : 1;
        d.px.assign(size_t(d.w) * d.h * 4, 0);
        for (int y = 0; y < d.h; ++y) {
            const int sy0 = y * 2 < s.h ? y * 2 : s.h - 1;
            const int sy1 = y * 2 + 1 < s.h ? y * 2 + 1 : s.h - 1;
            for (int x = 0; x < d.w; ++x) {
                const int sx0 = x * 2 < s.w ? x * 2 : s.w - 1;
                const int sx1 = x * 2 + 1 < s.w ? x * 2 + 1 : s.w - 1;
                const size_t o[4] = {
                    (size_t(sy0) * s.w + sx0) * 4, (size_t(sy0) * s.w + sx1) * 4,
                    (size_t(sy1) * s.w + sx0) * 4, (size_t(sy1) * s.w + sx1) * 4 };
                // Premultiplied sum: each colour weighted by its own alpha, so a
                // transparent texel contributes its transparency and nothing else.
                unsigned aSum = 0, cSum[3] = { 0, 0, 0 };
                for (size_t k = 0; k < 4; ++k) {
                    const unsigned a = s.px[o[k] + 3];
                    aSum += a;
                    for (int c = 0; c < 3; ++c) cSum[c] += unsigned(s.px[o[k] + c]) * a;
                }
                const size_t dst = (size_t(y) * d.w + x) * 4;
                // Divide the premultiplied colour back out by the summed alpha,
                // NOT by 4: that is what makes the result independent of how much
                // of the footprint was transparent. Fully transparent stays black,
                // which is the one case with no colour to recover.
                for (int c = 0; c < 3; ++c)
                    d.px[dst + c] = aSum ? static_cast<uint8_t>((cSum[c] + aSum / 2) / aSum) : 0;
                d.px[dst + 3] = static_cast<uint8_t>((aSum + 2) / 4);
            }
        }
        t.mips.push_back(std::move(d));
    }
}

FntFont decodeFnt(const std::vector<uint8_t>& d) {
    FntFont fo;
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
        // Cap the file-declared atlas dimensions: fonts live in user-overridable
        // asset dirs, and a corrupt header declaring e.g. 100000x100000 would
        // throw bad_alloc out of the render loop. Legit atlases top out at 2048².
        if (fo.aw > 0 && fo.ah > 0 && fo.aw <= kMaxTexDim && fo.ah <= kMaxTexDim &&
            ctype == 2 && fo.cellH > 0) {
            fo.atlas.assign(need, 0);
            const uint8_t* src = &d[FNT_BITMAP_HDR + 24];
            size_t srcLen = d.size() - (FNT_BITMAP_HDR + 24);
            // Raw DEFLATE (no zlib header) -> flags 0.
            size_t got = tinfl_decompress_mem_to_mem(fo.atlas.data(), need, src, srcLen, 0);
            if (got == need) { fo.ok = true; buildFntMips(fo); }
        }
    }
    return fo;
}

Texture decodeTga(const std::vector<uint8_t>& d) {
    Texture t;
    if (d.size() >= 18) {
        int idLen = d[0], imgType = d[2], bpp = d[16], desc = d[17];
        t.w = d[12] | (d[13] << 8); t.h = d[14] | (d[15] << 8);
        int bpx = bpp / 8;
        // Same rationale as the font-atlas cap: TGA dimensions are file-declared
        // (user-overridable assets), so bound them before allocating w*h*4.
        if ((imgType == 2 || imgType == 10) && (bpp == 24 || bpp == 32) &&
            t.w > 0 && t.h > 0 && t.w <= kMaxTexDim && t.h <= kMaxTexDim) {
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
    return t;
}

}  // namespace hudassets
