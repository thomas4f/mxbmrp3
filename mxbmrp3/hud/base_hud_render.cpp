// ============================================================================
// hud/base_hud_render.cpp
// BaseHud shared rendering primitives — the drawing helpers every HUD builds on:
// strings/labels, background quads, dots, icons, line segments, needle quads,
// grid lines, strip charts, temperature colour, and the styled-string builder.
// Extracted verbatim from base_hud.cpp when it grew past ~1k lines; the BaseHud
// class, members, and public API are unchanged — only where these method bodies
// (and the file-local floatEquals helper they use) live moves. Same byte-
// identical-extraction pattern as the plugin_data / http_server splits.
// ============================================================================
#include "base_hud.h"
#include "center_stack.h"
#include "../diagnostics/call_counters.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_manager.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/font_config.h"
#include "../core/ui_config.h"
#include "../core/layout_config.h"
#include "../core/settings_manager.h"
#include "../core/hud_manager.h"
#include "../core/asset_manager.h"
#include "../core/companion_window.h"
#include "../diagnostics/logger.h"
#include "../handlers/draw_handler.h"
#include "../diagnostics/timer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>

namespace {
    // Epsilon comparison for floating-point values
    // Required to avoid precision issues when comparing scaled font sizes
    constexpr float FLOAT_EPSILON = 0.0001f;

    inline bool floatEquals(float a, float b) {
        return std::abs(a - b) < FLOAT_EPSILON;
    }
}

// ============================================================================
// Shared HUD Rendering Helpers (eliminates duplication across HUDs)
// ============================================================================

// Emits one render string. IMPORTANT: this always pushes an entry, even when `text` is
// empty — it never skips. Index-coordinated layout fast paths (e.g. StandingsHud /
// IdealLapHud / StatsHud rebuildLayout) reposition strings by index and assume a stable
// per-row string count/order across rebuilds. Callers therefore pass "" for a blank cell
// rather than skipping the call; don't "optimize" empties away here or the indices desync
// and rows scramble on drag/scale.
void BaseHud::addString(const char* text, float x, float y, int justify, int fontIndex,
                        unsigned long color, float fontSize, bool skipShadow) {
    MXB_COUNT_CALL(ADD_STRING);
    SPluginString_t stringEntry;

    strncpy_s(stringEntry.m_szString, sizeof(stringEntry.m_szString), text, sizeof(stringEntry.m_szString) - 1);
    stringEntry.m_szString[sizeof(stringEntry.m_szString) - 1] = '\0';

    applyOffset(x, y);
    stringEntry.m_afPos[0] = x;
    // Centred in its row, not flush with its top -- see rowCenterOffset().
    stringEntry.m_afPos[1] = y + rowCenterOffset(fontSize);
    stringEntry.m_iFont = fontIndex;
    stringEntry.m_fSize = fontSize;
    stringEntry.m_iJustify = justify;
    stringEntry.m_ulColor = color;

    m_strings.push_back(stringEntry);
    m_stringSkipShadow.push_back(skipShadow);  // Track shadow flag (shadow generated at collection time)
}

// Themed band behind the title row.
//
// This is the first theme element that is not a FRAME. A 9-slice frames a rect the
// HUD already has; a title band is NEW geometry, drawn where the HUD previously
// drew nothing -- which is why the "chevron" theme could not fake one from its top
// edge: an edge slice only ever occupies the corner inset (a fraction of a percent
// of screen height) and can never reach down behind the title text.
//
// Reuses the panel's own nine slices at a reduced inset, so it reads as an inner
// card in the theme's own language rather than needing a second sprite set. That is
// deliberately the FIRST thing to try; a theme that wants a distinct title
// treatment would need its own slices, and this is how we find out whether it does.
//
// Emitted from the caption path (after addBackgroundQuad, which every HUD calls
// first) so it lands on top of the panel background and behind all text -- the game
// draws every quad before every string, so text ordering takes care of itself.
// Whether a band would actually be drawn. Split out of the band emitter so a caller
// can RESERVE the band's height before emitting it -- the settings panel computes
// its panel height before it has laid anything out, and a predicate that drifted
// from the emitter's early-outs would reserve space for a band that never appears.
// Does the THEME draw a band for this panel kind -- regardless of whether this panel's
// caption is switched on. Width must not depend on the title toggle; see
// contentPaddingX().
bool BaseHud::themeDrawsTitleBandKind() const {
    const ThemeAsset* theme = activeTheme();
    return theme->titleBandKind(static_cast<int>(m_panelKind)) && theme->hasCard();
}

bool BaseHud::hasThemedTitleBand() const {
    const ThemeAsset* theme = activeTheme();
    // hasCard(): reusing the panel's own slices here was the first attempt and it
    // produced brackets-inside-brackets on any theme with a real corner motif, so a
    // theme without card_center simply has no band.
    return theme && theme->titleBandKind(static_cast<int>(m_panelKind)) && theme->hasCard();
}



// ==== THE BOX-MODEL PLAN (BOX-MODEL-PORT) ====================================
// See the block comment at the declarations in base_hud.h. Everything here is a
// thin bridge: the ENGINE (core/panel_box.h, pinned to the model page by the
// parity fixture) owns the geometry; these functions only resolve the theme's
// terms into a spec and emit the existing quad machinery at the plan's numbers.

