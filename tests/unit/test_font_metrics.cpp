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
// It also pins the MEASURE the plugin cuts text with (hudassets::
// decodeFntAdvances + fitFntChars, behind AssetManager::fitChars): counting a
// grid cell a character cut a Now Playing title in the Title font to 18 where
// about 25 fit.
//
// It parses the .fnt directly instead of going through Renderer::fnt(), whose
// glyph table is private; the layout it walks is the one documented at the top
// of core/hud_sw_renderer.cpp, and a change there breaks this loudly.
// ============================================================================
#include "doctest.h"

#include "core/font_codepage.h"
#include "core/layout_metrics.h"
#include "core/plugin_constants.h"
#include "core/render_asset_decode.h"
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

TEST_CASE("font metrics: every shipped font's digits are fixed-width") {
    // mxbmrp3_fontgen's `tabular_digits` (on under normalisation) gives 0-9 the
    // widest digit's advance. Display faces like Tomorrow and Chakra Petch ship
    // proportional digits with no `tnum` feature, so without it a running clock
    // jiggled as its digits changed ("1:11.111" is ~a fifth narrower than
    // "8:08.888") and right-aligned numbers did not line up in a column.
    for (const auto& p : shippedFonts()) {
        CAPTURE(p.filename().string());
        const Font f = loadFnt(p.string());
        REQUIRE(f.ok);
        const int zero = f.g[unsigned('0')].adv();
        for (char c = '1'; c <= '9'; ++c) {
            CAPTURE(c);
            CHECK(f.g[unsigned(c)].adv() == zero);
        }
    }
}

TEST_CASE("font metrics: text is cut by its own font's glyphs, the grid's cell for digits") {
    const double cell = LayoutMetrics{}.charWidthRatio;
    const float size = 0.03f;   // the Notices / Dash caption size
    const float dashCaption = static_cast<float>(18 * cell) * size;   // 18 cells, the Dash notice's box
    for (const auto& p : shippedFonts()) {
        CAPTURE(p.filename().string());
        std::ifstream in(p, std::ios::binary);
        std::vector<uint8_t> table(hudassets::FNT_TABLE_BYTES);
        REQUIRE(in.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size())));
        float adv[256];
        REQUIRE(hudassets::decodeFntAdvances(table.data(), table.size(), adv));
        for (float& a : adv) a /= PluginConstants::UI_ASPECT_RATIO;   // as AssetManager stores it
        // Digits advance one cell each (to 1%, above), so 18 and a half cells
        // hold 18 digits in every font.
        const float digitBox = static_cast<float>(18.5 * cell) * size;
        CHECK(hudassets::fitFntChars(adv, "012345678901234567890123", digitBox, size) == 18);
        // A title: the count the font's own advances give, from this file's parser.
        const Font f = loadFnt(p.string());
        REQUIRE(f.ok);
        const char* title = "Golem II: The Bionic Vapour Boy";
        double x = 0.0;
        int fits = 0;
        for (const char* c = title; *c; ++c) {
            x += double(f.g[static_cast<unsigned char>(*c)].adv()) / f.cellH / PluginConstants::UI_ASPECT_RATIO * size;
            if (x > dashCaption * 1.0001) break;
            ++fits;
        }
        CHECK(hudassets::fitFntChars(adv, title, dashCaption, size) == fits);
    }
}

