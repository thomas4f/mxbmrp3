// ============================================================================
// tests/integration/tests/widgets_tab_test.cpp
// THE WIDGETS TAB'S PAGES: groups of widgets, two pages, one pager.
//
// What the paging must not cost, each pinned here:
//   * every widget is still reachable -- each row on exactly one page;
//   * the pager holds still and neither page overflows the panel: a page is
//     padded to the fullest one, so paging never moves the button under the
//     pointer (the Achievements tab's rule);
//   * a Find result for a widget on the other page opens THAT page with its
//     row lit, and the Find index still holds every widget, not one page's;
//   * both hold unthemed and under thin and fat section cards, and the pager
//     sits on the panel under them, never in a card;
//   * the Prestige badge's "New" row opens its own page when the tab opens --
//     a band on a page nobody turns to announces nothing.
// The groups and their order are settings_tab_widgets.cpp's; this file names
// rows only by tooltip id, so a regrouping that keeps every row passes.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <string>
#include <vector>

namespace {

const char* const kRows[] = {
    "widgets.speed", "widgets.gear", "widgets.rpm", "widgets.fuel",
    "widgets.lap", "widgets.position", "widgets.time", "widgets.crashes",
    "widgets.toggles", "widgets.pointer", "widgets.gamepad", "widgets.bars",
    "widgets.clock", "widgets.now_playing", "widgets.tacho", "widgets.speedo",
    "widgets.lean", "widgets.slide", "widgets.wheelie", "widgets.gforce", "widgets.compass",
};

bool drawn(PluginHost& host, const char* id) {
    float x = 0.0f, y = 0.0f;
    return host.settingsRegionCenter(id, &x, &y);
}

void start(PluginHost& host, const char* dir) {
    REQUIRE(host.loaded());
    host.startup(dir);
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    host.showSettings(true);
    host.setActiveTab("Widgets");
    host.draw();
    // A release's "New" rows open their page instead of the first (the Prestige
    // case below pins that): spend them, and start every case on page one.
    for (const char* id : kRows) host.hoverSettingsRow(id);
    float x = 0.0f, y = 0.0f;
    if (host.settingsRegionCenter("pager.prev", &x, &y)) host.clickAt(x, y);
    host.draw();
}

}  // namespace

TEST_CASE("widgets tab: two pages, every widget on exactly one, the pager holding still") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_pages\\");

    float x = 0.0f, y = 0.0f;
    CHECK_FALSE(host.settingsRegionCenter("pager.prev", &x, &y));   // page one: nothing before it
    REQUIRE(host.settingsRegionCenter("pager.next", &x, &y));
    CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
    std::vector<bool> onFirst;
    for (const char* id : kRows) onFirst.push_back(drawn(host, id));
    CHECK(drawn(host, "widgets.speed"));   // the most-used widget leads

    host.clickAt(x, y);
    float px = 0.0f, py = 0.0f;
    REQUIRE(host.settingsRegionCenter("pager.prev", &px, &py));
    CHECK_FALSE(host.settingsRegionCenter("pager.next", &x, &y));   // the last page
    CHECK_MESSAGE(py == doctest::Approx(y), "the pager moved between pages");
    CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
    int firstCount = 0;
    for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); ++i) {
        const bool second = drawn(host, kRows[i]);
        CHECK_MESSAGE(onFirst[i] != second, kRows[i] << (second ? " is on both pages" : " is on neither page"));
        if (onFirst[i]) ++firstCount;
    }
    CHECK(firstCount > 0);
    CHECK(firstCount < static_cast<int>(sizeof(kRows) / sizeof(kRows[0])));

    host.clickAt(px, py);
    CHECK(drawn(host, "widgets.speed"));    // back on page one
    host.injectMouse(false);
    host.shutdown();
}