// The eleven theme terms + switches for THIS panel, resolved. A set box key
// wins; an absent one falls back to the legacy scalar chain, so a theme written
// in either vocabulary produces one spec. The fallbacks reproduce the model
// page's "reset to shipped" defaults: title margin 0, title padding
// [title] padding-y on all sides, content margin = half the visible gap (the
// seam is the SUM of two facing margins), content padding 0, button air 0.5.
PanelBox::Spec BaseHud::resolvePanelSpec(const ScaledDimensions& dim,
                                         const PanelWant& want) const {
    const ThemeAsset* th = activeTheme();
    const LayoutMetrics& L = layout();
    PanelBox::Spec s;
    // Scale-invariant: both cells scale together, so the unscaled lattice serves.
    s.unit = static_cast<double>(L.cellW) * PluginConstants::UI_ASPECT_RATIO / L.cellH;
    const auto uniform = [](float v) {
        const double d = static_cast<double>(v);
        return PanelBox::Sides{d, d, d, d};
    };
    const auto term = [](const ThemeAsset::BoxTerm& t, PanelBox::Sides fallback) {
        return t.set ? t.v : fallback;
    };
    s.panel.border = term(th->boxPanelBorder, uniform(th->frameBorder));
    // Air-term fallbacks are the RETUNABLE BUILT-INS (LayoutMetrics box*,
    // settable from [Advanced]) — the other half of the theme inis' sentinel
    // contract: an absent theme key follows a built-in the user can move.
    s.panel.padding = term(th->boxPanelPadding, L.boxPanelPadding);
    s.title.margin = term(th->boxTitleMargin, L.boxTitleMargin);
    s.title.border = term(th->boxTitleBorder, uniform(th->titleBorder()));
    s.title.padding = term(th->boxTitlePadding, L.boxTitlePadding);
    s.content.margin = term(th->boxContentMargin, L.boxContentMargin);
    s.content.border = term(th->boxContentBorder, uniform(th->cardBorder));
    s.content.padding = term(th->boxContentPadding, L.boxContentPadding);
    s.button.margin = term(th->boxButtonMargin, L.boxButtonMargin);
    s.button.border = term(th->boxButtonBorder, uniform(th->buttonBorder));
    s.button.padding = term(th->boxButtonPadding, L.boxButtonPadding);
    // The junction gap: a scalar (stored uniform in the theme's BoxTerm).
    s.gap = th->boxPanelGap.set ? th->boxPanelGap.v.t : L.boxPanelGap;
    s.themed = th->hasArt();
    s.band = hasThemedTitleBand();
    s.card = hasThemedContentCard();
    s.hasCaption = m_bShowTitle;
    // The caption's box: the normal tier's GLYPH CELL, band or not and WHATEVER
    // the tier — one figure, so a HUD's band still measures the same as a
    // widget's (one band height across the surface, per review) and a Large
    // title's em-box still overhangs it, which nothing clips.
    //
    // The CELL, not the lineHeight ROW. Line height is the pitch BETWEEN
    // stacked rows, and half of it is leading that belongs between them; a
    // caption has no next row, so spending it here buys air at the panel's own
    // top edge — 0.087 em of it, on top of whatever inset the .fnt's cell
    // already holds above its caps. That is the air a user sees after setting
    // every term in the box model to 0 and reasonably asking why. (What is
    // left after this is the font's own: the shipped faces set their caps
    // between 0.178 and 0.267 down their cell, and no single figure can trim
    // that without making a panel's height depend on which font is picked —
    // which is the reflow the whole normalisation exists to prevent.)
    s.captionW = want.captionW / dim.cellW;
    s.captionH = dim.fontSize / dim.cellH;
    for (const float h : want.sectionH) s.sections.push_back(h / dim.cellH);
    s.cols = want.contentW / dim.cellW;
    // PanelWant::contentFillsPanel -- move the panel's padding INTO the content
    // rather than dropping it, so the outer rect is unchanged and only the split
    // between air and slab moves: the slab runs FULL-BLEED to the panel's sides
    // and bottom. Unthemed only: `s.themed` is `hasArt()`, and with art the ring
    // is what the frame is drawn in.
    //
    // THE TOP PADDING STAYS WHILE A TITLE IS SHOWN: it is the caption's own air,
    // and spending it put "Gap Bar" flush against the panel's top edge while
    // every neighbouring caption kept its inset -- reported from exactly that
    // stack, twice: the first read of that report restored the SIDE padding
    // instead, and the slab stopped covering the panel, which is the flag's
    // whole point. Title off, the slab is the entire panel and takes the top
    // share too.
    //
    // The vertical terms are x-cells converted by `unit` (panel_box.h), so the
    // share added to the section makes the same conversion; the horizontal ones
    // are x-cells throughout and go straight onto `cols`. `want.bands`, not
    // `s.bands`: the bands loop below has not run yet, so the spec's copy is
    // empty here no matter what the caller asked for.
    // HOW THE SIDES ARE FREED: negative content margins, NOT zeroed padding. The
    // caption's X is panelInnerLeft + the [title] insets, and panelInnerLeft is
    // where the panel padding is spent -- so zeroing padding.l put the caption's
    // icon in the panel's corner while Timing's stayed inset (reported, the
    // third finding against this one flag). The padding stays, and the CARD is
    // pulled back out through it: cardLeft = panelInnerLeft + margin.l, so a
    // margin of -padding.l lands the slab exactly on the panel's edge while
    // everything positioned off the inner box (the caption) keeps its inset.
    if (want.contentFillsPanel && !s.themed && !s.sections.empty() && want.bands.empty()) {
        s.content.margin.l -= s.panel.padding.l;
        s.content.margin.r -= s.panel.padding.r;
        const double topShare = s.hasCaption ? 0.0 : s.panel.padding.t;
        s.sections.back() += (topShare + s.panel.padding.b) * s.unit;
        s.panel.padding.b = 0.0;
        if (!s.hasCaption) s.panel.padding.t = 0.0;
    }
    // The general body, when a caller states one. Spec resolves the pair itself
    // (bands wins), so both are handed over and the engine decides -- rather than
    // this converter deciding and the two rules drifting.
    for (const PanelWant::BandWant& bw : want.bands) {
        PanelBox::BandAsk ask;
        for (const PanelWant::ColumnWant& cw : bw.columns) {
            PanelBox::ColumnAsk col;
            col.cols = cw.contentW / dim.cellW;
            for (const float h : cw.sectionH) col.sections.push_back(h / dim.cellH);
            ask.columns.push_back(std::move(col));
        }
        s.bands.push_back(std::move(ask));
    }
    s.buttons = want.buttons;
    s.buttonW = want.buttonW / dim.cellW;
    s.buttonH = want.buttonH / dim.cellH;
    s.minPanelW = want.minPanelW / dim.cellW;
    s.minBodyH = want.minBodyH / dim.cellH;
    return s;
}

// The plan memo's key comparison. Written out rather than memcmp'd: ScaledDimensions
// is a float struct with no padding guarantee, and PanelWant holds vectors.
namespace {
bool sameDims(const BaseHud::ScaledDimensions& a, const BaseHud::ScaledDimensions& b) {
    return a.fontSize == b.fontSize && a.fontSizeExtraSmall == b.fontSizeExtraSmall
        && a.fontSizeSmall == b.fontSizeSmall && a.fontSizeLarge == b.fontSizeLarge
        && a.fontSizeExtraLarge == b.fontSizeExtraLarge
        && a.paddingH == b.paddingH && a.paddingV == b.paddingV
        && a.lineHeightExtraSmall == b.lineHeightExtraSmall
        && a.lineHeightSmall == b.lineHeightSmall && a.lineHeightLarge == b.lineHeightLarge
        && a.lineHeightNormal == b.lineHeightNormal
        && a.lineHeightExtraLarge == b.lineHeightExtraLarge
        && a.cellW == b.cellW && a.cellH == b.cellH && a.scale == b.scale;
}
bool sameWant(const BaseHud::PanelWant& a, const BaseHud::PanelWant& b) {
    if (a.contentW != b.contentW || a.captionW != b.captionW || a.tier != b.tier
        || a.buttons != b.buttons || a.buttonW != b.buttonW || a.buttonH != b.buttonH
        || a.minPanelW != b.minPanelW || a.minBodyH != b.minBodyH
        || a.contentFillsPanel != b.contentFillsPanel) return false;
    if (a.sectionH != b.sectionH) return false;
    if (a.bands.size() != b.bands.size()) return false;
    for (size_t i = 0; i < a.bands.size(); ++i) {
        const auto& ca = a.bands[i].columns; const auto& cb = b.bands[i].columns;
        if (ca.size() != cb.size()) return false;
        for (size_t j = 0; j < ca.size(); ++j)
            if (ca[j].contentW != cb[j].contentW || ca[j].sectionH != cb[j].sectionH)
                return false;
    }
    return true;
}
}  // namespace

BaseHud::PanelPlan& BaseHud::planPanel(const ScaledDimensions& dim,
                                       const PanelWant& want) const {
    MXB_COUNT_CALL(PLAN_PANEL);
    auto& bmPlan = PluginData::getInstance().getBenchmarkMetrics();
    const long long planStart = bmPlan.active ? DrawHandler::getCurrentTimeUs() : 0;

    // BY REFERENCE, into this HUD's own cache. PanelPlan carries a PanelBox::Geom,
    // which owns three std::vectors -- so returning it by value put a heap
    // round-trip in every rebuild, and a round-trip measured 1.57us inside the game
    // process (see small_vec.h). Nothing holds two plans at once: every caller in
    // the tree binds one per rebuild, so a reference to the cache is the whole
    // plan's lifetime.
    //
    // x0/y0 are cleared on the way out because a fresh plan has them at zero and
    // addPlanBackground stamps them later; MapHud reads contentX() before that call
    // and relies on it. They are NOT part of the memo key -- the same plan drawn at
    // a different origin is the same plan.
    const unsigned int gen = AssetManager::getInstance().themeGeneration();
    const bool hit = m_planCacheValid && m_planCacheGen == gen
        && sameDims(m_planCacheDim, dim) && sameWant(m_planCacheWant, want);
    if (!hit) {
        // Built straight INTO the cache, so a miss costs one layout rather than a
        // layout plus a copy of its result.
        m_planCachePlan.g = PanelBox::layoutPanel(resolvePanelSpec(dim, want));
        m_planCachePlan.cellW = dim.cellW;
        m_planCachePlan.cellH = dim.cellH;
        m_planCachePlan.capFontSize =
            (want.tier == TitleTier::Large) ? dim.fontSizeLarge : dim.fontSize;
        m_planCacheValid = true;
        m_planCacheGen = gen;
        m_planCacheDim = dim;
        m_planCacheWant = want;
    }
    m_planCachePlan.x0 = 0.0f;
    m_planCachePlan.y0 = 0.0f;
    if (bmPlan.active) {
        const long long dt = DrawHandler::getCurrentTimeUs() - planStart;
        bmPlan.planChainTimeUs += dt;
        bmPlan.planPanelTimeUs += dt;
        ++bmPlan.planChainCalls;
    }
    return m_planCachePlan;
}

