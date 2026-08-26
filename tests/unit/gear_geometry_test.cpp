// ============================================================================
// tests/unit/gear_geometry_test.cpp
// The gear digit is sized from the FONT and capped by its BOX.
//
// It used to be sized from the box's HEIGHT alone -- three lineHeightNormal --
// which made [Advanced] uiLineHeight, whose job is row PITCH, a font-size knob
// for this one widget. The box's WIDTH is a character count at the base font and
// uiLineHeight does not move it, so the two ran apart: at uiLineHeight = 1.8 the
// numeral came out half again as wide as the panel it is drawn in and hung off
// both sides, reported as "the gear widget has its number incorrectly fall off
// its hud". Every other element answers a raised uiLineHeight with more air and
// the same glyphs.
//
// Pure arithmetic, so it needs no game: the rule is a function of the base font
// size and the row the widget laid out. What it CANNOT see is the digit's ink
// against the panel's actual edges -- that lives in the software-renderer
// capture (tools/hud_window/companion_demo.sh gear), which is how the overflow
// was reproduced and how the fix was confirmed pixel-identical at the defaults.
// ============================================================================
#include "doctest.h"

#include "../../mxbmrp3/hud/gear_geometry.h"
#include "../../mxbmrp3/core/layout_metrics.h"

namespace {
// The widget's row: SpeedWidget's content height, value plus units.
float rowHeightAt(float lineHeightRatio) {
    LayoutMetrics m{};
    m.lineHeightRatio = lineHeightRatio;
    m.derive();
    return m.lineHeightLarge + m.lineHeightNormal;
}
float baseFontSize() {
    LayoutMetrics m{};
    m.derive();
    return m.fontSizeNormal;
}
}  // namespace

TEST_CASE("gear geometry: the shipped defaults are unchanged") {
    const float shipped = LayoutMetrics{}.lineHeightRatio;
    const float row = rowHeightAt(shipped);
    // The two terms meet exactly at the shipped metrics, which is what makes the
    // cap invisible there -- and is why the widget renders pixel-for-pixel as it
    // did before the rule changed.
    CHECK(GearGeometry::fontSize(baseFontSize(), row)
          == doctest::Approx(GearGeometry::boxDrivenFontSize(row)));
}

TEST_CASE("gear geometry: a raised uiLineHeight gives air, not a bigger digit") {
    const float base = baseFontSize();
    const float shipped = LayoutMetrics{}.lineHeightRatio;
    const float atShipped = GearGeometry::fontSize(base, rowHeightAt(shipped));

    for (float ratio : { 1.4f, 1.8f, 2.5f, 4.0f }) {   // 4.0 is the ini's clamp
        CAPTURE(ratio);
        const float row = rowHeightAt(ratio);
        REQUIRE(row > rowHeightAt(shipped));           // the box really did grow
        const float size = GearGeometry::fontSize(base, row);
        CHECK(size == doctest::Approx(atShipped));     // ...the digit did not
        // ...and it still fits the box it is drawn in.
        CHECK(size <= GearGeometry::boxDrivenFontSize(row));
    }
}

TEST_CASE("gear geometry: a lowered uiLineHeight shrinks the digit to fit") {
    const float base = baseFontSize();
    const float atShipped = GearGeometry::fontSize(base, rowHeightAt(LayoutMetrics{}.lineHeightRatio));

    for (float ratio : { 1.0f, 0.8f, 0.5f }) {         // 0.5 is the ini's floor
        CAPTURE(ratio);
        const float row = rowHeightAt(ratio);
        const float size = GearGeometry::fontSize(base, row);
        CHECK(size < atShipped);
        // The box is the binding term here, so the digit is exactly the size that
        // fills it -- the behaviour the widget always had in this direction.
        CHECK(size == doctest::Approx(GearGeometry::boxDrivenFontSize(row)));
    }
}
