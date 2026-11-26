// ============================================================================
// tests/unit/test_render_clip.cpp
// core/render_clip.h -- cutting a range of the frame at a rect, for the
// plugin's own renderers (Direct GL, the companion window).
//
// What it pins: a box crossing the rect is cut exactly at it with its UVs
// following the cut (the texture cropped, not squeezed), in either corner
// orientation; a box wholly outside is dropped and one wholly inside is left
// as it was; a quad off the axes is kept whole (the one thing the cut does
// not do, stated in the header); and the cursor finds the clip covering an
// index when the layer walk visits ranges out of array order.
// ============================================================================
#include "doctest.h"
#include "core/render_clip.h"

namespace {

// TL, BL, BR, TR -- the game's corner order.
void box(float p[4][2], float x0, float y0, float x1, float y1) {
    p[0][0] = x0; p[0][1] = y0;
    p[1][0] = x0; p[1][1] = y1;
    p[2][0] = x1; p[2][1] = y1;
    p[3][0] = x1; p[3][1] = y0;
}

void fullUv(float uv[4][2]) {
    const float u[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
    for (int c = 0; c < 4; ++c) { uv[c][0] = u[c][0]; uv[c][1] = u[c][1]; }
}

hudclip::Clip clip(uint32_t q0, uint32_t q1, uint32_t s0, uint32_t s1) {
    hudclip::Clip c;
    c.q0 = q0; c.q1 = q1; c.s0 = s0; c.s1 = s1;
    return c;
}

}  // namespace

TEST_CASE("render clip: a box crossing the bottom edge is cut there, its texture cropped") {
    float p[4][2], uv[4][2];
    box(p, 10, 10, 30, 50);
    fullUv(uv);
    REQUIRE(hudclip::cutQuad(p, uv, 0, 0, 100, 40));
    // The bottom corners moved up to the edge; the top ones stayed.
    CHECK(p[0][1] == doctest::Approx(10)); CHECK(p[3][1] == doctest::Approx(10));
    CHECK(p[1][1] == doctest::Approx(40)); CHECK(p[2][1] == doctest::Approx(40));
    CHECK(p[1][0] == doctest::Approx(10)); CHECK(p[2][0] == doctest::Approx(30));
    // Three quarters of the height is left, so V stops at 0.75: cropped.
    CHECK(uv[0][1] == doctest::Approx(0.0f)); CHECK(uv[1][1] == doctest::Approx(0.75f));
    CHECK(uv[2][1] == doctest::Approx(0.75f)); CHECK(uv[3][1] == doctest::Approx(0.0f));
    CHECK(uv[2][0] == doctest::Approx(1.0f)); CHECK(uv[1][0] == doctest::Approx(0.0f));
}

TEST_CASE("render clip: a glyph's sub-rect UVs are cut in proportion at the top") {
    float p[4][2];
    box(p, 0, 0, 10, 20);
    float uv[4][2] = { { 0.2f, 0.4f }, { 0.2f, 0.6f }, { 0.3f, 0.6f }, { 0.3f, 0.4f } };
    REQUIRE(hudclip::cutQuad(p, uv, -5, 5, 15, 100));
    CHECK(p[0][1] == doctest::Approx(5));
    CHECK(uv[0][1] == doctest::Approx(0.45f));   // a quarter of 0.4..0.6
    CHECK(uv[3][1] == doctest::Approx(0.45f));
    CHECK(uv[1][1] == doctest::Approx(0.6f));
    CHECK(uv[0][0] == doctest::Approx(0.2f)); CHECK(uv[3][0] == doctest::Approx(0.3f));
}

TEST_CASE("render clip: a box turned upside down is cut on the screen's edge, not its own") {
    // A sprite drawn rotated 180 degrees (the scrollbar's down caret): TL is at
    // the bottom right. The cut still lands at the screen's top, and the texture
    // row there is the one that was drawn there.
    float p[4][2] = { { 30, 50 }, { 30, 10 }, { 10, 10 }, { 10, 50 } };
    float uv[4][2];
    fullUv(uv);
    REQUIRE(hudclip::cutQuad(p, uv, 0, 20, 100, 100));
    for (int c = 0; c < 4; ++c) CHECK(p[c][1] >= 20.0f);
    // BL (screen top, v=1) moved to y=20: a quarter of the way back toward TL.
    CHECK(p[1][1] == doctest::Approx(20));
    CHECK(uv[1][1] == doctest::Approx(0.75f));
    CHECK(uv[0][1] == doctest::Approx(0.0f));
}

TEST_CASE("render clip: wholly outside is dropped, wholly inside untouched") {
    float p[4][2], uv[4][2];
    box(p, 10, 10, 30, 20);
    fullUv(uv);
    CHECK_FALSE(hudclip::cutQuad(p, uv, 0, 20, 100, 40));   // ends exactly at the top edge
    CHECK_FALSE(hudclip::cutQuad(p, uv, 31, 0, 100, 100));
    REQUIRE(hudclip::cutQuad(p, uv, 0, 0, 100, 100));
    CHECK(p[2][0] == 30); CHECK(p[2][1] == 20);
    CHECK(uv[2][0] == 1.0f); CHECK(uv[2][1] == 1.0f);
}

TEST_CASE("render clip: an untextured fill is cut with no UVs to carry") {
    float p[4][2];
    box(p, 0, 0, 10, 10);
    REQUIRE(hudclip::cutQuad(p, nullptr, 2, 3, 8, 7));
    CHECK(p[0][0] == 2); CHECK(p[0][1] == 3);
    CHECK(p[2][0] == 8); CHECK(p[2][1] == 7);
}

TEST_CASE("render clip: a quad off the axes is kept whole when it touches the rect") {
    // A diamond (a needle, an odd rotation): clamping its corners would change
    // its shape, so it is not cut -- drawn whole, or dropped when wholly outside.
    float p[4][2] = { { 10, 0 }, { 0, 10 }, { 10, 20 }, { 20, 10 } };
    float uv[4][2];
    fullUv(uv);
    REQUIRE(hudclip::cutQuad(p, uv, 0, 5, 100, 100));
    CHECK(p[0][1] == 0); CHECK(p[2][1] == 20);
    CHECK_FALSE(hudclip::cutQuad(p, uv, 0, 25, 100, 100));
}

TEST_CASE("render clip: the cursor finds the covering clip, in and out of array order") {
    const hudclip::Clip clips[3] = { clip(2, 4, 0, 1), clip(4, 4, 1, 3), clip(7, 9, 5, 6) };
    hudclip::Cursor cur(clips, 3);
    CHECK(cur.quad(0) == nullptr);
    CHECK(cur.quad(2) == &clips[0]);
    CHECK(cur.quad(3) == &clips[0]);
    CHECK(cur.quad(4) == nullptr);     // the second clip holds no quads
    CHECK(cur.quad(8) == &clips[2]);
    CHECK(cur.quad(9) == nullptr);
    CHECK(cur.quad(3) == &clips[0]);   // a later layer revisits an earlier range
    CHECK(cur.string(2) == &clips[1]);
    CHECK(cur.string(0) == &clips[0]);
    CHECK(cur.string(4) == nullptr);
    CHECK(cur.string(5) == &clips[2]);

    hudclip::Cursor none(nullptr, 3);
    CHECK(none.quad(2) == nullptr);
}
