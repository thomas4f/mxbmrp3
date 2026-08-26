// ============================================================================
// core/panel_box.h — the panel box model: boxes in, geometry out.
//
// A HUD panel is a stack of CSS-style boxes: a [panel] around a [title] band,
// one [content] card PER SECTION, and a footer row of [button]s. Each box has
// margin (air outside, nothing drawn in it), border (the nine-slice edge art's
// thickness) and padding (air inside), each specifiable per side in CSS
// shorthand. This header computes where every box and every content origin
// lands, from those terms alone.
//
// THE RULES. Two of them were wrong the first time and were caught by reading
// a drawn-and-measured model's own output rather than by reasoning about code:
// a [title] margin that shrank its own band instead of growing the panel (one
// number behaving oppositely per axis), and a box's horizontal term going quiet
// whenever an unrelated box happened to be wider. Both are why the terms below
// say which direction they grow:
//
//   * ONE SPATIAL UNIT: the grid cell (core/layout_metrics.h). A box term is a
//     count of x-cells on every side; top/bottom are converted by `unit`
//     (= cellW*aspect/cellH) so one number is square ON SCREEN — the same
//     conversion a theme's slice size already makes.
//   * A MARGIN GROWS THE PANEL, never shrinks its own box: the panel's width
//     is shrink-to-fit over every child's ask (content + its own m+b+p per
//     side), and the widest ask wins. A margin is therefore a MINIMUM.
//   * MARGINS DO NOT COLLAPSE: the seam between two siblings is the SUM of
//     the facing margins. Two intentions about one gap stay two numbers.
//   * EVERY BOX OWNS ITS OWN COLUMN: its content starts at its own
//     margin+border+padding. Whether the caption's column lines up with the
//     rows' is REPORTED (columnSplit), never forced — forcing a shared column
//     made a term go quiet whenever an unrelated box was wider.
//   * THE PANEL'S HEIGHT IS CEILED to a whole cell (panels tile only if their
//     heights differ by whole rows); the remainder is reported as slackY and
//     absorbed by the LAST SECTION's own box (content box = drawn card, one
//     rectangle); a button row rides down with it, so its junction and the
//     bottom chrome stay term-exact. Never absorbed silently.
//   * THE CAPTION BLOCK'S ADVANCE IS CEILED TOO (remainder = titleSlack): the
//     caption glyph box is the one fractional term that differs between a
//     titled and an untitled panel, so without this the two ceil to different
//     slack and siblings stop looking uniform. The remainder stretches the
//     DRAWN band bottom (bandDrawnBot) exactly as slackY stretches the last
//     card — never bare air between band and card.
//   * UNTHEMED IS NOT A SPECIAL CASE: no art means every border is zero and
//     only the air terms remain — a CSS box with `border: 0`.
//   * GAP IS JUNCTION-ONLY AIR (CSS flex/grid gap): spent between stacked
//     children, never at the frame edges. Margins alone cannot express "a
//     seam without edge air" — the seam is a sum of facing margins, and
//     zeroing the edge zeroes half of every seam.
//
// PARITY: tools/panel_box_model.js holds the same computation in JS
// (layoutPanel), and both sides are asserted against
// tests/fixtures/panel_box_parity.json — panel_box_test.cpp for this header,
// and box_terms_test.cpp for its terms. Change either side
// and one suite goes red until the fixture is regenerated
// (tools/gen_panel_box_fixture.js) and both agree again.
//
// DOUBLE, not float, deliberately: the JS side computes in doubles, and the
// ceil at panelH sits behind a 1e-9 epsilon — float32 rounding could flip it
// on exact-cell boundaries. Callers convert to float at the render edge.
//
// Pure and header-only: no I/O, no singletons, no game types, so it compiles
// straight into the unit suite.
// ============================================================================
#pragma once

#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace PanelBox {

// Raw per-side values, in x-cells (unconverted — what the INI states).
struct Sides {
    double t = 0.0, r = 0.0, b = 0.0, l = 0.0;
};

