// ============================================================================
// tests/unit/test_hud_sw_renderer.cpp
// Golden-frame tests for the companion window's in-process software renderer
// (core/hud_sw_renderer.{h,cpp}) — pure C++ (miniz + fstream, no Win32), so it
// compiles and runs natively. Asserts sampled pixels/properties, never a
// full-frame hash (a rounding tweak shouldn't fail the suite).
//
// Invariants pinned (each one a way the companion could silently diverge from
// the in-game HUD):
//  1. A solid opaque quad fills exactly its interior with the quad color.
//  2. Per-quad ALPHA blends over the background (the HUD opacity slider).
//  3. The texture stage is texel × quad color: a WHITE icon takes the quad
//     color, and the quad alpha fades textures too — CLAUDE.md explicitly
//     warns "don't simplify the blit back to a plain copy or icons stop
//     tinting / ignore opacity"; this is that guard.
//  4. .fnt bitmap text renders real glyph coverage in the expected region and
//     modulates by the string color (also proves the .fnt parse + DEFLATE
//     atlas decode against a real shipped font).
//  5. setViewport maps normalized [0,1] into the centered sub-rect while
//     coords outside [0,1] still land in the surrounding window area (a
//     scale viewport, NOT a letterbox clip).
//
// Uses the real shipped assets (mxbmrp3_data/fonts + icons), located relative
// to this file so the test runs from any cwd.
// ============================================================================
#include "doctest.h"

#include "core/hud_sw_renderer.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// mxbmrp3_data lives at the repo root; derive it from this file's own path so
// the binary is cwd-independent (run_tests.sh compiles with an absolute path).
std::string assetRoot() {
    std::string f = __FILE__;
    size_t cut = f.find("/tests/unit/");
    REQUIRE(cut != std::string::npos);
    return f.substr(0, cut) + "/mxbmrp3_data";
}

struct Px { int r, g, b, a; };
Px at(const hudsw::Image& im, int x, int y) {
    const uint8_t* p = &im.px[(size_t(y) * im.w + x) * 4];
    return { p[0], p[1], p[2], p[3] };
}
bool near8(int got, int want, int tol = 2) { return std::abs(got - want) <= tol; }

// The game's ABGR color packing (SPluginQuad_t / SPluginString_t m_ulColor).
unsigned long abgr(int r, int g, int b, int a = 255) {
    return static_cast<unsigned long>((uint32_t(a) << 24) | (uint32_t(b) << 16) |
                                      (uint32_t(g) << 8) | uint32_t(r));
}

// Axis-aligned quad in normalized coords, corners in the game's TL,BL,BR,TR
// order (the order the sprite blit derives its U/V basis from).
SPluginQuad_t quad(float x0, float y0, float x1, float y1, unsigned long color, int sprite = 0) {
    SPluginQuad_t q{};
    q.m_aafPos[0][0] = x0; q.m_aafPos[0][1] = y0;   // TL
    q.m_aafPos[1][0] = x0; q.m_aafPos[1][1] = y1;   // BL
    q.m_aafPos[2][0] = x1; q.m_aafPos[2][1] = y1;   // BR
    q.m_aafPos[3][0] = x1; q.m_aafPos[3][1] = y0;   // TR
    q.m_iSprite = sprite;
    q.m_ulColor = color;
    return q;
}

// A frame with the tables the renderer needs; keeps the name vectors alive.
struct TestFrame {
    std::vector<std::string> fonts;
    std::vector<std::string> sprites;
    std::vector<SPluginQuad_t> quads;
    std::vector<SPluginString_t> strings;
    hudsw::Frame frame;
    hudsw::Frame& build() {
        frame.fontNames = &fonts;
        frame.spriteNames = &sprites;
        frame.quads = quads.data();     frame.quadCount = (int)quads.size();
        frame.strings = strings.data(); frame.stringCount = (int)strings.size();
        frame.assetRoot = assetRoot();
        return frame;
    }
};

}  // namespace

