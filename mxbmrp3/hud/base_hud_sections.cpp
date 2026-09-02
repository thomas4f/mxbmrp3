// ============================================================================
// hud/base_hud_sections.cpp
// BaseHud's content card, content sections and buttons: the body card,
// section begin/end bookkeeping, the themed card emitters, the themed button
// background, row highlights, the button opacity-flattening rule, and the
// fill/glyph/ink legibility helpers (one TU because they share the file-local
// buttonIsInvisible predicate).
// ============================================================================
#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include <algorithm>
#include <cmath>

// Whether a body card would be drawn. Three conditions and each rules out a real
// case: the HUD has to opt in (m_bContentCard -- see its declaration for why this
// is not automatic), the THEME has to want one, and the theme needs card slices to
// draw it with.
bool BaseHud::hasThemedContentCard() const {
    const ThemeAsset* theme = activeTheme();
    return m_bContentCard && theme
        && theme->contentCardKind(static_cast<int>(m_panelKind)) && theme->hasCard();
}

// The card behind the HUD's content block. Same X span as the title band, so the
// two line up by construction rather than by two call sites agreeing; top one
// seam under the band (see bandSeam below), bottom at the panel's own frame
// clearance.
//
// bandBottom is 0 when no band was drawn (theme without one, or the title switched
// off), in which case the card starts at the frame clearance and the HUD is one
// card from top to bottom.
void BaseHud::emitContentCard(float bandBottom) {
    if (!m_bgRectValid) return;
    if (m_contentCardEmitted) return;
    if (!hasThemedContentCard()) return;
    m_contentCardEmitted = true;
    const ThemeAsset* theme = activeTheme();

    // Unscaled cells, like themedFrameMargin*: the frame is a fixed screen size at
    // any HUD scale, and this card has to stay flush with it.
    const float mx = frameBorderX();
    const float my = frameBorderY();
    // THE SEAM UNDER A BAND IS THE BAND'S BOTTOM MARGIN PLUS THE JUNCTION GAP, the
    // same sum the settings panel spends between its band and its first card. The
    // gap alone would let [title] margin-bottom pad the body card's INTERIOR (the
    // rows anchor to the card) instead of widening the band-to-card seam it names
    // -- the two panels disagreeing about one term, which is what titleRowHeight's
    // matching reservation exists to pay for.
    //
    // THE PREDICATE IS "IS A BAND DRAWN", NOT "IS THE ANCHOR NONZERO": a caption
    // the theme draws no band for can hand in a SYNTHETIC anchor (the reserved
    // row's bottom) that already contains this margin, because titleRowHeight put
    // it there. Gated on `bandBottom > 0` the term is spent twice on exactly that
    // path (a ~32px overshoot at 1080p), while the banded path, where the anchor is
    // the band's own bottom edge and carries no margin, is right.
    // hasThemedTitleBand() separates them.
    //
    // With the caption off there is no anchor at all (0), and contentCardTop ignores
    // its gap argument in that case: no band, no seam, the card takes the frame
    // clearance. So this term cannot leak into a captionless panel either way.
    const float bandSeam = hasThemedTitleBand() ? titleMarginY(true) : 0.0f;
    const float top = NineSlice::contentCardTop(m_bgRectY, my, bandBottom,
                                                contentGapY() + bandSeam);
    const float bottom = m_bgRectY + m_bgRectH - my;
    const float x = m_bgRectX + mx;
    const float w = m_bgRectW - 2.0f * mx;
    if (w <= 0.0f || (bottom - top) <= 0.0f) return;

    // The HUD is drawing a card per SECTION instead. hasThemedContentCard() still
    // answers yes -- the rows are carded either way, so they still clear the card's
    // border and full-row bands still inset past it -- but the whole-body card would
    // sit behind the section cards and undo the separation they exist for.
    //
    // The bounds are RECORDED first, because the section cards need them: the
    // outermost edges of a sectioned body must land exactly where this single card
    // would have, or a HUD's top and bottom gaps change with how many sections it
    // happens to have -- a card derived from its CONTENT sits visibly further from
    // the title band and the panel's bottom edge than one derived from the PANEL.
    //
    // Stored PRE-offset: m_bgRect* is post-offset (the band emitter fed
    // emitThemedSlices directly), but the section helpers take pre-offset coords like
    // every other add* helper, and rewriteThemedCard applies the offset itself.
    if (m_bContentSections) {
        m_bodyCardTop = top - m_fOffsetY;
        m_bodyCardBottom = bottom - m_fOffsetY;
        m_bodyCardValid = true;
        return;
    }

    emitThemedSlices(*theme, x, top, w, bottom - top, -1, /*useCard=*/true);
    m_wholeCardTop = top; m_wholeCardBottom = bottom; m_wholeCardValid = true;
}

