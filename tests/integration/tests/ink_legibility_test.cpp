// ============================================================================
// tests/integration/tests/ink_legibility_test.cpp
// A CAPTION DRAWN ON A COLOURED SLAB STAYS READABLE ON IT.
//
// Several panels draw text on a block of a palette slot's own colour: the Notices
// slabs, and the Gap Bar's gap figure over the fill that grows from the bar's centre.
// The text is coloured by the SAME kind of rule as the slab -- NEGATIVE for slower,
// POSITIVE for faster -- so the two land on the same slot whenever they agree, and the
// result is red ink on a red block.
//
// It survived because the slab is drawn at the HUD's background opacity: at the
// shipped default the block is a wash and full-strength ink reads over it. Raise the
// opacity, or pick an opaque theme, and the figure disappears into its own bar.
// Reported from the game twice -- "WRONG WAY" in red on red (Notices, fixed when
// captionOnSlabColor landed) and then the Gap Bar's gap figure, which had never been
// wired to that helper even though the helper's own comment claimed both panels.
//
// WHY THIS FILE EXISTS AT ALL. The whole legibility family -- legibleOnFill,
// captionOnSlabColor, chipGlyphColor -- had NO test. That is what let a helper ship
// half-applied for as long as it did: nothing could see the difference between a panel
// that called it and one that did not, because no reader could compare a string's
// colour with the colour behind it. MXBMRP3_Test_HudStringColor / _HudQuadColor exist
// for that comparison.
//
// The threshold and the luma formula are read from the plugin (minGlyphLumaGap(),
// luma601()) rather than restated here: a test carrying its own copy of either can
// agree with itself while disagreeing with the code it is checking.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// EVERY QUAD THE POINT (x, y) FALLS INSIDE, by index in emission order. Later quads
// draw over earlier ones, so the last match is what a glyph at that point actually
// competes with -- but the ink has to clear ALL of them, because a caption spanning a
// slab edge sits on two.
//
// Point containment, NOT "the widest quad": the first version of this file took the
// largest-area quad and passed with the fix reverted, because the Gap Bar's widest
// quad is its PANEL BACKGROUND. The fill grows from the bar's centre and reaches at
// most half the box, so it is never the biggest thing on the panel -- the check was
// comparing red ink against the panel's dark background and finding it perfectly
// legible, which it is, and which was not the question.
std::vector<int> quadsUnder(PluginHost& host, const char* panel, double x, double y) {
    std::vector<int> hits;
    const auto rects = host.hudQuadRects(panel);
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& r = rects[i];
        if (x >= r.l && x <= r.r && y >= r.t && y <= r.b) hits.push_back(static_cast<int>(i));
    }
    return hits;
}

}  // namespace

TEST_CASE("a caption on a coloured slab clears the plugin's own luma threshold") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ink_legibility\\");
    REQUIRE_MESSAGE(host.hasInkHooks(),
                    "MXBMRP3_Test_HudStringColor not exported (test build?)");
    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    const int threshold = host.minGlyphLumaGap();
    REQUIRE(threshold > 0);

    // OPAQUE. The whole point: at the shipped background opacity the slab is a wash and
    // even same-on-same ink reads, so a test at the default would pass on the broken
    // code. This drives the state the bug was reported in.
    REQUIRE(host.setHudOpacity("gap_bar_hud", 1.0f));
    REQUIRE(host.setHudOpacity("notices_hud", 1.0f));

    auto inkClearsWhatIsBehindIt = [&](const char* panel, const std::string& what) {
        const auto rows = host.hudStringRows(panel);
        REQUIRE_MESSAGE(!rows.empty(), what << ": no strings -- the check would be vacuous");

        int checked = 0, worst = 255;
        std::string worstText;
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto under = quadsUnder(host, panel, rows[i].x, rows[i].y);
            if (under.empty()) continue;                  // floating text, nothing behind it
            const unsigned long ink = host.stringColor(panel, static_cast<int>(i));
            for (int qi : under) {
                const unsigned long fill = host.quadColor(panel, qi);
                if ((fill >> 24) == 0) continue;          // fully transparent: not a backdrop
                const int d = std::abs(host.luma601(ink) - host.luma601(fill));
                ++checked;
                if (d < worst) { worst = d; worstText = rows[i].text; }
            }
        }
        INFO(what << ": " << checked << " ink/backdrop pairs, closest \"" << worstText
             << "\" at " << worst << " luma (threshold " << threshold << ")");
        // Not vacuous: SOMETHING has to sit on something, or the panel drew no slab.
        REQUIRE_MESSAGE(checked > 0, what << ": no string sits on any quad");
        CHECK(worst >= threshold);
    };

    SUBCASE("the gap bar's figure over its own fill") {
        // A gap large enough to grow a fill under the centred figure. The text is
        // CENTER-justified on exactly the point the fill grows from, so once a fill
        // exists half the digits sit on it. Forced rather than driven through laps:
        // the bar needs a personal best to compare against, and this is the seam that
        // exists for it.
        // A PERSONAL BEST FIRST. Both the fill and the coloured figure are gated on
        // PluginData::getBestLapEntry() -- without one the panel shows a MUTED "-" over
        // nothing but its own background, which is what the first version of this case
        // measured while reporting success.
        host.raceLap(1, 4, 1, 95000, /*best=*/1);
        REQUIRE(host.gapBarForceGap(4000, true));   // 4s slower -> a wide red fill
        host.draw();
        inkClearsWhatIsBehindIt("gap_bar_hud", "gap bar");
    }

    SUBCASE("a notice over its own slab") {
        // runInit with no setup name raises the persistent "DEFAULT SETUP" notice, so
        // there is a slab on screen for as long as the assertions take.
        host.draw();
        inkClearsWhatIsBehindIt("notices_hud", "notices");
    }

    host.shutdown();
}
