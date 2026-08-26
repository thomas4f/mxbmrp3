// ============================================================================
// core/widget_constants.h
// Centralized dimension constants for widgets
// ============================================================================
#pragma once

namespace WidgetDimensions {
    // Content width of a SMALL WIDGET, in characters at the NORMAL font. With the
    // panel padding either side, this is what makes Position / Lap / Time / Clock /
    // Speed / Gear / Lean / G-force / Compass all one width on screen, so a row or a
    // column of them tiles.
    //
    // ONE constant, for the same reason SMALL_WIDGET_CONTENT_LINES below is one:
    // "matches Lean/GForce/Fuel" in a comment is not a mechanism. Five of these were
    // separately-declared 8s agreeing by hand, and the sixth (the timing group, at 12)
    // had drifted away from them without anything noticing.
    //
    // 8 fits the widgets that draw their value at the NORMAL font. It does NOT fit the
    // timing group, which draws at the EXTRA-LARGE font -- twice normal -- so each
    // character costs two of these units: 8 buys four characters and "44/44" or "23:44"
    // is five. That was once accepted as a trade for tiling, and the trade was wrong:
    // the widgets tiled with each other while their own content ran out of the box.
    constexpr int SMALL_WIDGET_WIDTH = 8;

    // Position / Lap / Time / Clock: back to the 12 they were before the unification
    // above swept them in. Five XL characters need 10 units and the remaining two are
    // the padding that keeps "44/44" off the panel edge. These four still tile with
    // EACH OTHER, which is the row users actually build; they are simply wider than the
    // normal-font widgets, because their content is.
    constexpr int STANDARD_WIDTH = 12;                  // Position / Lap / Time / Clock
    constexpr int SPEED_WIDTH    = SMALL_WIDGET_WIDTH;
    constexpr int GEAR_WIDTH     = SMALL_WIDGET_WIDTH;
    constexpr int CRASH_WIDTH    = SMALL_WIDGET_WIDTH;   // tiles beside Speed/Gear
    constexpr int LEAN_WIDTH     = SMALL_WIDGET_WIDTH;
    constexpr int GFORCE_WIDTH   = SMALL_WIDGET_WIDTH;
    constexpr int COMPASS_WIDTH  = SMALL_WIDGET_WIDTH;
    constexpr int SESSION_WIDTH = 43;        // Session widget width
    // Content-area height of a SMALL WIDGET, in line-height units. With the panel
    // padding above and below, this is what makes G-force / Lean / Fuel / Compass /
    // Bars / Tyre Temp / ECU all the same height on screen.
    //
    // ONE constant because three headers each had their own 3.0f, and "matches
    // TyreTemp/Bars" in a comment is not a mechanism. Bars and Tyre Temp had in fact
    // drifted to FOUR lines by adding their label band on top of it rather than
    // carving it out -- which came out the right total height only because both were
    // also missing their bottom padding (1 pad + 4 content == 2 pad + 3 content).
    // Fixing the padding exposed both, so the two errors had been hiding each other.
    constexpr float SMALL_WIDGET_CONTENT_LINES = 3.0f;

    // How much of its em a CAPITAL actually inks, vertically. The gauges' edge
    // labels are all fixed capitals (T B C R S E, L M R, N E S W), so this is
    // their true height -- the rest of a text row is ascender and descender space
    // they never use, and reserving it is what made BarsWidget stand taller than
    // the gauges beside it.
    //
    // It is shared because it is the same measurement on both sides of an
    // ALIGNMENT: the compass insets its cardinal ring by half of it, so the ink
    // reaches the dial rim exactly, and BarsWidget/TyreTempWidget pin their label
    // ink to the bottom of the same three-line content box. Two widgets landing
    // on one line is the point; two spellings of this number is how that rots.
    constexpr float CAP_INK_RATIO = 0.63f;

    // The round-gauge dial (G-force's donut, the compass's ring), as a FRACTION OF
    // THE GAUGE AREA -- i.e. of SMALL_WIDGET_CONTENT_LINES line-heights.
    //
    // Fractions, not absolute normalized lengths, and that is the whole point: they
    // WERE absolute (0.035 and 0.006, pinned to the FMX rotation-arc constants) and
    // scaled only by the HUD's own scale setting. Raising [font] size then grew the
    // widget's PANEL -- its height is rows * line-height -- and left the dial exactly
    // the size it was, so both widgets appeared to ignore the one knob advertised as
    // moving the whole UI, while Bars and Lean (which size their drawing from the box)
    // tracked it correctly. Pinned by gauge_square_test.cpp.
    //
    // ONE definition because the compass's ring is deliberately the G-force donut at
    // another radius; two copies is how the pair drifts apart.
    //
    // The MID radius is derived, not stated, so the ring's OUTER edge is exactly the
    // gauge area. It was a flat 0.5 — a mid radius of half the area, which puts half
    // the ring's thickness OUTSIDE it — and that only ever looked right because the
    // panel's padding caught the overhang. Set the box model's air terms to 0 and the
    // dial pokes out of its own background.
    constexpr float GAUGE_RING_THICKNESS_RATIO = 0.085f;
    constexpr float GAUGE_RING_MID_RADIUS_RATIO = 0.5f - GAUGE_RING_THICKNESS_RATIO * 0.5f;

    // Shared width (chars, measured at the LARGE font) of the centered top-of-screen stack
    // (Timing / Notices) so they line up. NoticesHud sizes "DEFAULT SETUP" at 14; Timing matches.
    constexpr int CENTER_STACK_WIDTH_CHARS = 14;
}
