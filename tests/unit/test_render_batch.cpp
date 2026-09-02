// ============================================================================
// tests/unit/test_render_batch.cpp
// core/render_batch.h -- the shared, API-agnostic batcher extracted from the
// D3D11 backend so the GL backend consumes the same batches.
//
// WHY THIS FILE IS THE POINT OF THE EXTRACTION. Before it, the batching logic
// was untested on every backend: hud_gpu_renderer.h says so in its own header,
// that it "needs a real device AND a display, and headless CI has neither".
// That is true of device creation and presentation. It was never true of the
// geometry, the corner order, the UV basis, the run coalescing, the sprite and
// font index resolution, or the justify arithmetic -- and those are most of the
// function. With handles reduced to opaque pointers, all of it runs headlessly.
//
// HONEST LIMIT, stated because it would otherwise be assumed away: these tests
// characterise the batcher as it exists AFTER the extraction. They cannot prove
// the move itself was faithful, because the pre-extraction code could not be
// run without a D3D device. What backs the move is that it was mechanical --
// the diff changes only the signature, the handle types and the cache lookups,
// with no arithmetic, ordering or early-out touched -- plus in-game
// re-validation of the companion window and the overlay with hwAccel on.
// ============================================================================
#include "doctest.h"
#include "core/render_batch.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

// Distinct, recognisable handles. Their VALUES are what run coalescing keys on,
// so the test can assert runs split exactly where the texture changes.
const void* const kWhite = reinterpret_cast<const void*>(0x100);
const void* const kTexA  = reinterpret_cast<const void*>(0x200);
const void* const kTexB  = reinterpret_cast<const void*>(0x300);
const void* const kFont  = reinterpret_cast<const void*>(0x400);

struct FakeResolver : hudbatch::Resolver {
    hudassets::FntFont font_;
    bool failTexture = false;          // models a decode miss
    bool failFont = false;
    std::vector<std::string> textureAsks;
    std::vector<bool> iconAsks;

    FakeResolver() {
        font_.ok = true;
        font_.cellH = 10; font_.aw = 100; font_.ah = 50;
        // 'A' is drawable and 10 wide; ' ' advances without drawing, which is
        // the case that separates "advance" from "emit" in the layout.
        font_.glyphs['A'] = hudassets::FntGlyph{ true, 0, 0, 8, 10, 1, 10 };
        font_.glyphs[' '] = hudassets::FntGlyph{ true, 0, 0, 0, 0, 0, 10 };
    }
    const void* texture(const std::string& base, bool icon, const std::string&) override {
        textureAsks.push_back(base);
        iconAsks.push_back(icon);
        if (failTexture) return nullptr;
        return base == "b" ? kTexB : kTexA;
    }
    const void* font(const std::string&, const std::string&,
                     const hudassets::FntFont** out) override {
        if (failFont) return nullptr;
        *out = &font_;
        return kFont;
    }
    const void* white() override { return kWhite; }
};

SPluginQuad_t makeQuad(int sprite, float x0, float y0, float x1, float y1,
                       unsigned long color = 0xFF112233ul) {
    SPluginQuad_t q{};
    q.m_iSprite = sprite;
    q.m_ulColor = color;
    // TL, BL, BR, TR -- the game's corner order.
    q.m_aafPos[0][0]=x0; q.m_aafPos[0][1]=y0;
    q.m_aafPos[1][0]=x0; q.m_aafPos[1][1]=y1;
    q.m_aafPos[2][0]=x1; q.m_aafPos[2][1]=y1;
    q.m_aafPos[3][0]=x1; q.m_aafPos[3][1]=y0;
    return q;
}

SPluginString_t makeString(const char* text, float x, float y, int justify,
                           float size = 0.05f, int fontIdx = 1) {
    SPluginString_t s{};
    std::strncpy(s.m_szString, text, sizeof(s.m_szString) - 1);
    s.m_afPos[0] = x; s.m_afPos[1] = y;
    s.m_iJustify = justify;
    s.m_fSize = size;
    s.m_iFont = fontIdx;
    s.m_ulColor = 0xFFAABBCCul;
    return s;
}

struct Built {
    std::vector<hudbatch::Vertex> verts;
    std::vector<hudbatch::Run> runs;
};

// A full-client viewport at 200x100, so NDC arithmetic is easy to check by hand.
Built run(FakeResolver& res, const std::vector<SPluginQuad_t>& quads,
          const std::vector<SPluginString_t>& strings,
          const std::vector<std::string>& sprites = { "a", "b" },
          int firstIcon = 1 << 30) {
    static std::vector<std::string> fonts;
    fonts = { "font" };
    static std::vector<std::string> spriteNames;
    spriteNames = sprites;
    hudsw::Frame f;
    f.quads = quads.empty() ? nullptr : quads.data();
    f.quadCount = static_cast<int>(quads.size());
    f.strings = strings.empty() ? nullptr : strings.data();
    f.stringCount = static_cast<int>(strings.size());
    f.fontNames = &fonts;
    f.spriteNames = &spriteNames;
    f.firstIcon = firstIcon;
    f.assetRoot = "root";
    Built b;
    hudbatch::build(f, 200, 100, 0, 0, 200, 100, res, b.verts, b.runs);
    return b;
}

}  // namespace