float BaseHud::planBodyHeight(const ScaledDimensions& dim, const PanelWant& want) const {
    const PanelBox::Geom g = PanelBox::layoutPanel(resolvePanelSpec(dim, want));
    if (g.bands.empty()) return 0.0f;
    // TIMES cellH, because the two sides of this function speak different units and
    // nothing in either signature says so: PanelBox::Geom is in CELLS (which is why
    // every reader goes through PanelPlan's X/Y/W/H converters), while PanelWant is in
    // SCREEN units -- minBodyH's previous caller spelled it `rows * lineHeightNormal`.
    // Returning the raw cell figure and assigning it to a want made resolvePanelSpec
    // divide by cellH a second time, and the settings panel came out SIXTY SCREENS
    // tall: centred, so it began ~30 screens above the viewport and rendered as one
    // black rectangle with its content nowhere near the visible area.
    return static_cast<float>(g.bands.back().bot - g.bands.front().top) * dim.cellH;
}

void BaseHud::addPlanBackground(PanelPlan& p, float x, float y) {
    auto& bmBg = PluginData::getInstance().getBenchmarkMetrics();
    const long long bgStart = bmBg.active ? DrawHandler::getCurrentTimeUs() : 0;
    struct BgTimer {
        decltype(bmBg)& m; long long s;
        ~BgTimer() { if (m.active) m.planChainTimeUs += DrawHandler::getCurrentTimeUs() - s; }
    } bgTimer{bmBg, bgStart};
    p.x0 = x;
    p.y0 = y;
    // Fill strips for the cut. Plan covers carry REAL rects inset from the
    // frame centre (panel padding, margins), unlike the legacy full-width
    // covers — so the slab sweep cuts columns beside them too. Bound: the
    // x-cuts are the centre's edges plus the band's and the cards' (cards
    // share one extent), at most 5 columns, each holding at most covers + 1
    // complement intervals. Undercounting is not cosmetic: cutFill overflows
    // to the full-centre fallback and the fill draws UNDER the band and
    // cards, the double layer the cut exists to remove.
    int bodyCards = 0;
    for (const PanelBox::BandGeom& band : p.g.bands)
        for (const PanelBox::ColumnGeom& col : band.columns)
            bodyCards += static_cast<int>(col.sections.size());
    const int covers = (p.g.hasTitle ? 1 : 0) + (p.g.hasCard ? bodyCards : 0);
    m_fillReserve = 5 * (covers + 1);
    addBackgroundQuad(x, y, p.width(), p.height());
    const ThemeAsset* theme = activeTheme();
    // The band: the same nine card sprites at the BAND's scale, at the plan's
    // box. Recorded as a card COVER with its real rect — unlike the legacy
    // band record, which assumed a band spanning the frame's full inner width;
    // this band sits inside the panel's padding, so a full-width cover would
    // cut fill where nothing covers it.
    if (p.g.hasTitle && theme->hasArt() && theme->hasCard()) {
        // Drawn height is bandDrawnBot, not titleH: the band absorbs the
        // caption block's quantization remainder (see Geom::bandDrawnBot).
        const float bw = p.W(p.g.titleW), bh = p.H(p.g.bandDrawnBot - p.g.titleTop);
        if (bw > 0.0f && bh > 0.0f) {
            const float bx = p.X(p.g.titleLeft), by = p.Y(p.g.titleTop);
            recordCardCover(static_cast<int>(m_quads.size()), bx, by, bw, bh);
            float ox = bx, oy = by;
            applyOffset(ox, oy);
            emitThemedSliceSet(*theme, ox, oy, bw, bh, -1, SliceSet::BAND, 0);
        }
    }
    // One card PER SECTION — siblings, never overlapping, their seam the sum of
    // the facing margins. addThemedCard records each as a fill cover itself.
    // The section boxes already carry the ceil remainder (the last one grew by
    // it in layoutPanel), so the drawn card IS the content box — no second
    // "drawn bottom" for fillers to miss.
    // EVERY COLUMN OF EVERY BAND, not g.sections -- that flattened view is the
    // FIRST column of each band, which is all a one-column panel has and half of
    // a split. A panel whose body is a sidebar beside its content draws cards in
    // both, and each column carries its own left/width.
    //
    // The card box inside a column is the column's own box minus the content
    // margins, which is what cardLeft/cardW are for the one-column case -- stated
    // here from the column so the two cases are one expression rather than a
    // special case bolted beside the other.
    if (p.g.hasCard) {
        for (const PanelBox::BandGeom& band : p.g.bands) {
            for (const PanelBox::ColumnGeom& col : band.columns) {
                for (const PanelBox::SectionGeom& sec : col.sections) {
                    addThemedCard(p.X(col.cardLeft), p.Y(sec.top),
                                  p.W(col.cardW), p.H(sec.bot - sec.top));
                }
            }
        }
    }
}

void BaseHud::addPlanTitle(const PanelPlan& p, const char* text, int fontIndex,
                           unsigned long color) {
    auto& bmT = PluginData::getInstance().getBenchmarkMetrics();
    const long long tStart = bmT.active ? DrawHandler::getCurrentTimeUs() : 0;
    struct TTimer {
        decltype(bmT)& m; long long s;
        ~TTimer() { if (m.active) m.planChainTimeUs += DrawHandler::getCurrentTimeUs() - s; }
    } tTimer{bmT, tStart};
    m_titleStringIndex = -1;
    m_titleIconQuadIndex = -1;
    if (!m_bShowTitle) {
        // The placeholder keeps string index 0 stable for every index-walking
        // layout fast path — the contract the retired legacy caption kept too. Returning
        // without it parked real content on the caption row and shifted every
        // later string one slot after a drag with the title off.
        addString("", p.X(p.g.captionX), p.Y(p.g.panelInner),
                  PluginConstants::Justify::LEFT, fontIndex, color, p.capFontSize);
        return;
    }
    const float fs = p.capFontSize;
    const float ty = planTitleY(p);
    const float tx = p.X(p.g.captionX);
    // The identity icon (a plan caption is
    // always LEFT-justified — it sits at its own column, never centred).
    const int spriteIndex = UiConfig::getInstance().getTitleIcons()
        ? AssetManager::getInstance().getIconSpriteIndex(getIconName()) : 0;

    if (spriteIndex <= 0) {
        // Centred captions (the settings menu; see centreTitle) hand the renderer the
        // band's midpoint and let it justify -- no measuring here. The first version
        // of this measured the text and shifted the start instead, which is what you
        // need to centre an icon AND its text as one block; but a caption with an icon
        // is exactly the case that does not want centring (see below), so the
        // measurement was solving a problem no caller has.
        const bool centred = centreTitle();
        addString(text,
                  centred ? p.X(p.g.titleLeft + p.g.titleW * 0.5) : tx, ty,
                  centred ? PluginConstants::Justify::CENTER : PluginConstants::Justify::LEFT,
                  fontIndex, color, fs);
        return;
    }
    // FROM HERE ON THE CAPTION HAS AN ICON, and stays left-aligned whatever
    // centreTitle() says. An icon is an identity marker for a panel you pick out of a
    // cluttered screen, which is the opposite need from a centred page heading -- and
    // the only panel that centres (the settings menu) has no icon, so the combination
    // has no user. If one ever wants it, centre the icon+text block by its measured
    // width via titleIconAdvance(), which owns icon-width-plus-gap.
    m_titleFontSize = fs;
    m_titleIconSize = fs * layout().titleIconSize;
    m_titleIconQuadIndex = static_cast<int>(m_quads.size());
    addIcon(tx, ty, spriteIndex, color, m_titleIconSize);
    m_titleStringIndex = static_cast<int>(m_strings.size());
    addString(text, tx, ty, PluginConstants::Justify::LEFT, fontIndex, color, fs);
    finalizeTitleIcon(m_strings[m_titleStringIndex].m_afPos[0],
                      m_strings[m_titleStringIndex].m_afPos[1]);
}