// The padding that holds the pager still is priced from row counts and the
// theme's card terms (settings_tab_widgets.cpp), so it is checked unthemed and
// under synthetic themes whose card pads differ: a thin card and a fat one.
TEST_CASE("widgets tab: the pager holds still and both pages fit, themed or not") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_themes\\");
    REQUIRE_MESSAGE(host.hasThemeGeometry(), "theme hooks not exported");
    struct Skin { const char* name; float frame, card; };
    for (const Skin& skin : { Skin{ "", 0.0f, 0.0f }, Skin{ "wt-thin", 1.0f, 1.0f }, Skin{ "wt-fat", 1.0f, 2.0f } }) {
        const std::string skinName = skin.name[0] ? skin.name : "unthemed";
        CAPTURE(skinName);
        if (skin.name[0]) {
            host.installTheme(skin.name, skin.frame, skin.card, /*titleBand=*/1, /*card=*/1,
                              /*cardSprites=*/true, /*buttonSprites=*/true);
        }
        host.setActiveTab("Timing");
        host.setActiveTab("Widgets");
        host.draw();
        float x = 0.0f, y = 0.0f;
        while (host.settingsRegionCenter("pager.prev", &x, &y)) host.clickAt(x, y);   // page one
        float nx = 0.0f, ny = 0.0f, px = 0.0f, py = 0.0f;
        REQUIRE(host.settingsRegionCenter("pager.next", &nx, &ny));
        CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
        host.clickAt(nx, ny);
        REQUIRE(host.settingsRegionCenter("pager.prev", &px, &py));
        CHECK_MESSAGE(py == doctest::Approx(ny), "the pager moved between pages");
        CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));

        // UNDER THE CARDS, ON THE PANEL: no card (or, unthemed, no section
        // tint) behind the pager. Whatever is wider than the whole pager and
        // covers its label must be exactly what covers the empty panel two rows
        // below it -- the panel's own fill and frame.
        float sx = 0.0f, sy = 0.0f, gx = 0.0f, gy = 0.0f;
        host.clickAt(px, py);   // page one: Speed and Gear give the row height
        REQUIRE(host.settingsRegionCenter("widgets.speed", &sx, &sy));
        REQUIRE(host.settingsRegionCenter("widgets.gear", &gx, &gy));
        const double row = gy - sy, wide = nx - px, cx = (px + nx) * 0.5;
        const auto covering = [&](double y) {
            int n = 0;
            for (const PluginHost::QuadRect& q : host.hudQuadRects(PluginHost::HUD_SETTINGS)) {
                if (q.r - q.l > wide && q.l <= cx && cx <= q.r && q.t <= y && y <= q.b) ++n;
            }
            return n;
        };
        CHECK_MESSAGE(covering(ny) == covering(ny + 2.0 * row), "something is drawn behind the pager");
        // ...and a theme's card, which a covering count cannot see: the panel's
        // fill is cut around every card, so a card only swaps for a fill strip.
        // The cut's covers are the cards, with their x, so the sidebar's are
        // told apart from the content column's.
        const PluginHost::FillCut cut = host.hudFillCut(PluginHost::HUD_SETTINGS);
        if (skin.name[0]) REQUIRE(cut.themed);   // the covers are seen at all
        for (const PluginHost::QuadRect& c : cut.covers) {
            CHECK_MESSAGE(!(c.l <= cx && cx <= c.r && c.t <= ny && ny <= c.b), "a card spans the pager row");
        }
    }
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("widgets tab: a Find result opens its widget's page, and the index holds every page") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_find\\");
    REQUIRE(drawn(host, "widgets.speed"));
    REQUIRE_FALSE(drawn(host, "widgets.compass"));   // the test needs it on the other page

    std::vector<std::string> lines;
    REQUIRE(host.settingsFind("compass", &lines) > 0);
    bool found = false;
    for (const std::string& l : lines) {
        if (l.rfind("Widgets|", 0) == 0 && l.find("|widgets.compass|") != std::string::npos) found = true;
    }
    CHECK_MESSAGE(found, "the Find index lost a widget that is not on the shown page");

    host.draw();
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("widgets.compass", &x, &y));   // the result row
    host.clickAt(x, y);
    CHECK(host.activeTab() == "Widgets");
    CHECK(host.settingsFindLanding() == "widgets.compass");
    CHECK(drawn(host, "widgets.compass"));
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("widgets tab: an unlocked Prestige badge opens the page its New row is on") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_prestige\\");
    REQUIRE_FALSE(drawn(host, "widgets.prestige"));   // not earned: no row
    host.setPrestige(1);
    REQUIRE(host.openSettingsTab("Timing"));
    REQUIRE(host.openSettingsTab("Widgets"));
    CHECK(drawn(host, "widgets.prestige"));
    // Hovered, the band is spent, and the tab opens where it was left.
    host.hoverSettingsRow("widgets.prestige");
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("pager.prev", &x, &y));
    host.clickAt(x, y);
    REQUIRE(host.openSettingsTab("Timing"));
    REQUIRE(host.openSettingsTab("Widgets"));
    CHECK(drawn(host, "widgets.speed"));
    host.injectMouse(false);
    host.shutdown();
}