// CSS shorthand, cells: "2" all sides · "2 4" vertical horizontal ·
// "1 2 3" top horizontal bottom · "1 2 3 4" top right bottom left.
// Whitespace or commas separate; a token that is not entirely a finite number
// is skipped (mirrors JS Number() + isFinite filtering).
inline Sides parseSides(const std::string& str) {
    std::vector<double> n;
    size_t i = 0;
    while (i < str.size()) {
        while (i < str.size() && (std::isspace(static_cast<unsigned char>(str[i])) || str[i] == ','))
            ++i;
        if (i >= str.size()) break;
        size_t j = i;
        while (j < str.size() && !std::isspace(static_cast<unsigned char>(str[j])) && str[j] != ',')
            ++j;
        const std::string tok = str.substr(i, j - i);
        char* end = nullptr;
        const double v = std::strtod(tok.c_str(), &end);
        // Full-token consumption required: strtod("2px") stops at 'p' where JS
        // Number("2px") is NaN — a partial parse must not half-accept a typo.
        if (end == tok.c_str() + tok.size() && std::isfinite(v)) n.push_back(v);
        i = j;
    }
    if (n.empty()) return {0.0, 0.0, 0.0, 0.0};
    if (n.size() == 1) return {n[0], n[0], n[0], n[0]};
    if (n.size() == 2) return {n[0], n[1], n[0], n[1]};
    if (n.size() == 3) return {n[0], n[1], n[2], n[1]};
    return {n[0], n[1], n[2], n[3]};
}

// The WRITE half of the shorthand: the most compact spelling that parses back
// to the same sides ("2", "2 4", "1 2 3 4"). Used by the settings writer for
// the [Advanced] air-term built-ins; round-trip pinned by panel_box_test.
inline std::string formatSides(const Sides& s) {
    const auto num = [](double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return std::string(buf);
    };
    if (s.t == s.r && s.r == s.b && s.b == s.l) return num(s.t);
    if (s.t == s.b && s.l == s.r) return num(s.t) + " " + num(s.l);
    return num(s.t) + " " + num(s.r) + " " + num(s.b) + " " + num(s.l);
}

// A box's three terms, raw (x-cells all sides).
struct BoxTerms {
    Sides margin, border, padding;
};

// A term CONVERTED for layout: t/b multiplied by `unit` (y-cells), l/r kept in
// x-cells, with the raw values retained (the readout and squareness checks
// compare raw, since converted t==l is exactly what square-on-screen breaks).
struct SideVals {
    double t = 0.0, r = 0.0, b = 0.0, l = 0.0;
    Sides raw;
};
struct BoxVals {
    SideVals m, b, p;
};

// One column of a band: its own content width and its own stack of sections.
struct ColumnAsk {
    double cols = 0.0;             // content width, x-cells
    std::vector<double> sections;  // content height per section, y-cells
};
// A horizontal group of columns. One column == a full-width section stack.
struct BandAsk {
    std::vector<ColumnAsk> columns;
};

struct Spec {
    // y-cells per x-cell distance: LayoutMetrics cellW*aspect/cellH.
    double unit = 1.0;
    BoxTerms panel;      // margin ignored — nothing outside a panel can see it
    BoxTerms title, content, button;
    bool themed = false; // false → every border reads as zero (border: 0)
    bool band = false;   // draw a themed band behind the caption
    bool card = false;   // draw the content card(s)
    bool hasCaption = false;
    double captionW = 0.0;   // caption content width, x-cells
    double captionH = 0.0;   // caption content height, y-cells
    // THE BODY, one-column shorthand: content height per section (y-cells) and
    // the width they share (x-cells). Every panel in the tree states its body
    // this way; layoutPanel normalises it into a single one-column band.
    std::vector<double> sections;
    double cols = 0.0;
    // THE BODY, general form. A band is a horizontal group of COLUMNS, each with
    // its own width ask and its own vertical stack of sections; the band is as
    // tall as its tallest column. One column is a full-width section stack --
    // which is what `sections`/`cols` above build, and why they can stay.
    //
    // NO RECURSION: a split holds sections, not further splits. Two cases want
    // this and both are covered without it -- the settings panel's sidebar
    // beside its content, and two narrow sections side by side inside one tab.
    //
    // Set `bands` OR `sections`/`cols`, never both: bands wins and the shorthand
    // is ignored.
    std::vector<BandAsk> bands;
    // A FLOOR UNDER THE BODY, y-cells (0 = none). The bands stack as usual; if
    // they total less than this, the remainder is left BELOW them, inside the
    // panel and outside every child's box -- so nothing is stretched to fill it.
    //
    // It exists for a panel whose height must not depend on which of several
    // interchangeable bodies is showing: the settings menu sizes itself to its
    // TALLEST tab so Save and Close do not move when you flick tabs, while the
    // cards still wrap the tab actually drawn. Inflating the last section instead
    // would size the panel right and draw one visibly stretched card.
    double minBodyH = 0.0;
    int buttons = 0;               // footer button count (0 = no row)
    double buttonW = 0.0;          // per-button content width, x-cells
    double buttonH = 0.0;          // button row content height, y-cells
    // Minimum PANEL width, x-cells (0 = none). For panels sharing a column
    // (the centre stack); widens the content box — extra air lands inside the
    // panel, outside every child's own box — and reports WidthSetBy MIN.
    double minPanelW = 0.0;
    // Air at each JUNCTION between stacked children (band→card, card→card,
    // card→buttons), x-cells (converted by unit). The frame edges get none —
    // this is the term margins cannot express: a seam without edge air. A
    // junction's total seam is gap + the facing margins.
    double gap = 0.0;
};