// The resolved [button] terms — the same fallbacks resolvePanelSpec applies,
// for panels that lay out their own button rows.
BaseHud::PlanButtonTerms BaseHud::planButtonTerms(const ScaledDimensions& dim) const {
    const ThemeAsset* th = activeTheme();
    const auto term = [](const ThemeAsset::BoxTerm& t, float fb) {
        const double d = static_cast<double>(fb);
        return t.set ? t.v : PanelBox::Sides{d, d, d, d};
    };
    const LayoutMetrics& L = layout();
    const PanelBox::Sides m = th->boxButtonMargin.set ? th->boxButtonMargin.v
                                                      : L.boxButtonMargin;
    const PanelBox::Sides b = th->hasArt() ? term(th->boxButtonBorder, th->buttonBorder)
                                           : PanelBox::Sides{};
    const PanelBox::Sides pd = th->boxButtonPadding.set ? th->boxButtonPadding.v
                                                        : L.boxButtonPadding;
    PlanButtonTerms out;
    out.insetL = static_cast<float>(b.l + pd.l) * dim.cellW;
    out.insetR = static_cast<float>(b.r + pd.r) * dim.cellW;
    // Vertical: a term is stated in x-cells and drawn square on screen, so the
    // pixel height is raw * cellW * aspect — the same conversion the engine's
    // `unit` makes (PanelBox::Spec::unit) and the margin helpers use.
    out.insetT = static_cast<float>(b.t + pd.t) * dim.cellW * PluginConstants::UI_ASPECT_RATIO;
    out.insetB = static_cast<float>(b.b + pd.b) * dim.cellW * PluginConstants::UI_ASPECT_RATIO;
    // Facing margins PLUS the junction gap, the same seam PanelBox::layoutPanel
    // spends between two buttons (btnSeam). A row of buttons is a stack laid
    // sideways and `gap` names them; without it, zeroing buttonMargin makes the
    // settings footer's Save and Close labels touch.
    out.gap = static_cast<float>(m.r + m.l + panelGapCells()) * dim.cellW;
    out.marginT = static_cast<float>(m.t) * dim.cellW * PluginConstants::UI_ASPECT_RATIO;
    out.marginB = static_cast<float>(m.b) * dim.cellW * PluginConstants::UI_ASPECT_RATIO;
    return out;
}

// The caption's glyph row, band or bare — one owner, shared by the emit above
// and any fast path repositioning the caption string.
float BaseHud::planTitleY(const PanelPlan& p) const {
    const float fs = p.capFontSize;
    if (!p.g.hasCaptionRow) return p.Y(p.g.panelInner);
    // ONE RULE, band or bare: the caption INK-centred in its caption box — its
    // OWN box, never the slack-stretched drawn band. titleSlack is a function
    // of the block's BOTTOM terms, the junction gap and the cell ratio
    // (ceil(adv) - adv), so a glyph centred into it moved when [title] margin
    // BOTTOM changed and when uiLineHeight changed — a sixth of a cell,
    // measured, on terms that must not touch a caption. The band's art still
    // absorbs the slack (bandDrawnBot); it sits below the glyph, where a
    // stretch reads as the band ending and moves nothing. inkCenteredY takes
    // off the row centring addString re-adds.
    //
    // The bare case used to hand back the row TOP and let addString centre the
    // glyph — but addString centres it in a row derived from the CAPTION's own
    // font size, not in the box. Identical at the Normal tier, where the two
    // are the same row; at the Large tier the full HUDs caption at, it pushed
    // the glyph 0.195 em below where its own box says it goes, which is most
    // of the gap over a Standings or Records title.
    const double boxCells = (p.g.titleBot - (p.g.T.b.b + p.g.T.p.b))
                          - p.g.captionY;
    return inkCenteredY(p.Y(p.g.captionY), p.H(boxCells), fs);
}

// The caption's ASK — what PanelWant::captionW should carry so a long title
// widens its panel instead of overhanging it (the model's widest-ask rule; the
// old chain simply let it overhang). Text width plus the icon's advance when
// one will be drawn.
float BaseHud::planTitleWidth(const ScaledDimensions& dim, const char* text,
                              TitleTier tier) const {
    if (!m_bShowTitle || !text) return 0.0f;
    const float fs = (tier == TitleTier::Large) ? dim.fontSizeLarge : dim.fontSize;
    float w = PluginUtils::calculateMonospaceTextWidth(
        static_cast<int>(std::strlen(text)), fs);
    if (UiConfig::getInstance().getTitleIcons()
        && AssetManager::getInstance().getIconSpriteIndex(getIconName()) > 0)
        w += titleIconAdvance(fs * layout().titleIconSize);
    return w;
}
// ==== end box-model plan =====================================================

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
// two line up by construction rather than by two call sites agreeing; top FLUSH
// under the band, bottom at the panel's own frame clearance. (There was a
// [content] gap-y between the two; scrapping the key made the seam flush.)
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
    // same sum the settings panel spends between its band and its first card. It was
    // the gap alone here, so [title] margin-bottom padded the body card's INTERIOR
    // (the rows anchor to the card, which had not moved) instead of widening the
    // band-to-card seam it names -- the two panels disagreeing about one term again,
    // which is what titleRowHeight's matching reservation exists to pay for.
    //
    // THE PREDICATE IS "IS A BAND DRAWN", NOT "IS THE ANCHOR NONZERO", and the
    // difference is a whole bug: with a caption the theme draws no band for,
    // The retired legacy caption handed in a SYNTHETIC anchor (the reserved row's bottom), and
    // that already contains this margin because titleRowHeight put it there. Gated
    // on `bandBottom > 0` the term was spent twice on exactly that path -- measured
    // at 0.0880 of card movement against 0.0587 of reservation, a ~32px overshoot at
    // 1080p -- while the banded path, where the anchor is the band's own bottom edge
    // and carries no margin, was right. hasThemedTitleBand() separates them.
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
    // happens to have. That was visible -- Performance and Session Charts sat 12px
    // further from the title band and 19px further from the panel's bottom edge than
    // Standings, purely because their cards were derived from their CONTENT while
    // every other card is derived from the PANEL.
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
    // section hangs off its own heading instead. Without this the first card was
    // placed from the CONTENT, which sits at the panel's padding rather than at the
    // band's bottom, so a sectioned HUD's gap under its title was a different size
    // from every other HUD's.
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
    // sides -- not a second number kept equal to it by hand. There used to be a
    // standalone LayoutMetrics::sectionGap holding 1.0 beside a boxContentMargin
    // of 0.5 per side, which is the same distance written twice; retuning the
    // margin and not it silently doubled every settings card seam. The THEME side
    // never had the duplicate: ThemeAsset::sectionGapOverride is already derived
    // from the theme's own [content] margin at parse time, and this reads through
    // the same accessor.
    const PanelBox::Sides& cm = layout().boxContentMargin;
    return activeTheme()->sectionGap(static_cast<float>(cm.t + cm.b)) + panelGapCells();
}

// The clearance a section card keeps between its border and its content, matching
// what the rows already keep from the whole-body card's border.
// SQUARE ON SCREEN (cellW * aspect), not cellH: the box-model rule is that one
// stated cell is square, and the engine spends every vertical term that way
// (Spec::unit). This read multiplied by cellH — the half-row lattice, ~20%
// taller — so a gap of 1 drew taller in the settings seams than the same gap
// drew wide in the trough or tall in any plan panel. One conversion now, so a
// term means one distance everywhere.
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
// padding above and below. This briefly shrank the PANEL instead (a bottom pad of
// sectionCardPaddingY + frameMargin, ~19px less than paddingV), which fixed the gap but
// left Performance and Session Charts 1.5 cells shorter than a Standings or Stats
// with the same content: heights that could never line up, and a bottom pad no
// other HUD used.
//
// The empty-tail worry that motivated the shrink was measured, and it is 19px: the
// last card sits that much deeper below its content than the sections above it.
// That is the same slack a single-card HUD's rows have under them, so it reads as
// the body ending rather than as one section being wrong.
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
// m_bContentCard before ever reaching contentCardKind(), so the switch governed
// nothing for this panel. Measured by perturbing every theme key and diffing the
// render -- settings-content moved zero pixels even after the panel was given its
// own PanelKind. Gating the clearance with the cards (this is read for the panel's
// borders as well) matches what contentPaddingX() already does for a HUD.
bool BaseHud::hasThemedCard() const {
    const ThemeAsset* theme = activeTheme();
    return theme && theme->hasCard()
        && theme->contentCardKind(static_cast<int>(m_panelKind));
}

