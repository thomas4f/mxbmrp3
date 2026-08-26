// ============================================================================
// tests/unit/nine_slice_test.cpp
// Pure geometry of mxbmrp3/hud/nine_slice.h.
//
// Restored after the box-model rework, which specified the five properties
// below after the original was removed in 8bf6610 (the box model was being
// reworked and the old rows re-blessed numbers on every step). This one only
// ever depended on nine_slice.h, so the spec lists it among the "pure ones
// first" -- it does not wait on the per-panel migration onto panel_box.h.
//
// The handedness case is named directly by a comment at nine_slice.h's winding
// rule, and it is the load-bearing one: the game CULLS a quad wound the wrong
// way while the software renderer behind the companion window does not, so a
// reversed slice is invisibly wrong on the very surface most likely to be
// screenshotted. Nothing else asserts it.
// ============================================================================
#include "doctest.h"

#include <algorithm>
#include <cmath>

#include "hud/nine_slice.h"

namespace {

// 16:9 at 1080p, the aspect the shipped themes are authored against.
constexpr float kAspect = 1920.0f / 1080.0f;

// Signed area of one emitted quad, via the shoelace formula over its 4 corners.
// Only the SIGN is meaningful here: it is the quad's winding handedness.
float signedArea(const NineSlice::Slice& s) {
    float acc = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        acc += s.pos[i][0] * s.pos[j][1] - s.pos[j][0] * s.pos[i][1];
    }
    return 0.5f * acc;
}

// Axis-aligned bounds of a slice, which is all a slice ever is.
struct Rect { float l, t, r, b; };
Rect boundsOf(const NineSlice::Slice& s) {
    Rect out{s.pos[0][0], s.pos[0][1], s.pos[0][0], s.pos[0][1]};
    for (int i = 1; i < 4; ++i) {
        out.l = std::min(out.l, s.pos[i][0]);
        out.t = std::min(out.t, s.pos[i][1]);
        out.r = std::max(out.r, s.pos[i][0]);
        out.b = std::max(out.b, s.pos[i][1]);
    }
    return out;
}

}  // namespace

TEST_CASE("nine_slice: the corner is square in PIXELS, not in normalized units") {
    // The bug this pins: a corner made square in normalized units renders
    // aspect-times too wide -- 1.78x at 16:9 -- because x and y normalize
    // against different pixel extents.
    const float borderY = 0.02f;
    const NineSlice::Border in = NineSlice::clampedBorder(1.0f, 1.0f, borderY, kAspect);

    // Square in pixels means x * screenW == y * screenH, i.e. x * aspect == y.
    CHECK(in.x * kAspect == doctest::Approx(in.y));

    // And state the failure mode explicitly, so the number in the spec is the
    // number in the test: taking the border equally on both axes would make the
    // corner exactly `aspect` times too wide.
    CHECK(borderY / in.x == doctest::Approx(kAspect));
}

TEST_CASE("nine_slice: the clamp scales BOTH axes by one factor") {
    const float borderY = 0.4f;   // deliberately too big for the rect below
    const NineSlice::Border unclamped =
        NineSlice::clampedBorder(1.0f, 1.0f, borderY, kAspect);
    const NineSlice::Border clamped =
        NineSlice::clampedBorder(0.10f, 0.10f, borderY, kAspect);

    // The clamp fired...
    CHECK(clamped.y < unclamped.y);
    // ...and the corner is still square in pixels afterwards, which is only true
    // if one scale was applied to both axes rather than each being clipped
    // against its own half-extent.
    CHECK(clamped.x * kAspect == doctest::Approx(clamped.y));
    CHECK(clamped.x / clamped.y == doctest::Approx(unclamped.x / unclamped.y));

    // Fully clamped, the inset is exactly half the rect: the centre degenerates
    // to zero area rather than inverting.
    const NineSlice::Border full =
        NineSlice::clampedBorder(0.05f, 0.05f, 10.0f, kAspect);
    CHECK(full.x <= doctest::Approx(0.025f));
    CHECK(full.y <= doctest::Approx(0.025f));
}

