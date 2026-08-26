// ============================================================================
// tests/integration/tests/standings_row_band_test.cpp
// A FULL-ROW BAND SITS INSIDE ITS CARD, at any amount of theme air.
//
// THE BUG, reported from a screenshot: "the highlight grows outside the content of
// the card way beyond where the text is". A band's span was derived as the frame's
// border plus the card's -- `contentRowInsetX()`, true when it was written and false
// once [panel] padding started acting on a plan panel's card. The card moved inward
// by the padding; the band did not, so it drew OUTSIDE the card at both ends.
// Measured at [panel] padding 3: band 61..414 against a card at 81..394.
//
// It is the same shape as the two settings-panel faults this branch already fixed --
// an expression that predates a term becoming live, agreeing with itself forever --
// and it had eight copies, one per call site across three HUDs. They ask the plan for
// the content column now.
//
// SO THIS ASSERTS THE RELATIONSHIP, not the arithmetic. The band's span is REPORTED
// by the HUD (MXBMRP3_Test_StandingsRowBand) rather than recomputed here: a test that
// derives the expected span agrees with whichever derivation it copied, which is
// exactly how the fault survived. What is checked is that the reported band lies
// inside the reported CARD (MXBMRP3_Test_HudFillCut's section covers, which carry the
// card's real rect), that it stays a BAND rather than collapsing (containment is free
// for a zero-width one), and that the clearance between card and band GROWS with the
// air inside the card -- the number the fault had inverted to negative.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace {

// The widest section card Standings drew -- its body card. covers[0] is the title
// band; the rest are the section cards, and a plan panel records their real rects
// (unlike the legacy whole-body card, which is recorded at the frame's extents).
bool bodyCard(PluginHost& host, double& l, double& r) {
    const PluginHost::FillCut cut = host.hudFillCut(PluginHost::HUD_STANDINGS);
    if (!cut.themed || cut.covers.size() < 2u) return false;
    l = 1e9; r = -1e9;
    for (size_t i = 1; i < cut.covers.size(); ++i) {
        l = (std::min)(l, cut.covers[i].l);
        r = (std::max)(r, cut.covers[i].r);
    }
    return r > l;
}

}  // namespace

TEST_CASE("the standings row band stays inside its card at any padding") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\row_band\\");
    REQUIRE_MESSAGE(host.hasRowBand(),
                    "MXBMRP3_Test_StandingsRowBand not exported (test build?)");
    REQUIRE(host.hasFillCut());
    REQUIRE(host.hasBoxTerms());

    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    // The PLAYER's own entry, because the band this pins is the player-row highlight.
    host.addEntry(4, "Thomas");
    for (int i = 5; i <= 9; ++i)
        host.addEntry(i, ("Rider " + std::to_string(i)).c_str());
    host.classify(6, 120000, {
        { .num = 4, .best = 90000, .gap = 0 },
        { .num = 5, .best = 90500, .gap = 500 },
        { .num = 6, .best = 91000, .gap = 1000 },
    });

    host.installTheme("band", /*frameBorder=*/2.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/1, /*contentCard=*/1, /*cardSprites=*/1);

    // SWEPT OVER BOTH terms that put air between the panel's edge and its text:
    // [panel] padding, which moves the card and used not to move the band, and
    // [content] padding, which is air inside the card. Either one alone leaves the
    // other's contribution unmeasured, and it was their SUM that pushed the band out.
    for (const char* panelPad : { "0", "2", "4" }) {
        for (const char* contentPad : { "0", "2" }) {
            const std::string cfg = std::string("[panel] ") + panelPad
                                  + ", [content] " + contentPad;
            host.setBoxTerm(PluginHost::BOX_PANEL_PADDING, panelPad);
            host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, contentPad);
            // Twice: a box term does not dirty the HUDs that read it, so the first
            // frame after it lands can measure the previous configuration.
            host.draw();
            host.draw();

            const PluginHost::RowBand band = host.standingsRowBand();
            REQUIRE_MESSAGE(band.ok, cfg << ": Standings drew no player-row band");
            double cardL = 0.0, cardR = 0.0;
            REQUIRE_MESSAGE(bodyCard(host, cardL, cardR),
                            cfg << ": Standings drew no section card");

            INFO(cfg << ": band " << band.x << ".." << (band.x + band.w)
                     << ", card " << cardL << ".." << cardR);
            CHECK(band.x >= cardL);
            CHECK(band.x + band.w <= cardR);
            // ...and it is a band, not a sliver: containment is free for a zero-width
            // one, and a collapsed highlight is its own bug.
            CHECK(band.w > (cardR - cardL) * 0.5);

        }
    }

    // THE CLEARANCE GROWS, and the band does not move. [content] padding is air inside
    // the card, and in this model it grows the CARD around a fixed content ask rather
    // than squeezing the column -- so the band keeps its width and the card's edges
    // pull away from it. That is the number the fault inverted: the clearance was
    // NEGATIVE, which is what "the highlight grows outside the card" means.
    //
    // (Written first as "the band narrows", which is the other model and is wrong
    // here. It failed by measuring the band identical at padding 0 and 3 -- correct
    // behaviour reported as a failure, which is the useful half of asserting a
    // relationship you had to look up.)
    auto clearance = [&]() {
        host.draw();
        host.draw();
        const PluginHost::RowBand b = host.standingsRowBand();
        double l = 0.0, r = 0.0;
        REQUIRE(bodyCard(host, l, r));
        REQUIRE(b.ok);
        return std::make_pair((b.x - l) + (r - (b.x + b.w)), b.w);
    };
    host.setBoxTerm(PluginHost::BOX_PANEL_PADDING, "0");
    host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "0");
    const auto tight = clearance();
    host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "3");
    const auto padded = clearance();
    INFO("card-to-band clearance " << tight.first << " at [content] padding 0, "
         << padded.first << " at 3 (band width " << tight.second << " -> "
         << padded.second << ")");
    CHECK(padded.first > tight.first);
    CHECK(std::abs(padded.second - tight.second) < 1e-6);

    host.clearTheme();
}
