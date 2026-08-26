// ============================================================================
// tests/integration/tests/settings_surface_test.cpp
// THE SETTINGS PANEL'S CHROME, MEASURED AGAINST A HUD'S -- the two questions no
// existing case could ask, because every one of them measures ONE panel against
// ITSELF.
//
// Both bugs this pins were reported from a screenshot, and both were invisible to
// the suite for the same reason: the settings panel is internally consistent. Its
// band spans what it means to span, its cards sit where it puts them, its caption
// is centred in its own band. What was wrong was the RELATIONSHIP -- band to card,
// and settings panel to the HUD sitting next to it on the same screen.
//
//   1. THE BAND OVERHUNG THE PANEL'S OWN CARDS. emitTitleBand spans the frame's
//      inner boundary, like every other panel's; the settings panel insets its
//      sidebar and section cards by [panel] padding ON TOP of that border, so past
//      padding 0 its surfaces sit inside the band and it pokes out at both ends.
//      Reported as "the card of the settings title is wider than the others", then
//      as "holes at the sides of the settings title".
//
//   2. ITS BAND WAS A DIFFERENT HEIGHT FROM A HUD'S. Same theme, same screen, two
//      headers that do not line up. Reported as "the height of the settings title
//      does not line up with the height of the hud title to the left of it".
//
// MEASURED THROUGH MXBMRP3_Test_HudFillCut, which reports exactly the rects
// finalizeThemedFill cut against -- the band at the frame's centre extents, each
// section card at its own. So this asks the screenshot's question of the real
// geometry rather than of a re-derivation of it.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr float FRAME_INSET = 2.0f;
constexpr float CARD_INSET  = 1.0f;

// A fifth of a pixel at 1080p on either axis -- below what a screenshot can show
// and far below the half-cell quantisation the panel snaps to.
constexpr double EPS = 1e-4;

// A closed circular track, the same shape map_render_test's fixture builds. Here
// only so MapHud draws AT ALL: it returns early without track data, and a panel
// that drew nothing reports no themed centre rather than a wrong number.
std::vector<TrackSegmentRow> circleTrack(int segs = 64, float trackLen = 1600.0f) {
    std::vector<TrackSegmentRow> v(segs);
    const float radius = trackLen / (2.0f * 3.14159265f);
    for (int i = 0; i < segs; ++i) {
        v[i].type = 1;                 // curve
        v[i].length = trackLen / segs;
        v[i].radius = radius;
        v[i].angle = 0.0f;
    }
    return v;
}

// Bring every panel up with the same theme and the same box terms, so a
// difference between them is the panels' and not the configuration's.
void stage(PluginHost& host, const char* dir, const char* panelPadding) {
    host.startup(dir);
    REQUIRE(host.hasThemeGeometry());
    REQUIRE_MESSAGE(host.hasFillCut(),
                    "MXBMRP3_Test_HudFillCut not exported (test build?)");
    REQUIRE_MESSAGE(host.hasBoxTerms(),
                    "MXBMRP3_Test_SetBoxTerm not exported (test build?)");
    host.setBoxTerm(PluginHost::BOX_PANEL_PADDING, panelPadding);
    host.installTheme("surf", FRAME_INSET, CARD_INSET, /*titleBand=*/1, /*card=*/1);
    host.showAllHuds(true);
    host.showSettings(true);
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");
    host.trackCenterline(circleTrack(), { 800.0f, 400.0f, 1200.0f, 0.0f });
    // Map ships with its caption OFF, so it draws no band and its first recorded
    // cover is its body card -- which is how this case first read a 43%-of-screen
    // "band". Turned on explicitly rather than worked around: the whole point of
    // driving Map is that it is the legacy caption chain's representative.
    REQUIRE_MESSAGE(host.setHudTitle("map_hud", true), "no HUD labelled hud-map");
    host.draw();
}

}  // namespace

