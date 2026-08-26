// ============================================================================
// tests/unit/center_stack_test.cpp
// The three centered top panels do not overlap each other or the screen edge.
//
// THE BUG THIS PINS. The stack was three frozen cell numbers (1, 6, 11) written
// against boxes that were 4 cells tall. GapBar and Notices then gained panel
// padding like every other HUD -- 8 cells and 6.56 -- and nothing moved the
// numbers. At the SHIPPED defaults the GapBar's box top landed 12.7px above the
// screen and the Notices box overlapped it by 45px, both visible together in an
// ordinary race. No test could see it: nothing asserted a pixel, and the two HUDs
// are "compile-and-reason only" in the headless suite.
//
// So the geometry is asserted from the same metrics the HUDs build from, over 45
// layouts spanning padding 0-9 cells, three font sizes and three row pitches --
// because "the default is fine" is exactly what was believed before. CenterStack's
// helpers take the metrics explicitly so that claim is real; the no-argument
// overloads the HUDs call just pass layoutDefaults().
// ============================================================================
#include "doctest.h"

#include "hud/center_stack.h"

#include <cmath>
#include <initializer_list>
#include <vector>

namespace {
    // What the HUDs actually draw, mirrored from gap_bar_hud.cpp / notices_hud.cpp
    // so a change to either box shows up here as a failure rather than as an
    // overlap on screen.
    struct Box { float top, height; float bottom() const { return top + height; } };

    // The box top IS the offset. It used to be (offset - paddingV), with
    // CenterStack::gapBarOffsetY() adding the pad back on -- a panel top derived from
    // the padding, which is what let a theme's [card] hud-content flag slide this panel
    // down. See center_stack.h.
    //
    // All three now share ONE top: they stopped being a stack, so there is no
    // second-and-third to derive. What is left worth checking is the HEIGHTS, which the
    // panels still measure by, and that the shared top is on the grid and on-screen.
    Box gapBarBox(const LayoutMetrics& L) {
        return { CenterStack::stackBoxTop(L), CenterStack::gapBarBoxHeight(L) };
    }
    Box noticesBox(const LayoutMetrics& L) {
        return { CenterStack::stackBoxTop(L), CenterStack::noticesBoxHeight(L) };
    }

    // THE HUDs' OWN FORMULA, restated. The two boxes above read CenterStack for their
    // heights, so every case built on them is self-consistent BY CONSTRUCTION and
    // cannot notice the stack disagreeing with what the HUDs draw -- which is exactly
    // what happened when the content row went from lineHeightLarge to one normal row
    // and these functions kept the old term. Stating the formula independently is what
    // makes that a failure here instead of a gap on screen.
    //
    // Mirrors what the PLAN-based HUDs draw unthemed (gap_bar_hud.cpp /
    // notices_hud.cpp through BaseHud::planPanel): the [Advanced] built-in
    // panel padding, square-on-screen (top/bottom sides convert by
    // cellW * aspect), around one normal content row. If a HUD's content row
    // or the engine's padding conversion changes, this line and CenterStack
    // change together.
    float hudDrawnBoxHeight(const LayoutMetrics& L) {
        return static_cast<float>(L.boxPanelPadding.t + L.boxPanelPadding.b)
                   * L.cellW * (16.0f / 9.0f)
             + L.lineHeightNormal;
    }

    // The layouts to assert over. The shipped one, then a spread of paddings and
    // type sizes -- the derivation is what generalises, so it is the derivation
    // that gets exercised rather than one lucky default.
    std::vector<LayoutMetrics> layouts() {
        std::vector<LayoutMetrics> out;
        for (float pv : {0.0f, 1.0f, 2.0f, 5.0f, 9.0f}) {
            for (float fs : {0.012f, 0.02f, 0.031f}) {
                for (float lh : {0.9f, 1.17335f, 1.8f}) {
                    LayoutMetrics m;
                    // Both padding vocabularies, swept together: the plan reads
                    // the box built-in, the legacy holdouts the cells scalar.
                    m.panelPaddingYCells = pv;
                    m.boxPanelPadding = PanelBox::Sides{pv, pv, pv, pv};
                    m.fontSizeNormal = fs;
                    m.lineHeightRatio = lh;
                    m.derive();
                    out.push_back(m);
                }
            }
        }
        return out;
    }
}

TEST_CASE("center stack: the derived box heights are what the HUDs actually draw") {
    // See hudDrawnBoxHeight -- the one check in this file that does not read its
    // expectation back out of the thing under test.
    for (const LayoutMetrics& L : layouts()) {
        CHECK(CenterStack::gapBarBoxHeight(L)  == doctest::Approx(hudDrawnBoxHeight(L)));
        CHECK(CenterStack::noticesBoxHeight(L) == doctest::Approx(hudDrawnBoxHeight(L)));
    }
}

TEST_CASE("center stack: the shared top does not start above the screen") {
    // The GapBar's is the one that did: its box hangs one padding ABOVE its own
    // offset, so a stack line used directly as the offset clipped the top frame
    // slice away on any theme.
    for (const LayoutMetrics& L : layouts()) {
        CHECK(CenterStack::stackBoxTop(L) >= 0.0f);
    }
}

// NO "the three do not overlap" CASE ANY MORE, and this note is why. The three used
// to default to a column and that case guarded the arithmetic placing the second and
// third. They now share one top and overlap by design: they are alternatives more
// often than companions, so the default is the good spot rather than three spots, and
// a player wanting all three drags two of them. The check would fail by construction.

TEST_CASE("center stack: the shared top lands on the grid") {
    // The panels snap their own edges to the lattice, so a top between two grid lines
    // makes a panel jump by up to half a cell when snapping is on.
    for (const LayoutMetrics& L : layouts()) {
        const float n = CenterStack::stackBoxTop(L) / L.cellH;
        CHECK(std::fabs(n - std::round(n)) < 1.0e-3f);
    }
}

TEST_CASE("center stack: the shipped default is cell 1") {
    // A literal, so a change to it shows up as a decision rather than as a silent
    // reflow of the top of everyone's screen.
    //
    // It was three numbers -- 1, 10 and 18, then 1, 8 and 15 when the GapBar's bar and
    // the Notices slab went from a lineHeightLarge band to one normal row, then 1, 9
    // and 17 when boxGap went to two cells, then 1, 7 and 13 when boxPanelPadding went
    // from 2 cells to 1. Every one of those revisions moved the second and third while
    // the FIRST never budged: it is the one boundary no padding sits inside of. The
    // other two now share it, and the whole class of reflow goes with them.
    //
    // Asserted end-to-end on the rendered panels by
    // tests/integration/tests/center_stack_theme_test.cpp.
    const float cell = layoutDefaults().cellH;
    CHECK(CenterStack::stackBoxTop() / cell == doctest::Approx(1.0f));
}
