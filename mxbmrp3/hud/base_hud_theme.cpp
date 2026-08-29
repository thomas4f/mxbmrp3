// ============================================================================
// hud/base_hud_theme.cpp
// BaseHud's theme plumbing: the title icon, active-theme resolution, the
// nine-slice emitters, the background quad, and the themed-fill cutter.
// Split from base_hud_render.cpp; every method body is unchanged.
// ============================================================================
#include "base_hud.h"
#include "../diagnostics/call_counters.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/ui_config.h"
#include "../core/asset_manager.h"
#include <algorithm>
#include <cmath>

// Places the title icon quad with its left edge at (baseX) and centered on the title glyph,
// then advances the title text right past the icon. baseX/baseY are the offset-applied,
// un-advanced title position. No-op when there is no title icon.
void BaseHud::finalizeTitleIcon(float baseX, float baseY) {
    if (m_titleIconQuadIndex < 0 || m_titleStringIndex < 0) return;
    if (m_titleIconQuadIndex >= static_cast<int>(m_quads.size())) return;
    if (m_titleStringIndex >= static_cast<int>(m_strings.size())) return;

    float size = m_titleIconSize;
    float halfX = (size * 0.5f) / PluginConstants::UI_ASPECT_RATIO;
    float halfY = size * 0.5f;
    // Icon left edge at baseX; vertically centered on the full title-font height (not
    // the icon's own height) so a smaller icon still sits centred on the title text.
    float cx = baseX + halfX;
    float cy = baseY + m_titleFontSize * 0.5f;

    SPluginQuad_t& q = m_quads[m_titleIconQuadIndex];
    q.m_aafPos[0][0] = cx - halfX; q.m_aafPos[0][1] = cy - halfY;  // top-left
    q.m_aafPos[1][0] = cx - halfX; q.m_aafPos[1][1] = cy + halfY;  // bottom-left
    q.m_aafPos[2][0] = cx + halfX; q.m_aafPos[2][1] = cy + halfY;  // bottom-right
    q.m_aafPos[3][0] = cx + halfX; q.m_aafPos[3][1] = cy - halfY;  // top-right

    // Advance the title text past the icon plus [title] icon-gap. Through the helper,
    // not a second copy of its arithmetic: positionTitleIcon() compares against
    // titleIconAdvance() to decide whether the title is already advanced, so a copy
    // here that drifted by any amount would make that check permanently false and
    // re-advance the title on every rebuild.
    m_strings[m_titleStringIndex].m_afPos[0] = baseX + titleIconAdvance(size);
}

// Re-derives the title icon position from the title string's current position. Safe to call
// after any rebuild and idempotent: it places the icon only when the title is NOT already
// advanced past it. This means a full rebuild (which self-places via addPlanTitle) and a
// layout fast path that *doesn't* reposition the title (e.g. an early-returning rebuildLayout
// on an empty HUD) are both left untouched, while a fast path that DID move the title to its
// un-advanced base gets the icon + advance re-applied.
void BaseHud::positionTitleIcon() {
    if (m_titleIconQuadIndex < 0 || m_titleStringIndex < 0) return;
    if (m_titleIconQuadIndex >= static_cast<int>(m_quads.size())) return;
    if (m_titleStringIndex >= static_cast<int>(m_strings.size())) return;

    float advance = titleIconAdvance(m_titleIconSize);
    float titleX = m_strings[m_titleStringIndex].m_afPos[0];
    float iconLeft = m_quads[m_titleIconQuadIndex].m_aafPos[0][0];
    // Invariant when correctly placed: icon's left edge sits one advance left of the title.
    if (std::fabs(iconLeft - (titleX - advance)) < 1.0e-5f) return;

    // Title is at a fresh un-advanced base; place the icon there and advance the title.
    finalizeTitleIcon(titleX, m_strings[m_titleStringIndex].m_afPos[1]);
}