// ---------------------------------------------------------------------------
// NO SURFACE OVERHANGS ANY OTHER.
//
// SWEPT OVER [panel] padding because padding 0 is exactly where the bug hides: the
// band's line and the cards' line are the same number there, and every shipped
// theme at the time set 0. One value would have passed against the fault.
//
// Asserted as an EQUALITY at both ends, not as "the cards fit inside the band".
// Containment is what the fix would look like if the cards were pulled IN to match
// -- which was tried, and puts the same strip of bare panel outside the band
// instead, where it reads as a notch at each end. The surfaces have to sit on ONE
// line, so the outermost card must reach the band exactly.
TEST_CASE("every settings surface sits on the line its band sits on") {
    for (const char* padding : { "0", "1", "2", "4" }) {
        const std::string pad(padding);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        stage(host, "Z:\\tmp\\mxbmrp3-tests\\settings_surface\\", padding);

        const PluginHost::FillCut cut = host.hudFillCut(PluginHost::HUD_SETTINGS);
        REQUIRE_MESSAGE(cut.themed, "settings panel drew no themed centre");
        REQUIRE_MESSAGE(cut.covers.size() >= 3u,
                        "expected the band, the sidebar card and a section card");
        // The band is the first cover finalizeThemedFill records; asserted rather
        // than assumed, so a reshuffled cover list fails here instead of quietly
        // measuring a card.
        const PluginHost::QuadRect band = cut.covers.front();
        for (size_t i = 1; i < cut.covers.size(); ++i)
            REQUIRE_MESSAGE(cut.covers[i].t >= band.t,
                            "cover[0] is not the topmost rect -- not the band?");

        double leftmost = 1e9, rightmost = -1e9;
        for (size_t i = 1; i < cut.covers.size(); ++i) {
            leftmost  = (std::min)(leftmost,  cut.covers[i].l);
            rightmost = (std::max)(rightmost, cut.covers[i].r);
        }
        INFO("[panel] padding = " << pad << ": band spans " << band.l << ".." << band.r
             << ", cards span " << leftmost << ".." << rightmost);
        CHECK(std::abs(leftmost  - band.l) < EPS);
        CHECK(std::abs(rightmost - band.r) < EPS);

        host.showSettings(false);
        host.clearTheme();
    }
}

// ---------------------------------------------------------------------------
// THE PANEL RESPECTS [panel] PADDING, MEASURED AGAINST A HUD THAT ALREADY DOES.
//
// The case above only says the settings panel's own surfaces agree with each other,
// which is satisfied perfectly by all three sitting on the frame and ignoring the
// padding entirely -- and that is exactly what shipped after it was written.
// Reported from a screenshot at `[panel] padding = 3`: the HUD beside the menu held
// its card three cells inside its frame, the menu held nothing.
//
// So this measures the DISTANCE from each panel's own background edge to its own
// band, and requires the two to be the same number. Standings is a plan panel, where
// PanelBox puts the band at panelInnerLeft (border + padding) -- it is the behaviour
// being asked for, so it is the reference rather than a second copy of the
// arithmetic.
//
// [title] margin is pinned to 0 for the comparison. A plan panel adds the title
// box's own margin to that inset and the settings panel does not; with both at the
// shipped 0 they agree, and pinning it here says which term is NOT being compared
// instead of leaving it to a default that could move.
TEST_CASE("the settings panel insets its band by [panel] padding, like a HUD") {
    for (const char* padding : { "0", "1", "3" }) {
        const std::string pad(padding);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        stage(host, "Z:\\tmp\\mxbmrp3-tests\\settings_padding\\", padding);
        host.setBoxTerm(PluginHost::BOX_TITLE_MARGIN, "0");
        host.draw();
        host.draw();
        REQUIRE(host.hasScreenEdges());

        auto bandInset = [&](PluginHost::HudId id) {
            const PluginHost::FillCut cut = host.hudFillCut(id);
            REQUIRE(cut.themed);
            REQUIRE(cut.covers.size() >= 2u);
            const double panelLeft = host.hudScreenEdges(id).l / 1e6;
            return cut.covers.front().l - panelLeft;
        };
        const double settings = bandInset(PluginHost::HUD_SETTINGS);
        const double hud      = bandInset(PluginHost::HUD_STANDINGS);
        INFO("[panel] padding = " << pad << ": settings band inset " << settings
             << ", Standings band inset " << hud);
        CHECK(std::abs(settings - hud) < EPS);
    }
}

