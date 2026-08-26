// ============================================================================
// tests/integration/tests/asym_border_test.cpp
// A lone value is centred in the CARD it is drawn on, not in the content band
// inside it.
//
// WHY THIS FILE EXISTS. `[content] border` is CSS shorthand and may be
// asymmetric; the content band is inset by border.t at the top and border.b at
// the bottom, so it shares a centre with the card ONLY while those are equal.
// Every shipped theme is symmetric, so a panel centring its value in the band
// instead of the card looked identical everywhere -- and four of them were doing
// exactly that until a skinner wrote `border = 2 0 4 6` and reported them
// sitting high. The synthetic themes the rest of the suite installs take a
// single uniform border and cannot express the case at all, which is why the
// fault was reachable only in game.
//
// So this drives the asymmetry directly (PluginHost::setThemeContentBorder) and
// asserts against the CARD's own rect, which is what the player sees.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cmath>
#include <vector>

namespace {
// Deliberately lopsided, and the shape the report used: 2 top, 4 bottom means the
// band's centre sits a full cell above the card's.
constexpr float BT = 2.0f, BR = 0.0f, BB = 4.0f, BL = 6.0f;
}  // namespace

TEST_CASE("asymmetric [content] border: a lone value stays centred in its card") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asym_border\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasStringRows());

    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick");
    host.session(6, 5, 0);
    host.addEntry(4, "Thomas");
    host.runInit(6);

    host.installTheme("asym", /*inset=*/1.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/1, /*card=*/1);
    REQUIRE_MESSAGE(host.setThemeContentBorder(BT, BR, BB, BL),
                    "no synthetic theme to inject an asymmetric border into");
    host.draw();

    // The Notices slab is the one card whose extent is unambiguous in the quad list:
    // it is drawn in its own colour as the LAST quad (see center_stack_theme_test for
    // why buttonSprites stays off). Its message must sit in the middle of it.
    const auto quads = host.hudQuadRects(PluginHost::HUD_NOTICES);
    REQUIRE_MESSAGE(!quads.empty(), "notices drew nothing under the asymmetric theme");
    const PluginHost::QuadRect slab = quads.back();

    double textY = -1.0;
    for (const auto& row : host.hudStringRows(PluginHost::HUD_NOTICES)) {
        if (row.text == "Notices") continue;    // the caption
        textY = row.y;
    }

    if (textY >= 0.0) {
        const double slabMid = (slab.t + slab.b) / 2.0;
        // The string's y is its TOP, so the comparison is against the ink's own centre
        // rather than the glyph's -- which is why this is a band rather than a point:
        // what is being caught is a WHOLE CELL of offset (the band's centre against the
        // card's), not the sub-cell difference between ink and cell.
        const double slabH = slab.b - slab.t;
        INFO("slab [" << slab.t << ".." << slab.b << "] mid " << slabMid
             << "  text top " << textY);
        CHECK(textY > slab.t);
        CHECK(textY < slabMid);
        // ...and it is in the LOWER half of the space above the middle, i.e. roughly
        // centred rather than pinned near the top, which is where centring in the band
        // put it. A quarter of the slab above its middle is a generous floor and still
        // fails the band-centred placement.
        CHECK(textY > slabMid - slabH * 0.25);
    }

    host.shutdown();
}

// ============================================================================
// The companion case: asymmetric [content] MARGIN. Margin does not move the
// content band inside the card -- it moves the whole CARD inside the panel, so
// what it separates is the card's centre from the PANEL's. Every widget that
// centred its value at `startX + backgroundWidth / 2` tracked the panel's
// centre off the card (`margin = 4 6 8 0`, reported with the value, the gauge
// and the chip all sitting right of the card they are drawn on).
//
// The probe: these panels are LEFT-ANCHORED, so growing a RIGHT-only content
// margin widens the panel while the card stays exactly where it was. Nothing
// these panels draw is anchored to the panel's right edge, so NO drawn string
// may move -- panel-centred content moves right by half the margin and fails.
// ============================================================================
TEST_CASE("asymmetric [content] margin: content stays on the card, not the panel") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\asym_margin\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasStringRows());

    host.showAllHuds(true);
    host.eventInit("Southwick", "Thomas");
    host.raceEvent("Southwick");
    host.session(6, 5, 0);
    host.addEntry(4, "Thomas");
    host.runInit(6);

    host.installTheme("asym_m", /*inset=*/1.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/1, /*card=*/1);
    REQUIRE_MESSAGE(host.setThemeContentMargin(0.0f, 0.0f, 0.0f, 0.0f),
                    "no synthetic theme to inject a content margin into");
    host.draw();

    const PluginHost::HudId widgets[] = {
        PluginHost::HUD_SPEED, PluginHost::HUD_GEAR,
        PluginHost::HUD_CRASH, PluginHost::HUD_GFORCE,
    };
    std::vector<std::vector<PluginHost::StringRow>> before;
    for (const auto id : widgets) before.push_back(host.hudStringRows(id));

    REQUIRE(host.setThemeContentMargin(0.0f, 8.0f, 0.0f, 0.0f));
    host.draw();

    for (size_t w = 0; w < std::size(widgets); ++w) {
        const auto after = host.hudStringRows(widgets[w]);
        REQUIRE_MESSAGE(after.size() == before[w].size(),
                        "widget " << w << " changed its string count under a margin");
        for (size_t i = 0; i < after.size(); ++i) {
            INFO("widget " << w << " string '" << after[i].text << "'");
            CHECK(after[i].text == before[w][i].text);
            CHECK(std::fabs(after[i].x - before[w][i].x) < 1e-4);
            CHECK(std::fabs(after[i].y - before[w][i].y) < 1e-4);
        }
    }

    // And the exact form of the rule, on the one card whose extent is unambiguous
    // (see the border case above): the Notices message anchors at the slab's own
    // horizontal centre, which the right-only margin has pulled well left of the
    // panel's.
    const auto quads = host.hudQuadRects(PluginHost::HUD_NOTICES);
    REQUIRE_MESSAGE(!quads.empty(), "notices drew nothing under the margin theme");
    const PluginHost::QuadRect slab = quads.back();
    double textX = -1.0;
    for (const auto& row : host.hudStringRows(PluginHost::HUD_NOTICES)) {
        if (row.text == "Notices") continue;    // the caption
        textX = row.x;
    }
    if (textX >= 0.0) {
        const double slabMidX = (slab.l + slab.r) / 2.0;
        INFO("slab [" << slab.l << ".." << slab.r << "] mid " << slabMidX
             << "  text anchor " << textX);
        // CENTER-justified, so the recorded x IS the anchor: exact up to float noise.
        CHECK(std::fabs(textX - slabMidX) < 1e-3);
    }

    host.shutdown();
}