TEST_CASE("render batch: normalized coords map to NDC with y=+1 at the top") {
    // The mapping both APIs share. If this ever flips, every HUD element lands
    // mirrored vertically - and it is the one piece of the batcher that looks
    // API-specific and is not.
    FakeResolver res;
    Built b = run(res, { makeQuad(0, 0.0f, 0.0f, 1.0f, 1.0f) }, {});
    REQUIRE(b.verts.size() == 6);
    // Full-viewport quad: x spans -1..+1, y spans +1 (top) down to -1 (bottom).
    CHECK(b.verts[0].x == doctest::Approx(-1.0f));
    CHECK(b.verts[0].y == doctest::Approx(1.0f));    // TL -> top
    CHECK(b.verts[2].x == doctest::Approx(1.0f));
    CHECK(b.verts[2].y == doctest::Approx(-1.0f));   // BR -> bottom
}

TEST_CASE("render batch: a quad is two triangles in TL,BL,BR / TL,BR,TR order") {
    FakeResolver res;
    Built b = run(res, { makeQuad(0, 0.0f, 0.0f, 0.5f, 0.5f) }, {});
    REQUIRE(b.verts.size() == 6);
    // tri = {0,1,2, 0,2,3} over corners TL,BL,BR,TR.
    CHECK(b.verts[0].x == b.verts[3].x);   // both are TL
    CHECK(b.verts[0].y == b.verts[3].y);
    CHECK(b.verts[2].x == b.verts[4].x);   // both are BR
    CHECK(b.verts[2].y == b.verts[4].y);
}

TEST_CASE("render batch: color passes through untouched") {
    // m_ulColor is the game's ABGR little-endian packing and is read back as
    // R8G8B8A8_UNORM, so it must arrive bit-identical - any "helpful" swizzle
    // here would tint every HUD element.
    FakeResolver res;
    Built b = run(res, { makeQuad(0, 0, 0, 1, 1, 0xDEADBEEFul) }, {});
    REQUIRE(!b.verts.empty());
    for (const auto& v : b.verts) CHECK(v.rgba == 0xDEADBEEFu);
}

TEST_CASE("render batch: untextured quads go through the white texture") {
    FakeResolver res;
    Built b = run(res, { makeQuad(0, 0, 0, 1, 1) }, {});
    REQUIRE(b.runs.size() == 1);
    CHECK(b.runs[0].tex == kWhite);
    CHECK(b.runs[0].shader == hudbatch::Shader::Sprite);
    // Zero UVs: the white texel, so one shader serves both cases.
    for (const auto& v : b.verts) { CHECK(v.u == 0.0f); CHECK(v.v == 0.0f); }
}

TEST_CASE("render batch: consecutive same-texture quads coalesce, a change splits") {
    // This is the whole point of batching, and it was previously unpinned on
    // both backends. Sprite 1 -> "a", sprite 2 -> "b" in the fake resolver.
    FakeResolver res;
    Built b = run(res, { makeQuad(1, 0, 0, 0.1f, 0.1f),
                         makeQuad(1, 0.1f, 0, 0.2f, 0.1f),
                         makeQuad(2, 0.2f, 0, 0.3f, 0.1f),
                         makeQuad(2, 0.3f, 0, 0.4f, 0.1f) }, {});
    REQUIRE(b.runs.size() == 2);
    CHECK(b.runs[0].tex == kTexA);
    CHECK(b.runs[0].start == 0);
    CHECK(b.runs[0].count == 12);          // two quads in one run
    CHECK(b.runs[1].tex == kTexB);
    CHECK(b.runs[1].start == 12);
    CHECK(b.runs[1].count == 12);
    CHECK(b.verts.size() == 24);
}

TEST_CASE("render batch: an unresolvable texture skips only its own quad") {
    // A decode miss must not take the rest of the frame with it.
    FakeResolver res;
    res.failTexture = true;
    Built b = run(res, { makeQuad(1, 0, 0, 0.1f, 0.1f),
                         makeQuad(0, 0.2f, 0, 0.3f, 0.1f) }, {});
    CHECK(b.verts.size() == 6);            // only the untextured one survived
    REQUIRE(b.runs.size() == 1);
    CHECK(b.runs[0].tex == kWhite);
}

TEST_CASE("render batch: an out-of-range sprite index is skipped, not clamped") {
    FakeResolver res;
    Built b = run(res, { makeQuad(99, 0, 0, 0.1f, 0.1f) }, {});
    CHECK(b.verts.empty());
    CHECK(res.textureAsks.empty());        // never even asked
}