// ---------------------------------------------------------------------------
// ONE BAND HEIGHT ACROSS THE SURFACE.
//
// titleBandBoxHeight()'s own comment claimed this before it was true, and that is
// the reason to drive it rather than read it: there are TWO owners of a band's
// drawn height, not one. A plan panel's band is PanelBox's (titleTop..bandDrawnBot);
// the legacy chain's and the settings panel's is titleBandBoxHeight()'s. They spend
// the same terms -- border, caption cell, padding -- and differed by the one term
// only the plan had: the caption block's quantization remainder (titleSlack), which
// the plan stretches its band by and this chain used to drop. 0.009335 of screen
// height, ~10px at 1080p, on every themed screen with a HUD beside the menu.
//
// SWEPT OVER [title] padding because the remainder is a function of the advance:
// one padding value is one lattice phase, and a phase that happens to land on a
// whole cell hides the whole fault.
TEST_CASE("the settings caption band is a HUD's caption band, to the pixel") {
    // ALL THREE CAPTION CHAINS, because the fault was one chain's and the other two
    // agreed with each other: Standings composes through planPanel, Map is the legacy
    // addTitleString chain's representative (the same choice testHudById documents),
    // and the settings panel is its own third. A case driving only Standings against
    // the settings panel could not tell which of the two had moved.
    struct Panel { PluginHost::HudId id; const char* name; };
    const Panel panels[] = {
        { PluginHost::HUD_STANDINGS, "Standings (plan)" },
        { PluginHost::HUD_MAP,       "Map (legacy chain)" },
        { PluginHost::HUD_SETTINGS,  "settings panel" },
    };
    for (const char* titlePad : { "0", "1", "3" }) {
        const std::string pad(titlePad);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        stage(host, "Z:\\tmp\\mxbmrp3-tests\\settings_band_h\\", "2");
        host.setBoxTerm(PluginHost::BOX_TITLE_PADDING, titlePad);
        // TWICE. A box term does not dirty the HUDs that read it, so the first frame
        // after it lands rebuilds only the panels that were dirty for other reasons --
        // the settings panel every frame, a HUD not at all. One draw() measured a
        // stale Standings band and reported a difference that was a frame old.
        host.draw();
        host.draw();

        double first = -1.0;
        for (const Panel& p : panels) {
            const PluginHost::FillCut cut = host.hudFillCut(p.id);
            const std::string who(p.name);
            INFO("[title] padding = " << pad << ": " << who);
            REQUIRE_MESSAGE(cut.themed, "panel drew no themed centre");
            // THE BAND, and proved to be one: the hook records it first only when a
            // band was drawn, so a panel with its caption off silently offers its
            // BODY CARD in that slot -- which is how this first measured a "band"
            // 43% of the screen tall. A band sits above the card and ends where it
            // begins, so requiring a second cover below the first says so.
            REQUIRE_MESSAGE(cut.covers.size() >= 2u,
                            "expected a band and at least one card");
            REQUIRE_MESSAGE(cut.covers[0].b <= cut.covers[1].t + EPS,
                            "cover[0] is not above cover[1] -- not a band?");
            const double h = cut.covers.front().b - cut.covers.front().t;
            INFO("band " << h << " against " << first);
            CHECK(h > 0.0);
            if (first < 0.0) first = h;
            else CHECK(std::abs(h - first) < EPS);
        }

        host.showSettings(false);
        host.clearTheme();
    }
}

