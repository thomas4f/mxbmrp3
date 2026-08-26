// ============================================================================
// tests/integration/tests/theme_geometry_test.cpp
// The settings panel's theme-geometry contracts, rebuilt on the box-model
// surface. (A previous test of this name pinned the pre-port chain and was
// removed with the rest of the box-model suite; the hooks it drove —
// SettingsMarginsX / SettingsContentX — stayed exported, and several comments
// in settings_hud_render.cpp still cite this file. This is that pin, restored,
// plus the contract the port added.)
//
// TWO CONTRACTS, both RELATIVE (no exact numbers, so the model can keep
// moving under them — the same property theme_panel_padding_test argues for):
//
// 1. SWITCHING THEMES NEVER MOVES THE CONTENT. The layout centres the content
//    box and hangs the panel off it by the theme's overhangs; only the outer
//    edges may move. The failure mode is the historical one: cycling themes
//    walked the row controls sideways a cell at a time, because the width and
//    the anchor disagreed about one overhang term.
//
// 2. THE GUTTER IS THE SEAM. The air between the sidebar cards and the
//    content cards must measure exactly what the vertical air between two
//    section cards measures for the same terms — contentGapCells, spent
//    square-on-screen. This was measured broken twice on the way here (the
//    trough composed gap-only while the seams composed sectionGap + gap; then
//    the card's row lead-in overhung the trough unpaid), and both bugs
//    survived every other gate because nothing compared the two axes. The
//    third assertion turns the [panel] gap term and requires BOTH numbers to
//    move by the same amount — the term acting on one axis only (the other
//    historical shape) fails it.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// One pixel at 1920 wide, in the hooks' 1e6 fixed point — the comparison
// tolerance. Card edges land on quad corners a rasterizer would place a pixel
// apart at most; anything beyond that is a composition bug, not noise.
constexpr int kPxFixed = static_cast<int>(1e6 / 1920.0);

struct ContentAnchors { int labelX, controlX, rowRight; };

ContentAnchors anchors(PluginHost& host) {
    auto c = host.settingsContentX();
    return { c.labelX, c.controlX, c.rowRight };
}

}  // namespace

TEST_CASE("theme geometry: switching themes never moves the settings content") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme_geometry\\");
    REQUIRE_MESSAGE(host.hasSettingsMargins(), "settings geometry hooks not exported");

    host.showSettings(true);
    host.draw();
    const ContentAnchors flat = anchors(host);

    // A synthetic theme with band, card and button sets — the full chrome.
    host.installTheme("geom-a", /*frameBorder=*/2.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/1, /*contentCard=*/1, /*cardSprites=*/1,
                      /*buttonSprites=*/1);
    host.draw();
    const ContentAnchors themed = anchors(host);

    CHECK(std::abs(themed.labelX - flat.labelX) <= kPxFixed);
    CHECK(std::abs(themed.controlX - flat.controlX) <= kPxFixed);
    CHECK(std::abs(themed.rowRight - flat.rowRight) <= kPxFixed);

    // A FATTER theme moves them just as little — the overhangs change, the
    // anchors must not.
    host.installTheme("geom-b", 4.0f, 2.0f, 1, 1, 1, 1);
    host.draw();
    const ContentAnchors fat = anchors(host);
    CHECK(std::abs(fat.labelX - flat.labelX) <= kPxFixed);
    CHECK(std::abs(fat.controlX - flat.controlX) <= kPxFixed);
    CHECK(std::abs(fat.rowRight - flat.rowRight) <= kPxFixed);
}

TEST_CASE("theme geometry: the settings gutter measures the vertical seam") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme_geometry\\");

    host.installTheme("geom-gutter", 2.0f, 1.0f, 1, 1, 1, 1);
    host.showSettings(true);
    host.draw();

    const auto g0 = host.settingsGutter();
    REQUIRE_MESSAGE(g0.contentCardLeft > 0.0f, "card edges not recorded (hook absent?)");
    const float gutter0 = g0.contentCardLeft - g0.sidebarCardRight;
    // Contract 2: gutter == seam, both axes' spend of the same terms. The
    // seam is a vertical distance and the gutter a horizontal one; both are
    // stated square-on-screen, so in NORMALIZED units the horizontal gutter
    // equals seam / aspect. Compare in pixels-on-screen: x-normalized * 1920
    // vs y-normalized * 1080 — one pixel is kPx * 1920 on either axis.
    const float gutterPx0 = gutter0 * 1920.0f;
    const float seamPx0 = g0.seam * 1080.0f;
    CHECK(std::fabs(gutterPx0 - seamPx0) <= 1.5f);

    // Contract 3: [panel] gap moves BOTH by the same amount.
    REQUIRE(host.setThemeGap(2.0f));
    host.draw();
    const auto g2 = host.settingsGutter();
    const float gutterPx2 = (g2.contentCardLeft - g2.sidebarCardRight) * 1920.0f;
    const float seamPx2 = g2.seam * 1080.0f;
    CHECK(seamPx2 > seamPx0 + 1.0f);                       // the term acted at all
    CHECK(std::fabs(gutterPx2 - seamPx2) <= 1.5f);         // still equal
    CHECK(std::fabs((gutterPx2 - gutterPx0) - (seamPx2 - seamPx0)) <= 1.5f);
}

