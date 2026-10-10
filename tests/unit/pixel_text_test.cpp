// ============================================================================
// tests/unit/pixel_text_test.cpp
// The block alphabet of mxbmrp3/core/pixel_text.h, which draws the "not
// installed correctly" warning when the plugin has no font to draw it with.
//
// That warning is only ever seen on a broken install, which no developer runs,
// so a wrong glyph or a run that spills out of its letter would ship unseen.
// These read each glyph back from the runs it emits, and check the one-quad-
// per-run merge that keeps the warning to a few hundred quads.
// ============================================================================
#include "doctest.h"

#include <vector>

#include "core/pixel_text.h"

namespace {

struct Run { int glyph, col, row, len; };

std::vector<Run> runs(const char* text) {
    std::vector<Run> out;
    PixelText::forEachRun(text, [&](int g, int c, int r, int l) { out.push_back({g, c, r, l}); });
    return out;
}

// Rebuild a glyph's bitmap from its runs, so the test reads the picture back.
std::vector<int> rowsOf(const char* text) {
    std::vector<int> rows(PixelText::GLYPH_H, 0);
    for (const Run& r : runs(text))
        for (int c = r.col; c < r.col + r.len; ++c) rows[static_cast<size_t>(r.row)] |= 0x10 >> c;
    return rows;
}

}  // namespace

TEST_CASE("pixel text: runs rebuild the glyph they came from") {
    // "T": a full top bar, then the stem down the middle column.
    const std::vector<int> t = rowsOf("T");
    CHECK(t[0] == 0x1F);
    for (int row = 1; row < PixelText::CAP_H; ++row) CHECK(t[static_cast<size_t>(row)] == 0x04);
    for (int row = PixelText::CAP_H; row < PixelText::GLYPH_H; ++row) CHECK(t[static_cast<size_t>(row)] == 0);
    // A full bar is ONE run, not five pixels.
    CHECK(runs("-").size() == 1);
    CHECK(runs("-")[0].len == 5);
}

TEST_CASE("pixel text: lowercase has its own glyphs, unknown characters draw as spaces") {
    CHECK(rowsOf("a") != rowsOf("A"));
    CHECK(rowsOf("o")[0] == 0);   // x-height letters leave the cap rows blank
    // Descenders go below the baseline, as the font's do; capitals never do.
    CHECK(rowsOf("p")[PixelText::GLYPH_H - 1] != 0);
    CHECK(rowsOf("P")[PixelText::CAP_H] == 0);
    CHECK(runs("~").empty());
    CHECK(runs(" ").empty());
}

TEST_CASE("pixel text: every run stays inside its own glyph") {
    // The caller spaces glyphs on the font's cell; a run that spilled past the
    // five columns would draw into the next letter.
    const std::vector<Run> all = runs("Mm Ww 0123456789");
    REQUIRE(!all.empty());
    for (const Run& r : all) {
        CHECK(r.col >= 0);
        CHECK(r.col + r.len <= PixelText::GLYPH_W);
        CHECK(r.row < PixelText::GLYPH_H);
    }
    // Glyph indices count characters, spaces included.
    CHECK(runs("I I").back().glyph == 2);
}

TEST_CASE("pixel text: every character the install warning uses has a glyph") {
    const char* used = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,:-_/\\!'()";
    for (const char* p = used; *p; ++p) {
        const char one[2] = {*p, '\0'};
        INFO("character " << *p);
        CHECK_FALSE(runs(one).empty());
    }
}
