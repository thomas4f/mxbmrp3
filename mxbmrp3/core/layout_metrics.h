// ============================================================================
// core/layout_metrics.h
// The UI's spacing vocabulary in one struct: font sizes, the snap grid, panel
// padding, and the settings panel's block metrics.
//
// TWO of these are settable, from [Advanced] in the settings INI: `uiFontSize` and
// `uiLineHeight`. Everything else is a design constant.
//
// Those two are the two that answer a real question -- "make it bigger", "give rows
// more air" -- and answer it without moving anything relative to anything else. A
// layout knob that moves several things at once, or nothing, is not exposed: slice
// sizes are stated in cells, so they land on the grid by construction rather than
// by a ceil, and no border term is needed to work around quantisation.
//
// [panel] padding-x/-y is a per-theme key rather than a layout one. It is inert
// below the frame's clearance (at the debug theme's [frame] 6, inert below 6
// cells); a knob that is dead in part of its range is easier to explain than a
// constant that is dead in all of it. The dead zone is documented at
// ThemeAsset::panelPaddingXOverride, pinned by theme_panel_padding_test, and shown in
// the box-model mock, which flags when the border rather than the key is setting the
// padding.
//
// ONE SPATIAL UNIT: the grid cell. The exceptions are deliberate and both are CSS's
// own -- font size is a fraction of screen height, and the ratios (char-width,
// line-height, the size/row tiers, icon-size) are fractions of the thing they belong
// to, like `em`.
//
// ONE GRID, AND IT IS THE CHARACTER BOX. cellW and cellH are the character's own
// width and the row's own height, divided by cellsPerChar / cellsPerRow. Nothing
// else defines a lattice: snapX/snapY/ceilX/ceilY hang off this struct, and the grid
// overlay draws these. A second copy of either axis cannot follow a font-size
// change, and panels then snap to one lattice while laying out their insides on
// another.
//
// ROOTS vs DERIVED. derive() recomputes everything from the roots, because the
// relationships are LOAD-BEARING. The one that matters most: a normal row is
// exactly `cellsPerRow` cells, so vertical snapping lands on row boundaries. Break
// the link between the row and the cell and it stops.
//
// EACH KNOB CHANGES ONE THING. lineHeightNormal is the font's alone, never solved
// from the gauge widgets' pixel-square requirement -- that would make the row pitch
// of the WHOLE UI a function of three widgets' padding. The squareness requirement
// belongs to those three widgets and is a property of the shipped defaults, not an
// invariant enforced against the user.
//
// Pure and header-only: no I/O, no singletons, no game types, so it compiles straight
// into the unit suite.
// ============================================================================
#pragma once

#include <cmath>   // std::isfinite, in the clamped setters at the bottom
#include "panel_box.h"   // PanelBox::Sides — the box-model air-term built-ins

struct LayoutMetrics {
    // ---- [font] -- THE ROOTS. Settable only in the defaults file. ----
    // font.size -- the base size, as a fraction of SCREEN HEIGHT. Everything else
    // in this section is a multiple of it, so this one number moves the whole UI.
    float fontSizeNormal = 0.0200f;

    // font.size-xs/-s/-l/-xl -- the other tiers, as multiples of font.size, not
    // absolute screen fractions: raising font.size must grow the titles with the
    // body text, or the one knob advertised as "moves the entire UI" is the one
    // that doesn't.
    float fontSizeXS = 0.625f;              // 0.0125 at the default size
    float fontSizeS = 0.75f;                // 0.0150
    float fontSizeL = 1.5f;                 // 0.0300
    float fontSizeXL = 2.0f;                // 0.0400

    // font.row-xs/-s/-l/-xl -- the ROW each tier occupies, as a multiple of
    // font.line-height. Separate from the text size on purpose, and the numbers say
    // why: -l text is 1.5x but sits in a 2x row, because a title wants air its
    // glyphs do not.
    float rowXS = 0.625f;
    float rowS = 0.75f;
    float rowL = 2.0f;
    float rowXL = 2.0f;