// Coordinates are PRE-offset, like addString/addBackgroundQuad and every other
// add* helper -- callers lay out in un-dragged space and the offset is applied
// here. Taking post-offset coords instead is what made the settings panel's
// section cards stay behind when the panel was dragged: the panel fully rebuilds
// on a layout change, so the cards were rebuilt correctly at the WRONG origin.
//
// Every card drawn here is ALSO recorded as a fill cover: a card the sweep does
// not know about sits on the frame's centre fill and composites twice, which at
// any opacity below 100% reads a shade darker than the panel around it. That was
// visible on every settings card -- the tab-group and section cards went through
// these helpers while the sweep only knew the covers the BaseHud section helpers
// recorded by hand. Recording HERE, at the one place every card is emitted, is
// what makes a new card-drawing caller correct by construction.
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
    // button slices. Deciding here on `theme->hasButton()` alone therefore rewrote nine
    // quads over a one-quad band and blanked the eight that followed it. Reachable only
    // if a palette slot resolves to alpha 0, which is why it never fired -- but the two
    // decisions have to be the same decision, not two that happen to agree.
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
    // it now scales the TINT rather than the whole fill, so the ordering survives.
    const float alpha = static_cast<float>((stateColor >> 24) & 0xFFu) / 255.0f;
    return PluginUtils::flattenOnto(
        PluginUtils::applyOpacity(stateColor, alpha * BUTTON_STATE_TINT), surface);
}

// See the declaration. The legible ink for `ink` drawn on `fill`, keeping the hue
// wherever it can and only abandoning it as a floor.
//
// EXTRACTED FROM buttonGlyphColor, which is where this reasoning was worked out and
// where it stayed. Notices and the Gap Bar draw a caption on a fill of ITS OWN slot
// colour -- NEGATIVE text on a NEGATIVE slab, POSITIVE on POSITIVE -- which is the
// "accent on accent" pair chipGlyphColor's comment already records as invisible on a
// light theme. It survived there because the slab is drawn at the HUD's background
// opacity: at the shipped 60% the slab is a wash and full-strength ink reads over it,
// so the bug only appears when a user raises opacity or picks an opaque theme.
// Reported from the game on a light theme at full opacity: "WRONG WAY" in red on red.
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
    // flattening turned a faint tint over the track into a solid box. A button is a
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

// ONE ARC EMITTER for every HUD that draws a ring. FmxHud and LeanWidget each
// carried a byte-identical private copy of this (they differed only in a
// `numSegments < 1` guard), so a fix to one silently left the other behind --
// which is exactly what happened with the cost below.
//
// NO sin/cos PER SEGMENT. Each vertex pair needs (sin, cos) of its own angle, and
// the obvious spelling calls both every iteration: at 32 segments per arc and eight
// arcs, FmxHud spent ~400 transcendental calls per rebuild, which is most of its
// 9.3us/frame. The angles are an arithmetic sequence, so the pair can be ROTATED
// instead of recomputed -- one angle-addition per step, two transcendentals for the
// whole arc regardless of length:
//
//     sin(A+d) = sinA*cosd + cosA*sind
//     cos(A+d) = cosA*cosd - sinA*sind
//
// The rotor accumulates in double so the drift over an arc is far below a pixel;
// recomputing from scratch would defeat the point, and float would visibly wander
// on a long arc.
void BaseHud::addArcSegment(float centerX, float centerY, float innerRadius, float outerRadius,
                            float startAngleRad, float endAngleRad, unsigned long color,
                            int numSegments) {
    using namespace PluginConstants;
    if (numSegments < 1) numSegments = 1;

    const double step = (static_cast<double>(endAngleRad) - startAngleRad) / numSegments;
    const double cosStep = std::cos(step);
    const double sinStep = std::sin(step);

    // The rotating unit vector, seeded at the start angle. sin/cos rather than
    // cos/sin because 0 rad means UP here and positive means clockwise.
    double sinA = std::sin(static_cast<double>(startAngleRad));
    double cosA = std::cos(static_cast<double>(startAngleRad));

    float prevInnerX = 0.0f, prevInnerY = 0.0f;
    float prevOuterX = 0.0f, prevOuterY = 0.0f;
    bool hasPrevPoint = false;

    for (int i = 0; i <= numSegments; ++i) {
        const float sa = static_cast<float>(sinA);
        const float ca = static_cast<float>(cosA);

        const float innerX = centerX + sa * innerRadius / UI_ASPECT_RATIO;
        const float innerY = centerY - ca * innerRadius;
        const float outerX = centerX + sa * outerRadius / UI_ASPECT_RATIO;
        const float outerY = centerY - ca * outerRadius;

        if (hasPrevPoint) {
            float pix = prevInnerX, piy = prevInnerY;
            float pox = prevOuterX, poy = prevOuterY;
            float cix = innerX,     ciy = innerY;
            float cox = outerX,     coy = outerY;
            applyOffset(pix, piy);
            applyOffset(pox, poy);
            applyOffset(cix, ciy);
            applyOffset(cox, coy);

            // prevOuter -> prevInner -> currInner -> currOuter (counter-clockwise,
            // matching what the engine expects).
            SPluginQuad_t quad;
            quad.m_aafPos[0][0] = pox; quad.m_aafPos[0][1] = poy;
            quad.m_aafPos[1][0] = pix; quad.m_aafPos[1][1] = piy;
            quad.m_aafPos[2][0] = cix; quad.m_aafPos[2][1] = ciy;
            quad.m_aafPos[3][0] = cox; quad.m_aafPos[3][1] = coy;
            quad.m_iSprite = SpriteIndex::SOLID_COLOR;
            quad.m_ulColor = color;
            m_quads.push_back(quad);
        }

        prevInnerX = innerX; prevInnerY = innerY;
        prevOuterX = outerX; prevOuterY = outerY;
        hasPrevPoint = true;

        const double nextSin = sinA * cosStep + cosA * sinStep;
        const double nextCos = cosA * cosStep - sinA * sinStep;
        sinA = nextSin;
        cosA = nextCos;
    }
}

void BaseHud::addDot(float x, float y, unsigned long color, float size) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset before setting quad positions
    applyOffset(x, y);

    // Create a small square centered at (x, y)
    // Apply aspect ratio correction to horizontal dimension to maintain square appearance
    float halfSizeX = (size * 0.5f) / UI_ASPECT_RATIO;
    float halfSizeY = size * 0.5f;

    quadEntry.m_aafPos[0][0] = x - halfSizeX;  // Top-left
    quadEntry.m_aafPos[0][1] = y - halfSizeY;
    quadEntry.m_aafPos[1][0] = x - halfSizeX;  // Bottom-left
    quadEntry.m_aafPos[1][1] = y + halfSizeY;
    quadEntry.m_aafPos[2][0] = x + halfSizeX;  // Bottom-right
    quadEntry.m_aafPos[2][1] = y + halfSizeY;
    quadEntry.m_aafPos[3][0] = x + halfSizeX;  // Top-right
    quadEntry.m_aafPos[3][1] = y - halfSizeY;

    quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
    quadEntry.m_ulColor = color;

    m_quads.push_back(quadEntry);
}

void BaseHud::addIcon(float x, float y, int spriteIndex, unsigned long color, float size) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset before setting quad positions
    applyOffset(x, y);

    // Centered square, aspect-corrected so the icon stays round (not stretched).
    float halfSizeX = (size * 0.5f) / UI_ASPECT_RATIO;
    float halfSizeY = size * 0.5f;

    quadEntry.m_aafPos[0][0] = x - halfSizeX;  // Top-left
    quadEntry.m_aafPos[0][1] = y - halfSizeY;
    quadEntry.m_aafPos[1][0] = x - halfSizeX;  // Bottom-left
    quadEntry.m_aafPos[1][1] = y + halfSizeY;
    quadEntry.m_aafPos[2][0] = x + halfSizeX;  // Bottom-right
    quadEntry.m_aafPos[2][1] = y + halfSizeY;
    quadEntry.m_aafPos[3][0] = x + halfSizeX;  // Top-right
    quadEntry.m_aafPos[3][1] = y - halfSizeY;

    quadEntry.m_iSprite = spriteIndex;
    quadEntry.m_ulColor = color;

    m_quads.push_back(quadEntry);
}

