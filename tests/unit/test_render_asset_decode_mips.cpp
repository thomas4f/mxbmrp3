// ============================================================================
// tests/unit/test_render_asset_decode_mips.cpp
// The glyph-atlas mip chain (hudassets::buildFntMips).
//
// All three renderers - GL, D3D11 and software - sample text from these levels,
// so a fault here is a fault in every backend at once, and the chain's SHAPE is
// load-bearing beyond image quality: GL 1.1 has no GL_TEXTURE_MAX_LEVEL, so a
// chain that stops before 1x1 makes the texture INCOMPLETE and it samples as
// white. That is why "terminates at 1x1" is asserted rather than assumed.
// ============================================================================
// The doctest implementation + main() live in test_plugin_utils.cpp.
#include "doctest.h"

#include "core/render_asset_decode.h"

#include <fstream>
#include <string>
#include <vector>

namespace {
std::string dataRoot() {
    std::string f = __FILE__;
    const size_t cut = f.find("/tests/unit/");
    REQUIRE(cut != std::string::npos);
    return f.substr(0, cut) + "/mxbmrp3_data";
}
// A font whose level 0 is a known pattern, so a level's VALUES can be checked
// rather than only its dimensions.
hudassets::FntFont synthetic(int w, int h, uint8_t fill) {
    hudassets::FntFont f;
    f.ok = true; f.cellH = 8; f.aw = w; f.ah = h;
    f.atlas.assign(size_t(w) * h, fill);
    hudassets::buildFntMips(f);
    return f;
}
}  // namespace

TEST_CASE("buildFntMips: the chain halves to 1x1, so the texture is never incomplete") {
    hudassets::FntFont f = synthetic(64, 32, 200);
    REQUIRE(f.mips.size() >= 2);
    CHECK(f.mips.front().w == 64);
    CHECK(f.mips.front().h == 32);
    CHECK(f.mips.back().w == 1);
    CHECK(f.mips.back().h == 1);
    for (size_t i = 1; i < f.mips.size(); ++i) {
        const auto& a = f.mips[i - 1];
        const auto& b = f.mips[i];
        // max(1, floor(prev/2)) - what GL and D3D11 REQUIRE, not just what
        // shrinks. This asserted round-UP originally and so pinned a chain both
        // APIs would reject: incomplete in GL (samples white), a failed
        // CreateTexture2D in D3D11.
        CHECK(b.w == (a.w > 1 ? a.w / 2 : 1));
        CHECK(b.h == (a.h > 1 ? a.h / 2 : 1));
        CHECK(b.px.size() == size_t(b.w) * b.h);
    }
}

TEST_CASE("buildFntMips: an ODD axis floors, and still reaches 1x1") {
    // 7 -> 3 -> 1 and 3 -> 1: floor is what the APIs require, and it terminates
    // anyway, so the round-up this once used bought nothing and cost
    // conformance. The >1 guard is what keeps floor from reaching 0.
    hudassets::FntFont f = synthetic(7, 3, 128);
    REQUIRE(f.mips.size() >= 2);
    CHECK(f.mips[1].w == 3);
    CHECK(f.mips[1].h == 1);
    CHECK(f.mips.back().w == 1);
    CHECK(f.mips.back().h == 1);
}

TEST_CASE("buildFntMips: a level is the BOX AVERAGE of the one above it") {
    // Coverage, not colour: the atlas byte is alpha and already linear, so a
    // plain mean is exactly "how much ink covers this texel". A uniform field
    // must therefore survive the whole chain unchanged - any weighting bug
    // (or an sRGB "correction" someone adds later) shows up here immediately.
    hudassets::FntFont flat = synthetic(16, 16, 180);
    for (const auto& m : flat.mips)
        for (uint8_t v : m.px) CHECK(int(v) == 180);

    // A half-black/half-white split averages to mid-grey one level down.
    hudassets::FntFont f;
    f.ok = true; f.cellH = 8; f.aw = 2; f.ah = 2;
    f.atlas = { 255, 255, 0, 0 };        // top row ink, bottom row empty
    hudassets::buildFntMips(f);
    REQUIRE(f.mips.size() == 2);
    CHECK(int(f.mips[1].px[0]) == 128);  // (255+255+0+0+2)/4
}

TEST_CASE("buildFntMips: a shipped font gets a full chain off its real atlas") {
    // The synthetic cases above prove the filter; this proves it actually runs on
    // what ships, through the real decode path, at the real 2048^2 atlas size.
    const std::string path = dataRoot() + "/fonts/RobotoMono-Regular.fnt";
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "shipped font missing: ", path);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    hudassets::FntFont f = hudassets::decodeFnt(bytes);
    REQUIRE(f.ok);
    CHECK(f.cellH == 135);                    // the normalised shipped cell
    REQUIRE(f.mips.size() > 3);               // ~20px text lands on level 2-3
    CHECK(f.mips[0].w == f.aw);
    CHECK(f.mips[0].h == f.ah);
    CHECK(f.mips.back().w == 1);
    CHECK(f.mips.back().h == 1);
}