    // font.char-width -- width of one monospace character as a fraction of its font
    // size. A MEASUREMENT of the .fnt in use, not a style choice: get it wrong and
    // every column in the settings panel mis-measures. It is also one axis of the
    // grid (see [grid]), which is why it is a root.
    float charWidthRatio = 0.275f;

    // font.ink-center -- where the CAP/DIGIT ink band sits inside the glyph cell,
    // as a fraction of the font size, measured from the cell's top. The other
    // measurement of the .fnt, and the one a panel needs to centre a value in a
    // row shorter than the cell (BaseHud::inkCenteredY).
    //
    // 0.5 is not a guess and not a tuning: mxbmrp3_fontgen normalises every
    // shipped font with `center = 1`, which shifts the baseline until that band is
    // centred in the cell — so the answer is 0.5 BY CONSTRUCTION, and the ink is
    // the only thing that could make it otherwise. test_font_metrics.cpp measures
    // all eight shipped atlases against it. One number, not an ink top plus an ink
    // height: only their combination `top + height/2` ever reaches the arithmetic,
    // and a wrong value places every centred value low -- invisible at default
    // padding, but with the air terms at 0 it is a value hanging out of the bottom
    // of its own panel.
    float inkCenterRatio = 0.5f;

    // font.line-height -- row pitch, as a multiple of the base font size. A ROOT,
    // because cellH is half of it and cellH is the vertical snap grid.
    //
    // 1.1 is TASTE, not arithmetic: a tenth of a row of air between every pair of
    // text rows. At the panel densities this UI actually runs -- twenty-odd rows in
    // the settings panel, a full standings grid -- a sixth of a row (1.17335) reads
    // as slack rather than as breathing room.
    //
    // Settings v9 moves an existing file to it, but ONLY where the stored value is
    // PREV_DEFAULT_LINE_HEIGHT_RATIO: the writer emits this key into every INI
    // whether or not anyone chose it, so "the file says 1.17335" carries no user
    // intent, while any other value does and is left alone. Same conservative rule
    // the v5 colour/font unpinning uses, and for the same reason.
    //
    // It is NOT load-bearing: the border lattice is cellW * UI_ASPECT_RATIO
    // (font-derived, 0.009778), which NO positive ratio makes equal to cellH, so no
    // value here aligns the row grid with it. The engine is exact at ANY ratio:
    // terms spend exactly, panel heights ceil to whole cells with the remainder
    // going to a named absorber (panel_box.h), and the geometry tests derive
    // expectations from the live lattice (MXBMRP3_Test_LayoutCells) rather than
    // from this default -- which is what makes moving the default a one-line
    // change rather than a re-baselining. Pinned by the split-tall-second parity
    // vectors and title_band_test.
    //
    // Row pitch is its own knob, never a function of any widget's padding: the
    // gauge widgets' content-plus-padding arithmetic stays theirs, so widening the
    // panel padding cannot crush every text row in the settings panel.
    float lineHeightRatio = 1.1f;

    // The pre-v9 default, which the v9 migration needs to RECOGNISE -- a stored
    // 1.17335 is the old writer's, not a choice, and only a value that is neither
    // default carries intent. Not a fallback and not a clamp bound; it exists solely
    // so that the migration test can be written against a number rather than a
    // literal buried in the migration.
    static constexpr float PREV_DEFAULT_LINE_HEIGHT_RATIO = 1.17335f;

    // ---- [grid] -- the snap lattice, as a subdivision of the CHARACTER BOX ----
    // The grid is not a separate thing from the type; it IS the character box,
    // divided. One character is `cells-per-char` cells wide and one text row is
    // `cells-per-row` cells tall:
    //
    //     cellW = font.size * font.char-width  / cells-per-char
    //     cellH = font.size * font.line-height / cells-per-row
    //
    // 1 and 2 is the shipped lattice, and the asymmetry is deliberate rather than
    // an oversight: horizontal layout is done in whole characters (the settings
    // panel's columns are character counts), while vertical layout wants a HALF-row
    // step so a small row and the gap above it can both land on it. That is why
    // padding-x = 2 is not the same amount of space as padding-y = 2.
    //
    // Whatever these are, cellW/cellH are the ONLY grid: snapping, ceil-to-cell and
    // the grid overlay all read them, so a changed font moves the lattice with it.
    int cellsPerChar = 1;                   // grid.cells-per-char
    int cellsPerRow = 2;                    // grid.cells-per-row