// Resolve the active panel theme, or nullptr when themes are off/unavailable.
// Themes are a GLOBAL appearance choice (UiConfig), like fonts and colours -- not
// a per-HUD setting -- so this is the single place the decision is made.
//
// A theme is deliberately NOT applied when this HUD has a background texture set:
// that texture IS the panel's look, and drawing a themed frame around it would
// stack two backgrounds. Texture wins, theme is the styling for everything else.
const ThemeAsset* BaseHud::activeTheme() const {
    MXB_COUNT_CALL(ACTIVE_THEME);
    // MEMOISED, because this is a linear string scan over the theme list and one
    // rebuild asks for it dozens of times: every themed* helper resolves it, and
    // each of those calls layout(), which resolves it again -- the row-band inset
    // alone cost six, getScaledDimensions() about twenty, and StandingsHud's
    // per-frame slide loop was running ~250 a frame on a full grid.
    //
    // The cache key is the GLOBAL theme generation plus the three per-HUD inputs
    // below, so it cannot go stale: the generation covers discovery, a config
    // reload and a change of the selected theme, and the setters for the per-HUD
    // inputs clear it directly (see invalidateThemeCache()).
    const unsigned int gen = AssetManager::getInstance().themeGeneration();
    if (m_themeCacheGen == gen) return m_themeCache;
    m_themeCache = resolveActiveTheme();
    m_themeCacheGen = gen;
    return m_themeCache;
}

const ThemeAsset* BaseHud::resolveActiveTheme() const {
    // NEVER NULL. "No theme" is ThemeAsset::nullTheme() -- zero insets, no art -- so the
    // layout helpers below need no null branch and produce exactly the flat geometry they
    // did before. Only addBackgroundQuad() still asks, and only to decide between nine
    // slices and one quad.
    const ThemeAsset* const none = &ThemeAsset::nullTheme();
    if (m_bShowBackgroundTexture && m_iBackgroundTextureIndex > 0) return none;

    const AssetManager& assets = AssetManager::getInstance();

    // Per-HUD override first: "none" opts this HUD out entirely, a name pins it to
    // one theme. An unknown name falls THROUGH to the global setting rather than to
    // no theme, so deleting a theme folder doesn't strip a single HUD out of an
    // otherwise themed set -- it just rejoins the others.
    const std::string& override_ = m_themeOverride;
    if (override_ == THEME_NONE) return none;
    if (!override_.empty()) {
        if (const ThemeAsset* t = assets.getThemeByName(override_)) return t;
    }

    const std::string& name = UiConfig::getInstance().getThemeName();
    if (name.empty()) return none;
    // An unknown name (theme folder deleted, settings file carried over from another
    // install) degrades to the flat background rather than to some arbitrary other theme.
    const ThemeAsset* t = assets.getThemeByName(name);
    return t ? t : none;
}

// Emit (or overwrite) the 9 themed slices covering the rect. When `firstIndex` is
// negative the slices are appended; otherwise they overwrite the 9 quads starting
// there, which is what the layout fast path needs.
void BaseHud::emitThemedBackground(const ThemeAsset& theme, float x, float y,
                                   float width, float height, int firstIndex) {
    emitThemedSlices(theme, x, y, width, height, firstIndex, /*useCard=*/false);
}

// One emitter for both slice sets. `useCard` picks the quieter INNER sprites and
// their smaller inset -- used for title bands and section cards, which sit inside a
// panel and must not repeat its corner motif.
void BaseHud::emitThemedSlices(const ThemeAsset& theme, float x, float y,
                               float width, float height, int firstIndex, bool useCard) {
    emitThemedSliceSet(theme, x, y, width, height, firstIndex,
                       useCard ? SliceSet::INNER : SliceSet::OUTER, 0);
}

