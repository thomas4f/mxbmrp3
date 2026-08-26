// ============================================================================
// tests/integration/tests/center_stack_theme_test.cpp
// THE CENTRE STACK DOES NOT MOVE WHEN THE THEME'S BODY CARD IS SWITCHED OFF.
//
// A theme decides, per panel kind, whether it draws a body card ([card] hud-content).
// That choice changes dim.paddingV, because contentPaddingY() is gated on it -- and
// a panel whose TOP EDGE is derived by subtracting that padding from its stored offset
// therefore moves vertically when a skinner flips the flag. Every other panel anchors
// its top AT the offset and grows downward, so only the padding-derived one moves.
//
// Reported from two screenshots of the same scene under one theme with hud-content on
// and off: "timing remained in place, but gap bar shifted down".
//
// This is not a themed-look question that a screenshot answers better -- it is two
// numbers, the panel's top edge in each state, and hudScreenEdges reports exactly that
// (bounds PLUS the live offset, which is the quantity that must not change).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstdlib>

namespace {

// Frame / card insets in grid cells, matching the shipped themes' proportions.
constexpr float FRAME_INSET = 2.0f;
constexpr float CARD_INSET  = 1.0f;

// hudScreenEdges is quantised x1e6 of a 0..1 normalized coordinate, so one unit is a
// millionth of the screen height -- far below a pixel. Exact equality is the honest
// assertion here (both states run the same arithmetic on the same floats), but allow a
// unit of float slop rather than pinning bit-exactness.
constexpr int SLOP = 2;

const char* kName[] = { "standings", "gforce", "timing", "performance",
                        "charts", "settings", "gapbar", "notices" };

}  // namespace

TEST_CASE("centre stack: [card] hud-content does not move a panel's top edge") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\center_stack_theme\\");
    REQUIRE_MESSAGE(host.hasThemeGeometry(),
                    "MXBMRP3_Test_InstallTheme not exported (test build?)");
    REQUIRE_MESSAGE(host.hasScreenEdges(),
                    "MXBMRP3_Test_HudScreenEdges not exported (test build?)");
    host.showAllHuds(true);

    // NOTICES NEEDS SOMETHING TO SAY. With no active notice it emits nothing and its
    // panelRect reads [0..0] -- so a case that only compares its top edge between two
    // states compares zero with zero and passes having measured nothing. That is what
    // the first version of this file did. runInit with no setup file name raises the
    // "DEFAULT SETUP" notice, which is persistent rather than timed, so it is still on
    // screen however long the assertions take.
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.draw();
    REQUIRE_MESSAGE(host.hudScreenEdges(PluginHost::HUD_NOTICES).b > 0,
                    "Notices rendered nothing -- the no-movement check below would be vacuous");

    // The three centre-stack panels, plus Standings as a control: it is an ordinary
    // table HUD with a body card, so if the card flag moved IT too the fault would be
    // somewhere shared rather than in how these three anchor themselves.
    const PluginHost::HudId stack[] = {
        PluginHost::HUD_GAPBAR, PluginHost::HUD_NOTICES,
        PluginHost::HUD_TIMING, PluginHost::HUD_STANDINGS
    };

    // A DISTINCT THEME NAME per install: installSyntheticTheme appends and
    // getThemeByName returns the first match, so one name would keep resolving to the
    // card-on theme and both measurements would come from the same state.
    host.installTheme("card_on", FRAME_INSET, CARD_INSET, /*titleBand=*/1, /*card=*/1);
    host.draw();
    int topWithCard[4];
    for (int i = 0; i < 4; ++i) topWithCard[i] = host.hudScreenEdges(stack[i]).t;

    host.installTheme("card_off", FRAME_INSET, CARD_INSET, /*titleBand=*/1, /*card=*/0);
    host.draw();

    for (int i = 0; i < 4; ++i) {
        const int now = host.hudScreenEdges(stack[i]).t;
        INFO(kName[static_cast<int>(stack[i])] << ": top " << topWithCard[i]
             << " with the card, " << now << " without (delta "
             << (now - topWithCard[i]) << ")");
        CHECK(std::abs(now - topWithCard[i]) <= SLOP);
    }

    // AND THE ABSOLUTE POSITION, not only that the two agree. The same root cause had a
    // second symptom that "they agree" cannot see: with the card on, the Gap Bar's box
    // top computed to y = 0 -- flush against the screen edge, its top frame slice
    // clipped off -- because its stored default had the UNTHEMED padding added in and a
    // bigger themed one subtracted back out. Both states reading zero would satisfy the
    // no-movement check above and still be wrong.
    //
    // The stack's first box belongs one grid cell down (CenterStack::gapBarBoxTop is
    // rowY(1)), which is also the row the settings and camera buttons sit on.
    const int gapTop = host.hudScreenEdges(PluginHost::HUD_GAPBAR).t;
    REQUIRE(host.hasLayoutCells());
    const auto lat = host.layoutCells();
    INFO("gap bar top: " << gapTop << " millionths of screen height, one cell = "
         << lat.cellH);
    CHECK(gapTop > 0);         // not clipped against the screen edge
    // One grid cell, read off the LIVE lattice rather than frozen as 11733 --
    // the literal asserted the shipped uiLineHeight, not the design, and went
    // red the moment the ratio moved.
    CHECK(std::abs(gapTop / 1e6 - lat.cellH) < 2e-6);

    // AND THE THREE DO NOT OVERLAP EACH OTHER -- UNTHEMED, which is the configuration
    // the stack's stored defaults are derived for. center_stack.h computes the three
    // box tops from the [Advanced] built-in (L.boxPanelPadding), writes them ONCE via
    // resetToDefaults(), and its own header states the limitation: the stored tops
    // "follow the built-in at reset/first-run but cannot follow a THEME at all".
    //
    // THE THEMED HALF OF THIS ASSERTION IS GONE, DELIBERATELY, and this note is the
    // record of why. The removed version asserted no-overlap "in either state"; under
    // the additive plan model a frame-2/card-1 theme grows each box ~4 cells while the
    // stored tops stay put, so the themed stack DOES overlap at defaults (~3.7 cells at
    // both seams, measured here). That is the documented, accepted cost of stored
    // default offsets -- "a themed or captioned stack is nudged by hand exactly as any
    // other panel is" (center_stack.h), with the alternative (live theme-following
    // defaults) named at contentPaddingY() as a persistence migration this branch did
    // not take. What IS pinned for the themed state is the mechanism itself: the tops
    // do not move when a theme lands, which is the same stored-offset contract the
    // no-movement case above pins for the card flag. If the persistence migration ever
    // happens, THAT change flips this assertion -- restore the themed no-overlap check
    // then, because it becomes the contract.
    host.clearTheme();
    host.draw();
    {
        const auto g = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
        const auto n = host.hudScreenEdges(PluginHost::HUD_NOTICES);
        const auto t = host.hudScreenEdges(PluginHost::HUD_TIMING);
        INFO("unthemed gapbar [" << g.t << ".." << g.b << "]"
             << " notices [" << n.t << ".." << n.b << "]"
             << " timing [" << t.t << ".." << t.b << "]");
        // THEY SHARE A TOP AND THEREFORE OVERLAP, deliberately. This used to assert
        // no-crossing, because the three defaulted to a column; they are alternatives
        // more often than companions, so the default is now the one good spot and a
        // player running all three drags two of them.
        //
        // What is pinned instead is that the three AGREE. Their offsets are stored
        // per-HUD and only Notices ever carried a hidden base (its render baked
        // CenterStack::noticesBoxTop in and treated the offset as a delta from it), so
        // "the same INI value puts them in the same place" is exactly the property
        // that was false before and has to stay true now.
        CHECK(std::abs(n.t - g.t) <= SLOP);
        CHECK(std::abs(t.t - g.t) <= SLOP);
        // AND THE WIDTHS AGREE, edge for edge. They deliberately did NOT for most
        // of 1.29's development: the gap bar sized its BAR to the others' panel
        // width, which left its own panel one padding wider per side (159500
        // against 137500 unthemed) -- documented then as this HUD's exception.
        // gap_bar_hud.cpp now passes the same CenterStack::boxWidth as its panel
        // MINIMUM at the 50% default, so all three share both edges exactly; a
        // player lining the stack up sees the background through any mismatch,
        // which is how the old exception was finally reported.
        CHECK(std::abs(n.l - g.l) <= SLOP);
        CHECK(std::abs(n.r - g.r) <= SLOP);
        CHECK(std::abs(t.l - g.l) <= SLOP);
        CHECK(std::abs(t.r - g.r) <= SLOP);

        const int topsUnthemed[3] = { g.t, n.t, t.t };
        host.installTheme("overlap", FRAME_INSET, CARD_INSET, 1, 1);
        host.draw();
        const int topsThemed[3] = { host.hudScreenEdges(PluginHost::HUD_GAPBAR).t,
                                    host.hudScreenEdges(PluginHost::HUD_NOTICES).t,
                                    host.hudScreenEdges(PluginHost::HUD_TIMING).t };
        for (int i = 0; i < 3; ++i) {
            INFO("stack panel " << i << ": top " << topsUnthemed[i]
                 << " unthemed, " << topsThemed[i] << " themed");
            CHECK(std::abs(topsThemed[i] - topsUnthemed[i]) <= SLOP);
        }
        // Widths agree under the theme too (the frame grows all three equally).
        const auto gt = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
        const auto nt = host.hudScreenEdges(PluginHost::HUD_NOTICES);
        const auto tt = host.hudScreenEdges(PluginHost::HUD_TIMING);
        CHECK(std::abs(nt.l - gt.l) <= SLOP);
        CHECK(std::abs(nt.r - gt.r) <= SLOP);
        CHECK(std::abs(tt.l - gt.l) <= SLOP);
        CHECK(std::abs(tt.r - gt.r) <= SLOP);
    }
}