    // ---- [panel] -- every HUD panel and the settings panel. In CELLS. ----
    // panel.padding-x / panel.padding-y -- the gap between a panel's background
    // quad and its content, so what you measure against the grid overlay is what
    // you type.
    //
    // Ordinary spacing, NOT a root: nothing derives from it. These are the BUILT-IN
    // values; a theme overrides either axis with `[panel] padding-x/-y`
    // (ThemeAsset::panelPaddingXOverride), and BaseHud::basePadding{X,Y}() is the only
    // reader that resolves the two. Read here directly and you get the built-in for a
    // theme that set its own.
    // ---- [box] -- the AIR-term BUILT-INS the box model falls back to when the
    // active theme does not name the key (BaseHud::resolvePanelSpec's fallbacks;
    // the theme inis' "(default N)" notes are THESE numbers). Settable from the
    // main ini's [Advanced] section in CSS shorthand — which is what makes the
    // sentinel contract complete: a theme that omits a key follows a built-in
    // the user can actually retune, and an UNTHEMED panel (whose only air is the
    // panel padding) finally has a reachable spacing knob. Borders are
    // deliberately NOT here: with no art there is nothing to draw a border with,
    // and a themed border is the theme's own statement about its art.
    // ONE OUTER RING AND ONE SEAM, and nothing else. The panel spends a cell
    // between its edge and its content, a cell at each junction between its
    // stacked children, and every other term is zero — so the air a reader sees
    // is the sum of at most two numbers rather than of six, and the two that are
    // left are the two a user reaches for ("looser panel", "looser stack").
    PanelBox::Sides boxPanelPadding{1.0, 1.0, 1.0, 1.0};
    PanelBox::Sides boxTitleMargin{0.0, 0.0, 0.0, 0.0};
    PanelBox::Sides boxTitlePadding{0.0, 0.0, 0.0, 0.0};
    PanelBox::Sides boxContentMargin{0.0, 0.0, 0.0, 0.0};
    PanelBox::Sides boxContentPadding{0.0, 0.0, 0.0, 0.0};
    PanelBox::Sides boxButtonMargin{0.0, 0.0, 0.0, 0.0};
    // The one term that is NOT zero besides the outer ring and the seam: a
    // button's label wants air on its own sides more than a card's rows do, and
    // twice as much across as down — a button is read as an object, not as a row.
    PanelBox::Sides boxButtonPadding{0.5, 1.0, 0.5, 1.0};
    // Junction-only air between a panel's stacked children (band→card,
    // card→card, card→buttons; PanelBox::Spec::gap). A SCALAR, not Sides — a
    // junction has no sides. It carries the whole seam now that the margins
    // above are zero, which is what a junction term is for: air BETWEEN
    // children and none at the frame's edges, the one thing a margin cannot say.
    double boxPanelGap = 1.0;

    // The LEGACY panel padding, read by BaseHud::basePadding{X,Y}() for the panels
    // that have NOT moved onto the box model — speedo, tacho, and the settings panel.
    //
    // Deliberately 2 while boxPanelPadding is 1, and NOT because they are different
    // quantities: they are the same one, and a box panel pads a cell where a legacy
    // panel pads two. Do not follow it to 1: basePaddingY does not convert cells the
    // way the box model does, so at 1 the legacy panels pad 1.5 CELLS and break the
    // whole-cell invariant theme_panel_padding_test's "padding lands on the lattice"
    // case exists to hold. Closing the gap means porting those three panels, not
    // retuning this number.
    float panelPaddingXCells = 2.0f;
    float panelPaddingYCells = 2.0f;

    // THE VISIBLE GAP BETWEEN TWO CARDED BOXES has no built-in of its own: it is
    // boxContentMargin's two facing sides, summed, read by BaseHud::contentGapCells.
    // A separate `sectionGap` beside the margin would be one distance written twice,
    // kept equal by hand.
    //
    // ONE TERM FOR ALL THREE BOUNDARIES where two carded boxes meet -- title band ->
    // body card, HUD section -> HUD section, settings -> settings. Each boundary
    // spends its own pads (which legitimately differ) plus the shared margin seam, so
    // the settings panel cannot separate its cards by a cell while every HUD hugs.

