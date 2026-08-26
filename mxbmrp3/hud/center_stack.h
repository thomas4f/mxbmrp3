// ============================================================================
// hud/center_stack.h
// Where the three centered top-of-screen panels sit: GapBar -> Notices -> Timing.
//
// THE STACK IS DERIVED FROM THE BOXES, not from three frozen cell numbers.
//
// It used to be the numbers: "each box is lineHeightLarge tall (4 cells), one cell
// between, so the tops land on 1, 6 and 11". That held exactly as long as the boxes
// really were 4 cells. When GapBar and Notices gained panel padding like every
// other HUD, the GapBar box became 8 cells and the Notices box 6.56 -- and the
// frozen 1/6/11 put the GapBar's top edge 12.7px ABOVE the screen (its top frame
// slice clipped away) while the Notices box overlapped it by 45px. Both are on
// screen together in an ordinary race: gap bar plus a blue-flag or PB notice.
//
// So the tops are computed from the same metrics the HUDs build their boxes from,
// and a padding change now moves the whole stack instead of breaking it. Each line
// is rounded UP to a whole cell, because a box height need not be integral (the
// Notices box is fontSize-large plus padding = 6.56 cells) and the stack has to
// stay on the lattice its panels snap to.
//
// UNSCALED, from layoutDefaults(): these are DEFAULT POSITIONS, written once into
// each HUD's offset, and a HUD's own scale is a user setting applied afterwards.
// Scaling one panel of the stack has always moved it relative to the others.
//
// Deliberately NOT in widget_constants.h, which 26 headers include -- this needs
// layout_config.h, and putting that on a universally-included header is what makes
// a one-line edit rebuild the whole project.
// ============================================================================
#pragma once

#include "../core/layout_config.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"

