// ============================================================================
// tests/unit/layout_metrics_test.cpp
// The layout vocabulary as data: that the derived values still hold the
// relationships the compiled constants used to guarantee.
//
// WHY THESE STILL EARN THEIR KEEP now that almost every field is a compiled
// constant again: derive() is what holds the relationships, and it runs against
// ARBITRARY values, not just the shipped ones -- a user can still type uiFontSize
// and uiLineHeight into [Advanced], and every tier, row and grid cell follows from
// them. A static_assert would only cover the defaults.
//
// The cases that tested a KEY-STRING surface (apply/unknown/out-of-range/roots/
// coherence) are gone with it: there is no layout file any more. See
// layout_metrics.h's header for why the tuning surface shrank from forty keys to
// two.
// ============================================================================
#include "doctest.h"

#include <limits>

#include "core/layout_metrics.h"
#include "core/plugin_constants.h"

namespace {
    LayoutMetrics derived() { LayoutMetrics m; m.derive(); return m; }
}

TEST_CASE("layout metrics: a non-finite font size or line height cannot poison the vocabulary") {
    // THE CLAMP DOES NOT CATCH NaN. `(v < lo) ? lo : (v > hi ? hi : v)` compares false
    // against BOTH bounds for NaN and stores it -- and these two feed derive(), so one
    // bad value is not one bad setting: every metric, the snap lattice and every HUD
    // coordinate computed from them go NaN, and the settings writer then persists `nan`
    // straight back out. Nothing recovers on its own.
    //
    // Hand-editing the INI is a supported workflow (CLAUDE.md), so this arrives from a
    // file, not from the UI -- which is why the guard is in the setter and not in a
    // control's validation.
    for (float bad : {std::numeric_limits<float>::quiet_NaN(),
                      std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()}) {
        LayoutMetrics m;
        layoutSetFontSize(m, bad);
        CHECK(std::isfinite(m.fontSizeNormal));
        CHECK(std::isfinite(m.cellW));
        CHECK(std::isfinite(m.cellH));
        CHECK(std::isfinite(m.lineHeightNormal));
        CHECK(m.fontSizeNormal > 0.0f);

        LayoutMetrics n;
        layoutSetLineHeight(n, bad);
        CHECK(std::isfinite(n.lineHeightRatio));
        CHECK(std::isfinite(n.cellH));
        CHECK(std::isfinite(n.lineHeightNormal));
        CHECK(n.lineHeightRatio > 0.0f);
    }

    // The finite bounds still clamp, which is what says the guard was added ahead of the
    // comparisons rather than replacing them.
    LayoutMetrics lo;  layoutSetFontSize(lo, -5.0f);
    CHECK(lo.fontSizeNormal == doctest::Approx(0.002f));
    LayoutMetrics hi;  layoutSetLineHeight(hi, 99.0f);
    CHECK(hi.lineHeightRatio == doctest::Approx(4.0f));
}

TEST_CASE("layout: no tier's glyph box is taller than the row it sits in") {
    // THE INVARIANT uiLineHeight has and never stated: it IS the glyph box measured
    // against its row. Every tier pairs fontSizeX with rowX, and all of them except
    // Large pair EQUAL multiples (normal 1.0/1.0, XL 2.0/2.0) -- so the ratio reduces
    // to lineHeightRatio itself, and any value below 1.0 makes the box taller than the
    // line for four tiers out of five.
    //
    // Found by eye and confirmed by capture: at 0.89 the Position and Lap widgets'
    // XL digits clipped, because a 2.0x glyph does not fit a 2.0x0.89 row. Large is
    // the exception and the reason is deliberate -- 1.5x text in a 2x row, because a
    // title wants air its glyphs do not.
    for (float lhr : {1.0f, 1.05f, 1.1f, 1.5f, 4.0f}) {
        LayoutMetrics m; m.lineHeightRatio = lhr; m.derive();
        CHECK(m.fontSizeExtraSmall  <= m.lineHeightExtraSmall);
        CHECK(m.fontSizeSmall       <= m.lineHeightSmall);
        CHECK(m.fontSizeNormal      <= m.lineHeightNormal);
        CHECK(m.fontSizeLarge       <= m.lineHeightLarge);
        CHECK(m.fontSizeExtraLarge  <= m.lineHeightExtraLarge);
    }
    // ...and the shipped default is on the safe side of that floor.
    CHECK(derived().lineHeightRatio >= 1.0f);
}