    // ---- [title] -- the themed title band ----
    // THERE IS NO title.padding-y HERE. The air above and below the caption glyph is
    // [title] padding -- the box term, defaulted by boxTitlePadding above and
    // resolved by BaseHud::titlePadY(). A second spelling of that distance here, on
    // the ROW lattice, would let [Advanced] titlePadding change what a panel RESERVES
    // without moving the band or the glyph.
    //
    // The box term is in CELLS, not an em of the caption's font. As a ratio one
    // value produces different air per tier -- 8.1px on a HUD titling at font.size-l
    // against 5.4px on a widget at font.size -- so two bands set from the same
    // number do not look alike. In cells both get the same air, and the band's
    // spacing reads against the grid overlay like every other distance here.

    // title.icon-gap -- between the identity icon and the caption, in CELLS rather
    // than a fraction of the icon size, so the gap does not move when the icon is
    // resized.
    float titleIconGap = 1.0f;
    // title.icon-size -- identity icons are drawn smaller than the title font: icon
    // glyphs fill their box more than text fills the em. A ratio because it is a
    // glyph SIZE, not a gap -- the same kind of number as font.size-l.
    float titleIconSize = 0.63f;

    // ---- [content] -- the themed card behind a HUD's content block ----
    // Enabled per theme ([card] content); these place it. In CELLS.
    //
    // THERE IS NO content.gap-y. The body card hugs the title band's bottom, as its
    // other three edges hug the frame and an untitled panel's card hugs it: two
    // abutting edge slices reading as one double-thickness rule is easier to explain
    // than the one seam in the panel where two themed boxes do NOT meet.
    // THERE IS NO content.padding-x / -y EITHER: a knob no theme can turn is not a
    // knob, it is arithmetic pretending to be one.

    // ---- [label] -- a styled string's own background quad ----
    // label.padding-x, in CELLS like every other spacing value (a cell is one
    // character wide at the default grid).
    float labelPaddingX = 0.5f;

    // ---- [settings] -- the settings panel. In CELLS throughout. ----
    // Horizontal values are character counts, which are cell counts at the default
    // [grid] (one cell, one character).
    //
    // Padding comes from [panel], both axes, like every other panel: padding-x at
    // the sides, padding-y above the title row and below the button row. (A themed
    // panel's TOP is the title band instead, flush at [panel] border-y -- also the
    // same rule every other themed HUD follows.)
    //
    // The panel measures itself, so nothing here pays for its height. It is NOT
    // clamped to the display -- a theme can ask for more air than the screen has, and
    // that is the theme author's to see rather than something to hide by clipping.
    // The footer's button box (margins, border, padding, the [panel] gap above it)
    // and the sidebar's group gaps are theme terms, so a wide frame CAN push the one
    // panel a user cannot move, resize or hide past both screen edges:
    // theme_geometry_test holds the SHIPPED configurations to fitting 1080p; see
    // there for why a frame of 3+ cells cannot be made to.
    // Sidebar column's content ask, in characters. It has to hold, on one row:
    //   4  the icon/checkbox column (drawTabToggle / drawTabIcon)
    //  11  the longest tab name ("Performance")
    // ...and, on a badged row, the status tag right-aligned on the same edge
    // (settings_hud_render.cpp): 0.75 for a Small-size space plus 3 x fontSizeS
    // (2.25) for "New" -- 3 cells, on top of that row's own name.
    //
    // 17 rather than 16 because the WORST ROW is not the longest name: it is
    // "Appearance" (10) plus a "New", at 4 + 10 + 3 = 17. At 16 the tag starts
    // exactly where the label's last glyph ends and the row reads "AppearanceNew"
    // -- not visible in any layout test, which count strings and rows rather than
    // looking at them.
    //
    // Badging a LONGER name than "Appearance" means redoing this sum. Nothing
    // automates it: the sidebar is one row per tab with no wrap and no ellipsis, so
    // a collision is a rendering artefact rather than a measurable overflow.
    int settingsSidebarWidth = 17;
    // The content column's stated ask, characters. The panel's width is
    // COMPOSED from the columns — sidebar + trough + this + the theme/padding
    // chrome — growing outward with the terms like any plan panel's, not a
    // fixed total the columns carve.
    int settingsContentColumnChars = 53;
    // settings.label-column -- where labels start, in characters from the content
    // area's left edge. Also the inset a section card is drawn at, so the card wraps
    // its content: one number, because a second key holding a copy of it unwraps
    // every card as soon as the two disagree.
    int settingsLabelColumn = 2;
    int settingsControlColumn = 28;      // settings.control-column
    // There is no settings.rows, not even as a floor. The panel MEASURES its tallest
    // tab (SettingsHud::measureTallestContentRows) and is that tall on every tab, so
    // the buttons never move and no number has to be re-established by a sweep. A
    // row count could not follow a theme in any case: `[content] padding` is paid
    // per section, so air in an ini would come out of the budget and the panel
    // would clip rather than grow.
    // There is no settings.section-padding. The pad inside a settings card, above
    // its first row and below its last, is [content] padding, the term every other
    // card spends: SettingsHud::cardPad*() reads boxContentPadding.
    // There are no settings.row-gap / settings.block-gap. The seam on either side of
    // a section card's boundary already spends [content] margin + [panel] gap, and
    // SettingsLayoutContext::addSpacing() spends the junction gap.