// `colorOverride` (non-zero) replaces the derived background colour. Buttons use
// it: their colour is state, not decoration.
void BaseHud::emitThemedSliceSet(const ThemeAsset& theme, float x, float y,
                                 float width, float height, int firstIndex,
                                 SliceSet set, unsigned long colorOverride) {
    // BAND draws the card sprites at the band's corner size -- one art set, two scales
    // (ThemeAsset::titleBorder). Only the `cells` line below tells them apart.
    const bool useCard  = (set == SliceSet::INNER) || (set == SliceSet::BAND);
    const bool useButton = (set == SliceSet::BUTTON);
    // Cells -> the normalized-Y corner size NineSlice::build wants. The SAME
    // conversion the margin helpers use, so the art a theme draws and the clearance
    // the layout reserves are one number by construction rather than by two call
    // sites agreeing. They were not one number before: the art took the raw inset and
    // the layout took it ceiled onto the grid, which is how a theme could resize its
    // corners without moving anything around them.
    const float cells = useButton ? theme.buttonBorder
                      : (set == SliceSet::BAND) ? theme.titleBorder()
                      : (useCard ? theme.cardBorder : theme.frameBorder);
    const float inset = NineSlice::cellsToBorderY(cells, layout().cellW,
                                               PluginConstants::UI_ASPECT_RATIO);
    const int centerSprite = useButton ? theme.buttonCenterSprite
                           : (useCard ? theme.cardCenterSprite : theme.centerSprite);
    const int* cornerSprites = useButton ? theme.buttonCornerSprites
                             : (useCard ? theme.cardCornerSprites : theme.cornerSprites);
    const int* edgeSprites   = useButton ? theme.buttonEdgeSprites
                             : (useCard ? theme.cardEdgeSprites   : theme.edgeSprites);

    NineSlice::Slice slices[NineSlice::SLICE_COUNT];
    NineSlice::build(slices, x, y, width, height, inset,
                     PluginConstants::UI_ASPECT_RATIO);

    // A tintable theme's sprites are white + alpha, so the HUD's background colour
    // and opacity recolour them exactly as they do the flat quad -- one theme
    // serves any palette. A theme that bakes its own colours must instead be
    // modulated by WHITE (opacity only), or the tint multiplies those colours away:
    // a baked cyan glow times a near-black background is near-black, which reads as
    // "the theme is just dark" rather than as a bug.
    // Button sprites are always white+alpha and always take the caller's colour,
    // whatever the theme's tintable flag says -- that colour carries state.
    const unsigned long base = theme.tintable ? this->getColor(ColorSlot::BACKGROUND)
                                              : ColorPalette::WHITE;
    const unsigned long color = colorOverride ? colorOverride
                              : PluginUtils::applyOpacity(base, m_fBackgroundOpacity);

    for (int i = 0; i < NineSlice::SLICE_COUNT; ++i) {
        SPluginQuad_t q;
        switch (slices[i].part) {
            case NineSlice::Slice::CENTER: q.m_iSprite = centerSprite; break;
            case NineSlice::Slice::EDGE:   q.m_iSprite = edgeSprites[slices[i].index]; break;
            default:                       q.m_iSprite = cornerSprites[slices[i].index]; break;
        }
        q.m_ulColor = color;
        for (int c = 0; c < 4; ++c) {
            q.m_aafPos[c][0] = slices[i].pos[c][0];
            q.m_aafPos[c][1] = slices[i].pos[c][1];
        }
        if (firstIndex < 0) {
            m_quads.push_back(q);
        } else {
            m_quads[static_cast<size_t>(firstIndex) + i] = q;
        }
    }
}

