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

// A TRANSLUCENT LIGHT THEME OVER A DARK TRACK. The case above drives every panel
// opaque, which is where the slab and the theme background are one pixel. At the
// shipped 33% they are not: the track shows through, and a light panel over a dark
// track reads mid-grey. The ALL-TIME PB caption was judged against the slab over the
// theme's WHITE, kept its green, and drew green on a dark-green slab (1.2:1 measured
// off a companion capture of Carbon Light). The fix judges the ink across the whole
// range from "over a black track" to "over the theme background"; this composites the
// quads actually drawn under each string, in emission order, onto both ends and a
// mid-grey track between them (two clean ends do not prove the range) and holds
// the ink to the threshold against each -- the plugin's model of the backdrop is not
// trusted, only the quads it emitted. Reverting inkOnSlabColor to the theme-only
// flatten fails the black-track check here.
TEST_CASE("a caption on a translucent light theme stays readable over a dark track") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ink_legibility_light\\");
    REQUIRE_MESSAGE(host.hasInkHooks(), "MXBMRP3_Test_HudStringColor not exported (test build?)");
    REQUIRE(host.hasPaletteHooks());
    // Carbon Light's palette (ABGR): the text, the field, the green and red.
    enum { SLOT_PRIMARY = 0, SLOT_BACKGROUND = 4, SLOT_POSITIVE = 5, SLOT_NEGATIVE = 8 };
    constexpr unsigned long LIGHT_BG = 0xFFF4F4F4ul;
    host.installTheme("light", 1.0f, 1.0f, /*titleBand=*/0, /*card=*/1);
    host.setThemeColor(SLOT_PRIMARY, 0xFF161616u);
    host.setThemeColor(SLOT_BACKGROUND, LIGHT_BG);
    host.setThemeColor(SLOT_NEGATIVE, 0xFF1C1ADAu);   // #da1e28
    host.setHudVisible("notices_hud", true);   // off by default
    host.showAllHuds(true);
    host.eventInit("TestTrack", "Thomas", 1600.0f, 2, "Bike A", "MX1", /*trackId=*/"TestTrack");
    host.raceEvent("TestTrack");
    host.session(1, 0, 0);
    host.addEntry(4, "Thomas");
    host.runInit(1);
    const int threshold = host.minGlyphLumaGap();
    REQUIRE(threshold > 0);

    // Each string's backdrop: every quad under it, composited in emission order onto
    // the track colour. Alpha 0 quads are skipped by the composite itself.
    auto composite = [&](const char* panel, const std::vector<int>& under, unsigned long track) {
        double rgb[3] = { double(track & 0xFF), double((track >> 8) & 0xFF), double((track >> 16) & 0xFF) };
        for (int qi : under) {
            const unsigned long c = host.quadColor(panel, qi);
            const double a = double((c >> 24) & 0xFF) / 255.0;
            for (int ch = 0; ch < 3; ++ch) rgb[ch] = rgb[ch] * (1.0 - a) + double((c >> (8 * ch)) & 0xFF) * a;
        }
        return 0xFF000000u | (static_cast<unsigned long>(rgb[2] + 0.5) << 16) |
               (static_cast<unsigned long>(rgb[1] + 0.5) << 8) | static_cast<unsigned long>(rgb[0] + 0.5);
    };
    auto inkClearsTheTrack = [&](const char* panel, const char* text, const std::string& what) {
        const auto rows = host.hudStringRows(panel);
        int checked = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].text != text) continue;
            const auto under = quadsUnder(host, panel, rows[i].x, rows[i].y);
            REQUIRE_MESSAGE(under.size() >= 2, what << ": \"" << text << "\" is not on a slab over a panel");
            const unsigned long ink = host.stringColor(panel, static_cast<int>(i));
            for (unsigned long track : { 0xFF000000ul, 0xFF808080ul, LIGHT_BG }) {
                const unsigned long back = composite(panel, under, track);
                const int d = std::abs(host.luma601(ink) - host.luma601(back));
                INFO(what << " over track " << track << ": ink " << ink << " on " << back
                          << ", " << d << " luma (threshold " << threshold << ")");
                CHECK(d >= threshold);
                ++checked;
            }
        }
        REQUIRE_MESSAGE(checked > 0, what << ": \"" << text << "\" not drawn -- the check would be vacuous");
    };

    SUBCASE("the ALL-TIME PB notice at Carbon Light's green and #691's darker one") {
        const unsigned long greens[] = { 0xFF48A124u /* #24a148 */, 0xFF388019u /* #198038 */ };
        int lap = 1;
        for (unsigned long green : greens) {
            host.setThemeColor(SLOT_POSITIVE, green);
            host.raceLap(1, 4, lap, 90000 - lap * 1000, /*best=*/1);
            ++lap;
            host.draw();
            host.draw();
            inkClearsTheTrack("notices_hud", "ALL-TIME PB", "notices");
        }
    }

    SUBCASE("the gap bar's figure over its fill") {
        host.raceLap(1, 4, 1, 95000, /*best=*/1);
        REQUIRE(host.gapBarForceGap(4000, true));
        host.draw();
        const auto rows = host.hudStringRows("gap_bar_hud");
        REQUIRE(!rows.empty());
        inkClearsTheTrack("gap_bar_hud", "+4.000", "gap bar");
    }

    host.shutdown();
}