// ---------------------------------------------------------------------------
// [content] MARGIN REACHES THE SETTINGS PANEL ON BOTH AXES.
//
// It only ever reached one. contentGapCells() reads the VERTICAL pair as the seam
// between two section cards, so `[content] margin = 2` visibly spread the settings
// sections apart -- and the horizontal pair was read nowhere at all, so every card
// went on running flush to the panel's surface line. Reported as "[content] margin
// does not appear to apply to the content of the settings menu"; half of it did,
// which is why nothing here caught it: a sweep over the term moved the panel, and a
// case asserting only that the term does SOMETHING would have passed throughout.
//
// MEASURED AS THE STEP FROM THE BAND, which is the shape of the claim: the band is
// a title box and answers to [title] margin, the cards are content boxes and answer
// to [content] margin, so raising one steps the cards in from a band that stays put.
// That is what a plan panel does with the same two terms (PanelBox: titleLeft =
// panelInnerLeft + t.m.l, cardLeft = panelInnerLeft + c.m.l), and Standings is
// carried alongside as the reference rather than as a second copy of the arithmetic.
//
// [title] margin is pinned to 0 throughout, so the band's own line cannot move and
// the step is the content term alone.
TEST_CASE("[content] margin insets the settings cards, like a HUD's") {
    for (const char* margin : { "0", "1", "2" }) {
        const std::string m(margin);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        stage(host, "Z:\\tmp\\mxbmrp3-tests\\settings_cmargin\\", "1");
        host.setBoxTerm(PluginHost::BOX_TITLE_MARGIN, "0");
        host.setBoxTerm(PluginHost::BOX_CONTENT_MARGIN, margin);
        host.draw();
        host.draw();

        // The band, then the outermost card edge each side -- the same reading the
        // surfaces case takes, which is what makes the two comparable: at margin 0
        // this case's step is 0 and that case's equality is this one.
        auto step = [&](PluginHost::HudId id) {
            const PluginHost::FillCut cut = host.hudFillCut(id);
            REQUIRE(cut.themed);
            REQUIRE(cut.covers.size() >= 2u);
            const PluginHost::QuadRect band = cut.covers.front();
            double leftmost = 1e9, rightmost = -1e9;
            for (size_t i = 1; i < cut.covers.size(); ++i) {
                leftmost  = (std::min)(leftmost,  cut.covers[i].l);
                rightmost = (std::max)(rightmost, cut.covers[i].r);
            }
            return std::pair<double, double>(leftmost - band.l, band.r - rightmost);
        };
        const std::pair<double, double> s = step(PluginHost::HUD_SETTINGS);
        const std::pair<double, double> h = step(PluginHost::HUD_STANDINGS);
        INFO("[content] margin = " << m << ": settings steps in " << s.first
             << " / " << s.second << ", Standings " << h.first << " / " << h.second);
        CHECK(std::abs(s.first  - h.first)  < EPS);
        CHECK(std::abs(s.second - h.second) < EPS);
        // ...and the step is REAL at a nonzero margin, or the equality above is
        // satisfied by both panels ignoring the term together -- which is exactly
        // the state this case was written against.
        if (m != "0") {
            CHECK(s.first  > EPS);
            CHECK(s.second > EPS);
        }

        host.showSettings(false);
        host.clearTheme();
    }
}