    // ---- DERIVED: written by derive(), never parsed ----
    float charWidth = 0.0f;          // one character, in screen units
    float cellW = 0.0f;              // grid cell width  == charWidth / cellsPerChar
    float cellH = 0.0f;              // grid cell height == a row / cellsPerRow
    float fontSizeExtraSmall = 0.0f; // the tier ratios resolved to screen units
    float fontSizeSmall = 0.0f;
    float fontSizeLarge = 0.0f;
    float fontSizeExtraLarge = 0.0f;
    float lineHeightNormal = 0.0f;
    float lineHeightExtraSmall = 0.0f;
    float lineHeightSmall = 0.0f;
    float lineHeightLarge = 0.0f;
    float lineHeightExtraLarge = 0.0f;
    float panelPaddingX = 0.0f;      // panelPaddingXCells resolved to screen units
    float panelPaddingY = 0.0f;

    // Recompute every derived field from the roots. Call after ANY root changes --
    // after loading defaults, and again after a theme overrides one.
    void derive() {
        charWidth = fontSizeNormal * charWidthRatio;
        // Row pitch from the font alone. Padding is deliberately absent: with it in
        // here every text row in the UI would move when a user touched the panel
        // padding.
        lineHeightNormal = fontSizeNormal * lineHeightRatio;

        // The grid IS the character box, subdivided. See [grid].
        const int perChar = (cellsPerChar > 0) ? cellsPerChar : 1;
        const int perRow = (cellsPerRow > 0) ? cellsPerRow : 1;
        cellW = charWidth / static_cast<float>(perChar);
        cellH = lineHeightNormal / static_cast<float>(perRow);

        fontSizeExtraSmall = fontSizeXS * fontSizeNormal;
        fontSizeSmall = fontSizeS * fontSizeNormal;
        fontSizeLarge = fontSizeL * fontSizeNormal;
        fontSizeExtraLarge = fontSizeXL * fontSizeNormal;

        lineHeightExtraSmall = rowXS * lineHeightNormal;
        lineHeightSmall = rowS * lineHeightNormal;
        lineHeightLarge = rowL * lineHeightNormal;
        lineHeightExtraLarge = rowXL * lineHeightNormal;

        panelPaddingX = panelPaddingXCells * cellW;
        panelPaddingY = panelPaddingYCells * cellH;
    }

    // ---- THE SNAP LATTICE. The only one; see [grid]. ----
    // Round a POSITION to the nearest grid line.
    float snapX(float pos) const { return snapTo(pos, cellW); }
    float snapY(float pos) const { return snapTo(pos, cellH); }