TEST_CASE("nine_slice: the nine slices tile the rect exactly") {
    NineSlice::Slice out[NineSlice::SLICE_COUNT];
    const float x = 0.2f, y = 0.3f, w = 0.5f, h = 0.25f;
    NineSlice::build(out, x, y, w, h, 0.02f, kAspect);

    // Union of the nine bounds is the whole rect...
    Rect all = boundsOf(out[0]);
    float area = 0.0f;
    for (const NineSlice::Slice& s : out) {
        const Rect r = boundsOf(s);
        all.l = std::min(all.l, r.l);
        all.t = std::min(all.t, r.t);
        all.r = std::max(all.r, r.r);
        all.b = std::max(all.b, r.b);
        area += (r.r - r.l) * (r.b - r.t);
    }
    CHECK(all.l == doctest::Approx(x));
    CHECK(all.t == doctest::Approx(y));
    CHECK(all.r == doctest::Approx(x + w));
    CHECK(all.b == doctest::Approx(y + h));

    // ...and the areas sum to the rect's area, so there is neither a gap nor an
    // overlap. (Nine axis-aligned rects covering the bounds with the right total
    // area can only be a partition.)
    CHECK(area == doctest::Approx(w * h));
}

TEST_CASE("nine_slice: every slice shares one winding handedness") {
    NineSlice::Slice out[NineSlice::SLICE_COUNT];
    NineSlice::build(out, 0.1f, 0.1f, 0.6f, 0.4f, 0.02f, kAspect);

    const float first = signedArea(out[0]);
    REQUIRE(std::fabs(first) > 0.0f);
    for (int i = 1; i < NineSlice::SLICE_COUNT; ++i) {
        const float a = signedArea(out[i]);
        REQUIRE_MESSAGE(std::fabs(a) > 0.0f, "slice " << i << " is degenerate");
        CHECK_MESSAGE((a > 0.0f) == (first > 0.0f),
                      "slice " << i << " is wound the other way");
    }
}

TEST_CASE("nine_slice: every slice is drawn as authored, with no rotation") {
    NineSlice::Slice out[NineSlice::SLICE_COUNT];
    NineSlice::build(out, 0.1f, 0.1f, 0.6f, 0.4f, 0.02f, kAspect);

    // u=+x, v=+y for all nine: pos[0]=TL, pos[1]=BL, pos[2]=BR, pos[3]=TR. A
    // sprite reoriented by permuting corners would break one of these.
    for (int i = 0; i < NineSlice::SLICE_COUNT; ++i) {
        CHECK(out[i].pos[0][0] == doctest::Approx(out[i].pos[1][0]));  // left edge
        CHECK(out[i].pos[2][0] == doctest::Approx(out[i].pos[3][0]));  // right edge
        CHECK(out[i].pos[0][1] == doctest::Approx(out[i].pos[3][1]));  // top edge
        CHECK(out[i].pos[1][1] == doctest::Approx(out[i].pos[2][1]));  // bottom edge
        CHECK(out[i].pos[0][0] <= out[i].pos[2][0]);
        CHECK(out[i].pos[0][1] <= out[i].pos[1][1]);
    }

    // The emission ORDER is part of the contract: BaseHud's layout fast path
    // overwrites a previously emitted background in place.
    CHECK(out[0].part == NineSlice::Slice::CENTER);
    for (int i = 1; i <= 4; ++i) CHECK(out[i].part == NineSlice::Slice::EDGE);
    for (int i = 5; i <= 8; ++i) CHECK(out[i].part == NineSlice::Slice::CORNER);
    CHECK(out[1].index == NineSlice::TOP);
    CHECK(out[2].index == NineSlice::BOTTOM);
    CHECK(out[3].index == NineSlice::LEFT);
    CHECK(out[4].index == NineSlice::RIGHT);
    CHECK(out[5].index == NineSlice::TL);
    CHECK(out[6].index == NineSlice::TR);
    CHECK(out[7].index == NineSlice::BL);
    CHECK(out[8].index == NineSlice::BR);
}

TEST_CASE("nine_slice: a slice size in cells costs exactly that many cells") {
    // The knob used to be a normalized-Y fraction put through a ceil, so 0.020
    // through 0.029 all resolved to the same 3-cell margin while continuing to
    // resize the drawn corner -- an edit moved the art and not the spacing.
    const float cellW = 10.56f / 1920.0f;   // one grid cell, normalized x at 1080p
    CHECK(NineSlice::cellsToBorderX(3.0f, cellW) == doctest::Approx(3.0f * cellW));
    CHECK(NineSlice::cellsToBorderY(3.0f, cellW, kAspect)
              == doctest::Approx(3.0f * cellW * kAspect));

    // Linear, with no rounding anywhere: 2.5 cells costs 2.5 cells.
    CHECK(NineSlice::cellsToBorderX(2.5f, cellW) == doctest::Approx(2.5f * cellW));
    // ...and the Y extent is that same distance in PIXELS, so it is square.
    const float x = NineSlice::cellsToBorderX(3.0f, cellW);
    const float y = NineSlice::cellsToBorderY(3.0f, cellW, kAspect);
    CHECK(x * kAspect == doctest::Approx(y));
}