// THE SETTINGS PANEL'S SCREEN FIT IS NOT PINNED HERE ANY MORE, and the case that
// tried is gone rather than adapted. It recomputed the panel's height from
// LayoutMetrics -- a third spelling of the sum in SettingsHud, kept true by hand --
// times a `settingsRows` floor that no longer exists: the panel measures its own tab
// now (SettingsHud::buildPanel runs twice) and is as tall as that measurement makes
// it. A unit test cannot reach the measurement without the tab renderers, and a
// reduced copy of the sum would agree with itself while the real panel overflowed.
// theme_geometry_test's "the settings panel fits the screen" drives the real thing.

TEST_CASE("layout: font.size moves the WHOLE UI, tiers included") {
    // It did not. The tiers were absolute screen fractions, so raising font.size
    // grew the body text and left every title exactly where it was -- the one knob
    // whose documented job was "moves the entire UI" was the one that didn't.
    LayoutMetrics base; base.derive();
    LayoutMetrics m; m.fontSizeNormal = base.fontSizeNormal * 2.0f; m.derive();

    CHECK(m.fontSizeSmall == doctest::Approx(2.0f * base.fontSizeSmall));
    CHECK(m.fontSizeLarge == doctest::Approx(2.0f * base.fontSizeLarge));
    CHECK(m.fontSizeExtraLarge == doctest::Approx(2.0f * base.fontSizeExtraLarge));
    CHECK(m.lineHeightNormal == doctest::Approx(2.0f * base.lineHeightNormal));
    CHECK(m.cellW == doctest::Approx(2.0f * base.cellW));
    CHECK(m.cellH == doctest::Approx(2.0f * base.cellH));
}

TEST_CASE("layout: a tier's TEXT size and its ROW are separate knobs") {
    // The -l tier is 1.5x text in a 2x row, because a title wants air its glyphs do
    // not. Those were two literals inside derive(); pinning it here is what makes
    // the asymmetry a decision rather than a discrepancy.
    const LayoutMetrics d = derived();
    CHECK(d.fontSizeL == doctest::Approx(1.5f));
    CHECK(d.rowL == doctest::Approx(2.0f));
    CHECK(d.lineHeightLarge == doctest::Approx(2.0f * d.lineHeightNormal));

    // ...and moving one leaves the other alone.
    LayoutMetrics m; m.fontSizeL = 1.25f; m.derive();
    CHECK(m.fontSizeLarge == doctest::Approx(1.25f * m.fontSizeNormal));
    CHECK(m.lineHeightLarge == doctest::Approx(2.0f * m.lineHeightNormal));
}

TEST_CASE("layout: [grid] subdivides the character box and nothing else") {
    // cells-per-char / cells-per-row are the ONLY thing between the type and the
    // lattice, so this is where "the grid follows the font" is pinned.
    LayoutMetrics m; m.cellsPerRow = 1; m.derive();
    CHECK(m.cellH == doctest::Approx(m.lineHeightNormal));   // a cell IS a row now
    CHECK(m.cellW == doctest::Approx(m.charWidth));          // ...X is untouched

    LayoutMetrics x; x.cellsPerChar = 2; x.derive();
    CHECK(x.cellW == doctest::Approx(x.charWidth * 0.5f));
    CHECK(x.cellH == doctest::Approx(derived().cellH));      // ...Y is untouched
}

TEST_CASE("layout: padding does NOT move the row pitch") {
    // The bug this whole rework exists for. Row pitch used to be solved from the
    // gauge widgets' square requirement -- lineHeight = (8+2*padH)*cellW*aspect /
    // (3+padV) -- so padding dragged it: panelPaddingYCells 2->3 shortened every row by 17%
    // and panelPaddingXCells 2->3 lengthened it by 17%. On a 37-row settings panel that read
    // as the text being crushed, which is exactly how it was reported.
    LayoutMetrics base; base.derive();

    for (float ph : {0.0f, 1.0f, 2.0f, 5.0f}) {
        for (float pv : {0.0f, 1.0f, 2.0f, 5.0f}) {
            LayoutMetrics m;
            m.panelPaddingXCells = ph;
            m.panelPaddingYCells = pv;
            m.derive();
            // Pitch, glyph size and the grid are all untouched...
            CHECK(m.lineHeightNormal == doctest::Approx(base.lineHeightNormal));
            CHECK(m.cellH == doctest::Approx(base.cellH));
            CHECK(m.cellW == doctest::Approx(base.cellW));
            CHECK(m.fontSizeNormal == doctest::Approx(base.fontSizeNormal));
            // ...and the padding is the only thing that moved.
            CHECK(m.panelPaddingX == doctest::Approx(ph * base.cellW));
            CHECK(m.panelPaddingY == doctest::Approx(pv * base.cellH));
        }
    }
}