void BaseHud::addLineSegment(float x1, float y1, float x2, float y2, unsigned long color, float thickness) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset
    applyOffset(x1, y1);
    applyOffset(x2, y2);

    // Calculate perpendicular direction for thickness
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len = std::sqrt(dx * dx + dy * dy);

    if (len < 0.0001f) return;  // Skip zero-length segments

    // Perpendicular vector (normalized) - try reversed direction
    float px = dy / len;
    float py = -dx / len;

    // Half thickness offset (apply aspect ratio correction to horizontal component)
    float hx = (px * thickness * 0.5f) / PluginConstants::UI_ASPECT_RATIO;
    float hy = py * thickness * 0.5f;

    // Create rectangle quad (match stick trail pattern exactly: p1+perp, p1-perp, p2-perp, p2+perp)
    quadEntry.m_aafPos[0][0] = x1 + hx;
    quadEntry.m_aafPos[0][1] = y1 + hy;
    quadEntry.m_aafPos[1][0] = x1 - hx;
    quadEntry.m_aafPos[1][1] = y1 - hy;
    quadEntry.m_aafPos[2][0] = x2 - hx;
    quadEntry.m_aafPos[2][1] = y2 - hy;
    quadEntry.m_aafPos[3][0] = x2 + hx;
    quadEntry.m_aafPos[3][1] = y2 + hy;

    quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
    quadEntry.m_ulColor = color | 0xFF000000;  // Ensure full alpha

    m_quads.push_back(quadEntry);
}

void BaseHud::addNeedleQuad(float centerX, float centerY, float angleRad,
                            float needleLength, float needleWidth, unsigned long color) {
    using namespace PluginConstants;

    // Create needle as a trapezoid shape (flat tip, wider base)
    // The needle points from center outward in the direction of angleRad
    // Uses clockwise vertex order and applyOffset() on each point individually

    // Calculate tip center (pointing outward)
    float tipCenterX = centerX + std::sin(angleRad) * needleLength / UI_ASPECT_RATIO;
    float tipCenterY = centerY - std::cos(angleRad) * needleLength;

    // Calculate base center (opposite of tip, small distance from center)
    float baseLength = needleLength * 0.15f;  // Base extends 15% of needle length behind center
    float baseCenterX = centerX - std::sin(angleRad) * baseLength / UI_ASPECT_RATIO;
    float baseCenterY = centerY + std::cos(angleRad) * baseLength;

    // Calculate perpendicular direction for width
    float perpAngle = angleRad + Math::PI * 0.5f;  // 90 degrees to the right

    // Tip is narrower (30% of base width) - creates flat but tapered look
    float tipHalfWidth = needleWidth * 0.15f;
    float baseHalfWidth = needleWidth * 0.5f;

    // Calculate tip left and right points
    float tipLeftX = tipCenterX + std::sin(perpAngle) * tipHalfWidth / UI_ASPECT_RATIO;
    float tipLeftY = tipCenterY - std::cos(perpAngle) * tipHalfWidth;
    float tipRightX = tipCenterX - std::sin(perpAngle) * tipHalfWidth / UI_ASPECT_RATIO;
    float tipRightY = tipCenterY + std::cos(perpAngle) * tipHalfWidth;

    // Calculate base left and right points
    float baseLeftX = baseCenterX + std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseLeftY = baseCenterY - std::cos(perpAngle) * baseHalfWidth;
    float baseRightX = baseCenterX - std::sin(perpAngle) * baseHalfWidth / UI_ASPECT_RATIO;
    float baseRightY = baseCenterY + std::cos(perpAngle) * baseHalfWidth;

    // Apply HUD offset to each point individually (MapHud pattern)
    applyOffset(tipLeftX, tipLeftY);
    applyOffset(tipRightX, tipRightY);
    applyOffset(baseRightX, baseRightY);
    applyOffset(baseLeftX, baseLeftY);

    // Create quad with clockwise vertex order: tipLeft -> tipRight -> baseRight -> baseLeft
    // NOTE: Must use clockwise for proper rendering (counter-clockwise gets face-culled)
    SPluginQuad_t needle;
    needle.m_aafPos[0][0] = tipLeftX;      // Front left
    needle.m_aafPos[0][1] = tipLeftY;
    needle.m_aafPos[1][0] = tipRightX;     // Front right (clockwise)
    needle.m_aafPos[1][1] = tipRightY;
    needle.m_aafPos[2][0] = baseRightX;    // Back right
    needle.m_aafPos[2][1] = baseRightY;
    needle.m_aafPos[3][0] = baseLeftX;     // Back left (completes trapezoid)
    needle.m_aafPos[3][1] = baseLeftY;

    needle.m_iSprite = SpriteIndex::SOLID_COLOR;
    needle.m_ulColor = color;
    m_quads.push_back(needle);
}

void BaseHud::addRotatedSpriteQuad(float screenX, float screenY, float halfSize,
                                   float cosYaw, float sinYaw, int spriteIndex,
                                   unsigned long color) {
    using namespace PluginConstants;

    // Define corner offsets in uniform (square) space for proper rotation
    // TL, BL, BR, TR in local space
    float corners[4][2] = {
        {-halfSize, -halfSize},  // Top-left
        {-halfSize,  halfSize},  // Bottom-left
        { halfSize,  halfSize},  // Bottom-right
        { halfSize, -halfSize}   // Top-right
    };

    // Rotate corners in uniform space, then apply aspect ratio to X
    float rotatedCorners[4][2];
    for (int i = 0; i < 4; i++) {
        float dx = corners[i][0];
        float dy = corners[i][1];
        // Rotate in uniform space
        float rotX = dx * cosYaw - dy * sinYaw;
        float rotY = dx * sinYaw + dy * cosYaw;
        // Apply aspect ratio to X after rotation
        rotatedCorners[i][0] = screenX + rotX / UI_ASPECT_RATIO;
        rotatedCorners[i][1] = screenY + rotY;
        applyOffset(rotatedCorners[i][0], rotatedCorners[i][1]);
    }

    // Create rotated sprite quad
    SPluginQuad_t sprite;
    sprite.m_aafPos[0][0] = rotatedCorners[0][0];  // Top-left
    sprite.m_aafPos[0][1] = rotatedCorners[0][1];
    sprite.m_aafPos[1][0] = rotatedCorners[1][0];  // Bottom-left
    sprite.m_aafPos[1][1] = rotatedCorners[1][1];
    sprite.m_aafPos[2][0] = rotatedCorners[2][0];  // Bottom-right
    sprite.m_aafPos[2][1] = rotatedCorners[2][1];
    sprite.m_aafPos[3][0] = rotatedCorners[3][0];  // Top-right
    sprite.m_aafPos[3][1] = rotatedCorners[3][1];
    sprite.m_iSprite = spriteIndex;
    sprite.m_ulColor = color;
    m_quads.push_back(sprite);
}

unsigned long BaseHud::calculateTemperatureColor(float temp, float optTemp,
                                                 float alarmLow, float alarmHigh) {
    // Temperature color gradient:
    // - Below alarmLow: Deep blue (too cold)
    // - alarmLow to optTemp: Blue -> Green gradient (warming up)
    // - At optTemp: Green (optimal)
    // - optTemp to alarmHigh: Green -> Yellow -> Red gradient (getting hot)
    // - Above alarmHigh: Deep red (too hot)

    // Color constants (RGB values)
    constexpr unsigned char BLUE_R = 0x40, BLUE_G = 0x80, BLUE_B = 0xFF;   // Cold blue
    constexpr unsigned char GREEN_R = 0x40, GREEN_G = 0xFF, GREEN_B = 0x40; // Optimal green
    constexpr unsigned char YELLOW_R = 0xFF, YELLOW_G = 0xD0, YELLOW_B = 0x40; // Warning yellow
    constexpr unsigned char RED_R = 0xFF, RED_G = 0x40, RED_B = 0x40;      // Hot red

    unsigned char r, g, b;

    if (temp <= alarmLow) {
        // Below alarm low - solid blue (too cold)
        r = BLUE_R;
        g = BLUE_G;
        b = BLUE_B;
    } else if (temp < optTemp) {
        // Between alarmLow and optTemp - blue to green gradient
        float range = optTemp - alarmLow;
        float t = (range > 0.0f) ? (temp - alarmLow) / range : 1.0f;
        r = static_cast<unsigned char>(BLUE_R + t * (GREEN_R - BLUE_R));
        g = static_cast<unsigned char>(BLUE_G + t * (GREEN_G - BLUE_G));
        b = static_cast<unsigned char>(BLUE_B + t * (GREEN_B - BLUE_B));
    } else if (temp <= alarmHigh) {
        // Between optTemp and alarmHigh - green to yellow to red gradient
        float range = alarmHigh - optTemp;
        float normalized = (range > 0.0f) ? (temp - optTemp) / range : 0.0f;

        if (normalized < 0.5f) {
            // Green to yellow (first half)
            float t = normalized * 2.0f;
            r = static_cast<unsigned char>(GREEN_R + t * (YELLOW_R - GREEN_R));
            g = static_cast<unsigned char>(GREEN_G + t * (YELLOW_G - GREEN_G));
            b = static_cast<unsigned char>(GREEN_B + t * (YELLOW_B - GREEN_B));
        } else {
            // Yellow to red (second half)
            float t = (normalized - 0.5f) * 2.0f;
            r = static_cast<unsigned char>(YELLOW_R + t * (RED_R - YELLOW_R));
            g = static_cast<unsigned char>(YELLOW_G + t * (RED_G - YELLOW_G));
            b = static_cast<unsigned char>(YELLOW_B + t * (RED_B - YELLOW_B));
        }
    } else {
        // Above alarm high - solid red (too hot)
        r = RED_R;
        g = RED_G;
        b = RED_B;
    }

    return PluginUtils::makeColor(r, g, b);
}

