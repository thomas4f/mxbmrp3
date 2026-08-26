// ============================================================================
// tests/unit/test_gamepad_geometry.cpp
// Unit tests for hud/gamepad_geometry.h — the gamepad widget's unit system.
//
// THE BUG THIS PINS, reported in-game: raise [Advanced] uiFontSize and the
// controller picture grows while its buttons stay where they were. The sticks,
// d-pad and face buttons walk off their sockets.
//
// The cause is a split reference. The frame's width comes from the type; the ~30
// hand-placed per-variant offsets are absolute normalized distances, and they were
// scaled by the widget's own scale slider ALONE. Two of the three inputs that move
// the frame (uiFontSize, and a theme's content inset) therefore never reached the
// interior at all.
//
// The fix is to derive the interior's em BY INVERTING the frame's own width, so
// there is exactly one number and it necessarily carries all three inputs. That
// inversion is the thing these cases pin: kFrameEm must stay the true em-width of
// the frame, or interiorEm() hands back an em the frame was not drawn at and the
// whole interior silently rescales. It is one expression restating another, which
// is precisely the shape that rots when someone edits one side — change
// BACKGROUND_WIDTH_CHARS or the panel padding and the identity below is what says
// so.
// ============================================================================
#include "doctest.h"

#include "hud/gamepad_geometry.h"
#include "core/layout_metrics.h"

#include <cmath>
#include <initializer_list>

using namespace GamepadLayout;

// How the widget itself computes its frame: BACKGROUND_WIDTH_CHARS of monospace
// text plus panel padding at both ends. Written the way PluginUtils does it
// (chars * cellWidth, NOT chars * fontSize * ratio) — the two differ in float32
// rounding, and this file's whole subject is an exact inversion.
static float frameWidthAt(float fontSize, float charWidthRatio, float padCells) {
    const float cellWidth = fontSize * charWidthRatio;
    return kFrameChars * cellWidth + 2.0f * (padCells * cellWidth);
}

TEST_CASE("gamepad: interiorEm inverts the frame width the widget computes") {
    LayoutMetrics m;   // the shipped defaults
    const float pad = m.panelPaddingXCells;

    // The authored constants ARE the shipped metrics. If this fails, the widget's
    // copy has drifted from the real layout and every distance inside it is being
    // spent in the wrong em.
    REQUIRE(kCharRatio == doctest::Approx(m.charWidthRatio));
    REQUIRE(kPadChars == doctest::Approx(pad));
    REQUIRE(kFontSize == doctest::Approx(m.fontSizeNormal));

    for (float fs : {0.010f, 0.0200f, 0.025f, 0.040f, 0.200f}) {
        const float frame = frameWidthAt(fs, m.charWidthRatio, pad);
        CAPTURE(fs);
        // Exact to float32: interiorEm is the algebraic inverse, not a fit.
        CHECK(interiorEm(frame) == doctest::Approx(fs).epsilon(1e-6));
        // ...and at authoring size the offsets are spent unchanged.
        CHECK(unitScale(interiorEm(frameWidthAt(kFontSize, m.charWidthRatio, pad)))
              == doctest::Approx(1.0f));
    }
}

// The property the fix actually buys, stated without reference to font size: the
// widget is SIMILAR to itself at every frame width. Doubling the frame doubles
// every interior distance, so nothing inside can drift relative to the artwork
// however the frame came to be that size — bigger font, bigger scale slider, or a
// theme insetting the content.
TEST_CASE("gamepad: the interior is similar to itself at any frame width") {
    const float base = 0.2585f;                    // the shipped frame width
    for (float k : {0.5f, 1.0f, 1.37f, 3.0f}) {
        const float em = interiorEm(base * k);
        CAPTURE(k);
        CHECK(em == doctest::Approx(interiorEm(base) * k));
        // Everything the widget spends is one of these three times the em, so
        // showing the em scales is showing the interior scales.
        CHECK(em * kCharRatio == doctest::Approx(interiorEm(base) * kCharRatio * k));
        CHECK(em * kLineRatio == doctest::Approx(interiorEm(base) * kLineRatio * k));
        CHECK(unitScale(em) == doctest::Approx(unitScale(interiorEm(base)) * k));
    }
}

// The row pitch stays a property of the ARTWORK, not of the global grid. It was
// `dims.lineHeightNormal` once; a grid retune (0.0222 -> ~0.0235) grew the rows
// relative to the frame and pushed the buttons out of the bottom and right, and
// the workaround was to freeze the ratio at the widget. Freezing it was right —
// what was wrong was that it was frozen against fontSize, which the grid can still
// move. Here it is frozen against the frame, which is where it was measured.
TEST_CASE("gamepad: the row pitch is frozen to the artwork, not the grid") {
    LayoutMetrics m;
    const float frame = frameWidthAt(m.fontSizeNormal, m.charWidthRatio, m.panelPaddingXCells);
    const float lineH = interiorEm(frame) * kLineRatio;
    CHECK(lineH == doctest::Approx(0.0222f));   // the authored pitch, cbbd1a2

    // Retune the grid the way #256 did. The frame moves not at all (line height is
    // not one of its inputs), so neither does the interior.
    m.lineHeightRatio = 1.175f;
    m.derive();
    const float frameAfter = frameWidthAt(m.fontSizeNormal, m.charWidthRatio, m.panelPaddingXCells);
    CHECK(frameAfter == doctest::Approx(frame));
    CHECK(interiorEm(frameAfter) * kLineRatio == doctest::Approx(lineH));
}