    // How far to MOVE a panel so an edge already in final screen space lands on a
    // grid line.
    //
    // A panel's position is never the thing that gets snapped directly: it is a
    // layout built in un-dragged space, plus an offset (a saved drag) or a centring
    // term. Snapping the OFFSET only quantises the delta, so a panel whose own left
    // edge is off-grid stays off-grid at every offset: the internal rhythm on the
    // lattice and the panel holding it not. Snap the resulting EDGE and fold the
    // delta back into whatever moves the panel; that works the same for a drag
    // offset, a centring anchor and a bar drawn around its own centre, which is why
    // all four call sites share it.
    float snapDeltaX(float edge) const { return snapX(edge) - edge; }
    float snapDeltaY(float edge) const { return snapY(edge) - edge; }

    // Round a SIZE up to the next whole cell. A margin that has to clear something
    // must never round DOWN into it, which is what the themed frame/card margins
    // need. A value already on a cell boundary is left alone (the epsilon absorbs
    // float error, which otherwise pushes an exact multiple up a whole cell).
    float ceilX(float size) const { return ceilTo(size, cellW); }
    float ceilY(float size) const { return ceilTo(size, cellH); }

    // A cell count in screen units. Every vertical spacing value the file states is
    // in cells, so this is how they are spent -- one conversion, named, instead of
    // each call site remembering which multiplier its own value wanted.
    float cells(float n) const { return n * cellH; }
    float charsW(float n) const { return n * cellW; }

private:
    static float snapTo(float pos, float cell) {
        if (cell <= 0.0f) return pos;
        const float n = pos / cell;
        const int whole = static_cast<int>(n + (n >= 0.0f ? 0.5f : -0.5f));
        return static_cast<float>(whole) * cell;
    }
    static float ceilTo(float size, float cell) {
        if (cell <= 0.0f) return size;
        const float n = size / cell;
        const int whole = static_cast<int>(n);
        return ((n - static_cast<float>(whole)) > 1.0e-4f)
            ? static_cast<float>(whole + 1) * cell
            : static_cast<float>(whole) * cell;
    }

public:
    // WHY THE SETTINGS PANEL MEASURES IN CHARACTERS AND NOT PIXELS. It is laid out
    // in a monospace font, so every horizontal measurement is a whole number of
    // character cells that only becomes a float at the point of emission. Keeping
    // the character arithmetic integral -- rather than deriving it back out of the
    // emitted floats -- means the derived widths are exact.
    //
    // THAT IS NOT A STYLE POINT. Recovering the tooltip box's width by dividing a
    // float span by the width of one character gives settingsTooltipCharsPerLine()
    // in real arithmetic; in IEEE floats it gives that at most HUD scales and ONE
    // LESS at 0.70, so the tooltip box quietly loses a character for anyone running
    // the menu small -- a truncated tooltip at one scale and not another, with
    // nothing in the code saying so. The integer form cannot do that.

    // Content area width in characters — the stated ask, not derived by
    // subtraction from a panel width that is itself composed from the columns.
    // Integer so the tooltip budget below stays exact.
    int settingsContentAreaChars() const {
        return settingsContentColumnChars;
    }
    // Characters per line in the tooltip/description box.
    //
    // ONE CHARACTER NARROWER WITH A CARD, because the box is the only thing in the
    // panel that actually fills its width and the card has a border to clear. On the
    // left the text already clears it: the card starts (border + row lead-in) left of
    // the first glyph. On the right nothing takes that back, so without this a
    // full-width line's last glyph lands ON the card's right border, touching it with
    // nothing between (1317 against a border starting at 1318). Five shipped tab
    // tooltips wrap to a full line.
    //
    // ONE character, not the two SettingsLayoutContext::rowSpanWidth() takes off: a
    // row is inset by the whole label column at each end, while the text only owes the
    // border (one cell) plus the lead-in (half a char). One char rounds that up at any
    // font. Two would be tidier to state and truncates NINE shipped tooltips --
    // tests/unit/test_tooltip_length.cpp names them.
    int settingsTooltipCharsPerLine(bool themedCard) const {
        return settingsContentAreaChars() - settingsLabelColumn - (themedCard ? 1 : 0);
    }
};