// A body SECTION card. Geometry matches the whole-body card's on the X axis -- the
// panel's inner width, flush with the frame clearance -- so a HUD with sections and
// a HUD with one card line up with each other and with the title band above them.
//
// Vertically it wraps its CONTENT: from the heading's row down to whatever the
// caller reports as the section's bottom, extended by the same clearance the rows
// already keep from the card's border. That is what puts the heading inside its own
// card rather than floating above a card full of graph.
void BaseHud::beginContentSection(float x, float y, float width) {
    m_sectionCardIndex = -1;
    if (!hasThemedContentCard()) return;
    m_sectionX = x;
    m_sectionW = width;
    // THE FIRST section's top is the body's top -- exactly where a single whole-body
    // card would have started, flush under the title band. Every later
    // section hangs off its own heading instead. Placing the first card from the
    // CONTENT, which sits at the panel's padding rather than at the band's bottom,
    // would give a sectioned HUD a different gap under its title from every other
    // HUD's.
    m_sectionTop = (m_bodyCardValid && m_sectionCount == 0)
        ? m_bodyCardTop + sectionCardPaddingY()
        : y;
    m_sectionCount++;
    m_sectionCardIndex = static_cast<int>(m_quads.size());
    addThemedCard(0.0f, 0.0f, 0.0f, 0.0f, /*reserveOnly=*/true);
}

// See the declaration: the first section's card starts at the BODY's top, not at
// the y its caller passed, so these two are not the same number.
float BaseHud::sectionCardTop() const {
    return (m_sectionCount == 1 && m_bodyCardValid) ? m_bodyCardTop
                                                    : (m_sectionTop - sectionCardPaddingY());
}

// The seam between two carded boxes, in CELLS. The junction gap composes into
// the legacy seam exactly as it does into the plan's (a seam = margins + gap):
// sectionGap carries the margin half via the bridge; the gap term has no
// legacy scalar of its own, so it is added at the one read every legacy
// consumer goes through — which is what keeps the settings panel's section
// seams (the own-geometry holdout) tracking [panel] gap / [Advanced] panelGap.
// The settings HEIGHT BUDGET reads this too, so the sidebar walk and the rows
// it is budgeted against cannot disagree.
float BaseHud::panelGapCells() const {
    const ThemeAsset* th = activeTheme();
    return static_cast<float>(th->boxPanelGap.set ? th->boxPanelGap.v.t
                                                  : layout().boxPanelGap);
}

float BaseHud::contentGapCells() const {
    // The built-in is the CONTENT MARGIN's own seam -- the sum of the two facing
    // sides -- not a second number kept equal to it by hand: that is the same
    // distance written twice, and retuning the margin without the copy silently
    // doubles every settings card seam. The THEME side has no duplicate either:
    // ThemeAsset::sectionGapOverride is derived from the theme's own [content]
    // margin at parse time, and this reads through the same accessor.
    const PanelBox::Sides& cm = layout().boxContentMargin;
    return activeTheme()->sectionGap(static_cast<float>(cm.t + cm.b)) + panelGapCells();
}

// The clearance a section card keeps between its border and its content, matching
// what the rows already keep from the whole-body card's border.
// SQUARE ON SCREEN (cellW * aspect), not cellH: the box-model rule is that one
// stated cell is square, and the engine spends every vertical term that way
// (Spec::unit). Multiplying by cellH — the half-row lattice, ~20% taller — would
// draw a gap of 1 taller in the settings seams than the same gap draws wide in
// the trough or tall in any plan panel. One conversion, so a term means one
// distance everywhere.
float BaseHud::contentGapY() const {
    return contentGapCells() * layout().cellW * PluginConstants::UI_ASPECT_RATIO * m_fScale;
}

float BaseHud::sectionCardPaddingY() const {
    return cardBorderY();
}

