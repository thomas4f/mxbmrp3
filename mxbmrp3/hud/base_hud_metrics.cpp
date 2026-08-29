// ============================================================================
// hud/base_hud_metrics.cpp
// BaseHud's layout metrics: the theme nesting borders, edge snapping, the
// centre-stack width plumbing, and every padding/margin accessor the box
// model reads. Split from base_hud_render.cpp; every method body is
// unchanged.
// ============================================================================
#include "base_hud.h"
#include "center_stack.h"
#include "../diagnostics/call_counters.h"
#include "../core/plugin_constants.h"
#include "../core/ui_config.h"
#include "../core/layout_config.h"
#include <algorithm>
#include <cmath>

// Nesting metrics for the active theme. Zero without one, so an unthemed layout is
// byte-identical to what it was before themes existed.
//
// NO ROUNDING HERE ANY MORE, and that is the point of stating a slice size in cells
// (see NineSlice::cellsToBorderX). The X margin is a whole number of cells because the
// theme said so; the Y margin is the same distance in PIXELS, so the corner art is
// square and the gap above a title band matches the gap at its ends. Equal pixels and
// whole cells on BOTH axes cannot both hold -- the cells are 10.56 x 12.672px, so the
// smallest amount that is a whole number of each is 63.4px -- and symmetry is what a
// reader sees.
//
// This used to ceil a normalized-Y inset onto the grid, once per axis, and the two
// independent ceils were what pulled the axes apart: 31.7px at the sides against
// 25.3px on top, visible as a band sitting closer to the panel's top edge than to its
// ends. Both problems -- that, and a frame.size whose lower digits moved the art but
// not the layout -- were the same rounding, and both go with it.
//
// Deliberately NOT scaled by m_fScale: the frame is a fixed screen size at any HUD
// scale, so the unscaled grid is the right ruler.
const LayoutMetrics& BaseHud::layout() const {
    MXB_COUNT_CALL(LAYOUT);
    return LayoutConfig::getInstance().defaults();
}

float BaseHud::frameBorderX() const {
    return NineSlice::cellsToBorderX(activeTheme()->frameBorder, layout().cellW);
}

float BaseHud::frameBorderY() const {
    // No null branch and no zero branch: the null theme's inset IS zero, so this
    // returns zero for an unthemed panel by arithmetic rather than by special case.
    return NineSlice::cellsToBorderY(activeTheme()->frameBorder, layout().cellW,
                                  PluginConstants::UI_ASPECT_RATIO);
}

// STATIC, and layoutDefaults() rather than layout() -- the same numbers either way
// now that layout() IS the globals, but a static member has no `this` to call it on.
// The lattice has to be global: HUDs align to each other by landing on the SAME grid
// lines, which is the entire point of snapping an edge.
float BaseHud::snapEdgeX(float edge) {
    return UiConfig::getInstance().getGridSnapping()
        ? edge + layoutDefaults().snapDeltaX(edge) : edge;
}

float BaseHud::snapEdgeY(float edge) {
    return UiConfig::getInstance().getGridSnapping()
        ? edge + layoutDefaults().snapDeltaY(edge) : edge;
}

// The centre-stack contract; see the declarations in base_hud.h for why it is one
// function and not four call sites that agree today.
void BaseHud::wantCenterStackWidth(PanelWant& want, const ScaledDimensions& dim) const {
    wantCenterStackWidth(want,
        CenterStack::boxWidth(dim.fontSizeLarge, centerStackPaddingX()));
}

void BaseHud::wantCenterStackWidth(PanelWant& want, float panelW) const {
    want.minPanelW = panelW;
    // NOT a redundant zero: it is the half of the contract a call site forgets.
    want.contentW = 0.0f;
}

// Layout-space left for a centre-anchored panel: half its own width to the left of
// the stored centre. Deliberately NOT snapped -- see the declaration.
float BaseHud::centerAnchoredPanelLeft(float panelW) {
    return -panelW * 0.5f;
}

// A PANEL'S BASE PADDING, at this HUD's scale: the built-in, or the active theme's
// `[panel] padding-x/-y` when it names one. The ONLY reader of
// ThemeAsset::panelPadding*Override -- see it for why the sentinel and for when the
// theme's value has no effect.
//
// Not on LayoutMetrics, because that is the global lattice every HUD aligns to and this
// is a per-theme choice; not folded into contentPadding*(), because the settings panel
// and the caption row need the BASE on its own.
float BaseHud::basePaddingX() const {
    const ThemeAsset* theme = activeTheme();
    const float cells = (theme->panelPaddingXOverride >= 0.0f)
                      ? theme->panelPaddingXOverride : layout().panelPaddingXCells;
    return cells * layout().cellW * m_fScale;
}