struct SectionGeom {
    double top = 0.0, bot = 0.0, rowsTop = 0.0, h = 0.0;
};
// A column's own box within its band, and the sections stacked inside it.
struct ColumnGeom {
    double left = 0.0, w = 0.0;
    // The CARD box inside it: the column inset by the content margins, which is
    // what g.cardLeft/cardW are for a one-column body. Stated per column so a
    // painter walking a split reads one expression rather than re-deriving the
    // inset from terms it would have to be handed separately.
    double cardLeft = 0.0, cardW = 0.0;
    // ...and the ROW box inside that: the card inset by its border and padding,
    // which is g.rowsX/g.cols for a one-column body. Same reason as above -- the
    // terms live here, so no painter has to be handed them to re-derive it.
    double rowsLeft = 0.0, rowsW = 0.0;
    std::vector<SectionGeom> sections;
};
struct BandGeom {
    double top = 0.0, bot = 0.0;
    std::vector<ColumnGeom> columns;
};
struct ButtonGeom {
    double x = 0.0, w = 0.0, labelX = 0.0;
};

struct Geom {
    BoxVals P, T, C, B;      // T/C/B already collapsed to zero when absent
    bool hasTitle = false, hasCard = false;
    int nBtn = 0;
    // Which ask set the width — or MIN when Spec::minPanelW decided.
    enum WidthSetBy { ROWS = 0, CAPTION = 1, BUTTONS = 2, MIN = 3 };
    WidthSetBy widthSetBy = ROWS;
    double columnSplit = 0.0;
    double artBot = 0.0, panelInner = 0.0, panelInnerLeft = 0.0, innerW = 0.0;
    bool hasCaptionRow = false;    // JS: titleTop !== null
    double titleTop = 0.0, titleH = 0.0, titleBot = 0.0;
    // The remainder spent to make the caption block's advance a whole cell
    // (see the quantization note in layoutPanel), and the band's drawn bottom
    // absorbing it — titleBot + titleSlack when a band is drawn, the same
    // owner-absorbs-its-remainder rule the last section follows for slackY.
    // Painters read bandDrawnBot; captionY and every term stay glyph-exact.
    // Both 0/unset without a caption.
    double titleSlack = 0.0;
    double bandDrawnBot = 0.0;
    // THE BODY, general and flattened. `bands` is every column of every band;
    // `sections` is the first column of each, top to bottom — the view every
    // reader in the tree walks, and the whole body while nothing states a split.
    std::vector<BandGeom> bands;
    std::vector<SectionGeom> sections;
    double seam = 0.0;
    bool hasButtonRow = false;     // JS: btnTop !== null
    double btnTop = 0.0, btnH = 0.0, btnW = 0.0;
    std::vector<ButtonGeom> btns;
    double titleLeft = 0.0, titleW = 0.0, cardLeft = 0.0, cardW = 0.0;
    double panelH = 0.0, slackY = 0.0, panelCols = 0.0, cols = 0.0;
    // (lastCardBot lived here: the last card's drawn bottom, sections.back().bot
    // + slackY, read by painters while content math kept the unstretched bot.
    // Two bottoms for one box meant every full-bleed filler had to know which
    // one it wanted — Notices' slab and the Gap Bar's bar measured the short
    // one and stopped a strip above the drawn card. Now the last SECTION's own
    // box absorbs the remainder, so there is only one bottom again.)
    double captionX = 0.0, rowsX = 0.0, buttonX = 0.0;
    double rowsTop = 0.0, contentH = 0.0, cardTop = 0.0, cardBot = 0.0;
    double captionY = 0.0;
    bool squareBorders = false;
    // overBotY was here too, and it was DEAD by construction — panelH is
    // ceil(artBot - 1e-9), so artBot can never exceed it; a golden-pinned flag
    // that can't fire is worse than none.
    bool overTopY = false, overX = false;
};