// Close the last section AND stretch it to the body's bottom edge -- the mirror of
// beginContentSection() putting the FIRST section's top at the body's top. Both
// exist for one reason: a sectioned body's outermost edges must land exactly where
// a single whole-body card's would, so the panel around it can be built from the
// same paddingV as every other HUD.
//
// THE PANEL HEIGHT IS WHAT THIS IS ACTUALLY FOR. Users tile HUDs side by side, and
// two HUDs tile only if their heights differ by whole rows -- which needs identical
// padding above and below. Shrinking the PANEL to the last section instead (a
// bottom pad of sectionCardPaddingY + frameMargin, ~19px less than paddingV) would
// leave a sectioned HUD 1.5 cells shorter than a single-card one with the same
// content: heights that can never line up, and a bottom pad no other HUD uses.
//
// The cost is an empty tail of ~19px: the last card sits that much deeper below
// its content than the sections above it. That is the same slack a single-card
// HUD's rows have under them, so it reads as the body ending rather than as one
// section being wrong.
void BaseHud::finishContentSections(float bottomY) {
    endContentSection(bottomY);
    if (!m_bodyCardValid || m_lastSectionIndex < 0) return;
    // Never shrink: a section whose content already reaches past the body's bottom
    // (a HUD sized from something other than its sections) keeps its own height.
    const float height = m_bodyCardBottom - m_lastSectionTop;
    if (height <= 0.0f) return;
    const float mx = frameBorderX();
    // The recorded cover follows the stretch for free: rewriteThemedCard updates the
    // record keyed by the card's first quad.
    rewriteThemedCard(m_lastSectionIndex,
                           m_sectionX + mx, m_lastSectionTop,
                           m_sectionW - 2.0f * mx, height);
}

void BaseHud::endContentSection(float bottomY) {
    if (m_sectionCardIndex < 0) return;
    const int index = m_sectionCardIndex;
    m_sectionCardIndex = -1;                       // spent; one rewrite per section
    const float mx = frameBorderX();
    const float padY = sectionCardPaddingY();
    // The first section's top is already the body's top (see beginContentSection),
    // so it must not be raised by the pad a second time.
    const float top = (m_sectionCount == 1 && m_bodyCardValid)
        ? m_bodyCardTop : (m_sectionTop - padY);
    const float height = (bottomY + padY) - top;
    if (height <= 0.0f) return;
    // Remembered so finishContentSections() can stretch the last one to the body's
    // bottom without re-deriving where it started. The cover record comes from
    // rewriteThemedCard itself.
    m_lastSectionIndex = index;
    m_lastSectionTop = top;
    rewriteThemedCard(index,
                           m_sectionX + mx, top,
                           m_sectionW - 2.0f * mx, height);
}

// Public inner card, for callers that lay out their own sections (the settings
// panel's grouped blocks). Coordinates are already in offset space, matching how
// SettingsLayoutContext positions everything.
// Whether a panel that draws its OWN cards (rather than opting into the
// whole-body one via m_bContentCard) should draw them. The settings panel is the
// only such caller: it makes a card per tab group and per section, so the body card
// would sit behind them and undo the separation they exist for.
//
// The content-card switch has to be honoured HERE too, or that opt-out silently
// costs the panel its [card] <family>-content key: hasThemedContentCard() fails on
// m_bContentCard before ever reaching contentCardKind(), so the switch would govern
// nothing for this panel. Gating the clearance with the cards (this is read for the
// panel's borders as well) matches what contentPaddingX() already does for a HUD.
bool BaseHud::hasThemedCard() const {
    const ThemeAsset* theme = activeTheme();
    return theme && theme->hasCard()
        && theme->contentCardKind(static_cast<int>(m_panelKind));
}

// Coordinates are PRE-offset, like addString/addBackgroundQuad and every other
// add* helper -- callers lay out in un-dragged space and the offset is applied
// here. Taking post-offset coords instead makes a panel's section cards stay
// behind when it is dragged: the panel fully rebuilds on a layout change, so the
// cards are rebuilt correctly at the WRONG origin.
//
// Every card drawn here is ALSO recorded as a fill cover: a card the sweep does
// not know about sits on the frame's centre fill and composites twice, which at
// any opacity below 100% reads a shade darker than the panel around it. Recording
// HERE, at the one place every card is emitted, is what makes a new card-drawing
// caller correct by construction.
void BaseHud::recordCardCover(int firstQuad, float x, float y, float width, float height) {
    for (SectionCardSpan& sp : m_sectionCards) {
        if (sp.firstQuad == firstQuad) {
            sp = { y, y + height, x, x + width, firstQuad };
            return;
        }
    }
    m_sectionCards.push_back({ y, y + height, x, x + width, firstQuad });
}