// ---------------------------------------------------------------------------
// SWITCHING THE CAPTION BAND OFF SHRINKS ITS BLOCK BY THE BAND'S BORDER, AND BY
// NOTHING ELSE.
//
// The switch decides whether the band is DRAWN. Its whole geometric effect should
// therefore be the term it stops drawing -- the border -- with the caption's own
// air (padding inside the box, margin outside it) spent either way, because air
// owes a theme nothing.
//
// IT HAS BEEN WRONG IN BOTH DIRECTIONS, which is why this asserts a distance
// rather than a direction. The bandless branch first reserved a glyph ROW plus its
// air, which on this panel's scale came out twice the banded box -- "the titles
// appear to be taller with NO band than with them". Collapsing the two branches
// onto the banded box fixed that and reserved the border for a band nobody draws --
// "the border disappears, and the title moves, but it appears to reserve the height
// for it still". Either fault passes an equality-or-inequality check written for
// the other one; only the exact distance catches both.
//
// SWEPT OVER THE BAND'S OWN SIZE, because the claim is about that term: at border 0
// the two spellings agree whatever they are, and a case run only there says nothing.
TEST_CASE("switching the caption band off shrinks its block by the band's border") {
    // Panel top -> top of the first CARD: the caption block's whole advance, and
    // the same measurement whether or not a band was drawn (with a band the first
    // cover is the band and the card is second; without, the card is first).
    auto advance = [](PluginHost& host, PluginHost::HudId id, bool banded) {
        const PluginHost::FillCut cut = host.hudFillCut(id);
        REQUIRE(cut.themed);
        REQUIRE(cut.covers.size() >= (banded ? 2u : 1u));
        return cut.covers[banded ? 1 : 0].t - host.hudScreenEdges(id).t / 1e6;
    };
    double shrink[3] = { 0.0, 0.0, 0.0 };
    const float borders[3] = { 1.0f, 2.0f, 3.0f };
    double artV = 0.0, cellHv = 0.0;   // the live lattice, read off the first host
    for (int bi = 0; bi < 3; ++bi) {
        const float bandBorder = borders[bi];
        double adv[2] = { 0.0, 0.0 };
        for (int band = 1; band >= 0; --band) {
            PluginHost host(dllPath());
            REQUIRE(host.loaded());
            stage(host, band ? "Z:\\tmp\\mxbmrp3-tests\\settings_bandh1\\"
                             : "Z:\\tmp\\mxbmrp3-tests\\settings_bandh0\\", "0");
            if (artV == 0.0) {
                REQUIRE(host.hasLayoutCells());
                const auto lat = host.layoutCells();
                artV = lat.artV; cellHv = lat.cellH;
            }
            host.setBoxTerm(PluginHost::BOX_TITLE_MARGIN, "0");
            host.setBoxTerm(PluginHost::BOX_TITLE_PADDING, "1");   // air, spent either way
            host.setBoxTerm(PluginHost::BOX_CONTENT_MARGIN, "0");
            host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "0");
            host.installTheme("bandh", 0.0f, 1.0f, band, 1);
            REQUIRE(host.hasTitleBorder());
            REQUIRE(host.setThemeTitleBorder(bandBorder));
            host.draw();
            host.draw();
            adv[band ? 0 : 1] = advance(host, PluginHost::HUD_SETTINGS, band != 0);
            host.showSettings(false);
            host.clearTheme();
        }
        shrink[bi] = adv[0] - adv[1];
        INFO("band border " << bandBorder << " cells: block ON " << adv[0]
             << " OFF " << adv[1] << "  shrink " << shrink[bi]);
        CHECK(shrink[bi] > EPS);          // the term acts at all
    }
    // THE SHRINK IS THE BORDER, TWICE, pushed through the advance ceil. The raw
    // term is 2*border art cells (top + bottom of the caption box); the caption
    // block's advance is quantized to whole grid cells (titleSlack, panel_box.h),
    // so the measured shrink is ceil(adv) - ceil(adv - raw): a whole number of
    // grid cells, and one of the two cells bracketing the raw term. That is the
    // exact statement at ANY uiLineHeight -- the first version asserted equal
    // STEPS across the sweep instead, which held only at ratios where the
    // bracketing happens to move evenly (it did at the shipped 5:6, and broke at
    // 1.1 where the steps came out 2 cells then 1). Where the raw term lands on
    // the grid the bracket is a single value and this is an outright equality --
    // border 3 at the shipped ratio is exactly 5 cells, still pinned exactly.
    for (int bi = 0; bi < 3; ++bi) {
        const double raw = 2.0 * borders[bi] * artV;
        const double lo = std::floor(raw / cellHv + 1e-9) * cellHv;
        const double hi = std::ceil(raw / cellHv - 1e-9) * cellHv;
        const double cells = shrink[bi] / cellHv;
        INFO("band border " << borders[bi] << ": shrink " << shrink[bi] << " ("
             << cells << " cells) for a raw term of " << raw << " [" << lo
             << " .. " << hi << "]");
        CHECK(std::abs(cells - std::round(cells)) < 1e-3);
        CHECK(shrink[bi] > lo - EPS);
        CHECK(shrink[bi] < hi + EPS);
    }
}