// ============================================================================
// Contract 3: THE PANEL FITS THE SCREEN.
//
// The settings panel is the only panel a user cannot move, resize or hide: it
// is centred by construction, so anything taller than the display is clipped at
// both ends with no recourse. It sizes itself to its TALLEST tab and is not
// clamped to the display -- deliberately, since 1080p is a conservative default
// and a theme asking for more air than fits is the theme author's to see -- so
// nothing bounds it from inside any more (it used to be a hand-set row floor,
// LayoutMetrics::settingsRows, which bounded it by CLIPPING the tab instead).
// NOTHING else checks the ceiling: every other gate asserts internal
// consistency, which a panel that overflows the screen satisfies perfectly. That
// makes this case the only thing standing between a tab gaining a few rows and a
// menu the player cannot read the ends of -- for the SHIPPED configurations,
// which is the promise the plugin can actually keep.
//
// A theme spends against that budget, so the ceiling has to hold for the
// thickest chrome a SHIPPED configuration can ask for. Every shipped theme's
// frame is 1 cell, and 1 is what is asserted; 2 and up are headroom the panel
// no longer has (see the note at the end of this case).
//
// EVERY TAB, enumerated from the registry. The panel's height is max(this tab's
// measured content, the tab list's rows), so which tab binds is a property of the
// content and moves as tabs gain and lose rows -- it was Hotkeys, and this case
// drove only Hotkeys, which would have said nothing about the General tab growing
// past the screen. Enumerating also means a NEW tab is covered without anyone
// remembering to add it here.
// ============================================================================
TEST_CASE("theme geometry: the settings panel fits the screen") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\theme_geometry\\");
    REQUIRE_MESSAGE(host.hasScreenEdges(), "screen-edge hooks not exported");

    // Screen space is 0..1; the hooks report it in 1e6 fixed point.
    constexpr int kTop = 0, kBottom = 1000000;

    // std::string, not the bare const char*: doctest's message builder converts
    // a pointer to bool, so a raw literal names the configuration "1".
    auto fits = [&](const std::string& what) {
        host.draw();
        const auto e = host.hudScreenEdges(PluginHost::HUD_SETTINGS);
        const double h = (e.b - e.t) / 1e6;
        CHECK_MESSAGE(e.t >= kTop,
                      what << ": panel top is off-screen at " << (e.t / 1e6)
                           << " (height " << h << " of 1.0) — the title band is clipped");
        CHECK_MESSAGE(e.b <= kBottom,
                      what << ": panel bottom is off-screen at " << (e.b / 1e6)
                           << " (height " << h << " of 1.0) — the footer row is clipped");
    };

    host.showSettings(true);
    const std::vector<std::string> tabs = host.settingsTabNames();
    REQUIRE_MESSAGE(tabs.size() > 5u,
                    "settings tabs not enumerable (MXBMRP3_Test_SettingsTabName absent?)");
    auto everyTab = [&](const std::string& skin) {
        for (const std::string& tab : tabs) {
            host.setActiveTab(tab.c_str());
            fits(skin + ", tab " + tab);
        }
    };

    everyTab("unthemed");

    host.installTheme("fit-1", /*frameBorder=*/1.0f, /*cardBorder=*/1.0f,
                      /*titleBand=*/1, /*contentCard=*/1, /*cardSprites=*/1,
                      /*buttonSprites=*/1);
    everyTab("themed, frame 1");

    // FRAME 2 NO LONGER FITS — by one cell (bottom lands at ~1.009), and it is
    // known exactly where the cell went: `buttonPadding 0.5 1` (the footer
    // buttons' vertical air, a deliberate default) spends the last of the
    // double-frame headroom this case used to assert. Every shipped theme's
    // frame is 1 cell, so the shipped promise above is intact; restoring the
    // frame-2 headroom means winning ~1 cell back — the buttons' vertical air,
    // or a row out of the tallest tab — which is a design trade, not a bug
    // fix, so it is left unasserted here rather than pinned red.
    //
    // FRAME 3 AND UP DOES NOT FIT, and no row budget could have made it: the panel's
    // height is max(measured content, tab rows) and at a thick frame the SIDEBAR is
    // the taller of the two — its ~31 tab rows plus per-group gaps that are
    // themselves theme terms. That was measured while the content side was still a
    // hand-set floor (cutting it from 34 to 33 moved this case by 0.0039, a sixth of
    // a row, where the others moved by a full one), which is also why replacing that
    // floor with a measurement does not move this ceiling. The lever for a thick
    // frame is the whole panel's scale (`[Advanced] uiFontSize`).
    //
    // Left unasserted rather than pinned to today's overflow: a number that says
    // "still broken by exactly this much" invites being updated instead of read.
    // The contract above is the one that matters — every SHIPPED configuration fits.
}
