// ============================================================================
// tests/unit/panel_box_test.cpp
// The engine against the box model's golden vectors.
//
// core/panel_box.h is the plugin's panel layout engine. The same arithmetic
// exists in JS in tools/panel_box_model.js, which is where the model was worked
// out and which generates the golden vectors in
// tests/fixtures/panel_box_parity.json via tools/gen_panel_box_fixture.js.
// Regenerating those vectors without changing the engine the same way is what
// turns this suite red -- which is the point: the fixture is the only thing
// tying the settled model to the code that ships it.
//
// BOXMODEL_FIXTURE (the fixture's absolute path) is defined by the unit
// CMakeLists, like PARITY_FIXTURE.
// ============================================================================
#include "doctest.h"

#include <fstream>
#include <string>

#include "core/panel_box.h"
#include "vendor/nlohmann/json.hpp"

using nlohmann::json;

static json loadFixture() {
    std::ifstream in(BOXMODEL_FIXTURE);
    REQUIRE_MESSAGE(in.good(), "fixture not readable: " << BOXMODEL_FIXTURE);
    return json::parse(in);
}

static PanelBox::Sides sidesFrom(const json& j) {
    PanelBox::Sides s;
    s.t = j.at("t").get<double>(); s.r = j.at("r").get<double>();
    s.b = j.at("b").get<double>(); s.l = j.at("l").get<double>();
    return s;
}

static PanelBox::BoxTerms termsFrom(const json& j) {
    PanelBox::BoxTerms t;
    if (j.contains("margin")) t.margin = sidesFrom(j.at("margin"));
    t.border = sidesFrom(j.at("border"));
    t.padding = sidesFrom(j.at("padding"));
    return t;
}

static PanelBox::Spec specFrom(const json& j) {
    PanelBox::Spec s;
    s.unit = j.at("unit").get<double>();
    s.panel = termsFrom(j.at("panel"));
    s.title = termsFrom(j.at("title"));
    s.content = termsFrom(j.at("content"));
    s.button = termsFrom(j.at("button"));
    s.themed = j.at("themed").get<bool>();
    s.band = j.at("band").get<bool>();
    s.card = j.at("card").get<bool>();
    if (!j.at("caption").is_null()) {
        s.hasCaption = true;
        s.captionW = j.at("caption").at("w").get<double>();
        s.captionH = j.at("caption").at("h").get<double>();
    }
    s.sections = j.at("sections").get<std::vector<double>>();
    s.cols = j.at("cols").get<double>();
    // The GENERAL body. A case states `bands` or the sections/cols shorthand,
    // never both; the fixture writes an empty array for the shorthand cases.
    if (j.contains("bands")) {
        for (const auto& bj : j.at("bands")) {
            PanelBox::BandAsk band;
            for (const auto& cj : bj.at("columns")) {
                PanelBox::ColumnAsk col;
                col.cols = cj.at("cols").get<double>();
                col.sections = cj.at("sections").get<std::vector<double>>();
                band.columns.push_back(std::move(col));
            }
            s.bands.push_back(std::move(band));
        }
    }
    if (!j.at("buttons").is_null()) {
        s.buttons = j.at("buttons").at("n").get<int>();
        s.buttonW = j.at("buttons").at("w").get<double>();
        s.buttonH = j.at("buttons").at("h").get<double>();
    }
    if (j.contains("minPanelW")) s.minPanelW = j.at("minPanelW").get<double>();
    if (j.contains("gap")) s.gap = j.at("gap").get<double>();
    return s;
}

static const char* widthSetByName(PanelBox::Geom::WidthSetBy w) {
    switch (w) {
        case PanelBox::Geom::ROWS: return "rows";
        case PanelBox::Geom::CAPTION: return "caption";
        case PanelBox::Geom::BUTTONS: return "buttons";
        case PanelBox::Geom::MIN: return "min";
    }
    return "?";
}

// The fixture stores doubles rounded to 1e-9; both engines compute in double,
// so agreement well beyond this tolerance is expected — a looser epsilon here
// would only hide a real divergence.
static void checkNum(const json& expected, double actual, const char* field) {
    const double want = expected.get<double>();
    INFO("field " << field << ": got " << actual << " want " << want);
    CHECK(std::fabs(actual - want) <= 1e-7);   // absolute: values near 0 are common
}