TEST_CASE("layout: row pitch is its own knob and moves nothing else") {
    LayoutMetrics base; base.derive();
    LayoutMetrics m;
    m.lineHeightRatio = base.lineHeightRatio * 1.5f;
    m.derive();

    CHECK(m.lineHeightNormal == doctest::Approx(base.lineHeightNormal * 1.5f));
    CHECK(m.cellH == doctest::Approx(base.cellH * 1.5f));      // the vertical grid follows it
    CHECK(m.cellW == doctest::Approx(base.cellW));             // ...the horizontal one does not
    CHECK(m.fontSizeNormal == doctest::Approx(base.fontSizeNormal));   // glyphs unchanged
    CHECK(m.panelPaddingX == doctest::Approx(base.panelPaddingX));         // padding unchanged
}

TEST_CASE("layout: panel padding is in grid cells on BOTH axes") {
    // panelPaddingXCells/panelPaddingYCells are what a user types; panelPaddingX/V are what the HUDs
    // lay out with. Both keys are CELLS, so the same visual gap is the same number
    // on each axis -- the vertical one used to be line heights, where a 2-cell gap
    // had to be written as 1 and anyone measuring against the grid overlay found a
    // number the file disagreed with.
    LayoutMetrics m;
    m.panelPaddingXCells = 3.0f;
    m.panelPaddingYCells = 1.0f;
    m.derive();
    CHECK(m.panelPaddingX == doctest::Approx(3.0f * m.cellW));
    CHECK(m.panelPaddingY == doctest::Approx(1.0f * m.cellH));

    // The shipped look, and what the user reported seeing: exactly 2 grid cells
    // between quad edge and content, on both axes. Also what Padding::HUD_* held --
    // 2 cells across, and 2 cells down IS one full line height.
    LayoutMetrics d; d.derive();
    CHECK(d.panelPaddingX == doctest::Approx(2.0f * d.cellW));
    CHECK(d.panelPaddingY == doctest::Approx(2.0f * d.cellH));
    CHECK(d.panelPaddingY == doctest::Approx(d.lineHeightNormal));
}

TEST_CASE("layout: the two settable roots clamp rather than reject") {
    // These arrive from [Advanced] in a file the plugin itself rewrites, so there is
    // no author to warn and no previous value worth keeping -- landing on the nearest
    // sane number is the useful behaviour, and it is what stops a hand-typed 0 from
    // dividing the whole lattice by zero.
    LayoutMetrics m;
    layoutSetFontSize(m, 0.04f);
    CHECK(m.fontSizeNormal == doctest::Approx(0.04f));
    CHECK(m.cellW == doctest::Approx(0.04f * m.charWidthRatio));   // derive() ran

    layoutSetFontSize(m, 0.0f);
    CHECK(m.fontSizeNormal == doctest::Approx(0.002f));
    CHECK(m.cellW > 0.0f);
    layoutSetFontSize(m, 99.0f);
    CHECK(m.fontSizeNormal == doctest::Approx(0.200f));

    layoutSetLineHeight(m, 2.0f);
    CHECK(m.lineHeightRatio == doctest::Approx(2.0f));
    CHECK(m.lineHeightNormal == doctest::Approx(2.0f * m.fontSizeNormal));
    layoutSetLineHeight(m, -5.0f);
    CHECK(m.lineHeightRatio == doctest::Approx(0.5f));
    layoutSetLineHeight(m, 100.0f);
    CHECK(m.lineHeightRatio == doctest::Approx(4.0f));
}
TEST_CASE("layout: a normal row is two grid cells, a large row four") {
    // What vertical snapping relies on: snap to a cell and you land on a row
    // boundary. If cellH stopped being half a line height, every SNAP_TO_GRID_Y
    // would quantise to something that is not a row.
    for (float fs : {0.012f, 0.0200f, 0.05f}) {
        LayoutMetrics m;
        m.fontSizeNormal = fs;
        m.derive();
        CHECK(m.lineHeightNormal == doctest::Approx(2.0f * m.cellH));
        CHECK(m.lineHeightLarge == doctest::Approx(4.0f * m.cellH));
    }
}

