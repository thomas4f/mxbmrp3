// ============================================================================
// tests/integration/tests/settings_fit_test.cpp
// THE SETTINGS PANEL IS TALL ENOUGH FOR EVERY TAB, AND THE SAME HEIGHT ON ALL OF THEM.
//
// Those two together are the whole requirement, and they used to pull against each
// other. The panel was a fixed row budget (LayoutMetrics::settingsRows) established by
// hand -- render every tab, grep the log for "overflows the panel", adjust -- so it
// met the second and missed the first whenever a tab grew in between. That is the
// reported "the buttons and the Help & Community settings overlap on the General tab",
// and it was a player who found it. Sizing the panel to the ACTIVE tab meets the first
// and misses the second: Save, Close and Reset move every time you switch tabs.
//
// It measures its TALLEST tab now and draws every tab at that height, so both hold by
// construction. This drives the pair anyway, because "by construction" is a claim
// about code that has to keep being true:
//
//   1. NO TAB OVERFLOWS. The panel reports the overrun it would have warned about
//      (MXBMRP3_Test_SettingsOverflowRows), and with a measured height it should be
//      zero everywhere. A failure means the measure pass and the real lay-out
//      disagree about some tab -- a renderer that is not reproducible -- which is the
//      one way the split can be wrong rather than merely stale.
//   2. AND THE HEIGHT DOES NOT MOVE between tabs. Asserted on the panel's own edges,
//      because that is what a user sees move.
//
// DRIVEN AT SEVERAL THICKNESSES, because a theme's chrome is spent out of the same
// space: the section cards' padding, the seams between them and the footer's button
// box all grow with its terms.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

TEST_CASE("every settings tab fits the panel's row budget") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_fit\\");
    REQUIRE_MESSAGE(host.hasSettingsOverflow(),
                    "MXBMRP3_Test_SettingsOverflowRows not exported (test build?)");
    REQUIRE(host.hasThemeGeometry());

    // A populated session, because several tabs' row counts follow live state --
    // the Riders tab lists entries, and a tab measured on an empty grid is not the
    // tab a player is looking at.
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    for (int i = 1; i <= 8; ++i)
        host.addEntry(i, ("Rider " + std::to_string(i)).c_str());

    host.showSettings(true);
    const std::vector<std::string> tabs = host.settingsTabNames();
    REQUIRE_MESSAGE(tabs.size() > 5u,
                    "settings tabs not enumerable (MXBMRP3_Test_SettingsTabName absent?)");

    auto sweep = [&](const std::string& skin) {
        double height = -1.0;
        std::string tallestSeen;
        for (const std::string& tab : tabs) {
            host.setActiveTab(tab.c_str());
            host.draw();
            const double over = host.settingsOverflowRows();
            CHECK_MESSAGE(over <= 0.0,
                          skin << ": the " << tab << " tab overflows the panel by "
                               << over << " rows -- the measure pass and the real "
                               << "lay-out disagree about it, which means a tab "
                               << "renderer is not reproducible");
            const auto e = host.hudScreenEdges(PluginHost::HUD_SETTINGS);
            const double h = (e.b - e.t) / 1e6;
            if (height < 0.0) { height = h; tallestSeen = tab; }
            CHECK_MESSAGE(std::abs(h - height) < 1e-4,
                          skin << ": the panel is " << h << " tall on " << tab
                               << " and " << height << " on " << tallestSeen
                               << " -- switching tabs must not move the footer");
        }
    };

    sweep("unthemed");

    // THE SHIPPED GEOMETRY, term for term. Every shipped theme states the same box
    // (see any themes/*/*.ini). It used to differ from the [Advanced] built-ins the
    // sweep above ran at, on `[content] padding` -- the one term a tab pays PER
    // SECTION -- and that divergence was the reason this second sweep exists: a gate
    // driving only the built-ins would be silent about the configuration every player
    // actually has.
    //
    // The shipped themes now state 0 for that term too, so the two sweeps currently
    // agree on it. The block stays, and stays explicit, because the AGREEMENT is a
    // coincidence of today's values rather than a rule: the themes are data a skinner
    // edits, and the moment one states a padding again this is the only sweep that
    // would notice. Every other term here (frame border, card border, the band/card
    // toggles) still differs from the built-ins.
    REQUIRE(host.hasBoxTerms());
    host.installTheme("shipped", /*frameBorder=*/1.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/0, /*contentCard=*/1, /*cardSprites=*/1,
                      /*buttonSprites=*/1);
    host.setBoxTerm(PluginHost::BOX_PANEL_PADDING,   "0");
    host.setBoxTerm(PluginHost::BOX_PANEL_GAP,       "1");
    host.setBoxTerm(PluginHost::BOX_TITLE_PADDING,   "0");
    host.setBoxTerm(PluginHost::BOX_CONTENT_MARGIN,  "0");
    host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "0");   // as shipped; see above
    host.setBoxTerm(PluginHost::BOX_BUTTON_MARGIN,   "0");
    host.setBoxTerm(PluginHost::BOX_BUTTON_PADDING,  "0 1");
    sweep("shipped geometry");

    // ...and a 2-cell frame on top of it: headroom. No shipped theme asks for more
    // than 1, but a theme author picking 2 is doing nothing wrong, and the budget
    // has to hold for what a theme CAN ask rather than only for what ours do.
    host.installTheme("fit2", 2.0f, 1.0f, /*titleBand=*/0, /*contentCard=*/1, 1, 1);
    sweep("shipped geometry, frame 2");

    host.clearTheme();
    host.showSettings(false);
}