void BaseHud::addBackgroundQuad(float x, float y, float width, float height) {
    using namespace PluginConstants;

    // Always add quad to keep indices consistent, but use transparent color if hidden
    SPluginQuad_t quadEntry;

    applyOffset(x, y);

    // Themed panel: 9 slices instead of 1 quad. Record the span so the layout fast
    // path (updateBackgroundQuadPosition) can rewrite exactly these quads -- it
    // used to assume the background was m_quads[0] alone, which a themed panel
    // would leave stretching only its centre slice.
    // Remember the panel rect for the title band, which is emitted later from
    // the caption path and has to span the same width. Stored POST-offset because
    // the band emitter fed emitThemedSlices directly rather than going through the
    // offset-applying public helper.
    m_bgRectX = x; m_bgRectY = y; m_bgRectW = width; m_bgRectH = height;
    m_bgRectValid = true;
    // Shared with invalidatePanelRect(), so the two cannot drop different subsets --
    // see resetPanelDerivedState() for the stale-index write that costs.
    resetPanelDerivedState();
    m_fillFirst = -1;
    m_fillCount = 0;

    // NOTHING VISIBLE TO DRAW. At zero opacity every quad below carries alpha 0, and
    // the probe measured those at 100% the cost of an opaque one -- the engine bills
    // for submission, not for pixels. So a panel at zero opacity was paying for quads
    // nobody can see, every frame.
    //
    // THE FRAME ONLY, and that narrowing is load-bearing. The title band and the
    // content cards are tinted by the same opacity, so gating them here looks equally
    // correct and saves twice as much -- but their emission is where recordCardCover
    // and the m_wholeCard span are written, and those are GEOMETRY that content
    // placement, the fill cut and the click rects all read. Skipping the paint would
    // silently skip the measurements with it. (Caught by title_band_test, which reads
    // card geometry through the fill cut and lost every cover.) Saving the other
    // eighteen quads means separating the record from the draw inside addThemedCard,
    // which is a bigger change than the saving justifies today.
    //
    // Bails AFTER m_bgRect* is set and the derived state reset, because those feed the
    // title band, the content-card spans and the click rects, none of which care
    // whether the background was painted. m_bgQuadCount stays 0, which is the flag
    // updateBackgroundQuadPosition reads to know there is no span to rewrite -- see
    // there, where a 0 count would otherwise index the first CONTENT quad.
    if (backgroundIsInvisible()) {
        m_bgQuadFirst = static_cast<int>(m_quads.size());
        m_bgQuadCount = 0;
        return;
    }

    // THE ONLY BRANCH LEFT, and it is about PAINTING, not geometry: a themed panel is
    // nine slices, an unthemed one is a single flat quad. Everything above and below
    // reads ThemeAsset::nullTheme() for the unthemed case and needs no branch at all.
    const ThemeAsset* theme = activeTheme();
    if (theme->hasArt()) {
        m_bgQuadFirst = static_cast<int>(m_quads.size());
        emitThemedBackground(*theme, x, y, width, height, -1);
        // RESERVED HERE, next to the frame's own quads, so finalizeThemedFill() can
        // write them after the HUD has built without changing draw order -- a strip
        // appended at the end would draw over the rows it is meant to sit behind.
        // Degenerate until then, which draws nothing.
        m_fillFirst = m_bgQuadFirst;            // strip 0 IS the centre slice
        m_fillCount = std::max(m_bContentSections ? SECTION_FILL_STRIPS : 1, m_fillReserve);
        // BY VALUE. push_back below can reallocate, and a reference into the vector
        // would dangle on the very next iteration.
        const SPluginQuad_t centre = m_quads[static_cast<size_t>(m_bgQuadFirst)];
        for (int i = 1; i < m_fillCount; ++i) {
            SPluginQuad_t strip = centre;
            setQuadPositions(strip, 0.0f, 0.0f, 0.0f, 0.0f);
            m_quads.push_back(strip);
        }
        m_bgQuadCount = NineSlice::SLICE_COUNT + (m_fillCount - 1);
        return;
    }
    m_bgQuadFirst = static_cast<int>(m_quads.size());
    m_bgQuadCount = 1;

    // Check if background texture should be used
    if (m_bShowBackgroundTexture && m_iBackgroundTextureIndex > 0) {
        applyTextureAspectCorrection(x, y, width, height);
        setQuadPositions(quadEntry, x, y, width, height);

        // Use sprite texture for background
        quadEntry.m_iSprite = m_iBackgroundTextureIndex;
        // White color with opacity to allow texture to show through
        quadEntry.m_ulColor = PluginUtils::applyOpacity(ColorPalette::WHITE, m_fBackgroundOpacity);
    } else {
        setQuadPositions(quadEntry, x, y, width, height);

        // Use solid color background
        quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
        // Get configured background color and apply opacity (uses per-HUD override if set)
        unsigned long bgColor = this->getColor(ColorSlot::BACKGROUND);
        quadEntry.m_ulColor = PluginUtils::applyOpacity(bgColor, m_fBackgroundOpacity);
    }

    m_quads.push_back(quadEntry);
}

// See the declaration. Pre-offset, and inset by the card's own border on each side --
// that border is art, not padding, so a fill drawn onto it reads as spilling out.
bool BaseHud::contentCardSpanY(float& top, float& bottom) const {
    if (!m_bgRectValid || !m_wholeCardValid) return false;
    top    = m_wholeCardTop    + cardBorderY() - m_fOffsetY;
    bottom = m_wholeCardBottom - cardBorderY() - m_fOffsetY;
    return bottom > top;
}

// See the declaration. The card's INNER area -- inside its own edge slice -- because
// that border is art, not padding: content drawn onto it reads as spilling out.
bool BaseHud::contentClipRect(float& left, float& top, float& right, float& bottom) const {
    if (!m_bgRectValid) return false;
    if (m_wholeCardValid) {
        const float fx = frameBorderX();
        // Same span emitContentCard() drew, then inset by the card's border on each side.
        left  = m_bgRectX + fx + cardBorderX();
        right = m_bgRectX + m_bgRectW - fx - cardBorderX();
        top    = m_wholeCardTop + cardBorderY();
        bottom = m_wholeCardBottom - cardBorderY();
        return (right > left) && (bottom > top);
    }
    // Unthemed (or a theme with no card): the panel's own interior, which is what
    // these HUDs clipped to before themes existed.
    left = m_bgRectX;
    right = m_bgRectX + m_bgRectW;
    top = m_bgRectY;
    bottom = m_bgRectY + m_bgRectH;
    return (right > left) && (bottom > top);
}