TEST_CASE("render batch: firstIcon decides texture vs icon, at the boundary") {
    // Sprites at or past firstIcon are icons (tinted to the quad color); below
    // it they are textures. An off-by-one here silently swaps a HUD's art.
    FakeResolver res;
    run(res, { makeQuad(1, 0, 0, 0.1f, 0.1f), makeQuad(2, 0, 0, 0.1f, 0.1f) },
        {}, { "a", "b" }, /*firstIcon=*/2);
    REQUIRE(res.iconAsks.size() == 2);
    CHECK(res.iconAsks[0] == false);       // sprite 1 < firstIcon
    CHECK(res.iconAsks[1] == true);        // sprite 2 >= firstIcon
}

TEST_CASE("render batch: strings are emitted after quads, preserving z-order") {
    // The software renderer draws quads then strings; the batch must keep that
    // or every label falls behind its own panel.
    FakeResolver res;
    Built b = run(res, { makeQuad(0, 0, 0, 0.1f, 0.1f) },
                  { makeString("A", 0.5f, 0.5f, 0) });
    REQUIRE(b.runs.size() == 2);
    CHECK(b.runs[0].shader == hudbatch::Shader::Sprite);
    CHECK(b.runs[1].shader == hudbatch::Shader::Text);
    CHECK(b.runs[0].start < b.runs[1].start);
}

TEST_CASE("render batch: justify shifts the pen by half or all of the advance") {
    FakeResolver res;
    auto penXOf = [&](int justify) {
        Built b = run(res, {}, { makeString("AA", 0.5f, 0.5f, justify) });
        REQUIRE(!b.verts.empty());
        return b.verts[0].x;
    };
    const float left = penXOf(0), centre = penXOf(1), right = penXOf(2);
    CHECK(left > centre);
    CHECK(centre > right);
    // "AA" at scale = 0.05*100/10 = 0.5 advances 10*0.5 per glyph = 10px total.
    // Centre shifts half of that (5px), right the whole 10px. 200px wide => NDC
    // units of 2/200 = 0.01 per pixel.
    CHECK((left - centre) == doctest::Approx(5.0f * 0.01f));
    CHECK((left - right) == doctest::Approx(10.0f * 0.01f));
}

TEST_CASE("render batch: a space advances the pen without emitting a glyph") {
    FakeResolver res;
    Built b = run(res, {}, { makeString("A A", 0.5f, 0.5f, 0) });
    CHECK(b.verts.size() == 12);           // two drawable glyphs, not three
}

TEST_CASE("render batch: an empty string and a bad font index emit nothing") {
    FakeResolver res;
    CHECK(run(res, {}, { makeString("", 0.5f, 0.5f, 0) }).verts.empty());
    CHECK(run(res, {}, { makeString("A", 0.5f, 0.5f, 0, 0.05f, 99) }).verts.empty());
    res.failFont = true;
    CHECK(run(res, {}, { makeString("A", 0.5f, 0.5f, 0) }).verts.empty());
}

TEST_CASE("render batch: glyph UVs are normalised by the atlas size") {
    FakeResolver res;
    Built b = run(res, {}, { makeString("A", 0.5f, 0.5f, 0) });
    REQUIRE(b.verts.size() == 6);
    // 'A' occupies atlas rect (0,0)-(8,10) in a 100x50 atlas.
    CHECK(b.verts[0].u == doctest::Approx(0.0f));
    CHECK(b.verts[0].v == doctest::Approx(0.0f));
    CHECK(b.verts[2].u == doctest::Approx(8.0f / 100.0f));
    CHECK(b.verts[2].v == doctest::Approx(10.0f / 50.0f));
}

TEST_CASE("render batch: the viewport rect offsets and scales, not the client") {
    // Normalized coords map through the UiViewport rect, so a pillarboxed
    // client puts x=0 at the rect's left edge, not the window's.
    FakeResolver res;
    std::vector<std::string> fonts{ "font" }, sprites{ "a" };
    std::vector<SPluginQuad_t> quads{ makeQuad(0, 0.0f, 0.0f, 1.0f, 1.0f) };
    hudsw::Frame f;
    f.quads = quads.data(); f.quadCount = 1;
    f.fontNames = &fonts; f.spriteNames = &sprites;
    f.firstIcon = 1 << 30; f.assetRoot = "root";
    std::vector<hudbatch::Vertex> verts; std::vector<hudbatch::Run> runs;
    // 200x100 client, 16:9 rect inset by 20px each side.
    hudbatch::build(f, 200, 100, 20, 0, 160, 90, res, verts, runs);
    REQUIRE(verts.size() == 6);
    CHECK(verts[0].x == doctest::Approx(20.0f * 2.0f / 200.0f - 1.0f));
    CHECK(verts[2].x == doctest::Approx(180.0f * 2.0f / 200.0f - 1.0f));
}
