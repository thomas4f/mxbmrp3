// ============================================================================
// tests/unit/center_stack_test.cpp
// The centered top panels' default spots: the Dash at the top, and the Gap Bar,
// Notices and Timing sharing one spot under it, on the grid and on screen.
//
// THE BUG THIS PINS. The stack was three frozen cell numbers (1, 6, 11) written
// against boxes that were 4 cells tall. GapBar and Notices then gained panel
// padding like every other HUD -- 8 cells and 6.56 -- and nothing moved the
// numbers. At the SHIPPED defaults the GapBar's box top landed 12.7px above the
// screen, visible in an ordinary race. No test could see it: nothing asserted a
// pixel, and the HUDs were "compile-and-reason only" in the headless suite.
//
// So the tops are asserted from the same metrics the HUDs build from, over 45
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

TEST_CASE("center stack: the tops start on screen, the shared spot under the Dash's") {
    // The GapBar's top is the one that did not: its box hung one padding ABOVE its
    // own offset, so a stack line used directly as the offset clipped the top frame
    // slice away on any theme. Each offset is its box top now.
    for (const LayoutMetrics& L : layouts()) {
        CHECK(CenterStack::stackBoxTop(L) >= 0.0f);
        CHECK(CenterStack::underDashBoxTop(L) > CenterStack::stackBoxTop(L));
    }
}

TEST_CASE("center stack: the tops land on the grid") {
    // The panels snap their own edges to the lattice, so a top between two grid lines
    // makes a panel jump by up to half a cell when snapping is on.
    for (const LayoutMetrics& L : layouts()) {
        for (const float top : { CenterStack::stackBoxTop(L), CenterStack::underDashBoxTop(L) }) {
            const float n = top / L.cellH;
            CHECK(std::fabs(n - std::round(n)) < 1.0e-3f);
        }
    }
}

TEST_CASE("center stack: the shipped defaults are cells 1 and 11") {
    // Literals, so a change to either shows up as a decision rather than as a silent
    // reflow of the top of everyone's screen.
    //
    // The Gap Bar, Notices and Timing stacked under the Dash at 11, 17 and 23 during
    // 1.33's development, each top derived from the box heights above it; every
    // padding revision moved the lower two. They share one spot now, and that class
    // of reflow goes with them.
    //
    // Asserted end-to-end on the rendered panels by
    // tests/integration/tests/center_stack_theme_test.cpp.
    const float cell = layoutDefaults().cellH;
    CHECK(CenterStack::stackBoxTop() / cell == doctest::Approx(1.0f));
    // Two cells under the Dash's bottom at cell 9 (pinned against the drawn Dash by
    // dash_test's fresh-install Gap Bar case). hud/default_slots.h's centre spot is
    // pinned two cells under Timing's drawn box there by default_positions_test.
    CHECK(CenterStack::underDashBoxTop() / cell == doctest::Approx(11.0f));
}