namespace CenterStack {

// Top edge of the Nth grid cell from the screen top.
inline float rowY(const LayoutMetrics& L, int cell) {
    return static_cast<float>(cell) * L.cellH;
}
inline float rowY(int cell) { return rowY(layoutDefaults(), cell); }

// Every line below takes the metrics EXPLICITLY, with a no-argument overload that
// reads the global defaults -- which is what the HUDs call.
//
// Not ceremony: the test asserts these hold at ARBITRARY padding, not just the
// shipped value, and "the shipped default is fine" is precisely what was believed
// while the stack was broken at it. Reading a singleton inside would have made
// that claim untestable, so the test would have said "arbitrary" and checked one.

// TWO cells of air between boxes, and one above the first.
//
// It was one cell, which is what the boxes need UNTHEMED and one short of what they
// need under either shipped theme. A theme adds its frame clearance to every panel's
// padding -- one cell at each end, so two cells of growth per box -- while these
// defaults do not move, and the stack came out overlapping by exactly a cell at both
// boundaries. Measured at the shipped defaults under a 2-cell frame:
//
//   unthemed  gapbar 1..7   notices 9..15   timing 17..23    (2 cells of air each)
//   themed    gapbar 1..9   notices 9..17   timing 17..25    (flush, no overlap)
//
// So two cells is the number that holds in both, at the cost of a cell of extra air
// unthemed. The alternative -- making these defaults theme-aware -- is the bug that
// was just removed from the Gap Bar: a stored offset computed from the live padding is
// stale the moment the theme changes, and the offsets ARE stored (they are what a drag
// edits). A constant that is right in both states beats a live one that is right in
// neither for long.
//
// The overlap only became visible when Notices gained its body card: until then its
// coloured slab had no frame of its own, so the panel above it overlapped a shape with
// no border and nothing looked wrong.
inline float boxGap(const LayoutMetrics& L) { return 2.0f * L.cellH; }

// The two box HEIGHTS, mirroring what the HUDs actually build:
//   GapBar   paddingV + lineHeightLarge + paddingV   (gap_bar_hud.cpp, boxHeight)
//   Notices  paddingV + fontSizeLarge  + paddingV    (notices_hud.cpp, noticeQuadHeight)
// UNTHEMED AND TITLE-OFF, which is the shipped default and the only state these
// numbers describe. Two things move a real box off them and neither is visible from
// here:
//   - a TITLE, which adds its reserved row (the toggles default off);
//   - a THEME, which widens it to ceilY(frameBorderY + the body card's
//     border, where one is drawn, less the base padding). This claim has been wrong
//     twice -- first that the pad was zero for both boxes, then that it differed
//     between them -- so state it carefully: BOTH boxes carry a body card
//     (gap_bar_hud.cpp, notices_hud.cpp), so both count that card's border in the
//     need. The whole term is clamped away while the need stays under the panel's own
//     2-cell padding, which for a 1-cell card is true at [frame] size 1 and false from
//     size 2 -- one size earlier than for a card-less panel.
//     (theme_geometry_test prints all four in its Notices/GapBar height case.)
//
// The consequence is bounded and deliberate rather than latent: these are DEFAULT
// offsets, written once into each HUD's position, so a themed or captioned stack is
// nudged by hand exactly as any other panel is. Making the stack itself theme- and
// title-aware means making those two baked-in defaults live, which is a migration;
// see the note at GapBarHud::resetToDefaults.
// ONE NORMAL ROW of content each, which is what both HUDs draw (GapBar's bar and
// Notices' slab are both BaseHud::bigValueRowHeight; each box is that row plus a
// padding at each end, and untitled Notices spends its pair on the slab itself
// because there the slab IS the box).
//
// These read lineHeightLarge and fontSizeLarge until the row shrank, and the drift
// was invisible from here: every stack line below a box is derived from these
// heights, so an overstated height does not overlap anything -- it just leaves a gap
// nobody asked for between the boxes, which is the quiet half of the failure this
// header exists to prevent. Change either HUD's content row and change these.
// L.boxPanelPadding — the [Advanced] BUILT-IN the migrated panels actually spend
// (square-on-screen: top/bottom sides convert by cellW * aspect) — and NOT a
// theme's `[panel] padding`. These compute the three boxes' STORED default
// offsets, written once by resetToDefaults(), so they follow the built-in at
// reset/first-run but cannot follow a THEME at all — the stored-offset
// limitation named at contentPaddingY(); a user who retunes the built-in
// mid-flight keeps stale tops until a reset, exactly as with a thicker frame.
inline float gapBarBoxHeight(const LayoutMetrics& L) {
    return static_cast<float>(L.boxPanelPadding.t + L.boxPanelPadding.b)
               * L.cellW * (16.0f / 9.0f)
         + L.lineHeightNormal;
}
inline float noticesBoxHeight(const LayoutMetrics& L) {
    return static_cast<float>(L.boxPanelPadding.t + L.boxPanelPadding.b)
               * L.cellW * (16.0f / 9.0f)
         + L.lineHeightNormal;
}

// ONE TOP FOR ALL THREE, one grid cell down. They no longer stack by default: the
// three are alternatives more often than companions -- few players run a gap bar, a
// notice slab AND a timing panel at once -- so the default that serves most people is
// "the good spot", not "three good spots in a column". Anyone who does want all three
// drags two of them, which is a thing users do to every HUD anyway.
//
// The stacked tops that used to live here (cells 1, 10 and 18, each derived from the
// box above it) are gone with the arrangement they described. The HEIGHT helpers below
// stay: they are what a caller needs to lay the three out deliberately, and
// noticesBoxHeight is still the figure center_stack_theme_test pins the panel against.
inline float stackBoxTop(const LayoutMetrics& L) { return rowY(L, 1); }

// THE COLOURED BLOCK'S OVERHANG IS NOT HERE, and this note says where it went.
//
// The Gap Bar's fill and a Notices slab are the same object in two panels: a coloured
// block whose colour is the reading. Sized to the content ROW they come out slimmer
// than the card they sit in -- the card is that row plus its own border -- so each
// reads as floating rather than filling its box.
//
// The block IS the section card's BORDER BOX now, straight off the plan
// (PanelBox::SectionGeom's top/bot, which are the drawn extent) -- so it lands on
// the card's outer edge on both axes BY CONSTRUCTION. It used to be the content
// interior outset back over the card by BaseHud::drawnCardBorderX/Y(); that pair
// and the contentCardSpanY dance around it are gone with the box-model port, and
// the note survives because the two constants tried before them are worth not
// trying again. A fixed cell overshot: measured under both shipped themes the card
// interior is 3 cells and its outer edge 4.67, so a 3 + 2 = 5 cell block crossed
// the border by a sixth of a cell rather than meeting it. Theme-relative was right
// and a constant here could not be -- and asking the plan is righter still,
// because there is now no second expression of the card's edge to disagree with.
//
// Unthemed the card box degenerates to the content box, so both blocks keep the
// content row exactly as before themes existed.

// The stack's shared BOX WIDTH: CENTER_STACK_WIDTH_CHARS at the large font, plus one
// padding each side.
//
// The three HUDs each computed exactly this locally, which was one number in three
// places and held only while the padding was the same number for all three. It stopped
// being: dim.paddingH carries the BODY CARD's clearance, Timing and GapBar carry a card
// and Notices does not (its coloured slab IS its content), so a theme made Notices a
// cell narrower per side while the unthemed stack stayed flush. BaseHud::centerStackPaddingX()
// is the padding they agree on -- pass it, not dim.paddingH.
//
// Takes the two numbers rather than a HUD, for the same reason every line above does:
// the test drives it at arbitrary values instead of the shipped one.
inline float boxWidth(float fontSizeLarge, float padX) {
    return padX
         + PluginUtils::calculateMonospaceTextWidth(
               WidgetDimensions::CENTER_STACK_WIDTH_CHARS, fontSizeLarge)
         + padX;
}

// THE GAPBAR'S OFFSET IS ITS BOX TOP, like every other panel's, and this function is
// gone because there is nothing left to compute.
//
// It used to be `gapBarBoxTop(L) + paddingV`, because the HUD drew its BAR at the offset
// and derived the box one padding above it. That made a panel's top edge a function of
// dim.paddingV -- which IS contentPaddingY() -- so a theme switching its body
// card off slid the Gap Bar down while Timing and Notices stayed put, and a default
// written under one theme was wrong under another. Both are fixed at the source:
// gap_bar_hud.cpp anchors the box at the offset and puts the bar one padding inside it.

// --- what the HUDs call ---
inline float boxGap()           { return boxGap(layoutDefaults()); }
inline float gapBarBoxHeight()  { return gapBarBoxHeight(layoutDefaults()); }
inline float noticesBoxHeight() { return noticesBoxHeight(layoutDefaults()); }
inline float stackBoxTop()      { return stackBoxTop(layoutDefaults()); }

}  // namespace CenterStack