// ---------------------------------------------------------------------------
// THE COLOURED BLOCK MEETS THE CARD'S OUTER EDGE, at any card thickness.
//
// The Notices slab and the Gap Bar's fill ARE the section card's border box, taken
// from the plan (PanelBox::SectionGeom top/bot), so they land ON the card rather
// than inside it. They used to be the content row outset by
// BaseHud::drawnCardBorderX/Y(); that pair is gone, but the bug it was written to
// avoid is not, because layoutPanel makes the same distinction and can make the
// same mistake: cardBorderX/Y() answer "does the theme ship card art", which is
// TRUE for a theme whose [card] hud-content = 0 -- the art exists, this panel just
// gets no card. Reaching for a border that is never drawn is subtle enough that
// six screenshots nearly missed it, and it came back once at the engine level
// (layoutPanel spent the border on `themed` alone, see panel_box.h).
//
// Asserted by PARAMETER rather than by eye, over the card thicknesses a theme can
// choose and over the flag that turns the card off. A screenshot per combination is
// what this replaces.
TEST_CASE("the notice slab lands on the card's outer edge at any card size") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\center_stack_slab\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasStringRows());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    // buttonSprites deliberately OFF, so the slab is ONE quad and is unambiguously the
    // last one emitted. With button art it is a nine-slice like the card, and telling
    // the two groups apart by position is the kind of index arithmetic that goes stale.
    auto bbox = [](const std::vector<PluginHost::QuadRect>& q, size_t first, size_t count) {
        PluginHost::QuadRect r = q[first];
        for (size_t i = first + 1; i < first + count; ++i) {
            r.l = (std::min)(r.l, q[i].l); r.t = (std::min)(r.t, q[i].t);
            r.r = (std::max)(r.r, q[i].r); r.b = (std::max)(r.b, q[i].b);
        }
        return r;
    };

    // UNTHEMED FIRST: with the title OFF the slab is the WHOLE PANEL -- not the
    // panel less a cell of padding. Sized to the content row it came out 58% of
    // the panel's height with a 1-cell ring on all four sides, which reads as a
    // border nobody asked for around a block whose colour IS the reading (see
    // PanelWant::contentFillsPanel). With the title ON, the slab still reaches
    // the sides and the bottom but the TOP padding stays as the caption's air --
    // spending it put "Gap Bar" flush against the panel's top edge (reported),
    // and the first read of that report restored the SIDE padding instead, so
    // the slab stopped covering the panel (also reported). Both shapes are
    // pinned below.
    //
    // BOTH HALVES MATTER and are asserted together, because the flag moves the
    // padding into the content rather than dropping it: the slab has to reach
    // the edges AND the panel has to be the size it always was. Dropping the
    // padding instead would pass the first check and silently shrink the panel
    // - which center_stack.h's stored default tops are computed from
    // (noticesBoxHeight sums the padding), so the stack would open gaps at both
    // seams with nothing here to say so.
    {
        host.clearTheme();
        host.draw();
        const auto panel = host.hudScreenEdges(PluginHost::HUD_NOTICES);
        const auto q = host.hudQuadRects(PluginHost::HUD_NOTICES);
        REQUIRE_MESSAGE(!q.empty(), "notices drew nothing unthemed");
        const PluginHost::QuadRect slab = q.back();
        const double pl = panel.l / 1e6, pt = panel.t / 1e6;
        const double pr = panel.r / 1e6, pb = panel.b / 1e6;
        INFO("panel [" << pl << "," << pt << " .. " << pr << "," << pb << "]"
             << " slab [" << slab.l << "," << slab.t << " .. " << slab.r << "," << slab.b << "]");
        constexpr double kSlop = 1e-6;
        CHECK(std::abs(slab.l - pl) < kSlop);
        CHECK(std::abs(slab.t - pt) < kSlop);
        CHECK(std::abs(slab.r - pr) < kSlop);
        CHECK(std::abs(slab.b - pb) < kSlop);
        // The panel's own size: one border cell of padding each side plus one
        // normal line height, ceiled to whole grid cells (the engine's tiling
        // quantizer). Derived from the LIVE lattice, not a literal -- the point
        // is that this number does NOT move when the split between padding and
        // content does, and the derivation never mentions the split.
        REQUIRE(host.hasLayoutCells());
        const auto lat = host.layoutCells();
        const double raw = 2.0 * lat.artV + lat.lineH;
        const double expect = std::ceil(raw / lat.cellH - 1e-9) * lat.cellH;
        CHECK(std::abs((pb - pt) - expect) < 1e-5);
        // ...and the gap bar, which center_stack.h sizes by the same expression,
        // still agrees with it - so a change that shrank both in step is caught too.
        const auto gap = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
        CHECK(std::abs((gap.b - gap.t) - (panel.b - panel.t)) < 2);

        // TITLE ON: the slab keeps the sides and the bottom, and gives the TOP
        // back to the caption -- its top drops by at least a caption row while
        // the other three edges stay put (the whole panel grows by the title
        // band, so edges are compared as insets from the panel, not absolutes).
        REQUIRE(host.setHudTitle("notices_hud", true));
        host.draw();
        const auto p2 = host.hudScreenEdges(PluginHost::HUD_NOTICES);
        const auto q2 = host.hudQuadRects(PluginHost::HUD_NOTICES);
        REQUIRE_MESSAGE(!q2.empty(), "notices drew nothing with a title");
        const PluginHost::QuadRect s2 = q2.back();
        const double p2l = p2.l / 1e6, p2t = p2.t / 1e6;
        const double p2r = p2.r / 1e6, p2b = p2.b / 1e6;
        INFO("titled panel [" << p2l << "," << p2t << " .. " << p2r << "," << p2b
             << "] slab [" << s2.l << "," << s2.t << " .. " << s2.r << "," << s2.b << "]");
        CHECK(std::abs(s2.l - p2l) < kSlop);
        CHECK(std::abs(s2.r - p2r) < kSlop);
        CHECK(std::abs(s2.b - p2b) < kSlop);
        // At least a padding of air above the slab for the caption to sit in.
        CHECK(s2.t - p2t > 0.004);
        // ...and the caption keeps its side INSET too, measured against Timing's
        // caption -- the panel the reports compared against. Freeing the slab by
        // zeroing the side padding moved THIS x to the panel's corner while
        // Timing's stayed put (the third report against this one flag: the
        // padding is spent at panelInnerLeft, which positions the caption too,
        // so the slab must reach the edge some other way -- see
        // resolvePanelSpec's negative content margins).
        REQUIRE(host.setHudTitle("timing_hud", true));   // its default is off
        host.draw();
        double capX = -1.0, timX = -1.0;
        for (const auto& row : host.hudStringRows(PluginHost::HUD_NOTICES))
            if (row.text == "Notices") capX = row.x;
        for (const auto& row : host.hudStringRows(PluginHost::HUD_TIMING))
            if (row.text == "Timing") timX = row.x;
        REQUIRE(capX >= 0.0);
        REQUIRE(timX >= 0.0);
        const auto tp = host.hudScreenEdges(PluginHost::HUD_TIMING);
        INFO("caption inset " << capX - p2l << " vs Timing's " << timX - tp.l / 1e6);
        CHECK(std::abs((capX - p2l) - (timX - tp.l / 1e6)) < 1e-4);
        CHECK(capX - p2l > 0.004);
        host.setHudTitle("notices_hud", false);
        host.setHudTitle("timing_hud", false);
    }

    for (float cardBorder : { 1.0f, 2.0f, 3.0f }) {
        // A distinct name per install -- getThemeByName returns the FIRST match.
        const std::string name = "slab" + std::to_string(static_cast<int>(cardBorder));
        host.installTheme(name.c_str(), /*inset=*/2.0f, cardBorder, /*titleBand=*/1,
                          /*card=*/1, /*cardSprites=*/true, /*buttonSprites=*/false);
        host.draw();
        const auto q = host.hudQuadRects(PluginHost::HUD_NOTICES);
        REQUIRE(q.size() >= 10);

        const PluginHost::QuadRect slab = q[q.size() - 1];        // one quad
        const PluginHost::QuadRect card = bbox(q, q.size() - 10, 9);  // the nine before it

        INFO("cardBorder " << cardBorder
             << " slab [" << slab.l << "," << slab.t << " .. " << slab.r << "," << slab.b << "]"
             << " card [" << card.l << "," << card.t << " .. " << card.r << "," << card.b << "]");
        // FLUSH ON ALL FOUR SIDES. Inside would leave the gap this change removed;
        // outside is the fixed-cell version that overshot and was rejected.
        CHECK(slab.l == doctest::Approx(card.l).epsilon(0.0005));
        CHECK(slab.r == doctest::Approx(card.r).epsilon(0.0005));
        CHECK(slab.t == doctest::Approx(card.t).epsilon(0.0005));
        CHECK(slab.b == doctest::Approx(card.b).epsilon(0.0005));

    }

    // ...and with the card SWITCHED OFF the block keeps the content row, because there
    // is no border to meet.
    //
    // ISOLATED AS A DIFFERENCE OF ONE FLAG, because the obvious comparison does not
    // work: measuring the card-off slab against a card-ON one compares two different
    // content heights (with a card the section's box is the card's, not the bare
    // row), so it moves for reasons that have nothing to do with the border. The
    // first version of this case did exactly that and passed against the bug.
    //
    // Both themes below draw NO card. They differ only in whether card ART EXISTS --
    // which is the whole distinction the border gating rests on: the theme's card
    // border is non-zero whenever the art is there, so ungated, the second slab
    // grows by a border that was never drawn and these two stop matching.
    auto slabHeight = [&](const char* name, bool cardSprites) {
        host.installTheme(name, /*inset=*/2.0f, /*cardBorder=*/2.0f, /*titleBand=*/1,
                          /*card=*/0, cardSprites, /*buttonSprites=*/false);
        host.draw();
        const auto qq = host.hudQuadRects(PluginHost::HUD_NOTICES);
        REQUIRE(qq.size() >= 1);
        return qq[qq.size() - 1].b - qq[qq.size() - 1].t;
    };
    const double noArt   = slabHeight("slabnoart", /*cardSprites=*/false);
    const double artOnly = slabHeight("slabartonly", /*cardSprites=*/true);
    INFO("card off -- slab height without card art " << noArt << ", with it " << artOnly);
    CHECK(artOnly == doctest::Approx(noArt).epsilon(0.0005));
}