TEST_CASE("parity: parseSides matches the shared golden vectors") {
    const auto fx = loadFixture();
    for (const auto& c : fx.at("parseSides")) {
        const std::string in = c.at("in").get<std::string>();
        INFO("input \"" << in << "\"");
        const PanelBox::Sides got = PanelBox::parseSides(in);
        const PanelBox::Sides want = sidesFrom(c.at("out"));
        CHECK(got.t == doctest::Approx(want.t));
        CHECK(got.r == doctest::Approx(want.r));
        CHECK(got.b == doctest::Approx(want.b));
        CHECK(got.l == doctest::Approx(want.l));
    }
}

TEST_CASE("formatSides round-trips through parseSides at every arity") {
    // The settings writer emits the most compact spelling; the applier parses
    // it back. Both ends live in panel_box.h so they cannot drift, and this
    // pins the round-trip at each shorthand arity plus fractional values.
    const PanelBox::Sides cases[] = {
        {2, 2, 2, 2}, {0.5, 0.5, 0.5, 0.5}, {2, 4, 2, 4},
        {1, 2, 3, 4}, {0, 0, 0, 0}, {1.25, 0, 1.25, 0},
    };
    for (const PanelBox::Sides& c : cases) {
        const std::string text = PanelBox::formatSides(c);
        INFO("formatted \"" << text << "\"");
        const PanelBox::Sides back = PanelBox::parseSides(text);
        CHECK(back.t == doctest::Approx(c.t));
        CHECK(back.r == doctest::Approx(c.r));
        CHECK(back.b == doctest::Approx(c.b));
        CHECK(back.l == doctest::Approx(c.l));
    }
    CHECK(PanelBox::formatSides({2, 2, 2, 2}) == "2");
    CHECK(PanelBox::formatSides({2, 4, 2, 4}) == "2 4");
    CHECK(PanelBox::formatSides({1, 2, 3, 4}) == "1 2 3 4");
}