float BaseHud::basePaddingY() const {
    const ThemeAsset* theme = activeTheme();
    const float cells = (theme->panelPaddingYOverride >= 0.0f)
                      ? theme->panelPaddingYOverride : layout().panelPaddingYCells;
    return cells * layout().cellH * m_fScale;
}

float BaseHud::cardBorderX() const {
    const ThemeAsset* theme = activeTheme();
    if (!theme->hasCard()) return 0.0f;
    return NineSlice::cellsToBorderX(theme->cardBorder, layout().cellW);
}

float BaseHud::cardBorderY() const {
    const ThemeAsset* theme = activeTheme();
    if (!theme->hasCard()) return 0.0f;
    return NineSlice::cellsToBorderY(theme->cardBorder, layout().cellW,
                                  PluginConstants::UI_ASPECT_RATIO);
}

// The same two, at the BAND's size. Identical to the card pair for every theme that
// does not set `[card] band-size` -- titleBorder() falls back to cardBorder -- so these are
// not an alternative to the card helpers, they are the answer for band geometry
// specifically. Anything the band is made of asks these; anything a body card or a
// section card is made of asks the pair above.
float BaseHud::titleBorderX() const {
    const ThemeAsset* theme = activeTheme();
    if (!theme->hasCard()) return 0.0f;
    return NineSlice::cellsToBorderX(theme->titleBorder(), layout().cellW);
}

float BaseHud::titlePadY(bool bottom) const {
    const ThemeAsset* th = activeTheme();
    const PanelBox::Sides& sides = th->boxTitlePadding.set ? th->boxTitlePadding.v
                                                           : layout().boxTitlePadding;
    return static_cast<float>(bottom ? sides.b : sides.t)
         * layout().cellW * PluginConstants::UI_ASPECT_RATIO * m_fScale;
}

float BaseHud::titleMarginY(bool bottom) const {
    const ThemeAsset* th = activeTheme();
    const PanelBox::Sides& sides = th->boxTitleMargin.set ? th->boxTitleMargin.v
                                                          : layout().boxTitleMargin;
    return static_cast<float>(bottom ? sides.b : sides.t)
         * layout().cellW * PluginConstants::UI_ASPECT_RATIO * m_fScale;
}

float BaseHud::titleBorderY() const {
    const ThemeAsset* theme = activeTheme();
    if (!theme->hasCard()) return 0.0f;
    return NineSlice::cellsToBorderY(theme->titleBorder(), layout().cellW,
                                  PluginConstants::UI_ASPECT_RATIO);
}

// A PANEL'S HORIZONTAL PADDING: the distance from its background edge to its first
// glyph, in full. The base padding this HUD's scale earns, widened to whatever the
// theme's borders make the content clear.
//
// A MAX, NOT A SUM, and that is the first place this model parts company with a CSS
// box: the base padding and the frame's border occupy the SAME space rather than
// stacking. See themedContentPad's obituary at contentPaddingY() below for what the
// additive form would cost and why it is not built.
//
// THE FRAME'S CLEARANCE IS NOT GATED ON INNER GEOMETRY; the inner border on top of it
// is. A themed panel always draws a frame, so its side slices always have to be
// cleared -- and at the shipped themes the side border (31.68px, 3 cells) is wider
// than the base padding (21.12px, 2), so a titleless cardless widget put its
// left/right-aligned content 10.56px INSIDE its own edge slice. TimingHud's comparison
// rows sat on the frame; every gauge's content was off-centre in its box by the same
// amount. frameBorderX() is 0 with no theme, so an unthemed HUD is unaffected and the
// frame term needs no gate of its own.
float BaseHud::contentPaddingX() const {
    // THE INNER BORDER IS THE CARD'S *OR* THE BAND'S, whichever is wider -- they are the
    // same slice set at two sizes, and whichever this panel kind draws, the content
    // column has to clear it.
    //
    // It used to count the card alone, on the reasoning that content sits INSIDE a card
    // but only BELOW a band, so it owes a band's side slices nothing. True of the rows;
    // not true of the CAPTION, which sits inside the band -- so with `hud-content = 0`
    // the caption was inset by titleGlyphInsetX() while the rows were not, and the two
    // ended up on different columns, the caption overhanging its own band edge.
    //
    // Gated on the THEME's flag for this kind rather than on m_bShowTitle, so turning a
    // caption on and off cannot change the panel's width -- which is the bug charging the
    // band here caused the first time (a widget widened by 42px with its title).
    const float cardBorder = hasThemedContentCard()    ? cardBorderX() : 0.0f;
    const float titleBorder = themeDrawsTitleBandKind() ? titleBorderX() : 0.0f;
    const float need = frameBorderX() + std::max(cardBorder, titleBorder);
    const float have = basePaddingX();
    // No ceil: both terms are whole cells of the X lattice (the base padding by
    // definition, the borders because a slice size IS a cell count), so the wider of
    // them already lands on it. There used to be one here, and on this axis it was
    // always a no-op dressed as a safeguard.
    return (need > have) ? need : have;
}