// The frame's CENTRE slice's span, from the SAME clamp the slices were built with --
// a panel too small for its inset gets a shrunken frame, and reading theme->inset
// directly would put the strips outside it. Shared with the fill-cut test hook so the
// check and the thing it checks cannot disagree about where the centre is.
bool BaseHud::themedCentreRect(float& left, float& top, float& right, float& bottom) const {
    if (!m_bgRectValid) return false;
    const ThemeAsset* theme = activeTheme();

    const NineSlice::Border in = NineSlice::clampedBorder(
        m_bgRectW, m_bgRectH,
        NineSlice::cellsToBorderY(theme->frameBorder, layout().cellW, PluginConstants::UI_ASPECT_RATIO),
        PluginConstants::UI_ASPECT_RATIO);
    left = m_bgRectX + in.x;
    right = m_bgRectX + m_bgRectW - in.x;
    top = m_bgRectY + in.y;
    bottom = m_bgRectY + m_bgRectH - in.y;
    return (right > left) && (bottom > top);
}

// See the declaration. Cuts the frame's centre into the rects nothing else covers, so
// every interior pixel carries exactly ONE fill layer. The geometry is
// NineSlice::cutFill; this collects the covers and writes the reserved quads.
void BaseHud::finalizeThemedFill() {
    if (m_fillFirst < 0 || !m_bgRectValid) return;
    if (m_fillCount < 1) return;
    if (m_fillFirst + NineSlice::SLICE_COUNT + m_fillCount - 1
        > static_cast<int>(m_quads.size())) return;

    NineSlice::FillRect centre;
    if (!themedCentreRect(centre.l, centre.t, centre.r, centre.b)) return;

    // What covers it. The band and the whole-body card span the panel's inner width,
    // so they take the centre's own x extents; inner cards carry their real rects
    // (pre-offset -- they go through the public add* helpers), which is what lets the
    // settings panel's two card columns cut correctly instead of stacking on the fill.
    NineSlice::FillRect covers[35];
    constexpr int coverCap = static_cast<int>(sizeof(covers) / sizeof(covers[0]));
    int nCov = 0;
    bool truncated = false;
    if (m_bandValid) covers[nCov++] = { m_bandLeft, m_bandTop, m_bandRight, m_bandBottom };
    if (m_wholeCardValid)
        covers[nCov++] = { centre.l, m_wholeCardTop, centre.r, m_wholeCardBottom };
    for (const SectionCardSpan& sp : m_sectionCards) {
        if (nCov == coverCap) { truncated = true; break; }
        covers[nCov++] = { sp.left + m_fOffsetX, sp.top + m_fOffsetY,
                           sp.right + m_fOffsetX, sp.bottom + m_fOffsetY };
    }

    NineSlice::FillRect strips[32];
    constexpr int stripCap = static_cast<int>(sizeof(strips) / sizeof(strips[0]));
    static_assert(SECTION_FILL_STRIPS <= stripCap, "reserved pool exceeds strip buffer");
    const int maxOut = (m_fillCount < stripCap) ? m_fillCount : stripCap;
    // A truncated cover list must take the overflow fallback EXPLICITLY -- cutting
    // against a partial list would leave fill under whichever card was dropped, the
    // exact double layer the cut exists to remove. (Handing cutFill the overfull
    // count would usually overflow its own cap too, but that relies on every passed
    // cover surviving the clamp -- this doesn't.)
    int nStrips;
    if (truncated) {
        strips[0] = centre;
        nStrips = 1;
    } else {
        nStrips = NineSlice::cutFill(centre, covers, nCov, strips, maxOut);
    }

    // Strip 0 IS the centre slice, at m_bgQuadFirst. The rest are the pool reserved
    // AFTER the nine -- m_bgQuadFirst + 1 would be an edge slice.
    auto stripQuad = [&](int i) {
        return static_cast<size_t>((i == 0) ? m_fillFirst
                                            : m_fillFirst + NineSlice::SLICE_COUNT + i - 1);
    };
    for (int i = 0; i < nStrips; i++) {
        setQuadPositions(m_quads[stripQuad(i)], strips[i].l, strips[i].t,
                         strips[i].r - strips[i].l, strips[i].b - strips[i].t);
    }
    // Degenerate whatever is left over, including the centre slice itself when the
    // covers reach the panel's edges and there is no gap at all.
    for (int i = nStrips; i < m_fillCount; i++) {
        setQuadPositions(m_quads[stripQuad(i)], 0.0f, 0.0f, 0.0f, 0.0f);
    }

    // CONSUMED. This function WRITES quads by index, so it must run only against the
    // background that armed it -- addBackgroundQuad. A HUD that skips that call on some
    // rebuild (NoticesHud does, when its title is off) would otherwise be re-cut using
    // last rebuild's indices, which now belong to something else. invalidatePanelRect
    // covers the HUD that remembers to call it; this covers the one that does not.
    m_fillFirst = -1;
}