// ---------------------------------------------------------------------------
// [content] BORDER CLEARS THE ROWS ON ALL FOUR SIDES, NOT JUST TWO.
//
// A card's clearance from its content is border + padding per side. The settings
// panel paid it horizontally -- contentOverhangL/R have always carried
// cardBorderOverhangX beside the pad -- and vertically it paid the padding alone,
// because cardPadTopY/BotY were the box term and nothing else. So raising
// `[content] border` grew a HUD's card AROUND its rows and drove the settings
// panel's card ART over its own text. Reported as "the content of huds and widgets
// grow, keeping the border away from the content, but in the settings menu the
// border starts overflowing the content, but just vertically".
//
// COMPARED AS A DELTA against a HUD, not as an absolute: the two panels have
// different rows and different fonts, so their clearances are not the same number
// and never should be. What must match is how much each one GROWS when the term
// does -- that is the term acting, and it is what a screenshot of one panel alone
// cannot tell you.
TEST_CASE("[content] border clears the settings rows vertically, like a HUD's") {
    // First card's top edge -> the first row of text inside it.
    auto clearance = [](PluginHost& host, PluginHost::HudId id) {
        const PluginHost::FillCut cut = host.hudFillCut(id);
        REQUIRE(cut.themed);
        REQUIRE(cut.covers.size() >= 2u);
        double cardTop = 1e9;
        for (size_t i = 1; i < cut.covers.size(); ++i)
            cardTop = (std::min)(cardTop, cut.covers[i].t);
        double firstRow = 1e9;
        for (const auto& r : host.hudStringRows(id))
            if (r.y > cardTop) firstRow = (std::min)(firstRow, r.y);
        REQUIRE(firstRow < 1e8);
        return firstRow - cardTop;
    };
    double settings[2] = { 0.0, 0.0 }, standings[2] = { 0.0, 0.0 };
    const float borders[2] = { 0.0f, 3.0f };
    for (int i = 0; i < 2; ++i) {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        stage(host, i == 0 ? "Z:\\tmp\\mxbmrp3-tests\\settings_cb0\\"
                           : "Z:\\tmp\\mxbmrp3-tests\\settings_cb3\\", "0");
        // A nonzero padding under it, so the case cannot pass by the border being
        // the only thing in the clearance.
        host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "1");
        host.installTheme("cardb", 0.0f, borders[i], 1, 1);
        host.draw();
        host.draw();
        settings[i]  = clearance(host, PluginHost::HUD_SETTINGS);
        standings[i] = clearance(host, PluginHost::HUD_STANDINGS);
        host.showSettings(false);
        host.clearTheme();
    }
    INFO("settings  clearance: border 0 " << settings[0]  << " -> 3 " << settings[1]);
    INFO("standings clearance: border 0 " << standings[0] << " -> 3 " << standings[1]);
    // The HUD grows -- stated first, so a build where the term did nothing anywhere
    // fails here rather than passing the comparison below with two zeroes.
    CHECK(standings[1] - standings[0] > EPS);
    CHECK(std::abs((settings[1] - settings[0]) - (standings[1] - standings[0])) < EPS);
}