void BaseHud::addHorizontalGridLine(float x, float y, float width, unsigned long color, float thickness) {
    using namespace PluginConstants;

    SPluginQuad_t quadEntry;

    // Apply offset before setting quad positions
    float ox = x, oy = y;
    applyOffset(ox, oy);

    // Use width directly (no aspect ratio correction needed - already in correct coordinate space)
    float halfThickness = thickness * 0.5f;

    quadEntry.m_aafPos[0][0] = ox;                      // Top-left
    quadEntry.m_aafPos[0][1] = oy - halfThickness;
    quadEntry.m_aafPos[1][0] = ox;                      // Bottom-left
    quadEntry.m_aafPos[1][1] = oy + halfThickness;
    quadEntry.m_aafPos[2][0] = ox + width;              // Bottom-right
    quadEntry.m_aafPos[2][1] = oy + halfThickness;
    quadEntry.m_aafPos[3][0] = ox + width;              // Top-right
    quadEntry.m_aafPos[3][1] = oy - halfThickness;

    quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;
    quadEntry.m_ulColor = color;

    m_quads.push_back(quadEntry);
}

void BaseHud::addStripChartFrame(float x, float y, float width, float height,
                                 const char* topLabel, const char* midLabel, const char* botLabel,
                                 const ScaledDimensions& dims) {
    // Grid lines at 100%/50%/0% of the value range, drawn before the traces so
    // the data renders on top.
    const unsigned long gridColor = this->getColor(ColorSlot::MUTED);  // Muted gray for subtle grid lines
    const float gridLineThickness = stripChartGridThickness();
    static constexpr float GRID_FRACTIONS[] = { 1.0f, 0.5f, 0.0f };
    for (float fraction : GRID_FRACTIONS) {
        float gridY = y + height - (fraction * height);
        addHorizontalGridLine(x, gridY, width, gridColor, gridLineThickness);
    }

    // Axis labels down the left edge (top / middle / bottom), matching the grid lines.
    const float labelX = x + dims.paddingH * STRIP_CHART_LABEL_INSET;
    const unsigned long labelColor = this->getColor(ColorSlot::TERTIARY);
    const int labelFont = this->getFont(FontCategory::SMALL);
    addString(topLabel, labelX, y, PluginConstants::Justify::LEFT, labelFont, labelColor, dims.fontSizeSmall);
    addString(midLabel, labelX, y + height * 0.5f, PluginConstants::Justify::LEFT, labelFont, labelColor, dims.fontSizeSmall);
    addString(botLabel, labelX, y + height - dims.lineHeightSmall, PluginConstants::Justify::LEFT, labelFont, labelColor, dims.fontSizeSmall);
}


void BaseHud::setQuadPositions(SPluginQuad_t& quad, float x, float y, float width, float height) {
    quad.m_aafPos[0][0] = x;
    quad.m_aafPos[0][1] = y;
    quad.m_aafPos[1][0] = x;
    quad.m_aafPos[1][1] = y + height;
    quad.m_aafPos[2][0] = x + width;
    quad.m_aafPos[2][1] = y + height;
    quad.m_aafPos[3][0] = x + width;
    quad.m_aafPos[3][1] = y;
}

void BaseHud::setQuadPositionsArrowRight(SPluginQuad_t& quad, float x, float y,
                                         float width, float height) {
    // Same winding as setQuadPositions (TL, BL, then the right-hand pair), with the
    // right edge collapsed to one point so the quad renders as a triangle.
    const float tipY = y + height * 0.5f;
    quad.m_aafPos[0][0] = x;         quad.m_aafPos[0][1] = y;
    quad.m_aafPos[1][0] = x;         quad.m_aafPos[1][1] = y + height;
    quad.m_aafPos[2][0] = x + width; quad.m_aafPos[2][1] = tipY;
    quad.m_aafPos[3][0] = x + width; quad.m_aafPos[3][1] = tipY;
}

void BaseHud::setQuadPositionsRotatedCW(SPluginQuad_t& quad, float x, float y,
                                        float width, float height) {
    // Vertex order is fixed by the sprite's own corners -- 0 = its top-left, 1 =
    // bottom-left, 2 = bottom-right, 3 = top-right. Turning the picture clockwise
    // therefore means handing vertex 0 the rect's TOP-RIGHT and walking the rest
    // round from there. The apex of an up-caret (the midpoint of vertices 0 and 3)
    // lands on the rect's right edge, mid-height.
    quad.m_aafPos[0][0] = x + width; quad.m_aafPos[0][1] = y;
    quad.m_aafPos[1][0] = x;         quad.m_aafPos[1][1] = y;
    quad.m_aafPos[2][0] = x;         quad.m_aafPos[2][1] = y + height;
    quad.m_aafPos[3][0] = x + width; quad.m_aafPos[3][1] = y + height;
}

void BaseHud::updateBackgroundQuadPosition(float startX, float startY, float width, float height) {
    if (m_quads.empty()) return;

    float x = startX;
    float y = startY;
    applyOffset(x, y);

    // A themed background occupies 9 quads, not 1. Rewrite the whole recorded span
    // -- moving only m_quads[0] would slide the centre slice out from under a
    // stationary frame. The span is re-validated because a HUD may have rebuilt
    // with a different theme state since it was recorded.
    // >= SLICE_COUNT, not ==: a themed panel also carries the reserved fill strips (see
    // finalizeThemedFill). Testing for exactly nine sent every themed drag down the FLAT
    // path below, which rewrites the centre slice as a full-panel quad -- the frame
    // stays put and the fill swallows it.
    if (m_bgQuadCount >= NineSlice::SLICE_COUNT && m_bgQuadFirst >= 0 &&
        static_cast<size_t>(m_bgQuadFirst) + m_bgQuadCount <= m_quads.size()) {
        if (const ThemeAsset* theme = activeTheme()) {
            // The covering rects were recorded in the OLD offset's space; the panel is
            // moving, so they move with it and the strips are re-cut against them.
            // BOTH deltas read BEFORE m_bgRect* is overwritten below -- against the
            // new rect they are identically zero, which is a silent no-op rather than
            // a compile error.
            const float dx = x - m_bgRectX;
            const float dy = y - m_bgRectY;
            emitThemedBackground(*theme, x, y, width, height, m_bgQuadFirst);
            m_bgRectX = x; m_bgRectY = y; m_bgRectW = width; m_bgRectH = height;
            m_bandLeft += dx;     m_bandRight += dx;
            m_bandTop += dy;      m_bandBottom += dy;
            m_wholeCardTop += dy; m_wholeCardBottom += dy;
            m_fillFirst = m_bgQuadFirst;   // re-arm: finalize consumes it (see there)
            finalizeThemedFill();
            return;
        }
    }

    applyTextureAspectCorrection(x, y, width, height);

    // Same bounds check the themed branch above does. This one tested only
    // `> 0` -- reachable if a rebuild skips addBackgroundQuad while a stale index
    // survives, which is exactly the shape the themed branch already guards. Two
    // branches of one function disagreeing about whether the index is trustworthy
    // is how the trustworthy one ends up wrong.
    // A panel whose background was never emitted (zero opacity) has no quad here to
    // move -- and m_bgQuadFirst points at the first CONTENT quad, so rewriting it
    // would stretch a row or an icon across the whole panel on the first drag.
    if (m_bgQuadCount <= 0) {
        m_bgRectX = x; m_bgRectY = y; m_bgRectW = width; m_bgRectH = height;
        return;
    }
    const size_t bgIndex = (m_bgQuadFirst > 0) ? static_cast<size_t>(m_bgQuadFirst) : 0;
    if (bgIndex >= m_quads.size()) return;
    setQuadPositions(m_quads[bgIndex], x, y, width, height);
}

