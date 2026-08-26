// ============================================================================
// tests/unit/corner_button_test.cpp
// The two corner buttons (settings gear, director camera) do not overlap, stay on
// screen, and their declared box matches the box the widgets actually build.
//
// WHAT THIS PINS, AND THE BUG IT COMES FROM. Both widgets size their box with
// BaseHud::fitPanelToGrid(paddingH + icon + paddingH, panelHeight(dim, icon)) and
// then place it at a default stated in whole grid cells. Those are two different
// files, and the box grew without the defaults moving: it was authored at 6 cells,
// fitPanelToGrid rounded it onto the lattice at 8, and nothing recomputed the
// positions. The gear's right edge went 1.9px off the screen and the camera
// overlapped it by exactly one cell.
//
// THE OVERLAP HAD TEETH. isClicked() is a const edge test that consumes nothing and
// HudManager polls both buttons unconditionally in the same frame, so a cursor in
// the shared strip passed both hit-tests: one click toggled the auto-director AND
// opened the settings panel.
//
// CornerButtons::DIRECTOR_X is now derived from SETTINGS_X, so overlap is
// unspellable. What is NOT free is BOX_CELLS_W: the header states it, the widgets
// compute it. This test is the joint -- it recomputes the widgets' real expression
// from LayoutMetrics and fails if the constant has drifted, which is the exact
// check that was missing.
//
// Pure headers only (LayoutMetrics + the constants), so it compiles into the unit
// suite with no game and no BaseHud.
// ============================================================================
#include "doctest.h"

#include "core/layout_metrics.h"
#include "hud/corner_buttons.h"

namespace {

// BaseHud::fitPanelToGrid()'s width arm, and panelHeight()'s, at scale 1.0 -- the
// expression both widgets actually build their box from. Kept as a local copy on
// purpose: if BaseHud's changes, this test SHOULD fail and be re-derived, because
// that is precisely the drift it exists to catch.
struct Box { float w, h; };
Box widgetBox(const LayoutMetrics& L) {
    const float icon = L.fontSizeLarge * L.titleIconSize;
    return { L.ceilX(L.panelPaddingX + icon + L.panelPaddingX),
             L.ceilY(L.panelPaddingY + icon + L.panelPaddingY) };
}

}  // namespace

TEST_CASE("corner buttons: the declared box matches the one the widgets build") {
    LayoutMetrics L; L.derive();
    const Box b = widgetBox(L);

    CHECK(b.w == doctest::Approx(CornerButtons::BOX_CELLS_W * L.cellW));
    CHECK(b.h == doctest::Approx(CornerButtons::BOX_CELLS_H * L.cellH));
}

TEST_CASE("corner buttons: they do not overlap") {
    LayoutMetrics L; L.derive();

    const float dirL = CornerButtons::DIRECTOR_X * L.cellW;
    const float dirR = dirL + CornerButtons::BOX_CELLS_W * L.cellW;
    const float setL = CornerButtons::SETTINGS_X * L.cellW;

    // The whole point: a click cannot land in both boxes.
    CHECK(dirR <= setL);
    // And the air between them is the one cell the layout keeps everywhere else --
    // touching would pass the check above while still reading as one merged slab,
    // which is exactly how the bug looked on screen.
    CHECK((setL - dirR) == doctest::Approx(CornerButtons::GAP_CELLS * L.cellW));
}

TEST_CASE("corner buttons: both stay on screen") {
    LayoutMetrics L; L.derive();

    const float setR = (CornerButtons::SETTINGS_X + CornerButtons::BOX_CELLS_W) * L.cellW;
    CHECK(setR <= 1.0f);
    // The gear has always drawn with its right edge on 180 cells (0.990). Pinned as a
    // VALUE, not just as "on screen": the off-screen bug passed a bare `< 1.0` sense
    // check by only 1.9px, and the shipped look is the tighter statement.
    CHECK(setR == doctest::Approx(0.990f));

    CHECK(CornerButtons::DIRECTOR_X * L.cellW >= 0.0f);
}
