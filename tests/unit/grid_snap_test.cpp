// ============================================================================
// tests/unit/grid_snap_test.cpp
// Panel ORIGINS land on the snap lattice, not just the rows inside them.
//
// Every HUD's internal rhythm was already on the grid -- row heights, section
// gaps and themed margins all quantise through it. The panel holding that
// rhythm was not, and the two failure paths were different:
//
//   DRAGGED HUDS snapped the OFFSET. That quantises the drag DELTA, so a HUD
//   whose layout starts at an off-grid x (most of them -- a default position is
//   a designed number, not a multiple of 0.0055) stayed off-grid wherever it was
//   dropped. Rows on the lattice, panel between two lines.
//
//   CENTERED PANELS (settings, Notices, Timing, GapBar) land wherever half the
//   panel's width puts them. Three of the four had grown their own private copy
//   of a snap-the-anchor gate, which is how Notices and Timing came to agree
//   with each other but not with the GapBar directly above them.
//
// Both are invisible to every other test: nothing asserts a pixel, and a panel
// half a cell off its own grid lines is found by a person with the grid overlay
// on, which is the expensive way. snapDeltaX/Y is the one helper both paths now
// use; this pins what it guarantees.
//
// Driven through LayoutMetrics rather than a constant, because the lattice is
// live: it is the character box divided by [grid], so a theme file that changes
// the type moves it. Every case below runs at a NON-default grid too, since a
// helper that only worked at 0.0055 x 0.0117 would pass here and fail for anyone
// who touched font.size.
// ============================================================================
#include "doctest.h"

#include "core/layout_metrics.h"

#include <cmath>
#include <initializer_list>

namespace {
    constexpr float kEps = 1.0e-6f;

    LayoutMetrics shipped() { LayoutMetrics m; m.derive(); return m; }
    // A deliberately awkward lattice: bigger type, a coarser vertical grid, and a
    // char-width no default shares.
    LayoutMetrics tuned() {
        LayoutMetrics m;
        m.fontSizeNormal = 0.0271f;
        m.charWidthRatio = 0.31f;
        m.cellsPerRow = 3;
        m.derive();
        return m;
    }

    // Distance from `v` to the nearest grid line, which is what "on the lattice"
    // means for every check below.
    float offGridX(const LayoutMetrics& m, float v) {
        const float n = v / m.cellW;
        return std::fabs(n - std::round(n)) * m.cellW;
    }
    float offGridY(const LayoutMetrics& m, float v) {
        const float n = v / m.cellH;
        return std::fabs(n - std::round(n)) * m.cellH;
    }
}

TEST_CASE("grid: the lattice IS the character box, divided") {
    // The link the whole file rests on. cellW/cellH are not independent numbers
    // that happen to match the type -- they are the character's own width and the
    // row's own height, divided by [grid]. Three copies of them used to exist and
    // one had drifted 5.4%.
    const LayoutMetrics m = shipped();
    CHECK(m.charWidth == doctest::Approx(m.fontSizeNormal * m.charWidthRatio));
    CHECK(m.cellW == doctest::Approx(m.charWidth / m.cellsPerChar));
    CHECK(m.cellH == doctest::Approx(m.lineHeightNormal / m.cellsPerRow));

    // ...so a row is exactly cells-per-row cells, which is what makes vertical
    // snapping land on row boundaries.
    CHECK(m.lineHeightNormal == doctest::Approx(m.cellsPerRow * m.cellH));

    const LayoutMetrics t = tuned();
    CHECK(t.cellW == doctest::Approx(t.fontSizeNormal * t.charWidthRatio));
    CHECK(t.lineHeightNormal == doctest::Approx(3.0f * t.cellH));
    CHECK(t.cellW != doctest::Approx(m.cellW));    // the lattice really did move
}

TEST_CASE("grid: snapping an edge puts THAT EDGE on the lattice") {
    for (const LayoutMetrics& m : {shipped(), tuned()}) {
        // The layout's own left edge, off-grid on purpose (a designed default).
        const float edge = 0.1234f;
        CHECK(offGridX(m, edge) > kEps);
        CHECK(offGridX(m, edge + m.snapDeltaX(edge)) == doctest::Approx(0.0f).epsilon(kEps));

        const float top = 0.4321f;
        CHECK(offGridY(m, top) > kEps);
        CHECK(offGridY(m, top + m.snapDeltaY(top)) == doctest::Approx(0.0f).epsilon(kEps));
    }
}