// ============================================================================
// THE GAP BAR'S PANEL IS THE STACK'S WIDTH at the 50% default.
//
// It used to size its BAR to CenterStack::boxWidth while Notices, Timing and
// Version pass that number as their minPanelW -- so the gap bar's panel came out
// one padding wider per SIDE, because a bar is content and their box is a panel.
// Measured before the fix: 159500 against 137500 unthemed, 181500 against 148500
// themed, the difference being 2 * dim.paddingH each time. It was known and left
// alone as "this HUD's documented exception"; it is the one panel in the stack
// whose edges do not line up, which is what a user sees.
//
// Checked THEMED AND UNTHEMED because the two paddings differ between those
// states (that is why the two measurements above differ), so a fix that happened
// to work out in one could still be wrong in the other.
//
// EDGES, not just width, since the gap bar now anchors the way Notices and Timing
// do -- panel centred on 0.5 with its left edge on the lattice. It used to centre
// on its own offset instead and their left edges could differ by up to half a cell
// at the shipped defaults, which is what "does not line up" looks like on screen.
// ============================================================================
TEST_CASE("gap bar: at 50% its panel matches the rest of the centre stack") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\center_stack_width\\");
    REQUIRE_MESSAGE(host.hasScreenEdges(),
                    "MXBMRP3_Test_HudScreenEdges not exported (test build?)");
    host.showAllHuds(true);

    // Notices needs a live notice or it emits nothing and reads [0..0]; the
    // DEFAULT SETUP notice from runInit is persistent. Same reason as above.
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    auto compare = [&](const char* what) {
        host.draw();
        const auto gap = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
        const auto tim = host.hudScreenEdges(PluginHost::HUD_TIMING);
        const auto not_ = host.hudScreenEdges(PluginHost::HUD_NOTICES);
        REQUIRE_MESSAGE(gap.r > gap.l, "gap bar rendered nothing -- " << what);
        REQUIRE_MESSAGE(tim.r > tim.l, "timing rendered nothing -- " << what);
        REQUIRE_MESSAGE(not_.r > not_.l, "notices rendered nothing -- " << what);
        INFO(what << ": gapbar w=" << (gap.r - gap.l)
                  << " timing w=" << (tim.r - tim.l)
                  << " notices w=" << (not_.r - not_.l));
        CHECK(gap.r - gap.l == doctest::Approx(tim.r - tim.l).epsilon(0.005));
        CHECK(gap.r - gap.l == doctest::Approx(not_.r - not_.l).epsilon(0.005));
        // Same width AND the same lattice line: all three snap the panel's own
        // left edge, so at their shipped defaults the three panels share it.
        INFO(what << ": gapbar l=" << gap.l << " timing l=" << tim.l
                  << " notices l=" << not_.l);
        CHECK(gap.l == doctest::Approx(tim.l).epsilon(0.005));
        CHECK(gap.l == doctest::Approx(not_.l).epsilon(0.005));
    };

    compare("unthemed");
    if (host.hasThemeGeometry()) {
        host.installTheme("stackwidth", /*inset=*/2.0f, /*cardBorder=*/2.0f,
                          /*titleBand=*/1, /*card=*/1, /*cardSprites=*/true,
                          /*buttonSprites=*/false);
        compare("themed");
    }
}