inline Geom layoutPanel(const Spec& spec) {
    const double unit = spec.unit;
    const auto sides = [unit](const Sides& raw) {
        SideVals s;
        s.t = raw.t * unit; s.b = raw.b * unit; s.l = raw.l; s.r = raw.r;
        s.raw = raw;
        return s;
    };
    const auto box = [&sides](const BoxTerms& bx) {
        BoxVals v;
        v.m = sides(bx.margin); v.b = sides(bx.border); v.p = sides(bx.padding);
        return v;
    };
    const Sides Z{};
    BoxVals zbox;
    zbox.m = sides(Z); zbox.b = sides(Z); zbox.p = sides(Z);

    BoxVals P = box(spec.panel), T = box(spec.title), C = box(spec.content), B = box(spec.button);
    if (!spec.themed) { P.b = sides(Z); T.b = sides(Z); C.b = sides(Z); B.b = sides(Z); }

    const bool hasCaption = spec.hasCaption;
    // hasTitle / hasCard are about DRAWING: a band or a card needs a theme with
    // art to draw it with. They are NOT about whether the box exists.
    const bool hasTitle = spec.themed && spec.band && hasCaption;
    const bool hasCard = spec.themed && spec.card;
    const int nBtn = spec.buttons;
    // A BOX EXISTS WHENEVER ITS CONTENT DOES — but its BORDER exists only when
    // the art that fills it is actually drawn. Margin and padding are pure
    // spacing and owe a theme nothing; a border is the width of a slice, and
    // reserving one nobody paints is an empty inset the user cannot explain.
    //
    // These used to collapse the WHOLE box on `hasTitle` / `hasCard`, which
    // conflated the two — so on an unthemed panel titleMargin, titlePadding,
    // contentMargin and contentPadding did nothing at all, while panelPadding
    // (never collapsed) worked. Four of the box model's eight terms were dead
    // for every user who has no theme selected, which is the default.
    //
    // Un-collapsing them then went one step too far in the other direction:
    // `themed` zeroes the borders, but `[card] hud-content = 0` (or a theme
    // with frame art and no card art) leaves a THEMED panel whose card is not
    // drawn, and its border was still being spent. Both switches now say the
    // same thing to the border that they say to the painter.
    BoxVals t = hasCaption ? T : zbox;
    BoxVals c = C;
    BoxVals b = (nBtn > 0) ? B : zbox;
    if (!hasTitle) t.b = sides(Z);
    if (!hasCard) c.b = sides(Z);
    // THE BODY, normalised. `bands` is the general form; `sections`/`cols` is the
    // one-column shorthand every panel in the tree still states its body in, and
    // it builds exactly one band of one column — so a single-column panel's
    // numbers are untouched by the existence of the general path.
    //
    // At least one section, so the outputs derived from the first/last exist —
    // a panel with no content still has a content box of zero height.
    std::vector<BandAsk> bands = spec.bands;
    if (bands.empty()) {
        ColumnAsk only;
        only.cols = spec.cols;
        only.sections = spec.sections;
        bands.push_back(BandAsk{ { only } });
    }
    for (BandAsk& bandAsk : bands) {
        if (bandAsk.columns.empty()) bandAsk.columns.push_back(ColumnAsk{});
        for (ColumnAsk& col : bandAsk.columns)
            if (col.sections.empty()) col.sections.push_back(0.0);
    }

    Geom g;
    g.P = P; g.T = t; g.C = c; g.B = b;
    g.hasTitle = hasTitle; g.hasCard = hasCard; g.nBtn = nBtn;

    // ---- horizontal: shrink-to-fit over every child, each with its OWN column
    const auto insetL = [](const BoxVals& x) { return x.m.l + x.b.l + x.p.l; };
    const auto insetR = [](const BoxVals& x) { return x.m.r + x.b.r + x.p.r; };
    const double panelInnerLeft = P.b.l + P.p.l;
    const double btnW = (nBtn > 0) ? spec.buttonW + b.b.l + b.p.l + b.p.r + b.b.r : 0.0;
    // The seam between two buttons: their facing margins PLUS the junction gap.
    // `gap` is documented as the air at each junction between a panel's stacked
    // children, and it names the buttons among them — a button row is a stack
    // laid sideways. Without this, zeroing buttonMargin makes adjacent labels
    // touch, which is what the settings footer did the moment the shipped
    // margins went to 0 and the gap carried the seams instead.
    const double gapX = spec.gap;
    const double btnSeam = b.m.r + b.m.l + gapX;
    const double btnRowW = (nBtn > 0) ? nBtn * btnW + (nBtn - 1) * btnSeam : 0.0;
    // A BAND'S ASK is its columns' own asks plus the seams between them; the
    // ROWS ask is the widest band. One column collapses to today's expression.
    const double colSeam = c.m.r + c.m.l + gapX;
    double rowsAsk = 0.0;
    for (const BandAsk& bandAsk : bands) {
        double w = 0.0;
        for (const ColumnAsk& col : bandAsk.columns) w += insetL(c) + col.cols + insetR(c);
        w += static_cast<double>(bandAsk.columns.size() - 1) * colSeam;
        if (w > rowsAsk) rowsAsk = w;
    }
    struct Ask { Geom::WidthSetBy k; double w; };
    std::vector<Ask> asks{{Geom::ROWS, rowsAsk}};
    if (hasCaption) asks.push_back({Geom::CAPTION, insetL(t) + spec.captionW + insetR(t)});
    if (nBtn > 0) asks.push_back({Geom::BUTTONS, insetL(b) + btnRowW + insetR(b)});
    Ask winner = asks[0];
    for (const Ask& a : asks)
        if (a.w > winner.w + 1e-9) winner = a;
    double innerW = winner.w;
    g.widthSetBy = winner.k;
    if (spec.minPanelW > 0.0) {
        const double chrome = panelInnerLeft + P.p.r + P.b.r;
        if (spec.minPanelW - chrome > innerW + 1e-9) {
            innerW = spec.minPanelW - chrome;
            g.widthSetBy = Geom::MIN;
        }
    }
    g.innerW = innerW;
    g.panelInnerLeft = panelInnerLeft;
    g.captionX = panelInnerLeft + insetL(t);
    g.rowsX = panelInnerLeft + insetL(c);
    g.buttonX = panelInnerLeft + insetL(b);
    g.panelCols = panelInnerLeft + innerW + P.p.r + P.b.r;
    g.columnSplit = hasCaption ? std::fabs(g.captionX - g.rowsX) : 0.0;

    // ---- vertical, outside in ----------------------------------------------
    const double gapY = spec.gap * unit;
    g.panelInner = P.b.t + P.p.t;
    double y = g.panelInner;
    if (hasCaption) {
        g.hasCaptionRow = true;
        g.titleTop = y + t.m.t;
        g.titleH = t.b.t + t.p.t + spec.captionH + t.p.b + t.b.b;
        g.titleBot = g.titleTop + g.titleH;
        // The caption block's ADVANCE is quantized up to a whole cell (same
        // epsilon as the panelH ceil). The caption glyph box is fractional
        // (capH ≈ 1.7 cells), making it the one DIFFERENTIAL fractional term
        // between a titled and an untitled panel — without this, the two ceil
        // to different slackY, so the last card draws a few px taller on the
        // untitled sibling and every row sits at a different sub-cell phase.
        // The remainder stretches the DRAWN band bottom (bare air here reads
        // as a mystery gap); caption and terms stay glyph-exact, and siblings
        // below the block move in whole-cell steps. The band→card junction
        // gap is folded INTO the quantized advance: it exists only when the
        // caption does, so left outside it would be a second differential
        // fractional term and reintroduce the titled/untitled slack split.
        const double adv = t.m.t + g.titleH + t.m.b + gapY;
        g.titleSlack = std::ceil(adv - 1e-9) - adv;
        g.bandDrawnBot = g.titleBot + (hasTitle ? g.titleSlack : 0.0);
        y += adv + g.titleSlack;
    }
    // ONE BOX PER SECTION, stacked down its column; the seam between two is the
    // sum of their margins plus the junction gap. A BAND is as tall as its
    // tallest column, and the next band starts below the whole of it.
    //
    // THE LAST COLUMN ABSORBS THE LEFTOVER WIDTH, which is what makes the
    // one-column case identical to the plain stack it replaced: that column
    // spans the panel's whole inner width, exactly as a full-width section did.
    // For a real split it is also the one you want -- a sidebar keeps its stated
    // ask and the content column grows with the panel.
    bool firstBand = true;
    for (const BandAsk& bandAsk : bands) {
        if (!firstBand) y += gapY;
        firstBand = false;
        BandGeom band;
        band.top = y;
        const int nCol = static_cast<int>(bandAsk.columns.size());
        double colLeft = panelInnerLeft;
        double bandBot = y;
        for (int ci = 0; ci < nCol; ++ci) {
            const ColumnAsk& colAsk = bandAsk.columns[ci];
            ColumnGeom col;
            col.left = colLeft;
            col.w = (ci == nCol - 1)
                  ? (panelInnerLeft + innerW) - colLeft
                  : insetL(c) + colAsk.cols + insetR(c);
            col.cardLeft = col.left + c.m.l;
            col.cardW = col.w - c.m.l - c.m.r;
            col.rowsLeft = col.left + insetL(c);
            col.rowsW = col.w - insetL(c) - insetR(c);
            double cy = y;
            bool firstSec = true;
            for (const double h : colAsk.sections) {
                if (!firstSec) cy += gapY;
                firstSec = false;
                SectionGeom sec;
                sec.top = cy + c.m.t;
                sec.rowsTop = sec.top + c.b.t + c.p.t;
                sec.h = h;
                sec.bot = sec.rowsTop + h + c.p.b + c.b.b;
                col.sections.push_back(sec);
                cy = sec.bot + c.m.b;
            }
            if (cy > bandBot) bandBot = cy;
            colLeft += col.w + colSeam;
            band.columns.push_back(std::move(col));
        }
        band.bot = bandBot;
        g.bands.push_back(std::move(band));
        y = bandBot;
    }
    // The floor, spent once under the whole body -- after the bands, before the
    // button row picks up from y. A band's own geometry is untouched, which is
    // the point: the extra is air the panel carries, not height any card grew by.
    if (spec.minBodyH > 0.0) {
        const double bodyTop = g.bands.empty() ? y : g.bands.front().top;
        const double shortfall = spec.minBodyH - (y - bodyTop);
        if (shortfall > 0.0) y += shortfall;
    }
    // THE FLATTENED VIEW, first column of every band, top to bottom — what every
    // reader in the tree walks today, and identical to the old list while every
    // panel states a one-column body. A split's further columns are in g.bands.
    for (const BandGeom& band : g.bands)
        for (const SectionGeom& sec : band.columns.front().sections)
            g.sections.push_back(sec);
    g.seam = (g.sections.size() > 1) ? (c.m.b + c.m.t + gapY) : 0.0;
    // The button row, laid along X. Its own margin above and below, like any sibling.
    if (nBtn > 0) {
        g.hasButtonRow = true;
        y += gapY;
        g.btnTop = y + b.m.t;
        g.btnH = b.b.t + b.p.t + spec.buttonH + b.p.b + b.b.b;
        g.btnW = btnW;
        double bx = g.buttonX - b.b.l - b.p.l;
        for (int k = 0; k < nBtn; ++k) {
            g.btns.push_back({bx, btnW, bx + b.b.l + b.p.l});
            bx += btnW + btnSeam;
        }
        y = g.btnTop + g.btnH + b.m.b;
    }
    g.artBot = y + P.p.b + P.b.b;
    g.panelH = std::ceil(g.artBot - 1e-9);
    g.slackY = g.panelH - g.artBot;
    // THE CEIL REMAINDER GROWS THE LAST SECTION'S OWN BOX, buttons or not —
    // content box and drawn card stay one rectangle (bare fill below the last
    // card reads as stray pixels; a shorter content box makes every full-bleed
    // filler stop a strip above the card, the Notices-slab bug). With a button
    // row the row RIDES DOWN by the same amount, so the card→buttons junction
    // stays TERM-EXACT (gap + margins) and the bottom chrome stays exact.
    // Both other placements were tried and reported as bugs: bare below the
    // row (small-and-variable air under the Records footer), and joining the
    // junction (the same variable air, now above the button — 'space around
    // the button doesn't look right'). A section absorbing the remainder is
    // the one placement that reads as the body ending, which is why the
    // buttonless case always did it.
    //
    // THE COLUMN THAT SET THE BAND'S BOTTOM absorbs it — not columns.front()
    // blindly. The remainder exists between the band's bottom and the ceiled
    // panel edge, and only the tallest column's card reaches that bottom; give
    // it to a shorter column and the clearance below the REAL lowest card
    // carries the remainder instead of the bottom chrome. Invisible while
    // every fractional term summed to whole cells (the shipped metrics did,
    // for the settings panel's card sizes — a ratio coincidence), and measured
    // as the settings panel's bottom clearance varying by thirds of a cell
    // with [card] size the moment uiLineHeight moved. One-column panels are
    // untouched: front IS the column that set the bottom. Ties keep front.
    BandGeom& lastBand = g.bands.back();
    size_t tallCol = 0;
    double tallBot = lastBand.columns.front().sections.back().bot;
    for (size_t ci = 1; ci < lastBand.columns.size(); ++ci) {
        // NOT `b`: the button box is a local of that name in this function, and
        // MSVC's C4456 (shadowing) is an error under /WX.
        const double colBot = lastBand.columns[ci].sections.back().bot;
        if (colBot > tallBot + 1e-9) { tallBot = colBot; tallCol = ci; }
    }
    SectionGeom& lastInBand = lastBand.columns[tallCol].sections.back();
    lastInBand.h += g.slackY;
    lastInBand.bot += g.slackY;
    // The flattened view holds COPIES of the FIRST column's sections, so it
    // grows only when the first column is the absorber. (The JS mirror's
    // flattened list holds references, so it needs no copy step — the parity
    // fixture is what catches the two diverging.)
    if (tallCol == 0) {
        g.sections.back().h += g.slackY;
        g.sections.back().bot += g.slackY;
    }
    if (nBtn > 0) g.btnTop += g.slackY;

    const auto sq = [](const SideVals& bd) {
        return std::fabs(bd.raw.t - bd.raw.r) < 1e-9 && std::fabs(bd.raw.r - bd.raw.b) < 1e-9
            && std::fabs(bd.raw.b - bd.raw.l) < 1e-9;
    };
    // A block child SPANS its parent's content box, inset by its own margins.
    const auto lOf = [&](const BoxVals& x) { return panelInnerLeft + x.m.l; };
    const auto wOf = [&](const BoxVals& x) { return innerW - x.m.l - x.m.r; };
    g.titleLeft = lOf(t); g.titleW = wOf(t);
    g.cardLeft = lOf(c); g.cardW = wOf(c);
    // The one-column view of the ROW box, matching g.rowsX beside it: the column
    // the panel actually LAID OUT, which is what this field's own comment (and
    // ColumnGeom::rowsW's) already promised. It was `spec.cols` -- the ask echoed
    // straight back -- so every panel whose width was set by something OTHER than
    // its rows (a caption, a button row, or Spec::minPanelW) reported a content
    // column narrower than the one it drew.
    //
    // Worst for a HUD that states a MINIMUM PANEL WIDTH and reads the interior
    // back, because there cols is 0 and 0 is what it got: the Gap Bar's bar, the
    // rider markers placed across it and its centred gap text all collapsed onto
    // the panel's left edge.
    g.cols = g.bands.front().columns.front().rowsW;
    g.rowsTop = g.sections.front().rowsTop;
    g.contentH = g.sections.front().h;
    g.cardTop = g.sections.front().top;
    g.cardBot = g.sections.back().bot;
    g.captionY = hasCaption ? (g.titleTop + t.b.t + t.p.t) : 0.0;
    g.squareBorders = sq(P.b) && (!hasTitle || sq(T.b)) && (!hasCard || sq(C.b))
                   && (nBtn == 0 || sq(B.b));
    g.overTopY = g.sections.front().rowsTop < P.b.t - 1e-9;
    g.overX = panelInnerLeft < P.b.l - 1e-9;
    return g;
}

}  // namespace PanelBox