TEST_CASE("hud_sw_renderer: opaque quad fills its interior exactly, background outside") {
    hudsw::Image im; im.resize(320, 180);
    TestFrame tf;
    tf.quads.push_back(quad(0.25f, 0.25f, 0.75f, 0.75f, abgr(200, 60, 20)));
    hudsw::Renderer r;
    r.render(im, tf.build(), 10, 20, 30);

    // Interior (center + just-inside corners): opaque coverage-1 blend must land
    // the quad color exactly — the companion may not "tint" a plain fill.
    // Quad edges in pixels: x 80..240, y 45..135.
    for (auto [x, y] : { std::pair<int,int>{160, 90}, {84, 49}, {236, 49}, {84, 131}, {236, 131} }) {
        Px p = at(im, x, y);
        CHECK(p.r == 200); CHECK(p.g == 60); CHECK(p.b == 20); CHECK(p.a == 255);
    }
    // Just outside every edge: untouched backdrop.
    for (auto [x, y] : { std::pair<int,int>{74, 90}, {246, 90}, {160, 39}, {160, 141}, {5, 5} }) {
        Px p = at(im, x, y);
        CHECK(p.r == 10); CHECK(p.g == 20); CHECK(p.b == 30);
    }
}

TEST_CASE("hud_sw_renderer: per-quad alpha blends ~50/50 over the background") {
    hudsw::Image im; im.resize(320, 180);
    TestFrame tf;
    // Half-alpha quad over a mid-gray backdrop — the HUD opacity path.
    tf.quads.push_back(quad(0.25f, 0.25f, 0.75f, 0.75f, abgr(200, 40, 240, 128)));
    hudsw::Renderer r;
    r.render(im, tf.build(), 100, 100, 100);

    // a = 128/255: expect c*a + bg*(1-a) per channel, ±2/255 for rounding.
    Px p = at(im, 160, 90);
    CHECK(near8(p.r, 150));
    CHECK(near8(p.g, 70));
    CHECK(near8(p.b, 170));
}

TEST_CASE("hud_sw_renderer: white icon is COLORIZED by the quad color and honors quad alpha") {
    const std::string iconPath = assetRoot() + "/icons/circle.tga";
    REQUIRE_MESSAGE(std::ifstream(iconPath).good(), "shipped icon missing: ", iconPath);

    hudsw::Image im; im.resize(320, 180);
    TestFrame tf;
    tf.sprites = { "circle" };          // sprite 1
    tf.frame.firstIcon = 1;             // ...resolved as an icon (icons/ + tint)
    // circle.tga is a white filled circle on transparent — the canonical
    // white-icon case the modulate stage exists for.
    tf.quads.push_back(quad(0.25f, 0.25f, 0.75f, 0.75f, abgr(255, 0, 0), /*sprite=*/1));
    hudsw::Renderer r;
    r.render(im, tf.build(), 0, 0, 0);

    // Center texel is white+opaque -> must come out PURE RED (texel × color).
    // A plain-copy blit regression would leave it white.
    Px c = at(im, 160, 90);
    CHECK(c.r == 255); CHECK(c.g == 0); CHECK(c.b == 0);
    // Quad interior corner maps to a transparent texel -> backdrop preserved
    // (texture alpha still gates coverage after the modulate).
    Px t = at(im, 82, 47);
    CHECK(t.r == 0); CHECK(t.g == 0); CHECK(t.b == 0);

    // Half-alpha WHITE quad over black: the quad alpha (HUD opacity) must fade
    // the texture too — ~50% gray at the icon center, not full white.
    hudsw::Image im2; im2.resize(320, 180);
    TestFrame tf2;
    tf2.sprites = { "circle" };
    tf2.frame.firstIcon = 1;
    tf2.quads.push_back(quad(0.25f, 0.25f, 0.75f, 0.75f, abgr(255, 255, 255, 128), 1));
    r.render(im2, tf2.build(), 0, 0, 0);
    Px h = at(im2, 160, 90);
    CHECK(near8(h.r, 128));
    CHECK(near8(h.g, 128));
    CHECK(near8(h.b, 128));
}

