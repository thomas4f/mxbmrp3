// ============================================================================
// tests/unit/viewport_test.cpp
// UiViewport::compute — the one centered-16:9 UI-rect computation shared by
// the companion window's paint loop and InputManager's cursor / window-bounds
// maps (see ui_viewport.h for why it is one function).
//
// Before the extraction the three sites carried their own copies — the paint
// loop in integer math, the input maps via float ASPECT_RATIO with truncation
// — and could disagree by a pixel at odd client sizes (1704x958 is such a
// size: the float form pillarboxes it to 1703 wide, the old integer form kept
// 1704). A pixel of disagreement between where a HUD is painted and where a
// click is hit-tested is invisible in every screenshot and real at every
// panel edge. One function ends the class; these cases pin its contract:
// exact-16:9 fill, pillarbox/letterbox orientation, centering, the never-zero
// guard the inverse map divides by, and cursor round-trips through the same
// forward/inverse arithmetic the call sites use.
// ============================================================================
#include "doctest.h"

#include "core/ui_viewport.h"

#include <algorithm>
#include <cstdlib>
#include <utility>

using UiViewport::Rect;
using UiViewport::compute;

TEST_CASE("exact 16:9 clients fill the whole client") {
    for (int scale : {1, 2, 60, 120, 240}) {
        const int w = 16 * scale, h = 9 * scale;
        const Rect r = compute(w, h);
        CHECK(r.x == 0);
        CHECK(r.y == 0);
        CHECK(r.w == w);
        CHECK(r.h == h);
    }
}

TEST_CASE("wider than 16:9 pillarboxes: full height, centered horizontally") {
    const Rect r = compute(3440, 1440);   // 21:9 ultrawide
    CHECK(r.h == 1440);
    CHECK(r.w == 2560);                   // 1440 * 16 / 9
    CHECK(r.x == (3440 - 2560) / 2);
    CHECK(r.y == 0);
}

TEST_CASE("narrower than 16:9 letterboxes: full width, centered vertically") {
    const Rect r = compute(1080, 1920);   // portrait
    CHECK(r.w == 1080);
    CHECK(r.h == 1080 * 9 / 16);          // 607, truncated
    CHECK(r.x == 0);
    CHECK(r.y == (1920 - r.h) / 2);
}

TEST_CASE("the 1704x958 boundary case both old copies disagreed on") {
    // 1704*9 = 15336 > 958*16 = 15328: barely wider than 16:9, so the one
    // rule pillarboxes it. The old paint-loop copy truncated 1704*9/16 to 958
    // and kept the full 1704 width instead — a 1-px wider viewport than the
    // cursor map used. Whichever answer, there is now exactly one.
    const Rect r = compute(1704, 958);
    CHECK(r.h == 958);
    CHECK(r.w == 958 * 16 / 9);           // 1703
    CHECK(r.x == 0);                      // (1704 - 1703) / 2
    CHECK(r.y == 0);
}

TEST_CASE("degenerate clients never produce a zero-size rect") {
    // The inverse map divides by w and h, so the guard is load-bearing.
    for (auto wh : {std::pair<int, int>{0, 0}, {1, 1}, {-5, 10}, {10, -5},
                    {1, 10000}, {10000, 1}}) {
        const Rect r = compute(wh.first, wh.second);
        CHECK(r.w >= 1);
        CHECK(r.h >= 1);
    }
}

TEST_CASE("sweep: the rect stays inside the client, centered, and near 16:9") {
    for (int w = 1; w <= 400; w += 7) {
        for (int h = 1; h <= 400; h += 7) {
            const Rect r = compute(w, h);
            CAPTURE(w); CAPTURE(h);
            REQUIRE(r.w >= 1);
            REQUIRE(r.h >= 1);
            CHECK(r.w <= std::max(1, w));
            CHECK(r.h <= std::max(1, h));
            // Centered: the leftover split differs by at most the odd pixel.
            CHECK(r.x == (w - r.w) / 2);
            CHECK(r.y == (h - r.h) / 2);
            // Aspect within integer truncation of 16:9 (one full step of the
            // short side's contribution).
            CHECK(std::abs(r.w * 9 - r.h * 16) <= 16);
        }
    }
}

TEST_CASE("cursor round-trip through the call sites' forward/inverse math") {
    // The paint loop scales normalized [0,1] into the rect; the cursor map
    // inverts with (px - offset) / size. Same Rect on both sides must
    // round-trip to well under a pixel in every geometry.
    for (auto wh : {std::pair<int, int>{1920, 1080}, {3440, 1440},
                    {1080, 1920}, {1704, 958}, {333, 777}}) {
        const Rect r = compute(wh.first, wh.second);
        for (float nx : {0.0f, 0.25f, 0.5f, 1.0f, -0.2f, 1.3f}) {
            for (float ny : {0.0f, 0.5f, 1.0f, -0.1f, 1.1f}) {
                const float px = r.x + nx * r.w;    // forward (paint)
                const float py = r.y + ny * r.h;
                const float ix = (px - r.x) / r.w;  // inverse (cursor)
                const float iy = (py - r.y) / r.h;
                CHECK(ix == doctest::Approx(nx).epsilon(1e-5));
                CHECK(iy == doctest::Approx(ny).epsilon(1e-5));
            }
        }
    }
}