// ============================================================================
// A HAND-LAID BUTTON ROW OWES THE JUNCTION GAP ABOVE IT.
//
// The box model splits the two terms: a JUNCTION belongs to the stack, a MARGIN
// belongs to the box. panel_box.h spends the junction itself for a PLANNED button
// row (`y += gapY` before the row's own margin), and the settings tabs spend it
// by hand as addSpacing(). The Version widget lays out its own row and spent only
// planButtonTerms().marginT -- the button BOX's margin, which defaults to zero --
// so its buttons sat flush against the message above them.
//
// Measured as the panel's HEIGHT rather than the button's position, because the
// gap has to reach both: the row origin AND the height the panel reserves. Adding
// it to only one is how the buttons would overflow their own card instead.
//
// The comparison is the SAME widget in its two states, one row versus two-plus-
// buttons, so it needs no second HUD to agree about padding: the difference
// between them is exactly one text row, one junction, and the button box.
// ============================================================================
TEST_CASE("version widget: its button row keeps the junction gap above it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\version_buttons\\");
    REQUIRE_MESSAGE(host.hasScreenEdges(),
                    "MXBMRP3_Test_HudScreenEdges not exported (test build?)");
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.draw();

    const auto plain = host.hudScreenEdges(PluginHost::HUD_VERSION);
    const double plainH = plain.b - plain.t;
    REQUIRE_MESSAGE(plainH > 0, "version widget rendered nothing in its plain state");

    host.updateSetAvailable("9.9.9");
    host.draw();
    const auto withButtons = host.hudScreenEdges(PluginHost::HUD_VERSION);
    const double buttonsH = withButtons.b - withButtons.t;
    REQUIRE_MESSAGE(buttonsH > 0, "version widget rendered nothing with an update available");

    INFO("plain height " << plainH << ", with buttons " << buttonsH
         << " (grew " << (buttonsH - plainH) << ")");
    CHECK(buttonsH > plainH);

    // THE GAP ITSELF. Height alone cannot see it -- the button box grows the panel
    // whether or not the junction is spent, and the first version of this case
    // passed with the junction reverted for exactly that reason. So measure the two
    // DRAWN strings against each other: the message, and the label inside the first
    // button. The widget places the button row at message + one text row + junction
    // + the box's own margin, and the label a further inset down, so the distance
    // between the two is a row plus a junction at minimum -- and without the
    // junction it is a bare row.
    //
    // String-to-string on purpose: the widget stores its button BOUNDS before the
    // HUD offset while the strings carry it, so measuring a bound against a string
    // reads short by exactly the widget's screen position.
    const auto terms = host.versionRowTerms();
    REQUIRE_MESSAGE(terms.rowH > 0, "MXBMRP3_Test_VersionRowTerms not exported");
    REQUIRE_MESSAGE(terms.junction > 0,
                    "the [panel] junction gap resolved to zero -- nothing to assert");

    REQUIRE(host.hasStringRows());
    double messageY = -1.0, labelY = -1.0;
    for (const auto& r : host.hudStringRows(PluginHost::HUD_VERSION)) {
        if (r.text.find("9.9.9") != std::string::npos) messageY = r.y;
        if (r.text.find("View in Settings") != std::string::npos) labelY = r.y;
    }
    REQUIRE_MESSAGE(messageY >= 0, "the update message was not drawn");
    REQUIRE_MESSAGE(labelY >= 0, "the button label was not drawn");

    const double messageToLabel = labelY - messageY;
    INFO("message y " << messageY << ", button label y " << labelY
         << " (gap " << messageToLabel << ", one row " << terms.rowH
         << " + junction " << terms.junction << ")");
    CHECK(messageToLabel >= terms.rowH + terms.junction);
    // ...and not runaway: the row, the junction, and the box's own margin and top
    // inset, which no shipped theme sets beyond a row between them.
    CHECK(messageToLabel <= terms.rowH + terms.junction + terms.rowH);
}

