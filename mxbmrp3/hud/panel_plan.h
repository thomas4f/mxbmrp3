// ============================================================================
// hud/panel_plan.h
// The panel box-model VALUE TYPES: what a HUD asks for (PanelWant), what the
// engine answers (PanelPlan), the per-HUD scaled metrics both are stated in
// (ScaledDimensions) and the caption tier that picks font and row together.
//
// Plain data at namespace scope, deliberately outside BaseHud: they carry no
// HUD state, and SettingsLayoutContext (a standalone struct, not a HUD) hands a
// ScaledDimensions between the settings tabs. The methods that PRODUCE them --
// planPanel(), getScaledDimensions(), addPlanBackground() -- stay on BaseHud,
// which includes this header.
// ============================================================================
#pragma once
#include <cstddef>
#include <vector>
#include "../core/small_vec.h"
#include "../core/panel_box.h"

// Which type tier a panel captions at. A panel titles at ONE of two sizes -- the
// full HUDs at Large, the widgets at Normal -- and the tier picks BOTH the font and
// the row together, which is the whole reason it is an enum and not two arguments.
enum class TitleTier { Normal, Large };

struct ScaledDimensions {
    float fontSize;
    float fontSizeExtraSmall;
    float fontSizeSmall;
    float fontSizeLarge;
    float fontSizeExtraLarge;
    float paddingH;
    float paddingV;
    float lineHeightExtraSmall;
    float lineHeightSmall;
    float lineHeightLarge;
    float lineHeightNormal;
    float lineHeightExtraLarge;
    // The snap grid at THIS HUD's scale. Every distance a layout file states is
    // in cells, so this is what spends them -- one named conversion instead of
    // each call site remembering whether its value was in lines, cells or
    // characters.
    float cellW;
    float cellH;
    float scale;

    // Grid-aligned spacing, in cells. cellW/cellH are already scaled, so these
    // are just named multiplication -- kept because the call sites read better
    // as "one cell across" than as a bare product.
    float gridV(float units) const { return cellH * units; }
    float gridH(float units) const { return cellW * units; }
};

