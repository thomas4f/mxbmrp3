// ============================================================================
// hud/center_stack.h
// Where the centered top-of-screen panels sit: the Dash at the top, and the Gap
// Bar, Notices and Timing sharing ONE spot under it.
//
// The three shared that spot's predecessor -- the Dash's top -- until 1.33, as
// alternatives to one another; the Dash shipping on made it a hidden spot, so they
// moved under it. They SHARE it like every other default group (default_slots.h):
// two switched on overlap until the player drags one.
//
// UNSCALED, from layoutDefaults(): these are DEFAULT POSITIONS, written once into
// each HUD's offset, and a HUD's own scale is a user setting applied afterwards.
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
// reads the global defaults -- which is what the HUDs call. The test asserts these
// at arbitrary padding, not only the shipped value; reading a singleton inside
// would make that claim untestable.

// TWO cells of air under the Dash, the gap every default group keeps from the HUD
// it sits under (default_slots.h).
inline float boxGap(const LayoutMetrics& L) { return 2.0f * L.cellH; }

// THE STACK'S TOP, one grid cell down: the Dash's spot.
inline float stackBoxTop(const LayoutMetrics& L) { return rowY(L, 1); }

// THE SHARED SPOT UNDER THE DASH: one box gap under the Dash's box, centred with it.
// Every panel's offset is its box top (see below), so the one number places all
// three.
//
// The Dash's height is not derived here: its plan (page rows as tall as the Gear
// and Speed widgets' content, the strip, the caption reserve) has no closed form
// worth mirroring. So its default bottom is a cell number, UNTHEMED AND TITLE-OFF
// like every default position, and dash_test's fresh-install Gap Bar case pins it:
// any change to the Dash's height fails there, by the cell. A theme or a caption
// grows the Dash over the spot's top, and the player nudges the panel as with any
// other default -- following the theme would make a stored offset live, which is a
// persistence migration.
constexpr int DASH_DEFAULT_BOTTOM_CELL = 9;
inline float underDashBoxTop(const LayoutMetrics& L) {
    return rowY(L, DASH_DEFAULT_BOTTOM_CELL) + boxGap(L);
}

// THE COLOURED BLOCK'S OVERHANG IS NOT HERE.
//
// The Gap Bar's fill and a Notices slab are the same object in two panels: a coloured
// block whose colour is the reading. Sized to the content ROW they would come out
// slimmer than the card they sit in -- the card is that row plus its own border -- and
// read as floating rather than filling its box.
//
// So the block IS the section card's BORDER BOX, straight off the plan
// (PanelBox::SectionGeom's top/bot, which are the drawn extent), and lands on the
// card's outer edge on both axes BY CONSTRUCTION. A fixed cell of overhang cannot:
// measured under both shipped themes the card interior is 3 cells and its outer edge
// 4.67, so a 3 + 2 = 5 cell block crosses the border by a sixth of a cell rather than
// meeting it. Asking the plan leaves no second expression of the card's edge to
// disagree with.
//
// Unthemed the card box degenerates to the content box, so both blocks keep the
// content row.

// The stack's shared BOX WIDTH: CENTER_STACK_WIDTH_CHARS at the large font, plus one
// padding each side.
//
// One number in one place, because the padding is NOT the same for all three:
// dim.paddingH carries the BODY CARD's clearance, Timing and GapBar carry a card and
// Notices does not (its coloured slab IS its content), so computed per HUD from
// dim.paddingH a theme makes Notices a cell narrower per side while the unthemed
// stack stays flush. BaseHud::centerStackPaddingX() is the padding they agree on --
// pass it, not dim.paddingH.
//
// Takes the two numbers rather than a HUD, for the same reason every line above does:
// the test drives it at arbitrary values instead of the shipped one.
inline float boxWidth(float fontSizeLarge, float padX) {
    return padX
         + PluginUtils::calculateMonospaceTextWidth(
               WidgetDimensions::CENTER_STACK_WIDTH_CHARS, fontSizeLarge)
         + padX;
}

// THE GAPBAR'S OFFSET IS ITS BOX TOP, like every other panel's: gap_bar_hud.cpp
// anchors the box at the offset and puts the bar one padding inside it. Anchoring
// the BAR at the offset and deriving the box one padding above it would make the
// panel's top edge a function of dim.paddingV -- which IS contentPaddingY() -- so a
// theme switching its body card off would slide the Gap Bar down while Timing and
// Notices stayed put, and a default written under one theme would be wrong under
// another.

// --- what the HUDs call ---
inline float boxGap()          { return boxGap(layoutDefaults()); }
inline float stackBoxTop()     { return stackBoxTop(layoutDefaults()); }
inline float underDashBoxTop() { return underDashBoxTop(layoutDefaults()); }

}  // namespace CenterStack