// ============================================================================
// THE GAP BAR'S INTERIOR IS THE PANEL IT DREW, and its bounds are a pure
// function of its layout.
//
// Two regressions from the same commit, and the panel's outer size -- all the
// case above measures -- was right through both of them:
//
//  - The bar read its width back from PanelPlan::contentW(), which returned the
//    ASK. This HUD states a minimum PANEL width and asks for no content at all,
//    so the bar came back ZERO WIDE: the coloured fill, every rider marker
//    spread across it and the centred gap text all collapsed onto the panel's
//    left edge, inside a panel that still looked correct. Fixed in panel_box.h
//    (Geom::cols is the laid-out column) and pinned there too, in the unit
//    suite; this is the end-to-end half.
//
//  - The LAYOUT snapped its own left edge against the live m_fOffsetX, making
//    m_fBoundsLeft a function of the offset -- while base_hud.cpp's drag path
//    snaps `m_fBoundsLeft + newOffsetX` itself. Two snaps each reading the
//    other's output a frame late, and the bar jittered under the cursor for as
//    long as the user held it. A layout that ignores the offset moves by exactly
//    the offset delta, which is the assertion below and is what every other HUD
//    would satisfy trivially.
// ============================================================================
TEST_CASE("gap bar: its bar fills the panel, and its bounds ignore the offset") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\gapbar_interior\\");
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasStringRows());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.draw();

    const auto panel = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
    REQUIRE_MESSAGE(panel.r > panel.l, "gap bar rendered nothing");

    // The gap text is the bar's own centre, drawn CENTER-justified at
    // startX + barWidth/2. With a zero-wide bar it lands on startX -- the panel's
    // left edge plus one padding -- so its distance from the panel's centre is the
    // whole failure, and it is the one string this HUD draws at its defaults.
    // showAllHuds turns captions on, so skip the caption and take the value row.
    double barMid = -1.0;
    for (const auto& r : host.hudStringRows(PluginHost::HUD_GAPBAR)) {
        if (r.text.find("Gap Bar") == std::string::npos) barMid = r.x;
    }
    REQUIRE_MESSAGE(barMid >= 0.0, "gap bar drew no gap text");
    // ScreenEdges is fixed point (x 1e6); StringRow::x is screen units.
    const double panelL = panel.l / 1e6, panelR = panel.r / 1e6;
    const double panelMid = (panelL + panelR) * 0.5;
    INFO("panel [" << panelL << ", " << panelR << "] mid " << panelMid
         << ", gap text at " << barMid);
    // A tenth of the panel's width: far tighter than the failure (which put the
    // text a full half-width off) and loose enough not to pin the padding.
    CHECK(std::abs(barMid - panelMid) < (panelR - panelL) * 0.1);

    // ...and the panel translates with the offset instead of quantising against it.
    REQUIRE_MESSAGE(host.setHudOffset("gap_bar_hud", 0.5f, 0.02f),
                    "gap_bar_hud not found by texture base name");
    host.draw();
    const auto a = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
    const float kNudge = 0.037f;   // deliberately NOT a whole grid cell
    REQUIRE(host.setHudOffset("gap_bar_hud", 0.5f + kNudge, 0.02f));
    host.draw();
    const auto b = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
    const double moved = (b.l - a.l) / 1e6;
    INFO("left edge " << a.l << " -> " << b.l << " (moved " << moved
         << ") for an offset nudge of " << kNudge);
    // ABSOLUTE, not doctest::Approx: its epsilon is scaled by (1 + magnitude), so on
    // numbers this small a 1% tolerance is ~0.0104 -- four times the half-cell error
    // the self-snap produces, and the first version of this check passed against the
    // bug for exactly that reason. One grid cell is 0.0055 at the shipped metrics, so
    // a thousandth is well inside a half-cell quantisation and well outside float noise.
    CHECK(std::abs(moved - kNudge) < 0.001);
    CHECK(b.r - b.l == doctest::Approx(a.r - a.l).epsilon(0.001));

    host.shutdown();
}

