// ============================================================================
// hud/base_hud_plan.cpp
// BaseHud's box-model plan (BOX-MODEL-PORT): resolving the theme's terms into
// a PanelBox::Spec and emitting the panel background/title at the plan's
// numbers. Split from base_hud_render.cpp; every method body is unchanged.
// ============================================================================
#include "base_hud.h"
#include "../diagnostics/call_counters.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/ui_config.h"
#include "../core/asset_manager.h"
#include "../handlers/draw_handler.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