// ---- icon mip chain -------------------------------------------------------

namespace {
hudassets::Texture rgbaTex(int w, int h, const std::vector<uint8_t>& px) {
    hudassets::Texture t;
    t.ok = true; t.w = w; t.h = h; t.rgba = px;
    hudassets::buildTexMips(t);
    return t;
}
}  // namespace

TEST_CASE("buildTexMips: a transparent surround does NOT tint the edge it borders") {
    // THE HALO BUG this premultiply exists to prevent, and the reason a colour
    // mip filter is not just "average the four texels".
    //
    // A .tga's transparent region still HAS colour bytes, and they are usually
    // black. Averaging straight RGBA mixes that black into every edge texel by
    // weight, so each level darkens the icon's rim a little more and the icon
    // ends up ringed. Here: one opaque WHITE texel beside three transparent
    // BLACK ones. Straight averaging gives rgb 63 (a grey rim); weighting each
    // colour by its own alpha gives back white at quarter alpha, which is what
    // "one quarter of this texel is covered, by white" actually means.
    hudassets::Texture t = rgbaTex(2, 2, {
        255, 255, 255, 255,   0, 0, 0, 0,
        0,   0,   0,   0,     0, 0, 0, 0 });
    REQUIRE(t.mips.size() == 2);
    const auto& px = t.mips[1].px;
    CHECK_MESSAGE(int(px[0]) == 255, "red " << int(px[0]) << " - transparent black bled in");
    CHECK(int(px[1]) == 255);
    CHECK(int(px[2]) == 255);
    CHECK(int(px[3]) == 64);          // (255+0+0+0+2)/4: coverage is a plain mean
}

TEST_CASE("buildTexMips: a fully transparent footprint stays transparent, and cannot divide by zero") {
    // The one case with no colour to recover. It is also the divide-by-zero the
    // premultiply invites, so it is asserted rather than reasoned about.
    hudassets::Texture t = rgbaTex(2, 2, {
        10, 20, 30, 0,   40, 50, 60, 0,
        70, 80, 90, 0,   1,  2,  3,  0 });
    REQUIRE(t.mips.size() == 2);
    const auto& px = t.mips[1].px;
    CHECK(int(px[3]) == 0);
    CHECK(int(px[0]) == 0);
    CHECK(int(px[1]) == 0);
    CHECK(int(px[2]) == 0);
}

TEST_CASE("buildTexMips: a fully opaque texture averages colour exactly") {
    // With alpha constant the premultiply must cancel out completely, so this is
    // the check that the weighting did not introduce a bias of its own.
    hudassets::Texture t = rgbaTex(2, 2, {
        0,   0, 0, 255,   200, 100, 50, 255,
        100, 0, 0, 255,   100, 100, 50, 255 });
    REQUIRE(t.mips.size() == 2);
    const auto& px = t.mips[1].px;
    CHECK(int(px[0]) == 100);   // (0+200+100+100)/4
    CHECK(int(px[1]) == 50);    // (0+100+0+100)/4
    CHECK(int(px[2]) == 25);    // (0+50+0+50)/4
    CHECK(int(px[3]) == 255);
}

TEST_CASE("buildTexMips: the chain halves to 1x1 and an odd axis still shrinks") {
    // Same contract as the font chain, for the same GL 1.1 reason: no
    // GL_TEXTURE_MAX_LEVEL, so a chain that stops early is an INCOMPLETE texture
    // and samples as white.
    hudassets::Texture t = rgbaTex(5, 3, std::vector<uint8_t>(size_t(5) * 3 * 4, 128));
    REQUIRE(t.mips.size() >= 2);
    CHECK(t.mips[1].w == 2);      // floor(5/2)
    CHECK(t.mips[1].h == 1);      // floor(3/2)
    CHECK(t.mips.back().w == 1);
    CHECK(t.mips.back().h == 1);
    for (const auto& m : t.mips) CHECK(m.px.size() == size_t(m.w) * m.h * 4);
}

TEST_CASE("buildTexMips: it is opt-in - decodeTga alone builds no chain") {
    // Nine-slice panel art must NOT get one (stretched in one axis, so the level
    // comes from the stretched derivative and blurs the sharp one), which only
    // holds while decodeTga stays silent about mips and the CALLER decides.
    const std::string path = dataRoot() + "/icons/hud-performance.tga";
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) { MESSAGE("no shipped icon at " << path); return; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    hudassets::Texture t = hudassets::decodeTga(bytes);
    REQUIRE(t.ok);
    CHECK_MESSAGE(t.mips.empty(), "decodeTga built a chain on its own - nine-slice "
                                  "art would now get one too");
    hudassets::buildTexMips(t);
    CHECK(t.mips.size() > 1);
    CHECK(t.mips.back().w == 1);
}