// ---------------------------------------------------------------------------
// A THEME'S AIR REACHES THE PANEL instead of being taken out of its content.
//
// `[content] padding` is the one term a tab pays PER SECTION: only the FIRST card's
// top pad is in the panel's height sum, so every other card's pads and every seam
// between them used to come out of a FIXED budget -- a tab with more sections paid
// more, and past a point it drew its last rows under the footer. Measured at the
// terms this was first reported on (`[panel] 3`, `[title] 1`, `[content] 2`): 8 of 28
// tabs overflowed, General worst at 4.9 rows.
//
// Now the same air makes the panel TALLER. That is the whole benefit of measuring
// over budgeting, and it is asserted as a pair -- the height grows AND nothing
// overflows -- because either alone is satisfiable by the old behaviour (a panel that
// clips grows nothing; a panel that grows unboundedly could still clip if the growth
// went somewhere else).
//
// NOT CLAMPED to the display, deliberately: 1080p is a conservative default and a
// theme that asks for more air than fits is the theme author's to see. Clamping would
// put the silent clipping back for exactly the themes that provoked it.
TEST_CASE("a padded theme grows the panel rather than overflowing it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_fit_pad\\");
    REQUIRE(host.hasSettingsOverflow());
    REQUIRE(host.hasBoxTerms());
    REQUIRE(host.hasScreenEdges());
    host.showAllHuds(true);
    host.showSettings(true);
    host.installTheme("padfit", 2.0f, 1.0f, 1, 1, 1, 1);

    // The worst overrun across every tab, and the panel's height, at one setting.
    auto measure = [&]() {
        double worst = -1e9, height = 0.0;
        for (const std::string& tab : host.settingsTabNames()) {
            host.setActiveTab(tab.c_str());
            host.draw();
            worst = (std::max)(worst, host.settingsOverflowRows());
            const auto e = host.hudScreenEdges(PluginHost::HUD_SETTINGS);
            height = (e.b - e.t) / 1e6;
        }
        return std::make_pair(worst, height);
    };

    host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "0");
    const auto thin = measure();
    host.setBoxTerm(PluginHost::BOX_PANEL_PADDING, "3");
    host.setBoxTerm(PluginHost::BOX_TITLE_PADDING, "1");
    host.setBoxTerm(PluginHost::BOX_CONTENT_PADDING, "2");
    const auto padded = measure();

    INFO("worst overrun " << thin.first << " -> " << padded.first
         << ", panel height " << thin.second << " -> " << padded.second);
    CHECK(padded.second > thin.second);   // the air reached the panel...
    CHECK(padded.first <= 0.0);           // ...instead of the content
    CHECK(thin.first <= 0.0);

    host.clearTheme();
    host.showSettings(false);
}