TEST_CASE("layout: one grid cell is one character") {
    // The equality that lets the settings panel lay out in whole characters and
    // still land on the lattice -- its columns are char counts, its snapping is
    // cells, and they are only interchangeable while this holds.
    LayoutMetrics m;
    m.fontSizeNormal = 0.031f;
    m.charWidthRatio = 0.3f;
    m.derive();
    CHECK(m.cellW == doctest::Approx(m.fontSizeNormal * m.charWidthRatio));
}

TEST_CASE("layout: settings defaults are the values SettingsMetrics used to hold") {
    // Literals, because the constants these replaced are gone -- so this is the
    // only remaining record that the migration preserved them. A default changed
    // by accident reshapes the panel for every user who never opens a theme file.
    const LayoutMetrics m = derived();
    // The panel WIDTH is no longer a stated 71: it is composed from the column
    // asks below plus the trough (the [panel] gap term, zero by default — so
    // the columns sit flush until a theme states a gap) and the theme/padding
    // chrome. What is pinned now is the asks themselves.
    // 17, not the 16 SettingsMetrics held: the sidebar now has to fit a
    // right-aligned status tag ("New"/"Beta") on the same row as the tab name, and
    // "Appearance" + "New" needs every one of the 17 (see the comment on the field).
    CHECK(m.settingsSidebarWidth == 17);
    CHECK(m.settingsLabelColumn == 2);
    CHECK(m.settingsControlColumn == 28);
    CHECK(m.settingsContentAreaChars() == 53);      // the stated column ask
    // Unthemed the label column is an indent (off once); themed it is a margin inside
    // the section card (off twice), mirroring rowSpanWidth(). The themed number is the
    // tooltip guard's bound -- it is the narrower box.
    CHECK(m.settingsTooltipCharsPerLine(/*themedCard=*/false) == 51);  // 53 - 2
    CHECK(m.settingsTooltipCharsPerLine(/*themedCard=*/true)  == 50);  // ...less the card border
}

// ---------------------------------------------------------------------------
// Parsing. These files are hand-edited and a theme ships whatever a skinner
// typed, so the failure mode to guard is not "wrong shade of blue" -- it is a
// value that divides the layout by zero or pushes the panel off the screen.
// ---------------------------------------------------------------------------

TEST_CASE("layoutSetBoxSides clamps per side and degrades to the shipped default") {
    // The [Advanced] air-term built-ins arrive from a hand-edited INI; each
    // side is clamped to 0..12 cells, and a side that does not parse finite
    // falls to the shipped default's side rather than to zero -- the both-ends
    // rule for persisted values, applied per component.
    LayoutMetrics m;
    layoutSetBoxSides(m.boxPanelPadding, "3 1.5", LayoutMetrics{}.boxPanelPadding);
    CHECK(m.boxPanelPadding.t == doctest::Approx(3.0));
    CHECK(m.boxPanelPadding.l == doctest::Approx(1.5));
    layoutSetBoxSides(m.boxPanelPadding, "99", LayoutMetrics{}.boxPanelPadding);
    CHECK(m.boxPanelPadding.t == doctest::Approx(12.0));   // ceiling
    layoutSetBoxSides(m.boxPanelPadding, "-1 2", LayoutMetrics{}.boxPanelPadding);
    // -1 2: two tokens -> vertical -1 (negative -> the shipped default's own top),
    // horizontal 2. Against the SHIPPED value rather than a literal: what this
    // pins is that a bad side falls back to the default, not what the default is
    // (which the shipped-defaults case above states outright).
    CHECK(m.boxPanelPadding.t == doctest::Approx(LayoutMetrics{}.boxPanelPadding.t));
    CHECK(m.boxPanelPadding.l == doctest::Approx(2.0));
    layoutSetBoxSides(m.boxContentMargin, "garbage", LayoutMetrics{}.boxContentMargin);
    CHECK(m.boxContentMargin.t == doctest::Approx(LayoutMetrics{}.boxContentMargin.t));
}