TEST_CASE("hud_sw_renderer: .fnt text renders glyph coverage in the expected region, in the string color") {
    const std::string fontPath = assetRoot() + "/fonts/RobotoMono-Regular.fnt";
    REQUIRE_MESSAGE(std::ifstream(fontPath).good(), "shipped font missing: ", fontPath);

    // Count pixels in the image whose dominant channel matches `want` (r/g).
    auto countColored = [](const hudsw::Image& im, int x0, int y0, int x1, int y1, char want) {
        int n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                Px p = at(im, x, y);
                if (want == 'r' && p.r > 180 && p.g < 60 && p.b < 60) ++n;
                if (want == 'g' && p.g > 180 && p.r < 60 && p.b < 60) ++n;
            }
        return n;
    };

    auto drawText = [&](unsigned long color) {
        hudsw::Image im; im.resize(640, 360);
        TestFrame tf;
        tf.fonts = { "RobotoMono-Regular" };   // font 1
        SPluginString_t s{};
        std::strcpy(s.m_szString, "888");
        s.m_afPos[0] = 0.1f; s.m_afPos[1] = 0.2f;   // pen (64, 72) px
        s.m_iFont = 1;
        s.m_fSize = 0.2f;                            // 72 px line height
        s.m_iJustify = 0;
        s.m_ulColor = color;
        tf.strings.push_back(s);
        hudsw::Renderer r;
        r.render(im, tf.build(), 0, 0, 0);
        return im;
    };

    // Red "888": glyphs land in roughly [64, 64+3*advance] x [72, 144]
    // (advance ≈ 0.489 * 72 px for the normalized shipped fonts). Substantial
    // coverage inside a padded box; NOTHING outside it (pen/justify/scale drift
    // would leak pixels out).
    hudsw::Image red = drawText(abgr(255, 0, 0));
    int inBox = countColored(red, 56, 64, 190, 152, 'r');
    CHECK(inBox > 50);
    CHECK(countColored(red, 200, 0, 640, 360, 'r') == 0);   // right of the text
    CHECK(countColored(red, 0, 160, 200, 360, 'r') == 0);   // below the text
    CHECK(countColored(red, 0, 0, 200, 64, 'r') == 0);      // above the text

    // Same string in green: text color MODULATES the coverage (not baked white),
    // with comparable coverage and zero red survivors.
    hudsw::Image green = drawText(abgr(0, 255, 0));
    int greenBox = countColored(green, 56, 64, 190, 152, 'g');
    CHECK(greenBox > 50);
    CHECK(near8(greenBox, inBox, inBox / 4 + 4));   // same glyphs, same footprint (±25%)
    CHECK(countColored(green, 56, 64, 190, 152, 'r') == 0);
}

TEST_CASE("hud_sw_renderer: setViewport maps [0,1] into the sub-rect; outside-[0,1] uses the surrounding window") {
    // A 400x240 window with a centered-ish 320x180 (16:9) scale viewport at
    // (40, 30) — the companion's no-distortion mapping.
    hudsw::Image im; im.resize(400, 240);
    im.setViewport(40.0f, 30.0f, 320.0f, 180.0f);
    TestFrame tf;
    // In-viewport quad: normalized (0,0)-(0.5,0.5) -> pixels [40,200] x [30,120].
    tf.quads.push_back(quad(0.0f, 0.0f, 0.5f, 0.5f, abgr(0, 200, 0)));
    // Outside-[0,1] quad (x < 0): must land LEFT of the viewport, inside the
    // buffer — pixels [8,40] x [120,165] — not be clipped to a letterbox.
    tf.quads.push_back(quad(-0.1f, 0.5f, 0.0f, 0.75f, abgr(0, 0, 250)));
    hudsw::Renderer r;
    r.render(im, tf.build(), 10, 10, 10);

    // The viewport quad fills the mapped rect, not the naive whole-image rect
    // (which would be [0,200] x [0,120]).
    CHECK(at(im, 120, 75).g == 200);   // center of the mapped rect
    CHECK(at(im, 44, 34).g == 200);    // just inside the mapped top-left
    CHECK(at(im, 36, 26).g == 10);     // just outside (the naive rect would cover this)
    CHECK(at(im, 5, 5).g == 10);       // naive top-left: background
    CHECK(at(im, 204, 124).g == 10);   // just past the mapped bottom-right

    // The x<0 quad rendered into the surrounding window area.
    CHECK(at(im, 24, 140).b == 250);
    CHECK(at(im, 24, 100).b == 10);    // above it: background
}