TEST_CASE("parity: layoutPanel matches the shared golden vectors") {
    const auto fx = loadFixture();
    for (const auto& c : fx.at("layoutPanel")) {
        const std::string name = c.at("name").get<std::string>();
        INFO("case " << name);
        const PanelBox::Geom g = PanelBox::layoutPanel(specFrom(c.at("spec")));
        const json& o = c.at("out");

        CHECK(g.hasTitle == o.at("hasTitle").get<bool>());
        CHECK(g.hasCard == o.at("hasCard").get<bool>());
        CHECK(g.nBtn == o.at("nBtn").get<int>());
        CHECK(widthSetByName(g.widthSetBy) == o.at("widthSetBy").get<std::string>());
        CHECK(g.squareBorders == o.at("squareBorders").get<bool>());
        CHECK(g.overTopY == o.at("overTopY").get<bool>());
        CHECK(g.overX == o.at("overX").get<bool>());

        checkNum(o.at("columnSplit"), g.columnSplit, "columnSplit");
        checkNum(o.at("artBot"), g.artBot, "artBot");
        checkNum(o.at("panelInner"), g.panelInner, "panelInner");
        checkNum(o.at("panelInnerLeft"), g.panelInnerLeft, "panelInnerLeft");
        checkNum(o.at("innerW"), g.innerW, "innerW");
        checkNum(o.at("titleLeft"), g.titleLeft, "titleLeft");
        checkNum(o.at("titleW"), g.titleW, "titleW");
        checkNum(o.at("cardLeft"), g.cardLeft, "cardLeft");
        checkNum(o.at("cardW"), g.cardW, "cardW");
        checkNum(o.at("seam"), g.seam, "seam");
        checkNum(o.at("panelH"), g.panelH, "panelH");
        checkNum(o.at("slackY"), g.slackY, "slackY");
        checkNum(o.at("titleSlack"), g.titleSlack, "titleSlack");
        checkNum(o.at("panelCols"), g.panelCols, "panelCols");
        checkNum(o.at("captionX"), g.captionX, "captionX");
        checkNum(o.at("rowsX"), g.rowsX, "rowsX");
        checkNum(o.at("buttonX"), g.buttonX, "buttonX");
        checkNum(o.at("rowsTop"), g.rowsTop, "rowsTop");
        checkNum(o.at("contentH"), g.contentH, "contentH");
        checkNum(o.at("cardTop"), g.cardTop, "cardTop");
        checkNum(o.at("cardBot"), g.cardBot, "cardBot");
        checkNum(o.at("captionY"), g.captionY, "captionY");

        // JS reports "no caption row" / "no button row" as null; the C++ engine
        // as a presence flag beside zeroed values.
        if (o.at("titleTop").is_null()) {
            CHECK_FALSE(g.hasCaptionRow);
        } else {
            CHECK(g.hasCaptionRow);
            checkNum(o.at("titleTop"), g.titleTop, "titleTop");
            checkNum(o.at("titleH"), g.titleH, "titleH");
            checkNum(o.at("titleBot"), g.titleBot, "titleBot");
            checkNum(o.at("bandDrawnBot"), g.bandDrawnBot, "bandDrawnBot");
        }
        if (o.at("btnTop").is_null()) {
            CHECK_FALSE(g.hasButtonRow);
        } else {
            CHECK(g.hasButtonRow);
            checkNum(o.at("btnTop"), g.btnTop, "btnTop");
            checkNum(o.at("btnH"), g.btnH, "btnH");
            checkNum(o.at("btnW"), g.btnW, "btnW");
        }

        const auto& secs = o.at("sections");
        REQUIRE(g.sections.size() == secs.size());
        for (size_t i = 0; i < secs.size(); ++i) {
            INFO("section " << i);
            checkNum(secs[i].at("top"), g.sections[i].top, "sec.top");
            checkNum(secs[i].at("bot"), g.sections[i].bot, "sec.bot");
            checkNum(secs[i].at("rowsTop"), g.sections[i].rowsTop, "sec.rowsTop");
            checkNum(secs[i].at("h"), g.sections[i].h, "sec.h");
        }
        // THE BAND VIEW, which is the whole body once a case states a split —
        // `sections` above is only the first column of each band, so a split's
        // further columns are asserted here or nowhere.
        const auto& bandsJ = o.at("bands");
        REQUIRE(g.bands.size() == bandsJ.size());
        for (size_t bi = 0; bi < bandsJ.size(); ++bi) {
            INFO("band " << bi);
            checkNum(bandsJ[bi].at("top"), g.bands[bi].top, "band.top");
            checkNum(bandsJ[bi].at("bot"), g.bands[bi].bot, "band.bot");
            const auto& colsJ = bandsJ[bi].at("columns");
            REQUIRE(g.bands[bi].columns.size() == colsJ.size());
            for (size_t ci = 0; ci < colsJ.size(); ++ci) {
                INFO("column " << ci);
                const PanelBox::ColumnGeom& col = g.bands[bi].columns[ci];
                checkNum(colsJ[ci].at("left"), col.left, "col.left");
                checkNum(colsJ[ci].at("w"), col.w, "col.w");
                const auto& csecs = colsJ[ci].at("sections");
                REQUIRE(col.sections.size() == csecs.size());
                for (size_t si = 0; si < csecs.size(); ++si) {
                    INFO("section " << si);
                    checkNum(csecs[si].at("top"), col.sections[si].top, "col.sec.top");
                    checkNum(csecs[si].at("bot"), col.sections[si].bot, "col.sec.bot");
                    checkNum(csecs[si].at("rowsTop"), col.sections[si].rowsTop, "col.sec.rowsTop");
                    checkNum(csecs[si].at("h"), col.sections[si].h, "col.sec.h");
                }
            }
        }
        const auto& btns = o.at("btns");
        REQUIRE(g.btns.size() == btns.size());
        for (size_t i = 0; i < btns.size(); ++i) {
            INFO("button " << i);
            checkNum(btns[i].at("x"), g.btns[i].x, "btn.x");
            checkNum(btns[i].at("w"), g.btns[i].w, "btn.w");
            checkNum(btns[i].at("labelX"), g.btns[i].labelX, "btn.labelX");
        }
    }
}