TEST_CASE("font metrics: the Title font holds more of a title than a cell a character") {
    std::ifstream in(std::filesystem::path(MXB_DATA_DIR) / "fonts" / "EnterSansman-Italic.fnt", std::ios::binary);
    std::vector<uint8_t> table(hudassets::FNT_TABLE_BYTES);
    REQUIRE(in.read(reinterpret_cast<char*>(table.data()), static_cast<std::streamsize>(table.size())));
    float adv[256];
    REQUIRE(hudassets::decodeFntAdvances(table.data(), table.size(), adv));
    for (float& a : adv) a /= PluginConstants::UI_ASPECT_RATIO;
    const float size = 0.03f;
    const float box = static_cast<float>(18 * LayoutMetrics{}.charWidthRatio) * size;
    const int fits = hudassets::fitFntChars(adv, "Golem II: The Bionic Vapour Boy", box, size);
    CHECK(fits > 22);   // 25 today; the grid's count is 18
    CHECK(fits < 31);   // and not the whole title

    // Not a .fnt: refused, and the table left as it was.
    float untouched[256] = { 1.0f };
    const uint8_t junk[16] = { 'N', 'O', 'P', 'E' };
    CHECK_FALSE(hudassets::decodeFntAdvances(junk, sizeof(junk), untouched));
    CHECK_FALSE(hudassets::decodeFntAdvances(table.data(), 100, untouched));
    CHECK(untouched[0] == 1.0f);
}

// The Turkish slots (core/font_codepage.h): CP1252's six unused bytes carry
// Ğ ğ İ ı Ş ş. Before they were drawn, every shipped font held a blank glyph
// there, so a Turkish translation rendered with holes. A font that inks the
// slot's plain letter has to ink the slot too -- its own letter, else the plain
// letter with the font's accent mark, else the plain letter (fontgen).
TEST_CASE("font metrics: every shipped font inks the Turkish slots wherever it inks their plain letter") {
    const auto fonts = shippedFonts();
    REQUIRE(!fonts.empty());
    for (const auto& p : fonts) {
        CAPTURE(p.filename().string());
        const Font f = loadFnt(p.string());
        REQUIRE(f.ok);
        for (const FontCodePage::ExtraSlot& slot : FontCodePage::kExtraSlots) {
            CAPTURE(int(slot.byte));
            const bool plainInked = inkBand(f, std::string(1, slot.plain)).any;
            if (!plainInked) continue;   // the LCD faces have no lowercase
            CHECK(inkBand(f, std::string(1, static_cast<char>(slot.byte))).any);
            CHECK(f.g[slot.byte].adv() > 0);
        }
    }
}

// Dotless ı is a different letter from i in Turkish (sınır, sinir), so a font
// without its own ı must not stand a dotted i in for it: fontgen cuts the dot
// off the font's i. Enter Sansman, the default Title font, is the shipped face
// that needs it. Any font whose i carries a detached dot must draw slot ı with
// no ink above the top of its x.
TEST_CASE("font metrics: the dotless i slot has no dot in any shipped font") {
    const unsigned char dotless = FontCodePage::slotForCodepoint(0x0131)->byte;
    int checked = 0;
    for (const auto& p : shippedFonts()) {
        CAPTURE(p.filename().string());
        const Font f = loadFnt(p.string());
        REQUIRE(f.ok);
        auto litRows = [&](unsigned char ch) {
            std::vector<bool> lit(size_t(f.cellH), false);
            const Glyph& gl = f.g[ch];
            for (int y = gl.y0; gl.x1 > gl.x0 && y < gl.y1; ++y)
                for (int x = gl.x0; x < gl.x1; ++x)
                    if (f.atlas[size_t(y) * f.aw + x] != 0) { lit[size_t(y - gl.y0)] = true; break; }
            return lit;
        };
        auto firstLit = [](const std::vector<bool>& lit) {
            for (size_t r = 0; r < lit.size(); ++r) if (lit[r]) return int(r);
            return -1;
        };
        const std::vector<bool> i = litRows('i');
        const int xTop = firstLit(litRows('x'));
        const int iTop = firstLit(i);
        if (xTop < 0 || iTop < 0 || iTop >= xTop) continue;
        bool detached = false;
        for (int r = iTop; r < xTop - 1; ++r) detached = detached || !i[size_t(r)];
        if (!detached) continue;   // a capital-style i (the LCD faces): nothing to cut
        ++checked;
        CHECK(firstLit(litRows(dotless)) >= xTop - 2);
    }
    CHECK(checked > 10);
}
