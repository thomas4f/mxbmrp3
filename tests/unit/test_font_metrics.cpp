// ============================================================================
// tests/unit/test_font_metrics.cpp
// The two numbers in LayoutMetrics that are MEASUREMENTS of the shipped bitmap
// fonts rather than style choices, checked against the fonts themselves:
//
//   charWidthRatio  — one character's advance, as a fraction of the font size
//   inkCenterRatio  — where the cap/digit ink band sits inside the glyph cell
//
// Both used to be eyeballed off a screenshot, and both were wrong in the same
// direction the eye is worst at: 0.16/0.46 put the ink band's centre at 0.39 of
// the cell when mxbmrp3_fontgen normalises every shipped font to centre it at
// 0.50, so every value placed by inkCenteredY sat 0.11 font sizes LOW. At the
// stock air terms that reads as "a bit off"; with the box model's air set to 0
// it is a widget's value hanging out of the bottom of its own panel.
//
// A screenshot cannot tell you that — it can only tell you it looks wrong once
// somebody notices. The atlas can, so this asks the atlas.
//
// It parses the .fnt directly instead of going through Renderer::fnt(), whose
// glyph table is private; the layout it walks is the one documented at the top
// of core/hud_sw_renderer.cpp, and a change there breaks this loudly.
// ============================================================================
#include "doctest.h"

#include "core/layout_metrics.h"
#include "core/plugin_constants.h"
#include "vendor/miniz/miniz.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kTableOff = 268;
constexpr int kRecStride = 40;
constexpr int kBitmapHdr = kTableOff + 256 * kRecStride;

struct Glyph { int valid, xoff, w, rb, x0, x1, y0, y1; int adv() const { return xoff + w + rb; } };

struct Font {
    bool ok = false;
    int cellH = 0, aw = 0, ah = 0;
    Glyph g[256]{};
    std::vector<uint8_t> atlas;
};

Font loadFnt(const std::string& path) {
    Font f;
    std::ifstream in(path, std::ios::binary);
    if (!in) return f;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    if (d.size() < size_t(kBitmapHdr) + 24 || std::memcmp(d.data(), "FNT\0", 4) != 0) return f;
    auto i32 = [&](size_t o) { int32_t v; std::memcpy(&v, &d[o], 4); return int(v); };
    f.cellH = i32(264);
    for (int cp = 0; cp < 256; ++cp) {
        const size_t o = size_t(kTableOff) + size_t(cp) * kRecStride;
        f.g[cp] = { i32(o), i32(o + 4), i32(o + 8), i32(o + 12),
                    i32(o + 16), i32(o + 20), i32(o + 24), i32(o + 28) };
    }
    f.aw = i32(kBitmapHdr + 4);
    f.ah = i32(kBitmapHdr + 8);
    const size_t need = size_t(f.aw) * size_t(f.ah);
    if (f.cellH <= 0 || f.aw <= 0 || f.ah <= 0 || i32(kBitmapHdr + 16) != 2) return f;
    f.atlas.assign(need, 0);
    const size_t got = tinfl_decompress_mem_to_mem(
        f.atlas.data(), need, &d[kBitmapHdr + 24], d.size() - (kBitmapHdr + 24), 0);
    f.ok = (got == need);
    return f;
}

// The ink band of `text`, as fractions of the cell: where the topmost lit row
// of any of its glyphs falls, and the bottommost. Every glyph's box is the full
// cell (the renderer maps y0 to the string's y), so the offsets are comparable
// across glyphs without any baseline arithmetic.
struct Band { double top = 0.0, bot = 0.0; bool any = false;
              double centre() const { return (top + bot) * 0.5; }
              double height() const { return bot - top; } };

Band inkBand(const Font& f, const std::string& text) {
    Band b;
    int lo = f.cellH, hi = -1;
    for (unsigned char ch : text) {
        const Glyph& gl = f.g[ch];
        if (!gl.valid || gl.x1 <= gl.x0 || gl.y1 <= gl.y0) continue;
        for (int y = gl.y0; y < gl.y1; ++y) {
            bool lit = false;
            for (int x = gl.x0; x < gl.x1 && !lit; ++x) lit = f.atlas[size_t(y) * f.aw + x] != 0;
            if (!lit) continue;
            if (y - gl.y0 < lo) lo = y - gl.y0;
            if (y - gl.y0 > hi) hi = y - gl.y0;
        }
    }
    if (hi < 0) return b;
    b.any = true;
    b.top = double(lo) / f.cellH;
    b.bot = double(hi + 1) / f.cellH;
    return b;
}

std::vector<std::filesystem::path> shippedFonts() {
    std::vector<std::filesystem::path> out;
    for (const auto& e : std::filesystem::directory_iterator(
             std::filesystem::path(MXB_DATA_DIR) / "fonts")) {
        if (e.path().extension() == ".fnt") out.push_back(e.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("font metrics: every shipped atlas centres its digit band in the cell") {
    const auto fonts = shippedFonts();
    REQUIRE_MESSAGE(!fonts.empty(), "no .fnt found under " << MXB_DATA_DIR << "/fonts");
    const double expected = LayoutMetrics{}.inkCenterRatio;
    for (const auto& p : fonts) {
        CAPTURE(p.filename().string());
        const Font f = loadFnt(p.string());
        REQUIRE(f.ok);
        const Band d = inkBand(f, "0123456789");
        REQUIRE(d.any);
        // A 135-row cell cannot place a band's centre more finely than half a
        // row, and a stem's antialiased tip costs another; 0.01 of the cell is
        // ~1.4 rows, tight enough that the old 0.39 misses it by eleven times
        // over and loose enough not to chase rasteriser noise.
        CHECK(std::abs(d.centre() - expected) < 0.01);
    }
}

TEST_CASE("font metrics: a digit advances the width the layout reserves for it") {
    // charWidthRatio is a NORMALIZED-X figure and a .fnt advance is a fraction
    // of the CELL, i.e. of height — so the comparison only holds through the
    // aspect ratio the renderer divides by (see Renderer::drawStringFnt).
    const double expected = LayoutMetrics{}.charWidthRatio;
    for (const auto& p : shippedFonts()) {
        CAPTURE(p.filename().string());
        const Font f = loadFnt(p.string());
        REQUIRE(f.ok);
        double adv = 0.0;
        for (char c = '0'; c <= '9'; ++c) adv += f.g[unsigned(c)].adv();
        const double ratio = (adv / 10.0 / f.cellH) / PluginConstants::UI_ASPECT_RATIO;
        // EVERY shipped font, with no exception — which is the point. There
        // used to be one: RobotoMono-Regular was left as raw fontgen output so
        // tools/fontgen/test.sh had a genuine PiBoSo artefact to check
        // against, and it is also the DEFAULT for Normal and Digits. So the
        // single un-normalised face was the one most users read, advancing
        // 0.2833 against the 0.275 reserved for it: ~3% per character, enough
        // that a long line (the spotter's subtitle) overran its card and that
        // Normal and Strong did not measure alike in one column. It is
        // normalised now and test.sh's baseline moved to a fixture, so this
        // can be the flat assertion it always should have been.
        CHECK(ratio == doctest::Approx(expected).epsilon(0.01));
    }
}