void BaseHud::addThemedCard(float x, float y, float width, float height, bool reserveOnly) {
    const ThemeAsset* theme = activeTheme();
    if (!theme->hasCard()) return;
    if (!reserveOnly && (width <= 0.0f || height <= 0.0f)) return;
    if (!reserveOnly) recordCardCover(static_cast<int>(m_quads.size()), x, y, width, height);
    applyOffset(x, y);
    emitThemedSlices(*theme, x, y, width, height, -1, /*useCard=*/true);
}

// Reposition a span previously reserved by addThemedCard(reserveOnly=true).
// A section card must be pushed BEFORE its controls (quads draw in order, so it has
// to sit behind them) but is only sized once the section ends -- hence reserve then
// rewrite, rather than a second layout pass.
void BaseHud::rewriteThemedCard(int firstIndex, float x, float y, float width, float height) {
    const ThemeAsset* theme = activeTheme();
    if (!theme->hasCard()) return;
    if (firstIndex < 0 ||
        static_cast<size_t>(firstIndex) + NineSlice::SLICE_COUNT > m_quads.size()) return;
    if (width <= 0.0f || height <= 0.0f) return;
    recordCardCover(firstIndex, x, y, width, height);
    applyOffset(x, y);
    emitThemedSlices(*theme, x, y, width, height, firstIndex, /*useCard=*/true);
}

bool BaseHud::hasThemedButton() const {
    const ThemeAsset* theme = activeTheme();
    return theme && theme->hasButton();
}

// Themed button background. Returns false when the theme has no button slices, so
// the caller can fall back to its plain solid quad -- every call site keeps working
// on an unthemed build or a theme that does not style buttons.
// FULLY transparent stays gone. The corner buttons fade their chip out entirely as
// Background opacity approaches zero, leaving just the glyph -- a deliberate way to get
// a minimal corner control. Flattening alpha 0 would hand back an opaque background
// -coloured box instead, turning "vanishes" into "a dark rectangle", so drop the quad.
static inline bool buttonIsInvisible(unsigned long color) {
    return ((color >> 24) & 0xFFu) == 0u;
}

bool BaseHud::addThemedButton(float x, float y, float width, float height, unsigned long color,
                              bool opaque) {
    const ThemeAsset* theme = activeTheme();
    if (!theme->hasButton()) return false;
    if (width <= 0.0f || height <= 0.0f) return false;
    if (buttonIsInvisible(color)) return true;   // nothing to draw, but the theme owns it
    if (opaque) color = opaqueButtonColor(color);   // see addButtonQuad for the opt-out
    applyOffset(x, y);
    emitThemedSliceSet(*theme, x, y, width, height, -1, SliceSet::BUTTON, color);
    return true;
}


// A BUTTON IS NEVER SEE-THROUGH. Every button here encodes its state as an alpha of
// one colour -- disabled 64, idle 128, hovered 255 of the accent -- which reads right on
// an opaque panel and turns the control transparent the moment a user drags Background
// opacity down. A panel may be a window onto the track; a thing you click should not be.
//
// Flattened rather than forced to 255, so the states stay distinct: the colour is
// composited onto the panel's background first, which is the pixel it already produced
// on an opaque panel. So this changes NOTHING at full opacity and only stops the track
// showing through lower down. Done HERE, at the one funnel every button goes through
// (settings rows, the corner buttons, the director chip), so a new button gets it
// without knowing the rule exists.
int BaseHud::addRowHighlight(float x, float y, float width, float height,
                             unsigned long color) {
    if (width <= 0.0f || height <= 0.0f) return -1;
    const int first = static_cast<int>(m_quads.size());
    // opaque=false: a band is a TINT over the row's card, not a control (see the
    // declaration). addThemedButton returns true for a fully transparent colour
    // without drawing, which would leave `first` pointing at whatever comes next --
    // so that case takes the unthemed path below and pushes the invisible quad,
    // keeping the index a caller cached valid.
    if (!buttonIsInvisible(color) &&
        addThemedButton(x, y, width, height, color, /*opaque=*/false)) {
        return first;
    }
    SPluginQuad_t band;
    applyOffset(x, y);
    setQuadPositions(band, x, y, width, height);
    band.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    band.m_ulColor = color;
    m_quads.push_back(band);
    return first;
}