// ============================================================================
// A MARKER LABEL STAYS INSIDE THE BAR.
//
// This panel's content row is ONE text row tall, so centring the icon in it and
// hanging the label underneath put the label half outside the panel it belongs
// to. The fix moves the ICON instead -- marker_label.h's blockCenterShift, whose
// arithmetic the unit suite pins -- so the icon and its label straddle the row's
// centre together. Deliberately not solved by making the bar taller: the bar's
// height is what lines it up with the rest of the centre stack.
//
// Measured against the DRAWN primitives rather than the shift: the bug was
// visible as ink outside a box, so that is what this asserts.
// ============================================================================
TEST_CASE("gap bar: a marker label stays inside the bar") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\gapbar_label_fit\\";
    host.startup(save);
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasScreenEdges());

    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.addEntry(22, "Bob");

    // The marker ICON is the smallest quad this panel draws -- everything else is
    // the panel, its card and (with a gap) the fill, all of which span it.
    double iconHeight = 0.0;
    auto iconMidY = [&]() {
        double best = 1e9, mid = -1.0;
        for (const auto& q : host.hudQuadRects(PluginHost::HUD_GAPBAR)) {
            const double area = (q.r - q.l) * (q.b - q.t);
            if (area > 0 && area < best) { best = area; mid = (q.t + q.b) * 0.5; iconHeight = q.b - q.t; }
        }
        return mid;
    };
    auto labelY = [&]() {
        for (const auto& r : host.hudStringRows(PluginHost::HUD_GAPBAR))
            if (r.text == "22") return r.y;
        return -1.0;
    };
    auto load = [&](const char* labelMode) {
        host.writeSettingsFile(save,
            "[Settings]\nversion=6\n\n[GapBarHud]\nvisible=1\nmarkerMode=2\nshowGapText=0\n"
            "labelMode=" + std::string(labelMode) + "\n");
        host.loadSettings(save);
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.20f },
                                 { .num = 22, .trackPos = 0.60f } });
        host.draw();
    };

    load("NONE");
    const double iconPlain = iconMidY();
    REQUIRE_MESSAGE(iconPlain > 0, "no marker icon quad found with labels off");
    REQUIRE_MESSAGE(labelY() < 0, "a label was drawn with labelMode NONE");

    load("RACE_NUM");
    const double iconLabelled = iconMidY();
    const double lab = labelY();
    REQUIRE_MESSAGE(lab >= 0, "no marker label drawn with labelMode RACE_NUM");

    const auto panel = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
    const double panelBot = panel.b / 1e6;
    INFO("icon mid " << iconPlain << " -> " << iconLabelled
         << ", label top " << lab << ", panel bottom " << panelBot);

    // The icon rides UP to make room (y grows downward)...
    CHECK(iconLabelled < iconPlain);

    // ...and the LABEL'S BOTTOM lands inside the panel, which is the thing a user
    // sees. A string's y is its TOP, so the bottom needs the font size -- and the
    // shift itself yields it: the icon moved by (gap + font) / 2, and the gap is a
    // known fraction of the icon's own half-height, so font falls out. Deriving it
    // beats asserting on the top alone, which passes UNFIXED here: measured, the
    // unshifted top sits at 0.0437 against a panel bottom of 0.0587, and it is the
    // ink below it that hangs out.
    const double halfSize = iconHeight * 0.5;
    const double gap = halfSize * 0.2;            // MarkerLabel::GAP_RATIO
    const double font = 2.0 * (iconPlain - iconLabelled) - gap;
    REQUIRE_MESSAGE(font > 0.0, "derived label font size came out non-positive");
    INFO("label spans " << lab << " .. " << (lab + font) << " (derived font " << font << ")");
    CHECK(lab + font <= panelBot);

    host.shutdown();
}

// ============================================================================
// The gap FILL spans exactly its half of the full-bleed bar. The fill is a
// gauge -- it stops where the gap ratio stops -- so the layout sweeps can never
// see its extremes; this drives the gap to both rails through the test seam
// (GapBarHud::testForceGap) and measures the quad. At full deflection the fill
// must run from the panel's own edge to its centre: a residual side padding
// (the regression reported from the in-game screenshot) shows up here as a
// fill that stops one cell short of the rail.
// ============================================================================
TEST_CASE("the gap fill reaches the panel's edge at full deflection, unthemed") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\center_stack_fill\\");
    REQUIRE(host.hasQuadRects());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    // A best-lap entry, so rebuildRenderData believes the gap (the fill is
    // gated on personalBest && cachedGapValid).
    host.classify(1, 0, { { .num = 4, .best = 108231, .laps = 2, .gap = 0 } });
    host.raceLap(1, 4, 0, 109500, /*best=*/1, /*split0=*/36200, /*split1=*/73000);
    host.raceLap(1, 4, 1, 108231, /*best=*/2, /*split0=*/35900, /*split1=*/72400);
    host.clearTheme();

    const auto panel = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
    const double pl = panel.l / 1e6, pr = panel.r / 1e6;
    const double mid = (pl + pr) / 2.0;
    const double half = (pr - pl) / 2.0;

    // The fill is the one quad half the panel wide; everything else is either
    // the full-width background/slab or a small marker.
    auto findFill = [&](double expectL, double expectR) {
        int found = 0; PluginHost::QuadRect fill{};
        for (const auto& q : host.hudQuadRects(PluginHost::HUD_GAPBAR)) {
            if (std::abs((q.r - q.l) - half) < 1e-4) { ++found; fill = q; }
        }
        INFO("fill [" << fill.l << ".." << fill.r << "] expected ["
             << expectL << ".." << expectR << "], " << found << " candidate(s)");
        CHECK(found == 1);
        CHECK(std::abs(fill.l - expectL) < 1e-4);
        CHECK(std::abs(fill.r - expectR) < 1e-4);
    };

    REQUIRE(host.gapBarForceGap(999999, true));   // behind, clamped to -1.0
    host.draw();
    findFill(pl, mid);      // red: the LEFT rail to the centre

    REQUIRE(host.gapBarForceGap(-999999, true));  // ahead, clamped to +1.0
    host.draw();
    findFill(mid, pr);      // green: the centre to the RIGHT rail

    host.shutdown();
}