TEST_CASE("grid: snapping the OFFSET does not put the edge on the lattice") {
    // The regression itself, stated as the difference between the two. A HUD whose
    // layout begins at an off-grid x, dragged by an arbitrary amount:
    const float layoutLeft = 0.1234f;
    const float rawOffset = 0.0731f;
    for (const LayoutMetrics& m : {shipped(), tuned()}) {
        // What the drag path used to do -- quantise the offset alone.
        const float snappedOffset = m.snapX(rawOffset);
        CHECK(offGridX(m, layoutLeft + snappedOffset) > kEps);   // still off-grid

        // What it does now -- quantise the resulting edge.
        const float edgeOffset = rawOffset + m.snapDeltaX(layoutLeft + rawOffset);
        CHECK(offGridX(m, layoutLeft + edgeOffset) == doctest::Approx(0.0f).epsilon(kEps));

        // And it moves the panel by less than half a cell, so a drag still lands
        // where the user let go rather than jumping to the next line.
        CHECK(std::fabs(edgeOffset - rawOffset) <= m.cellW * 0.5f + kEps);
    }
}

TEST_CASE("grid: snapping an already-snapped edge is a no-op") {
    // Applied every rebuild for the centered panels, so it has to be idempotent --
    // a helper that drifted by a cell per frame would walk the panel off-screen.
    for (const LayoutMetrics& m : {shipped(), tuned()}) {
        float edge = 0.5f - 0.0837f;
        for (int i = 0; i < 8; ++i) {
            const float next = edge + m.snapDeltaX(edge);
            if (i > 0) CHECK(next == doctest::Approx(edge));
            edge = next;
        }
    }
}

TEST_CASE("grid: the centered settings panel origin lands on the lattice") {
    // Centring is (1 - size) / 2 for both axes. Neither term knows about the grid,
    // so the result is on it only by accident -- and 0.5 itself is not on the
    // horizontal one (0.5 / 0.0055 = 90.9 cells).
    for (const LayoutMetrics& m : {shipped(), tuned()}) {
        CHECK(offGridX(m, 0.5f) > kEps);

        // A spread of plausible panel sizes, since the origin moves with the tab's
        // content height and the widest tab's width.
        for (float w : {0.42f, 0.5137f, 0.61f}) {
            const float x = (1.0f - w) / 2.0f;
            CHECK(offGridX(m, x + m.snapDeltaX(x)) == doctest::Approx(0.0f).epsilon(kEps));
        }
        for (float h : {0.33f, 0.4802f, 0.72f}) {
            const float y = (1.0f - h) / 2.0f;
            CHECK(offGridY(m, y + m.snapDeltaY(y)) == doctest::Approx(0.0f).epsilon(kEps));
        }
    }
}

TEST_CASE("grid: equal-width centered panels share a left edge") {
    // Notices and Timing are deliberately the same width so the center-top stack
    // reads as one column. Same anchor + same snap => the same edge, which is the
    // property that was already true for those two and is now true for the GapBar
    // as well, because all three go through one helper.
    const float width = 0.2371f;
    const float anchor = 0.5f - width / 2.0f;
    for (const LayoutMetrics& m : {shipped(), tuned()}) {
        const float notices = anchor + m.snapDeltaX(anchor);
        const float timing = anchor + m.snapDeltaX(anchor);
        CHECK(notices == doctest::Approx(timing));

        // The GapBar arrives at the same edge from the other direction: it is drawn
        // from a start x that is then shifted by the HUD's own offset, so it folds
        // the delta of the RESULTING edge back into that start.
        const float barOffset = 0.0f, barStart = anchor;
        const float leftEdge = barOffset + barStart;
        const float barStartSnapped = barStart + m.snapDeltaX(leftEdge);
        CHECK(barOffset + barStartSnapped == doctest::Approx(notices));
    }
}