void BaseHud::repositionRowHighlight(int firstIndex, float x, float y,
                                     float width, float height) {
    if (firstIndex < 0 || static_cast<size_t>(firstIndex) >= m_quads.size()) return;
    if (width <= 0.0f || height <= 0.0f) return;
    applyOffset(x, y);
    // The band's own colour, so a reposition never restates the state its alpha
    // encodes -- and never re-flattens a tint that was emitted translucent.
    const unsigned long bandColor = m_quads[static_cast<size_t>(firstIndex)].m_ulColor;
    const ThemeAsset* theme = activeTheme();
    // THE SPAN IS ONE QUAD OR NINE, and which one is decided by re-running
    // addRowHighlight's OWN predicate against that colour -- not by asking the theme
    // alone. A fully transparent colour takes the unthemed path there (deliberately:
    // addThemedButton draws nothing for it, which would leave the caller's cached index
    // pointing at whatever came next), so it pushes ONE quad even under a theme with
    // button slices. Deciding here on `theme->hasButton()` alone would rewrite nine
    // quads over a one-quad band and blank the eight that follow it. Reachable only
    // if a palette slot resolves to alpha 0 -- but the two decisions have to be the
    // same decision, not two that happen to agree.
    if (theme && theme->hasButton() && !buttonIsInvisible(bandColor) &&
        static_cast<size_t>(firstIndex) + NineSlice::SLICE_COUNT <= m_quads.size()) {
        emitThemedSliceSet(*theme, x, y, width, height, firstIndex,
                           SliceSet::BUTTON, bandColor);
        return;
    }
    setQuadPositions(m_quads[static_cast<size_t>(firstIndex)], x, y, width, height);
}

unsigned long BaseHud::opaqueButtonColor(unsigned long color) const {
    return PluginUtils::flattenOnto(color, this->getColor(ColorSlot::BACKGROUND));
}


// See ButtonFill. SURFACE_LIFT is how far the chip sits above the panel; STATE_TINT
// caps how far a fully-opaque state colour pulls it. Both are mixes toward another
// palette slot rather than toward white, so they are correct in either polarity --
// on a light theme the "lift" darkens, because it moves toward the text colour.
namespace {
constexpr float BUTTON_SURFACE_LIFT = 0.16f;
constexpr float BUTTON_STATE_TINT   = 0.34f;
}

unsigned long BaseHud::buttonFillColor(unsigned long stateColor) const {
    if (hasThemedButton()) return opaqueButtonColor(stateColor);
    const unsigned long surface = PluginUtils::flattenOnto(
        PluginUtils::applyOpacity(this->getColor(ColorSlot::PRIMARY), BUTTON_SURFACE_LIFT),
        this->getColor(ColorSlot::BACKGROUND));
    // The caller's alpha still encodes state (disabled 64 / idle 128 / hovered 255);
    // it scales the TINT rather than the whole fill, so the ordering survives.
    const float alpha = static_cast<float>((stateColor >> 24) & 0xFFu) / 255.0f;
    return PluginUtils::flattenOnto(
        PluginUtils::applyOpacity(stateColor, alpha * BUTTON_STATE_TINT), surface);
}

// See the declaration. The legible ink for `ink` drawn on `fill`, keeping the hue
// wherever it can and only abandoning it as a floor.
//
// SHARED WITH buttonGlyphColor, because buttons are not the only case. Notices and
// the Gap Bar draw a caption on a fill of ITS OWN slot colour -- NEGATIVE text on a
// NEGATIVE slab, POSITIVE on POSITIVE -- which is the "accent on accent" pair
// chipGlyphColor's comment records as invisible on a light theme. The slab is drawn
// at the HUD's background opacity, so at the shipped 60% it is a wash and
// full-strength ink reads over it; the failure only appears when a user raises
// opacity or picks an opaque theme: "WRONG WAY" in red on red.
unsigned long BaseHud::legibleOnFill(unsigned long ink, unsigned long fill) const {
    auto lumaGap = [](unsigned long a, unsigned long b) {
        const unsigned int la = PluginUtils::luma601(a), lb = PluginUtils::luma601(b);
        return (la > lb) ? (la - lb) : (lb - la);
    };
    if (lumaGap(ink, fill) >= MIN_GLYPH_LUMA_GAP) return ink;
    const unsigned long toward = PluginUtils::isColorDark(fill)
        ? this->getColor(ColorSlot::PRIMARY) : this->getColor(ColorSlot::BACKGROUND);
    for (int step = 1; step <= 8; step++) {
        const unsigned long lifted = PluginUtils::flattenOnto(
            PluginUtils::applyOpacity(toward, static_cast<float>(step) / 8.0f), ink);
        if (lumaGap(lifted, fill) >= MIN_GLYPH_LUMA_GAP) return lifted;
    }
    return chipGlyphColor(fill);
}