// ---------------------------------------------------------------------------
// ALL FOUR STACK MEMBERS SHARE A WIDTH, AT ANY [Advanced] AIR TERMS.
//
// The three panels are pinned above; VERSION is the one this case exists for. It
// was the stack's odd member in two ways at once, and both were invisible against
// a dark backdrop and obvious on graph paper:
//
//   * it stated its TEXT WIDTH alongside the stack minimum, and a stated content
//     width carries the padding past that minimum -- so at the shipped terms it
//     matched (the string is narrower than the interior) and at fat padding it
//     came out 78px wider than its neighbours, 380 against 302;
//   * it anchored at a bare -w/2 with no snap, sitting ~4px off the lattice its
//     neighbours land on.
//
// Both are now one shared contract (BaseHud::wantCenterStackWidth /
// centerStackLeftX), so the assertion is over the SWEEP rather than the shipped
// default: a rule that only holds at one padding is the bug this replaces.
//
// EDGES, not widths: two panels of equal width sitting 4px apart is precisely the
// failure the old code shipped, and comparing widths alone would pass it.
// ---------------------------------------------------------------------------
TEST_CASE("the centre stack shares its edges at any air terms, Version included") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\stack_width_sweep\\");
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasBoxTerms());
    REQUIRE(host.hasThemeGeometry());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    struct Terms { const char* what; PluginHost::BoxTermId id; const char* value; };
    const Terms sweep[] = {
        { "shipped defaults",      PluginHost::BOX_PANEL_PADDING,  "1"       },
        { "no air at all",         PluginHost::BOX_PANEL_PADDING,  "0"       },
        { "fat panel padding",     PluginHost::BOX_PANEL_PADDING,  "4 6"     },
        { "fat content padding",   PluginHost::BOX_CONTENT_PADDING,"3"       },
        { "asymmetric content",    PluginHost::BOX_CONTENT_MARGIN, "2 3 4 1" },
    };

    for (int themed = 0; themed <= 1; ++themed) {
        if (themed) host.installTheme("sweep", FRAME_INSET, CARD_INSET, 1, 1);
        else        host.clearTheme();
        for (const Terms& t : sweep) {
            // One term at a time, from a clean base: a uniform sweep compares sums
            // and hides a member that spends a term on the wrong side.
            host.setBoxTerm(PluginHost::BOX_PANEL_PADDING, "1");
            host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "0");
            host.setBoxTerm(PluginHost::BOX_CONTENT_MARGIN, "0");
            host.setBoxTerm(t.id, t.value);
            host.draw();
            host.draw();

            const auto g = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
            const auto n = host.hudScreenEdges(PluginHost::HUD_NOTICES);
            const auto ti = host.hudScreenEdges(PluginHost::HUD_TIMING);
            const auto v = host.hudScreenEdges(PluginHost::HUD_VERSION);
            CAPTURE(std::string(t.what) + (themed ? " (themed)" : " (unthemed)"));
            INFO("gapbar [" << g.l << ".." << g.r << "] notices [" << n.l << ".." << n.r
                 << "] timing [" << ti.l << ".." << ti.r << "] version [" << v.l
                 << ".." << v.r << "]");
            // Drawn at all -- a panel that renders nothing has equal edges of zero,
            // which would satisfy every check below having measured nothing.
            REQUIRE(g.r > g.l);
            REQUIRE(v.r > v.l);
            for (const auto& p : { n, ti, v }) {
                CHECK(std::abs(p.l - g.l) <= SLOP);
                CHECK(std::abs(p.r - g.r) <= SLOP);
            }
        }
    }
    host.setBoxTerm(PluginHost::BOX_PANEL_PADDING, "1");
    host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "0");
    host.setBoxTerm(PluginHost::BOX_CONTENT_MARGIN, "0");
    host.clearTheme();
}

// ---------------------------------------------------------------------------
// THE CENTRE STACK STAYS CENTRED WHEN ITS MEMBERS CHANGE WIDTH.
//
// The five are CENTRE-ANCHORED: offsetX is where the panel's centre sits, not its
// left edge, because they live mid-screen where a width change has to grow both
// ways. Two things change their width without anyone dragging them -- the scale
// setting, and (for the Gap Bar) its own width percentage -- and under either the
// stored centre must not move.
//
// The case asserts BOTH halves, because they fail independently: that the panel is
// actually centred at its default, and that it stays put as its width changes. Every
// bug this file has caught passed one and failed the other.
//
// THIS CAUGHT THREE REAL BUGS, one per mechanism the anchor replaced.
//
// (1) The Gap Bar overrode setScale to call setScaleKeepingCenter, which shifts the
// offset to hold the visual centre. That is correct for a LEFT-anchored panel, and
// double-compensation for a centre-anchored one: the layout already recentres on the
// new width, so the extra shift walked the stored centre sideways by half the growth
// on every scale step. It only reproduced through a RUNTIME scale change (an INI load
// sets the field before layout), which is why MXBMRP3_Test_SetHudScale exists and why
// this case drives the settings-click path rather than the loader.
//
// (2) The shared anchor helper SNAPPED the resulting left edge to the grid. Snapping
// an edge and holding a centre are different quantizations and a panel cannot do both:
// the centre walked by up to a cell on every width change, and the stack's measured
// centre was 0.49775 rather than 0.5. Snapping belongs to the drag path.
//
// (3) The Radar's default was a frozen 0.43275f, "horizontally centered at scale 1.0",
// which centred its CONTENT width and ignored the rounding fitPanelToGrid applies to
// the drawn box -- so it was never quite centred even at the one scale it named, and
// setScaleKeepingCenter rewrote the stored offset to keep up at every other.
//
// Measured as the CENTRE, not an edge: a panel that grew symmetrically and one
// that walked left while growing right both change their left edge, and only the
// centre tells them apart.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// AN UNKNOWN PANEL NAME RESOLVES TO NOTHING -- not to somebody else's panel.
//
// The geometry hooks used to take an integer id through a 21-case switch whose
// `default:` arm returned THE G-FORCE WIDGET. An id nobody had added a case for did
// not fail: it handed back a real panel's geometry, and a test asserting against it
// passed while measuring the wrong thing. Twenty-three of the forty-four registered
// elements had no id, so "ask for a panel that isn't in the table" was not a hypothetical.
//
// The name lookup cannot do that, and this is the case that says so. It is also a
// live check that the harness's HudId -> name table has not drifted from the
// registration names: every id below must resolve to a panel that DRAWS.
// ---------------------------------------------------------------------------
TEST_CASE("an unknown panel name measures nothing, and every named id resolves") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hud_name_lookup\\");
    REQUIRE(host.hasScreenEdges());
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.draw();

    // A real panel, to show the reader the measurement works at all.
    const auto real = host.hudScreenEdges("standings_hud");
    REQUIRE(real.r > real.l);

    // Names nothing answers to. Under the old switch the middle one -- a plausible
    // out-of-range id -- returned the G-force widget's rect.
    for (const char* bogus : { "no_such_hud", "gforce", "standings", "" }) {
        const auto e = host.hudScreenEdges(bogus);
        INFO("unknown name \"" << bogus << "\" -> [" << e.l << ".." << e.r << "]");
        CHECK(e.l == 0); CHECK(e.t == 0); CHECK(e.r == 0); CHECK(e.b == 0);
        CHECK(host.hudQuadRects(bogus).empty());
        CHECK(host.hudStringRows(bogus).empty());
    }

    // ...and every name the harness's HudId table hands out is a name something is
    // actually REGISTERED under, so a rename in HudManager::initialize() breaks here
    // rather than silently measuring nothing somewhere else.
    //
    // Checked against the registration sweep, NOT by measuring the panel: whether a
    // panel draws depends on the scene (the settings panel is closed, the map has no
    // track), and "did not draw" and "no such name" both read as a zero rect. Asking
    // the sweep separates them -- it lists what is registered regardless of what drew.
    // The first version of this case asserted a non-zero rect and failed on exactly
    // those two, which is the confusion worth not repeating.
    REQUIRE(host.hasPanelSweep());
    std::vector<std::string> registered;
    for (const auto& p : host.panelCells()) registered.push_back(p.name);
    REQUIRE(registered.size() > 20);

    auto isRegistered = [&](const std::string& n) {
        return std::find(registered.begin(), registered.end(), n) != registered.end();
    };
    const PluginHost::HudId ids[] = {
        PluginHost::HUD_STANDINGS, PluginHost::HUD_GFORCE, PluginHost::HUD_TIMING,
        PluginHost::HUD_PERFORMANCE, PluginHost::HUD_SESSION_CHARTS,
        PluginHost::HUD_SETTINGS, PluginHost::HUD_GAPBAR, PluginHost::HUD_NOTICES,
        PluginHost::HUD_RECORDS, PluginHost::HUD_LAP, PluginHost::HUD_POSITION,
        PluginHost::HUD_MAP, PluginHost::HUD_SESSION, PluginHost::HUD_VERSION,
        PluginHost::HUD_RADAR, PluginHost::HUD_SPEED, PluginHost::HUD_GEAR,
        PluginHost::HUD_CRASH, PluginHost::HUD_BARS, PluginHost::HUD_COMPASS,
        PluginHost::HUD_LEAN,
    };
    for (PluginHost::HudId id : ids) {
        const std::string nm = PluginHost::hudName(id);
        INFO("HudId " << static_cast<int>(id) << " -> " << nm);
        REQUIRE(!nm.empty());
        CHECK(isRegistered(nm));
    }
    // The negative half, so the check above cannot pass by matching everything.
    CHECK_FALSE(isRegistered("no_such_hud"));
    CHECK_FALSE(isRegistered("standings"));      // the old ICON-name vocabulary
}