// The property the whole model exists for, checked directly rather than only
// through golden numbers: bumping ONE term on a box moves THAT box's own
// content by exactly the term, on both axes, whatever else is set — and the
// panel grows by the same amount rather than any child absorbing it.
TEST_CASE("box model: every term moves its own content by exactly itself") {
    PanelBox::Spec base;
    base.unit = 0.8333333333333334;
    base.panel = {PanelBox::Sides{}, PanelBox::parseSides("2"), PanelBox::parseSides("2")};
    base.title = {PanelBox::parseSides("0"), PanelBox::parseSides("1"), PanelBox::parseSides("0.5")};
    base.content = {PanelBox::parseSides("0.5"), PanelBox::parseSides("1"), PanelBox::parseSides("0")};
    base.button = {PanelBox::parseSides("0.5"), PanelBox::parseSides("1"), PanelBox::parseSides("0.5")};
    base.themed = true; base.band = true; base.card = true;
    base.hasCaption = true; base.captionW = 9.0; base.captionH = 1.7;
    base.sections = {2.0, 6.0};
    base.cols = 30.0;
    base.buttons = 3; base.buttonW = 7.0; base.buttonH = 2.0;

    const PanelBox::Geom g0 = PanelBox::layoutPanel(base);
    const auto bump = [](PanelBox::Sides s) {
        s.t += 1; s.r += 1; s.b += 1; s.l += 1;
        return s;
    };

    SUBCASE("[content] padding +1 moves the rows one cell on each axis") {
        PanelBox::Spec s = base;
        s.content.padding = bump(s.content.padding);
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g.rowsX - g0.rowsX == doctest::Approx(1.0));
        CHECK(g.rowsTop - g0.rowsTop == doctest::Approx(base.unit));
        // ...and the caption does not move: its box spent nothing.
        CHECK(g.captionX == doctest::Approx(g0.captionX));
        CHECK(g.captionY == doctest::Approx(g0.captionY));
    }
    SUBCASE("[title] padding +1 moves the caption and not the rows") {
        PanelBox::Spec s = base;
        s.title.padding = bump(s.title.padding);
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g.captionX - g0.captionX == doctest::Approx(1.0));
        CHECK(g.captionY - g0.captionY == doctest::Approx(base.unit));
        CHECK(g.rowsX == doctest::Approx(g0.rowsX));
        // The rows DO move down — but the caption block's advance is
        // quantized, so they move by the CEILED delta, not the raw t+b.
        const double adv0 = std::ceil(4.2 - 1e-9);            // base title block
        const double adv = std::ceil(4.2 + 2.0 * base.unit - 1e-9);
        CHECK(g.rowsTop - g0.rowsTop == doctest::Approx(adv - adv0));
    }
    SUBCASE("[button] margin +1 moves the button label sideways by one cell") {
        PanelBox::Spec s = base;
        s.button.margin = bump(s.button.margin);
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g.btns[0].labelX - g0.btns[0].labelX == doctest::Approx(1.0));
        // The gap between two buttons is the SUM of the facing margins.
        const double gap0 = g0.btns[1].x - (g0.btns[0].x + g0.btns[0].w);
        const double gap = g.btns[1].x - (g.btns[0].x + g.btns[0].w);
        CHECK(gap - gap0 == doctest::Approx(2.0));
    }
    SUBCASE("the section seam is the sum of the facing margins, not their max") {
        PanelBox::Spec s = base;
        s.content.margin = bump(s.content.margin);
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g0.seam == doctest::Approx(2.0 * 0.5 * base.unit));
        CHECK(g.seam == doctest::Approx(2.0 * 1.5 * base.unit));
    }
    SUBCASE("the ceil slack lands inside the LAST section's own box") {
        // The panel ceils to a whole cell and the remainder must go somewhere.
        // Below the last card it reads as stray fill (5.6px of panel surface,
        // as reported); a separate "drawn" bottom made full-bleed fillers stop
        // a strip above the card (the Notices slab). So the last section's box
        // ITSELF grows: content box = drawn card, and the grown bottom plus
        // its margin and the panel's bottom chrome lands exactly on panelH.
        // The fraction comes from the section stack (the caption advance is
        // quantized and contributes none).
        PanelBox::Spec s = base;
        s.sections = {2.0, 6.3};
        s.buttons = 0;
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g.slackY > 1e-9);
        CHECK(g.sections.back().bot + g.C.m.b + g.P.p.b + g.P.b.b
              == doctest::Approx(g.panelH));
        // With a BUTTON ROW last, the last section STILL absorbs the
        // remainder and the row rides down with it — the junction stays
        // term-exact and the bottom chrome exact. Air parked beside the
        // button (bare below it, or joined to the junction) was reported as
        // a bug both times it was tried.
        PanelBox::Spec sb = base;
        sb.sections = {2.0, 6.3};
        const PanelBox::Geom gb = PanelBox::layoutPanel(sb);
        CHECK(gb.slackY > 1e-9);
        CHECK(gb.sections.back().h == doctest::Approx(6.3 + gb.slackY));
        CHECK(gb.btnTop - gb.sections.back().bot
              == doctest::Approx(gb.C.m.b + gb.B.m.t));   // term-exact junction
        CHECK(gb.btnTop + gb.btnH + gb.B.m.b + gb.P.p.b + gb.P.b.b
              == doctest::Approx(gb.panelH));
    }
    SUBCASE("titled and untitled panels with identical terms get identical slack") {
        // The caption glyph box (capH, fractional) is the one term a titled
        // panel has that its untitled sibling doesn't; the quantized caption
        // advance keeps it from changing the ceil remainder. Without it, two
        // widgets side by side — one title on, one off — drew their last card
        // a few px apart and phased their rows differently (the Lap vs Time
        // report).
        PanelBox::Spec titled = base;
        titled.sections = {2.0, 6.3};   // fractional, so the slack is real
        PanelBox::Spec untitled = titled;
        untitled.hasCaption = false;
        const PanelBox::Geom gt = PanelBox::layoutPanel(titled);
        const PanelBox::Geom gu = PanelBox::layoutPanel(untitled);
        CHECK(gt.titleSlack > 1e-9);   // base capH 1.7 is deliberately fractional
        CHECK(gt.slackY > 1e-9);
        // The remainder stretches the drawn band, never bare air below it.
        CHECK(gt.bandDrawnBot == doctest::Approx(gt.titleBot + gt.titleSlack));
        CHECK(gu.slackY == doctest::Approx(gt.slackY));
        // Same drawn card height for the same section stack, title or not
        // (the section box carries the remainder, so bot - top IS drawn).
        CHECK(gu.sections.back().bot - gu.sections.back().top
              == doctest::Approx(gt.sections.back().bot - gt.sections.back().top));
    }
    SUBCASE("gap lands at every junction and never at the edges") {
        PanelBox::Spec s = base;
        s.gap = 1.0;
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        // Top edge: nothing above the first child moves.
        CHECK(g.panelInner == doctest::Approx(g0.panelInner));
        CHECK(g.titleTop == doctest::Approx(g0.titleTop));
        // Section seam: exactly one gap wider.
        CHECK(g.seam == doctest::Approx(g0.seam + base.unit));
        // Card→buttons junction: exactly one gap wider — the slack lives in
        // the last section's box now, so the junction is TERM-EXACT and the
        // comparison needs no slack accounting.
        const double junc0 = g0.btnTop - g0.sections.back().bot;
        const double junc = g.btnTop - g.sections.back().bot;
        CHECK(junc - junc0 == doctest::Approx(base.unit));
        // Bottom edge: the ridden-down row lands exactly on the whole-cell
        // bottom less the panel's own chrome, in both specs.
        CHECK(g.btnTop + g.btnH + g.B.m.b + g.P.p.b + g.P.b.b
              == doctest::Approx(g.panelH));
        CHECK(g0.btnTop + g0.btnH + g0.B.m.b + g0.P.p.b + g0.P.b.b
              == doctest::Approx(g0.panelH));
    }
    SUBCASE("a fractional gap keeps titled and untitled slack equal") {
        // The band→card junction gap exists only when the caption does, so it
        // is folded INTO the quantized caption advance — outside it, a
        // fractional gap would be a second differential fractional term and
        // reintroduce the very slack split the quantization removed.
        PanelBox::Spec titled = base;
        titled.gap = 0.7;
        PanelBox::Spec untitled = titled;
        untitled.hasCaption = false;
        const PanelBox::Geom gt = PanelBox::layoutPanel(titled);
        const PanelBox::Geom gu = PanelBox::layoutPanel(untitled);
        CHECK(gu.slackY == doctest::Approx(gt.slackY));
    }
    SUBCASE("a margin grows the panel; the child does not absorb it") {
        PanelBox::Spec s = base;
        s.title.margin = bump(s.title.margin);
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        // The caption's ask grew by l+r, and since the rows still win the
        // width, the band gets NARROWER by its own margins while the panel
        // holds — unless the caption's ask exceeds the rows'. Either way the
        // caption content itself moved by exactly one cell.
        CHECK(g.captionX - g0.captionX == doctest::Approx(1.0));
        CHECK(g.titleW == doctest::Approx(g.innerW - 2.0));
    }
}