// ==== THE BOX-MODEL PLAN (BOX-MODEL-PORT) ===============================
// The additive panel geometry, computed ONCE per rebuild by the engine in
// core/panel_box.h, and pinned to it by the golden vectors in
// tests/fixtures/panel_box_parity.json. A migrated HUD builds:
//
//     auto dim = getScaledDimensions();
//     PanelWant want;
//     want.contentW = <widest row, normalized>;
//     want.sectionH = { <section heights, normalized> };
//     want.captionW = <title text width, normalized>;   // caption can win the ask
//     PanelPlan& p = planPanel(dim, want);
//     addPlanBackground(p, x, y);       // frame + band + section cards
//     addPlanTitle(p, "Name", font, color);
//     // rows at p.contentX(), p.contentY(section) + k * rowH
//
// EVERYTHING IS ON THE MODEL EXCEPT FOUR DELIBERATE HOLDOUTS, each annotated
// at its own box: the corner buttons (Director/SettingsButton — themed BUTTON
// slices, not panels); the dial gauges (Speedo/Tacho — the box IS the dial
// art); the shape-driven panels (Map/Radar/Pitboard/Gamepad — track shape,
// radar circle, board and pad art drive the geometry); and the settings
// panel's OUTER box (screen-ceiling constrained and content-anchored in X,
// see the BOX-MODEL NOTE in settings_hud_render.cpp — every term it spends
// still resolves from the box-model surface). The legacy chain below survives
// solely as those four's vocabulary; anything else reaching for it is a HUD
// that has not been migrated.
//
// Every derivation the old chain needed (titleRowHeight, panelContentY,
// contentCardTop, the reserve/rewrite section dance, the caption row's three
// branches) collapses: geometry is fully known before the first quad, and
// every box's position is the engine's, not re-derived at the emit site.
//
// The plan is in CELLS (x-cells across, y-cells down); X()/Y() convert to
// normalized units at this HUD's scale. BaseHud's contentPaddingX/Y() and the helpers
// above remain for unmigrated panels; a HUD uses one path or the other,
// never both in one rebuild.
struct PanelWant {
    float contentW = 0.0f;             // widest section row, normalized units
    // INLINE up to 8 sections (see small_vec.h): this struct is built and
    // destroyed once per HUD per rebuild, and a std::vector here costs one
    // heap round-trip -- 1.57us in the game process -- per rebuild.
    SmallVec<float, 8> sectionH;       // per-section content height, normalized
    float captionW = 0.0f;             // caption text width (0 = never wins the ask)
    TitleTier tier = TitleTier::Normal;
    int buttons = 0;                   // footer button count
    float buttonW = 0.0f;              // per-button content width, normalized
    float buttonH = 0.0f;              // button row content height, normalized
    float minPanelW = 0.0f;            // minimum panel width, normalized (0 = none)
    // THE CONTENT IS A SLAB, NOT ROWS -- so the panel's own padding becomes part
    // of it instead of a margin around it: full-bleed to the sides and the
    // bottom, and to the top too when no title is shown. A shown title keeps
    // the top padding as its own air (the caption is rows, not slab, and flush
    // against the panel's top edge it reads as a defect). UNTHEMED ONLY: with a
    // theme the frame art needs that ring, and
    // the padding is the theme's to spend.
    //
    // The panel does NOT change size. The engine moves the padding into the
    // content band rather than dropping it, so the outer rect is identical with
    // the flag on or off -- which is the whole reason this is a flag the engine
    // honours and not two edits in the caller that could drift apart.
    //
    // Why only some panels: the Gap Bar's coloured fill and the Notices slab ARE
    // the panel; a cell of air around them reads as a border nobody asked for.
    // A panel of text rows wants that air, which is why this is opt-in.
    //
    // Applies to the sectionH path only -- the vertical share lands on the LAST
    // section, the one that already absorbs the panel's ceil remainder. A `bands`
    // caller is left alone (no current one is a slab).
    bool contentFillsPanel = false;
    // THE BODY AS COLUMNS, for a panel whose body is a horizontal split. Same
    // shape as PanelBox::BandAsk, in NORMALIZED units like every field above --
    // the engine carries columns (panel_box.h names the settings panel's sidebar
    // as the reason), and this is how a caller reaches them.
    //
    // Set `bands` OR contentW/sectionH, never both: bands wins, exactly as
    // PanelBox::Spec resolves the same pair.
    struct ColumnWant {
        float contentW = 0.0f;         // this column's content width, normalized
        // Plain vector, unlike PanelWant::sectionH above: only the settings
        // panel states columns, it rebuilds only while open, and its lists are
        // long enough that inline storage would spill anyway.
        std::vector<float> sectionH;   // per-section content height, normalized
    };
    struct BandWant { std::vector<ColumnWant> columns; };
    std::vector<BandWant> bands;
    // A floor under the body, normalized (0 = none) -- see Spec::minBodyH.
    float minBodyH = 0.0f;
};
struct PanelPlan {
    PanelBox::Geom g;                  // the engine's geometry, in cells
    float cellW = 0.0f, cellH = 0.0f;  // normalized units per cell (scaled)
    float x0 = 0.0f, y0 = 0.0f;        // panel origin, PRE-offset normalized
    float capFontSize = 0.0f;          // the tier's caption size, normalized
    float X(double cells) const { return x0 + static_cast<float>(cells) * cellW; }
    float Y(double cells) const { return y0 + static_cast<float>(cells) * cellH; }
    float W(double cells) const { return static_cast<float>(cells) * cellW; }
    float H(double cells) const { return static_cast<float>(cells) * cellH; }
    float width() const { return W(g.panelCols); }
    float height() const { return H(g.panelH); }
    // A section's content origin — where its first row starts, both axes.
    float contentX() const { return X(g.rowsX); }
    float contentY(size_t section = 0) const {
        return Y(g.sections[section < g.sections.size() ? section : 0].rowsTop);
    }
    float contentW() const { return W(g.cols); }
    // The content box's RIGHT edge -- where a right-aligned value ends. NOT the
    // LEFT inset mirrored onto the right edge (`panelLeft + width - (contentX -
    // panelLeft)`), which is the same number only while [content] border and
    // padding are horizontally symmetric. Write `border = 2 0 4 6` and the mirror
    // pulls right-aligned values a whole left border inward, into the labels
    // beside them.
    float contentRight() const { return contentX() + contentW(); }
    // A section's DRAWN BOX -- the card as the player sees it, border included,
    // which is NOT the content band above when [content] border is asymmetric:
    // the band is inset by border.t at the top and border.b at the bottom, so the
    // two share a centre only while those are equal.
    //
    // Centre a single big value in THIS, not in the content band. Every shipped
    // theme has a symmetric card border, so the two agree and this changes
    // nothing; write `border = 2 0 4 6` and the value drawn from the band sits a
    // cell above the middle of the card it is drawn on.
    //
    // Rows still start at contentY(): a LIST belongs inside the border, and only
    // a lone value centred in its card has a reason to ask where the card is.
    float sectionBoxY(size_t section = 0) const {
        return Y(g.sections[section < g.sections.size() ? section : 0].top);
    }
    float sectionBoxH(size_t section = 0) const {
        const PanelBox::SectionGeom& s =
            g.sections[section < g.sections.size() ? section : 0];
        return H(s.bot - s.top);
    }
    // The horizontal half of the same box: the card's drawn extent along X
    // (g.cardLeft/cardW -- one column, shared by every section). CENTRED
    // content anchors HERE, never at the panel's centre: the two are the same
    // number only while the [content] terms are left/right symmetric, and a
    // skinner's `margin = 4 6 8 0` moves the panel's centre outside the card
    // -- a big value, gauge or chip centred on `startX + backgroundWidth / 2`
    // slides off its own card while the card stays put.
    // ALIGNED content keeps contentX()/contentRight(): a column respects the
    // card's border and padding; only centring answers to the drawn box.
    float sectionBoxX() const { return X(g.cardLeft); }
    float sectionBoxW() const { return W(g.cardW); }
    float sectionBoxCenterX() const { return sectionBoxX() + sectionBoxW() / 2.0f; }
    // A COLUMN of a split body, by band and column index. The one-column
    // accessors above are `col(0, 0)` with the leftover-width rule applied,
    // so a caller that has a split reads its columns the same way.
    const PanelBox::ColumnGeom& col(size_t band, size_t column) const {
        static const PanelBox::ColumnGeom kNone;
        if (band >= g.bands.size()) return kNone;
        const PanelBox::BandGeom& b = g.bands[band];
        return column < b.columns.size() ? b.columns[column] : kNone;
    }
    // A column section's content origin and width -- the engine's own row box
    // for that column, which for one column IS g.rowsX / g.cols.
    float colContentX(const PanelBox::ColumnGeom& c) const { return X(c.rowsLeft); }
    float colContentW(const PanelBox::ColumnGeom& c) const { return W(c.rowsW); }
    float colContentY(const PanelBox::ColumnGeom& c, size_t section = 0) const {
        if (c.sections.empty()) return Y(g.panelInner);
        return Y(c.sections[section < c.sections.size() ? section : 0].rowsTop);
    }