TEST_CASE("the centre stack holds its centre under scale and width changes") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\stack_centre_hold\\");
    REQUIRE(host.hasScreenEdges());
    REQUIRE_MESSAGE(host.setHudScale("timing_hud", 1.0f),
                    "MXBMRP3_Test_SetHudScale not exported (test build?)");
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    struct Member { const char* label; PluginHost::HudId id; };
    const Member members[] = {
        { "gap_bar_hud",    PluginHost::HUD_GAPBAR  },
        { "notices_hud",    PluginHost::HUD_NOTICES },
        { "timing_hud",     PluginHost::HUD_TIMING  },
        { "version_widget", PluginHost::HUD_VERSION },
        // Not a member of the centre STACK -- it sits on its own and is not the same
        // width -- but it is centre-ANCHORED, which is what this case is about. It
        // came last and by a different route: its default was a frozen 0.43275f
        // ("horizontally centered at scale 1.0"), kept looking centred at other scales
        // by setScaleKeepingCenter rewriting the stored offset on every scale change.
        { "radar_hud",      PluginHost::HUD_RADAR   },
    };
    auto centre = [&](PluginHost::HudId id) {
        const auto e = host.hudScreenEdges(id);
        REQUIRE(e.r > e.l);                 // drawn at all
        return (e.l + e.r) / 2.0;
    };

    // AND THEY ARE ACTUALLY CENTRED, at the shipped defaults. "Holds its centre" is
    // satisfied by a panel that never moves off a WRONG centre, which is exactly what
    // shipped: the radar's default was a frozen 0.43275f that centred its content
    // width and ignored the grid rounding of the drawn box, and the stack measured
    // 0.49775 because its shared helper snapped the left edge. Both passed a
    // drift-only check. One cell of tolerance, because fitPanelToGrid rounds a drawn
    // box UP to whole cells and a panel is centred to within half of that rounding.
    REQUIRE(host.hasLayoutCells());
    const double cellW = host.layoutCells().cellW;
    for (const Member& m : members) {
        REQUIRE(host.setHudScale(m.label, 1.0f));
        host.draw();
        const auto e0 = host.hudScreenEdges(m.id);
        const double off = (e0.l + e0.r) / 2.0 / 1e6 - 0.5;
        INFO(std::string(m.label) << " centre " << ((e0.l + e0.r) / 2.0)
             << ", off centre by " << off << " (one cell = " << cellW << ")");
        CHECK(std::abs(off) <= cellW * 0.5);
    }

    for (const Member& m : members) {
        REQUIRE(host.setHudScale(m.label, 1.0f));
        host.draw();
        const double base = centre(m.id);
        const int baseW = host.hudScreenEdges(m.id).r - host.hudScreenEdges(m.id).l;

        for (float scale : { 1.35f, 0.75f, 1.0f }) {
            REQUIRE_MESSAGE(host.setHudScale(m.label, scale),
                            "no panel labelled " << m.label);
            host.draw();
            const auto e = host.hudScreenEdges(m.id);
            INFO(std::string(m.label) << " at scale " << scale << ": ["
                 << e.l << ".." << e.r << "] centre " << ((e.l + e.r) / 2.0)
                 << " against " << base);
            CHECK(std::abs((e.l + e.r) / 2.0 - base) <= SLOP);
        }
        // ...and the sweep was not vacuous: the panel really did change width.
        REQUIRE(host.setHudScale(m.label, 1.35f));
        host.draw();
        const int wideW = host.hudScreenEdges(m.id).r - host.hudScreenEdges(m.id).l;
        INFO(std::string(m.label) << " width " << baseW << " -> " << wideW);
        CHECK(wideW > baseW);
        REQUIRE(host.setHudScale(m.label, 1.0f));
        host.draw();
    }

    // The Gap Bar's OWN width setting, the second way it changes width -- and the
    // one a scale sweep cannot reach. REQUIRE rather than `if (hook)`: a missing
    // hook would silently drop half of what this case exists to check.
    {
        REQUIRE_MESSAGE(host.gapBarWidth(50),
                        "MXBMRP3_Test_GapBarWidth not exported (test build?)");
        host.draw();
        const double base = centre(PluginHost::HUD_GAPBAR);
        const int baseW = host.hudScreenEdges(PluginHost::HUD_GAPBAR).r
                        - host.hudScreenEdges(PluginHost::HUD_GAPBAR).l;
        for (int pct : { 80, 30, 50 }) {
            host.gapBarWidth(pct);
            host.draw();
            const auto e = host.hudScreenEdges(PluginHost::HUD_GAPBAR);
            INFO("gap bar at " << pct << "%: [" << e.l << ".." << e.r << "] centre "
                 << ((e.l + e.r) / 2.0) << " against " << base);
            CHECK(std::abs((e.l + e.r) / 2.0 - base) <= SLOP);
        }
        host.gapBarWidth(80);
        host.draw();
        CHECK(host.hudScreenEdges(PluginHost::HUD_GAPBAR).r
            - host.hudScreenEdges(PluginHost::HUD_GAPBAR).l > baseW);
        host.gapBarWidth(50);
        host.draw();
    }
}