// ============================================================================
// `cols` IS THE COLUMN THE PANEL LAID OUT, NOT THE ASK.
//
// Geom::cols is documented (and read by BaseHud::PanelPlan::contentW /
// rowBandW) as the one-column view of the ROW box -- the same box
// ColumnGeom::rowsW carries per column. It used to be `spec.cols` echoed
// straight back, which is only the same number when the ROWS ask is what set
// the panel's width. Anything else that can widen a panel -- a wider caption,
// a button row, or Spec::minPanelW -- left the two disagreeing, and a painter
// filling "the content column" drew short of the card it sits in.
//
// The degenerate case is the one that shipped: a HUD that states a minimum
// PANEL width and reads the interior back asks for cols = 0. It got 0, and the
// Gap Bar's bar, the rider markers spread across it and its centred gap text
// all collapsed onto the panel's left edge.
TEST_CASE("panel box: cols reports the laid-out row column, not the ask") {
    PanelBox::Spec s;
    s.unit = 0.8333333333333334;
    s.panel = {PanelBox::Sides{}, PanelBox::parseSides("2"), PanelBox::parseSides("2")};
    s.content = {PanelBox::parseSides("0.5"), PanelBox::parseSides("1"), PanelBox::parseSides("0")};
    s.themed = true; s.card = true;
    s.sections = {2.0};

    SUBCASE("the rows set the width: cols is the ask, as before") {
        s.cols = 30.0;
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g.widthSetBy == PanelBox::Geom::ROWS);
        CHECK(g.cols == doctest::Approx(30.0));
        CHECK(g.cols == doctest::Approx(g.bands.front().columns.front().rowsW));
    }
    SUBCASE("minPanelW sets the width: cols is the column, not the zero asked for") {
        s.cols = 0.0;
        s.minPanelW = 60.0;
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g.widthSetBy == PanelBox::Geom::MIN);
        CHECK(g.cols > 0.0);
        CHECK(g.cols == doctest::Approx(g.bands.front().columns.front().rowsW));
        // ...and it spans the panel: everything outside it is chrome, so the
        // column plus the insets either side is the whole panel width.
        CHECK(g.rowsX + g.cols + (g.panelCols - g.panelInnerLeft - g.innerW)
                  + (g.innerW - (g.rowsX - g.panelInnerLeft) - g.cols)
              == doctest::Approx(g.panelCols));
    }
    SUBCASE("a wide caption sets the width: cols follows the panel, not the rows' ask") {
        s.cols = 10.0;
        s.hasCaption = true; s.captionW = 40.0; s.captionH = 1.7;
        s.title = {PanelBox::parseSides("0"), PanelBox::parseSides("1"), PanelBox::parseSides("0.5")};
        s.band = true;
        const PanelBox::Geom g = PanelBox::layoutPanel(s);
        CHECK(g.widthSetBy == PanelBox::Geom::CAPTION);
        CHECK(g.cols > 10.0);
        CHECK(g.cols == doctest::Approx(g.bands.front().columns.front().rowsW));
    }
}