    // THE BAND A ROW HIGHLIGHT SPANS: the ROWS box, the same box the text sits
    // in. At the shipped default ([content] padding 0) that IS the card's
    // interior, so a band reads flush to the card; where a theme asks for
    // padding, the padding is air around the rows and the highlight is one of
    // the things it is air around.
    //
    // NOT THE CARD'S INTERIOR: that makes the band absorb [content] padding, i.e.
    // the highlight grows outside the content of the card, way beyond where the
    // text is. standings_row_band_test pins it: the card-to-band clearance GROWS
    // with the padding, which only the rows box does.
    //
    // ONE OWNER for every emitter -- StandingsHud, RecordsHud, the settings
    // SIDEBAR and the settings panel's content rows -- so one panel cannot
    // highlight two ways at once, a column apart.
    float rowBandX(const PanelBox::ColumnGeom& c) const { return X(c.rowsLeft); }
    float rowBandW(const PanelBox::ColumnGeom& c) const { return W(c.rowsW); }
    // The one-column form, for a panel with no split body.
    float rowBandX() const { return X(g.rowsX); }
    float rowBandW() const { return W(g.cols); }
};

// The [button] box terms at a HUD's scale, for a panel laying out its own
// NON-UNIFORM button row: see BaseHud::planButtonTerms().
struct PlanButtonTerms { float insetL, insetR, insetT, insetB, gap, marginT, marginB; };