// The ink for a caption drawn on a slab of its OWN slot colour, at `fillOpacity`.
// Flattens the slab onto the panel background first, because that is the pixel the
// caption actually competes with -- the slot colour alone says nothing about how the
// slab will read once its alpha is spent.
unsigned long BaseHud::captionOnSlabColor(unsigned long slot, float fillOpacity) const {
    return inkOnSlabColor(slot, slot, fillOpacity);
}

unsigned long BaseHud::inkOnSlabColor(unsigned long ink, unsigned long slot,
                                      float fillOpacity) const {
    const unsigned long fill = PluginUtils::flattenOnto(
        PluginUtils::applyOpacity(slot, fillOpacity), this->getColor(ColorSlot::BACKGROUND));
    return legibleOnFill(ink, fill);
}

unsigned long BaseHud::buttonGlyphColor(unsigned long stateColor) const {
    // THEMED: punched out of the chip, and deliberately hueless. The chip is the
    // theme's own art at full strength, so the only thing that reads on every theme
    // is the palette's light or dark end -- see chipGlyphColor. Lifting the hue here
    // instead would put light-red on red where white-on-red is available, which is
    // worse, and worse again on a light theme.
    if (hasThemedButton()) return chipGlyphColor(opaqueButtonColor(stateColor));
    // UNTHEMED: full strength on the neutral surface, which is where the state lives
    // once the fill stops carrying it. legibleOnFill() decides whether that survives
    // against this particular fill and lifts the hue if not -- see there for the
    // NEGATIVE-on-red case that forces the lift, and why it lifts rather than falling
    // back to the panel's text colour.
    return legibleOnFill(PluginUtils::applyOpacity(stateColor, 1.0f),
                         buttonFillColor(stateColor));
}

unsigned long BaseHud::buttonStateColor(unsigned long stateColor, ButtonState state) const {
    // The three alphas, once. Disabled drops the HUE as well -- a greyed control is
    // not a dimmer version of its action, it is a control that has no action -- while
    // idle keeps the hue at half strength and hover takes it to full.
    switch (state) {
        case ButtonState::Disabled:
            return PluginUtils::applyOpacity(getColor(ColorSlot::MUTED), 64.0f / 255.0f);
        case ButtonState::Hovered:
            return stateColor;
        case ButtonState::Idle:
        default:
            return PluginUtils::applyOpacity(stateColor, 128.0f / 255.0f);
    }
}

void BaseHud::addStateButton(float x, float y, float width, float height,
                             const char* label, float labelY, float fontSize,
                             unsigned long stateColor, ButtonState state,
                             unsigned long glyphColorOverride) {
    const unsigned long fill = buttonStateColor(stateColor, state);
    addButtonQuad(x, y, width, height, fill);
    if (!label || !label[0]) return;
    // DERIVED from the fill, except:
    //  - an OVERRIDE, for a label carrying something the fill cannot say (RecordsHud's
    //    Compare reports the fetch RESULT in green or red -- the point of that label);
    //  - DISABLED, which stays muted. Deriving here would hand back the palette's
    //    light end on a grey chip, i.e. a control that looks live.
    const unsigned long ink =
        glyphColorOverride                 ? glyphColorOverride
        : (state == ButtonState::Disabled) ? getColor(ColorSlot::MUTED)
                                           : buttonGlyphColor(fill);
    addString(label, x + width / 2.0f, labelY, PluginConstants::Justify::CENTER,
              getFont(FontCategory::NORMAL), ink, fontSize);
}

void BaseHud::addButtonQuad(float x, float y, float width, float height, unsigned long color,
                            bool opaque, ButtonFill fill) {
    if (buttonIsInvisible(color)) return;
    // opaque=false is for a caller that borrows the BUTTON SLICES without being a
    // control. NoticesHud is the one: its slabs ship at 10% background opacity, so
    // flattening would turn a faint tint over the track into a solid box. A button is a
    // thing you click and must stay legible; a notice slab is deliberately a whisper.
    if (opaque) {
        color = (fill == ButtonFill::Surface) ? buttonFillColor(color)
                                              : opaqueButtonColor(color);
    }
    if (addThemedButton(x, y, width, height, color, opaque)) return;
    SPluginQuad_t q;
    applyOffset(x, y);
    setQuadPositions(q, x, y, width, height);
    q.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    q.m_ulColor = color;
    m_quads.push_back(q);
}