void BaseHud::applyTextureAspectCorrection(float& x, float& y, float& width, float& height) const {
    using namespace PluginConstants;

    if (!m_bShowBackgroundTexture || m_iBackgroundTextureIndex <= 0) return;

    float textureAspect = AssetManager::getInstance().getTextureAspectRatio(m_iBackgroundTextureIndex);
    if (textureAspect <= 0.0f) return;

    // Convert content dimensions to pixel-space aspect ratio
    // In normalized 16:9 coords: pixel_width = w * 16, pixel_height = h * 9
    // So content pixel aspect = (width * 16) / (height * 9) = width * UI_ASPECT_RATIO / height
    float contentAspect = (height > 0.0001f) ? (width * UI_ASPECT_RATIO / height) : textureAspect;

    if (contentAspect < textureAspect) {
        // Content is taller than texture - expand width to match texture aspect
        float newWidth = height * textureAspect / UI_ASPECT_RATIO;
        x -= (newWidth - width) * 0.5f;  // Center horizontally
        width = newWidth;
    } else if (contentAspect > textureAspect) {
        // Content is wider than texture - expand height to match texture aspect
        float newHeight = width * UI_ASPECT_RATIO / textureAspect;
        y -= (newHeight - height) * 0.5f;  // Center vertically
        height = newHeight;
    }
}

BaseHud::ScaledDimensions BaseHud::getScaledDimensions() const {
    MXB_COUNT_CALL(GET_SCALED_DIMENSIONS);
    // THE migration point. Nearly every HUD lays out through this struct rather
    // than reaching for the constants itself, so pointing it at layout() is what
    // makes the whole UI follow a theme's spacing -- without touching the HUDs.
    const LayoutMetrics& L = layout();
    return {
        L.fontSizeNormal * m_fScale,
        L.fontSizeExtraSmall * m_fScale,
        L.fontSizeSmall * m_fScale,
        L.fontSizeLarge * m_fScale,
        L.fontSizeExtraLarge * m_fScale,
        // Theme-aware: a themed HUD's content is pushed in far enough to clear the
        // frame's edge slices AND the edge slice of the title band wrapped around it.
        // Applied HERE rather than at each title/row site so the full rebuild and
        // every HUD's rebuildLayout fast path pick it up identically -- an indent
        // applied only at caption time would be lost the moment the HUD was dragged.
        contentPaddingX(),
        contentPaddingY(),
        L.lineHeightExtraSmall * m_fScale,
        L.lineHeightSmall * m_fScale,
        L.lineHeightLarge * m_fScale,
        L.lineHeightNormal * m_fScale,
        L.lineHeightExtraLarge * m_fScale,
        L.cellW * m_fScale,
        L.cellH * m_fScale,
        m_fScale
    };
}

unsigned long BaseHud::getTextColorWithOpacity(uint8_t r, uint8_t g, uint8_t b) const {
    uint8_t alpha = static_cast<uint8_t>(m_fBackgroundOpacity * 255.0f);
    return PluginUtils::makeColor(r, g, b, alpha);
}

float BaseHud::calculateBackgroundWidth(int charWidth) const {
    auto dim = getScaledDimensions();
    return PluginUtils::calculateMonospaceTextWidth(charWidth, dim.fontSize)
        + dim.paddingH + dim.paddingH;
}

float BaseHud::calculateBackgroundHeight(int rowCount, bool includeTitle) const {
    auto dim = getScaledDimensions();
    // titleRowHeight(), not a bare lineHeightLarge. Both answer "how tall is a title
    // row", and having two answers is how a HUD sized through this helper could tile
    // differently from one that reserved its row directly -- the exact class of bug
    // check_hud_helpers.sh rule 7 exists for. Identical at the shipped metrics (the
    // band is 0.042 against a 0.047 large row, so the max() picks the row either way);
    // the point is that it stays identical when the band grows.
    float titleHeight = (includeTitle && m_bShowTitle)
        ? titleRowHeight(dim.fontSizeLarge, dim.lineHeightLarge) : 0.0f;
    return panelHeight(dim, titleHeight + (rowCount * dim.lineHeightNormal));
}

bool BaseHud::positionString(size_t stringIndex, float x, float y) {
    if (stringIndex >= m_strings.size()) {
        return false;
    }
    applyOffset(x, y);
    m_strings[stringIndex].m_afPos[0] = x;
    // The SAME centring addString applied when this string was built. The reposition
    // fast paths take the string's own size from the entry rather than being told it,
    // so a caller cannot pass one that disagrees with what is on screen.
    m_strings[stringIndex].m_afPos[1] = y + rowCenterOffset(m_strings[stringIndex].m_fSize);
    return true;
}

// ============================================================================
// Styled String Rendering (per-string padding and backgrounds)
// ============================================================================

void BaseHud::addStyledString(const HudStringConfig& config) {
    m_styledStringConfigs.push_back(config);
}

void BaseHud::renderStyledStrings() {
    using namespace PluginConstants;

    for (const auto& config : m_styledStringConfigs) {
        // Use cached text width if available (PERFORMANCE OPTIMIZATION)
        float textWidth = (config.cachedTextWidth > 0.0f)
            ? config.cachedTextWidth
            : PluginUtils::calculateMonospaceTextWidth(static_cast<int>(config.text.length()), config.fontSize);
        float lineHeight = floatEquals(config.fontSize, layoutDefaults().fontSizeLarge * m_fScale)
                          ? layoutDefaults().lineHeightLarge * m_fScale
                          : layoutDefaults().lineHeightNormal * m_fScale;

        // Add background quad if requested
        if (config.hasBackground) {
            float bgX = config.x - config.bgPaddingLeft;
            float bgY = config.y - config.bgPaddingTop;
            float bgWidth = textWidth + config.bgPaddingLeft + config.bgPaddingRight;
            float bgHeight = lineHeight + config.bgPaddingTop + config.bgPaddingBottom;

            SPluginQuad_t quadEntry;
            applyOffset(bgX, bgY);
            setQuadPositions(quadEntry, bgX, bgY, bgWidth, bgHeight);
            quadEntry.m_iSprite = SpriteIndex::SOLID_COLOR;

            // Use the per-string background color and opacity
            uint8_t alpha = static_cast<uint8_t>(config.backgroundOpacity * 255.0f);
            uint8_t r = (config.backgroundColor >> 16) & 0xFF;
            uint8_t g = (config.backgroundColor >> 8) & 0xFF;
            uint8_t b = config.backgroundColor & 0xFF;
            quadEntry.m_ulColor = PluginUtils::makeColor(r, g, b, alpha);

            m_quads.push_back(quadEntry);
        }

        // Add the text string
        addString(config.text.c_str(), config.x, config.y, config.justify,
                 config.fontIndex, config.color, config.fontSize);
    }
}

BaseHud::StyledStringBounds BaseHud::calculateStyledStringBounds() const {
    using namespace PluginConstants;

    if (m_styledStringConfigs.empty()) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }

    float minX = 1e10f;  // Large positive value
    float minY = 1e10f;
    float maxX = -1e10f; // Large negative value
    float maxY = -1e10f;

    for (const auto& config : m_styledStringConfigs) {
        // Use cached text width if available (PERFORMANCE OPTIMIZATION)
        float textWidth = (config.cachedTextWidth > 0.0f)
            ? config.cachedTextWidth
            : PluginUtils::calculateMonospaceTextWidth(static_cast<int>(config.text.length()), config.fontSize);
        float lineHeight = floatEquals(config.fontSize, layoutDefaults().fontSizeLarge * m_fScale)
                          ? layoutDefaults().lineHeightLarge * m_fScale
                          : layoutDefaults().lineHeightNormal * m_fScale;

        // Calculate bounds including layout padding
        float left = config.x - config.paddingLeft;
        float right = config.x + textWidth + config.paddingRight;
        float top = config.y - config.paddingTop;
        float bottom = config.y + lineHeight + config.paddingBottom;

        // Update min/max using ternary operators (avoids Windows macro conflicts)
        minX = (left < minX) ? left : minX;
        maxX = (right > maxX) ? right : maxX;
        minY = (top < minY) ? top : minY;
        maxY = (bottom > maxY) ? bottom : maxY;
    }

    return {minX, minY, maxX, maxY};
}