float BaseHud::centerStackPaddingX() const {
    // The carded form of contentPaddingX()'s "need", unconditionally: see the
    // declaration for why the three center-stack boxes share one padding.
    const float have = basePaddingX();
    const float need = frameBorderX() + cardBorderX();
    return (need > have) ? need : have;
}

// WHAT WOULD MAKE THE FLIP A SINGLE NUMBER, and why it is not done here.
//
// panelContentY() anchors content to the body card, so toggling `[card] hud-content`
// SHOULD move content by exactly the card's border. It does not: measured across four
// panels and three frame sizes the flip still moves it by -1.85 to +0.33 cells. The
// reason is this function. With a card the content origin is card-relative and lands at
// a fractional row; without one it is max(basePad, frameMargin) CEILED to a whole cell.
// The difference of a fractional quantity and an integral one cannot be constant.
//
// The only composition that closes it is the CSS one -- additive rather than max:
//
//     paddingV = panelPaddingY + frameMarginY + cardBorderY
//     paddingH = panelPaddingX + frameMarginX + cardBorderX
//
// Then a card toggle moves content by exactly cardBorder on both axes, a band toggle by
// exactly the band's height, and panelContentY() loses its card branch entirely (the
// padding already lands one panelPadding inside the card's interior). It is a smaller,
// more regular rule than what is here.
//
// IT WAS BUILT AND MEASURED, then set aside; the patch is not lost for want of trying.
// With both pads additive and titleRowHeight() reduced to the caption's own row,
// Standings' card flip moves content by -0.833 cells -- EXACTLY the card's border -- at
// [frame] 1, 2 and 4 and with the caption on or off. Six configurations, one number. The
// model is right.
//
// What stopped it is the centre stack. Each box grows five cells against a two-cell
// boxGap, so the three panels overlap by three. Making noticesBoxTop()/timingTop() take
// the live themed padding fixes Notices, which computes its y every rebuild -- but NOT
// Timing, whose default is a STORED offset written once by resetToDefaults(). A stored
// offset cannot follow a theme change without re-deriving defaults for every HUD still
// sitting at one, while leaving anyone who has dragged alone. That is a persistence
// migration, not a layout change, and it is the real prerequisite -- the header at
// gapBarBoxTop() has named it as such since before any of this work.
//
// One smaller consequence, measured and cheap once the above is settled: TimingHud
// spelled panelHeight()'s terms itself, so it came out 19.667 cells the moment the
// content origin stopped being integral. (A second one named drawnCardBorderX/Y
// here; both are gone -- the Gap Bar's and Notices' blocks take the section card's
// border box from the plan now, see center_stack.h.)
//
// The cost to the look, at [frame] size 2 with a 1-cell card:
//     paddingH   3 cells -> 5      (+2 per side, every themed panel 4 cells wider)
//     paddingV   3 cells -> 4.5    (+1.5 per side, 3 cells taller)
float BaseHud::contentPaddingY() const {
    // THE FRAME'S CLEARANCE IS UNGATED, exactly as in contentPaddingX(); only the
    // INNER border on top of it depends on inner geometry. This used to open with
    // `if (!themeNeedsContentPad()) return 0.0f;`, which dropped the frame term too --
    // so a panel the theme drew no body card for paid nothing for a frame it still had.
    //
    // The shortfall was measured and found survivable at the sizes the shipped themes
    // use, which is true and was the wrong test. frameMarginY is `cells * cellW * 16/9`
    // (NineSlice::cellsToBorderY -- a frame size is whole cells on the X lattice, converted
    // to the Y one by aspect), so against a 2-cell panelPaddingY the shortfall runs:
    //
    //     [frame] size   1        2        3        4        6
    //     shortfall     -0.0137  -0.0039  +0.0059  +0.0156  +0.0352
    //
    // Negative at 1 and 2 -- the only sizes either shipped theme uses -- and positive
    // from 3, growing a cell per size. At size 4 a Timing panel with `hud-content = 0`
    // drew its first row starting on the frame's top edge slice and its last row over
    // the bottom one. The sweep in MXBMRP3_Test_PanelPadY found 32 of 34 panels short in
    // that configuration; pinned by theme_panel_padding_test.cpp.
    //
    // Restoring it is a NO-OP at sizes 1 and 2 for that same reason, which is what makes
    // it safe: the `need > have` test below clamps a negative shortfall away, so nothing
    // a shipped theme renders moves. (With a body card the card's own border joins the
    // need, which pushes the same crossing down between sizes 1 and 2.)
    // ONE INNER PAD PER BORDER THE CONTENT IS INSIDE OF. The rows sit inside the
    // frame and inside a body card, and must clear each: the frame's clearance gets
    // them past the outer edge slice, the card's past its own. Counting only the
    // frame put the first row 1.2px below the card's top edge slice, close enough to
    // read as the row bleeding through it.
    //
    // Symmetric by construction, and that is exactly why the list stops here:
    // paddingV is spent at BOTH ends of the panel, so every term in it is paid twice.
    // A term the bottom has no use for is dead air at the bottom.
    //
    // NO BAND TERM, for that reason. Clearing the band's bottom edge slice is a
    // TOP-side requirement -- content sits BELOW a band, so the bottom of the panel
    // owes it nothing. Charging it here bought the bottom 1.5 cells of air it had no
    // use for, which is what stopped a titled widget from stacking with two untitled
    // ones (17 cells against 16).
    //
    // The clearance is still owed; titleRowHeight() carries it now, which is where it
    // belongs -- a quantity that only exists when a title does. For widgets the two
    // moves cancel exactly (the pad loses a cell, the title row gains one) so their
    // content does not shift; only the unused bottom cell goes.
    const float cardBorder = hasThemedContentCard() ? cardBorderY() : 0.0f;
    const float need = frameBorderY() + cardBorder;
    const float have = basePaddingY();
    // NOT A PLAIN max() LIKE THE X RULE, and the difference is the ceil rather than a
    // different model: the SHORTFALL is rounded up to a whole cell and added to the base,
    // because a frame border is the side border's pixel equivalent and so lands at 2.5
    // cells for a 3-cell frame -- content starting off-row is visible against every other
    // row in the panel, while the X difference is already on its lattice.
    //
    // WHAT WOULD MAKE THIS A SUM, and why it is not: this pair returns the TOTAL, and
    // the two used to return only the shortfall (themedContentPadX/Y) with each caller
    // adding the base itself. Folding them removed the ambiguity of one word naming both
    // the part and the whole -- but it does NOT make the composition additive. Additive
    // padding (base + frame + card, the CSS form) was built and measured: it closes the
    // card flip to exactly one border on both axes, and it overlaps the centre stack by
    // three cells because Timing's default offset is STORED. See the note at
    // panelContentY() and gapBarBoxTop() -- the prerequisite is a persistence migration.
    return (need > have) ? have + layout().ceilY(need - have) : have;
}

// How far a LEFT-justified caption must move right of the content column to clear
// the title band's left edge slice. See addPlanTitle for why this moves the
// CAPTION rather than padding the content -- padding the content is what made a
// title widen a widget.
//
// contentX is PRE-offset and m_bgRect* is POST-offset; applyOffset is a pure
// translation, so comparing in offset space and returning the DELTA is valid in
// either. (A titleStringX() sat beside this for a fast path repositioning the
// caption by index; the plan owns the caption's x now, and it went with the rest
// of the pre-plan geometry.)
float BaseHud::titleGlyphInsetX(float contentX) const {
    if (!m_bShowTitle || !m_bgRectValid || !hasThemedTitleBand()) return 0.0f;
    const float bandInner = m_bgRectX + frameBorderX() + titleBorderX();
    const float contentPost = contentX + m_fOffsetX;
    return (bandInner > contentPost) ? (bandInner - contentPost) : 0.0f;
}