// The two values the user may still tune, from [Advanced] in the settings INI.
// Clamped rather than rejected: these arrive from a file the plugin itself rewrites,
// so there is no author to warn and no previous value to keep -- the useful behaviour
// is to land on the nearest sane number and carry on.
//
// Everything else in this struct is a design constant; these two are the two that
// answer a real question ("make it bigger", "give rows more air") without moving
// anything relative to anything else.
// NON-FINITE FIRST, and that ordering is the whole guard: a clamp written as
// `(v < lo) ? lo : (v > hi ? hi : v)` passes NaN straight through, because NaN
// compares false against BOTH bounds. These two feed derive(), so a NaN here is not
// one bad setting -- every metric, the snap lattice and every HUD coordinate derived
// from them become NaN, and writeGlobalSettings then persists `nan` back out, so the
// UI never recovers on its own. Exactly the both-ends rule CLAUDE.md states for
// persisted floats; Settings::parseFiniteFloat is the load-side half.
//
// The fallback is the DEFAULT rather than the previous value: these arrive from a
// hand-edited INI (the supported workflow), where there is no author to warn and no
// earlier value worth preserving.
inline void layoutSetFontSize(LayoutMetrics& m, float v) {
    if (!std::isfinite(v)) v = LayoutMetrics{}.fontSizeNormal;
    m.fontSizeNormal = (v < 0.002f) ? 0.002f : (v > 0.200f ? 0.200f : v);
    m.derive();
}
inline void layoutSetLineHeight(LayoutMetrics& m, float v) {
    if (!std::isfinite(v)) v = LayoutMetrics{}.lineHeightRatio;
    m.lineHeightRatio = (v < 0.5f) ? 0.5f : (v > 4.0f ? 4.0f : v);
    m.derive();
}

// An air-term built-in from its [Advanced] shorthand, each side clamped to
// 0..12 cells (the same range the theme keys hold; negatives and non-finites
// fall to the shipped default's side). No derive(): the air terms are not part
// of the lattice. `fallback` is the shipped default for this term, so a
// hand-edited "nan nan" degrades to the default rather than to zero.
inline void layoutSetBoxSides(PanelBox::Sides& dst, const std::string& shorthand,
                              const PanelBox::Sides& fallback) {
    // parseSides returns all-zeros for BOTH "0" and an unparseable string, and
    // only the second should degrade to the default -- so check that at least
    // one token is a full number before trusting the parse.
    bool anyNumber = false;
    for (size_t i = 0; i < shorthand.size() && !anyNumber;) {
        while (i < shorthand.size()
               && (std::isspace(static_cast<unsigned char>(shorthand[i])) || shorthand[i] == ','))
            ++i;
        size_t j = i;
        while (j < shorthand.size()
               && !std::isspace(static_cast<unsigned char>(shorthand[j])) && shorthand[j] != ',')
            ++j;
        if (j > i) {
            const std::string tok = shorthand.substr(i, j - i);
            char* end = nullptr;
            std::strtod(tok.c_str(), &end);
            anyNumber = (end == tok.c_str() + tok.size());
        }
        i = j;
    }
    if (!anyNumber) { dst = fallback; return; }
    const PanelBox::Sides parsed = PanelBox::parseSides(shorthand);
    const auto side = [](double v, double fb) {
        if (!std::isfinite(v) || v < 0.0) return fb;
        return (v > 12.0) ? 12.0 : v;
    };
    dst.t = side(parsed.t, fallback.t);
    dst.r = side(parsed.r, fallback.r);
    dst.b = side(parsed.b, fallback.b);
    dst.l = side(parsed.l, fallback.l);
}

// The scalar variant, for the junction gap: same parse, validity and 0..12
// clamp as the sides — the first number is the value (a junction has no
// sides, so shorthand arity is meaningless here).
inline void layoutSetBoxScalar(double& dst, const std::string& text, double fallback) {
    PanelBox::Sides tmp{fallback, fallback, fallback, fallback};
    layoutSetBoxSides(tmp, text, tmp);
    dst = tmp.t;
}
