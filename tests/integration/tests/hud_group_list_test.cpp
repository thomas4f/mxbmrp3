// ============================================================================
// tests/integration/tests/hud_group_list_test.cpp
// THE GROUPED HUD LISTS: the Widgets and Decks tabs, groups of HUDs in one
// scrolling list of cards (hud_group_list.cpp draws both).
//
// What the scrolling must not cost, each pinned here:
//   * every widget and every deck is still reachable -- stepping from the top
//     to the foot shows each row, and each list starts with its first group;
//   * the list fits the panel unthemed and under thin and fat section cards
//     (its height is priced from row counts and the theme's card terms);
//   * a Find result for a row out of view scrolls the list to it, lit, and the
//     Find index still holds every row, not the rows in view;
//   * the Prestige badge's "New" row is scrolled into view when the tab opens
//     -- a band nobody scrolls to announces nothing -- and once seen, the tab
//     opens where it was left;
//   * the Decks tab's Style and Carousel cards sit above its list, which is
//     only each deck's Visible switch, two to a line: every deck in view at
//     once, the panel no taller for it.
// The groups and their order are the settings_tab_*.cpp files'; this file names
// rows only by tooltip id, so a regrouping that keeps every row passes. The
// list mechanics themselves are settings_scroll_test.cpp's.
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
    "widgets.gamepad", "widgets.bars",
    "widgets.clock", "widgets.now_playing", "widgets.tacho", "widgets.speedo",
    "widgets.lean", "widgets.slide", "widgets.wheelie", "widgets.gforce", "widgets.compass",
    "widgets.toggles", "widgets.pointer",
};

bool drawn(PluginHost& host, const char* id) {
    float x = 0.0f, y = 0.0f;
    return host.settingsRegionCenter(id, &x, &y);
}

constexpr int WIDGETS_LIST = 4;   // ScrollView::WIDGETS

// Move the list `notches` steps (the scrollbar track's shortest move).
void scrollBy(PluginHost& host, int notches) {
    REQUIRE(host.settingsScroll(WIDGETS_LIST).laidOut);
    host.stepScroll(WIDGETS_LIST, notches);
}

void start(PluginHost& host, const char* dir) {
    REQUIRE(host.loaded());
    host.startup(dir);
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    host.showSettings(true);
    host.setActiveTab("Widgets");
    host.draw();
    // A release's "New" rows scroll the list to themselves when the tab opens
    // (the Prestige case below pins that): spend them, and start at the top.
    for (const char* id : kRows) host.hoverSettingsRow(id);
    host.draw();
    scrollBy(host, -50);
}

}  // namespace

TEST_CASE("widgets tab: one list, every widget reachable by scrolling, the most-used first, Interface last") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_list\\");

    const PluginHost::ScrollView top = host.settingsScroll(WIDGETS_LIST);
    CHECK(top.shown == doctest::Approx(0.0f));
    CHECK(top.maxRest > 0.0f);      // more rows than the card shows: it scrolls
    CHECK(top.thumbH > 0.0f);       // ...and says so
    CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
    CHECK(drawn(host, "widgets.speed"));   // the most-used widget leads
    CHECK_FALSE(drawn(host, "widgets.compass"));
    CHECK_FALSE(drawn(host, "widgets.pointer"));   // Interface is last, out of the way

    const size_t n = sizeof(kRows) / sizeof(kRows[0]);
    std::vector<bool> seen(n, false);
    for (int guard = 0; guard < 40; ++guard) {
        for (size_t i = 0; i < n; ++i) seen[i] = seen[i] || drawn(host, kRows[i]);
        const float before = host.settingsScroll(WIDGETS_LIST).target;
        scrollBy(host, 1);
        if (host.settingsScroll(WIDGETS_LIST).target == doctest::Approx(before)) break;
    }
    CHECK(host.settingsScroll(WIDGETS_LIST).target == doctest::Approx(top.maxRest));
    CHECK(drawn(host, "widgets.pointer"));   // ...and at the foot
    CHECK_FALSE(drawn(host, "widgets.speed"));
    for (size_t i = 0; i < n; ++i) CHECK_MESSAGE(seen[i], kRows[i] << " never came into view");
    CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
    host.injectMouse(false);
    host.shutdown();
}

// Pointer Off leaves the settings menu to its hotkey (nothing to click it open
// with while racing), so the row's tooltip is a WARNING: it draws in the slot
// the Spotter tab's Beta notice uses, where every other row's draws Muted.
TEST_CASE("widgets tab: the Pointer row's tooltip is a warning, its neighbour's is not") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_pointer_warn\\");
    scrollBy(host, 50);   // Interface is last

    // The first line of whatever tooltip the description box shows
    const auto tooltipLine = [&](const char* id, PluginHost::DrawnText* out) {
        float x = 0.0f, y = 0.0f;
        REQUIRE(host.settingsRegionCenter(id, &x, &y));
        host.injectMouse(true, x, y, 0);
        host.draw();
        for (const PluginHost::DrawnText& t : host.lastStrings()) {
            if (t.text.rfind("Off = only in the settings menu", 0) == 0) { *out = t; return true; }
        }
        return false;
    };
    PluginHost::DrawnText warn{};
    REQUIRE(tooltipLine("widgets.pointer", &warn));
    // The same box, hovering Toggles: its first line sits where Pointer's did
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("widgets.toggles", &x, &y));
    host.injectMouse(true, x, y, 0);
    host.draw();
    bool found = false;
    for (const PluginHost::DrawnText& t : host.lastStrings()) {
        if (t.x == doctest::Approx(warn.x) && t.y == doctest::Approx(warn.y)) {
            found = true;
            CHECK(t.text.rfind("Off = only", 0) != 0);
            CHECK_MESSAGE(t.color != warn.color, "the Pointer tooltip draws like any other");
        }
    }
    CHECK(found);
    host.injectMouse(false);
    host.shutdown();
}

// The list's height is priced from row counts and the theme's card terms
// (settings_tab_widgets.cpp), so it is checked unthemed and under synthetic
// themes whose card pads differ: a thin card and a fat one.
TEST_CASE("widgets tab: the list fits, themed or not, top and foot") {
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
        scrollBy(host, -50);
        CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
        CHECK(drawn(host, "widgets.speed"));
        scrollBy(host, 50);
        CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
        CHECK(drawn(host, "widgets.compass"));
    }
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("widgets tab: a Find result scrolls to its widget, and the index holds every row") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_find\\");
    REQUIRE(drawn(host, "widgets.speed"));
    REQUIRE_FALSE(drawn(host, "widgets.compass"));   // the test needs it out of view

    std::vector<std::string> lines;
    REQUIRE(host.settingsFind("compass", &lines) > 0);
    bool found = false;
    for (const std::string& l : lines) {
        if (l.rfind("Widgets|", 0) == 0 && l.find("|widgets.compass|") != std::string::npos) found = true;
    }
    CHECK_MESSAGE(found, "the Find index lost a widget that is out of view");

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

TEST_CASE("widgets tab: an unlocked Prestige badge's New row is scrolled into view") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\widgets_tab_prestige\\");
    REQUIRE_FALSE(drawn(host, "widgets.prestige"));   // not earned: no row
    host.setPrestige(1);
    REQUIRE(host.openSettingsTab("Timing"));
    REQUIRE(host.openSettingsTab("Widgets"));
    CHECK(drawn(host, "widgets.prestige"));
    // Hovered, the band is spent, and the tab opens where it was left.
    host.hoverSettingsRow("widgets.prestige");
    scrollBy(host, -50);
    REQUIRE(host.openSettingsTab("Timing"));
    REQUIRE(host.openSettingsTab("Widgets"));
    CHECK(drawn(host, "widgets.speed"));
    host.injectMouse(false);
    host.shutdown();
}

// ---------------------------------------------------------------------------
// THE DECKS TAB: the same list, under the Style and Carousel cards.
namespace {

const char* const kDeckRows[] = {
    "decks.race", "decks.standings", "decks.session", "decks.notice",
    "decks.timing", "decks.gap", "decks.rival", "decks.laplog",
    "decks.speed", "decks.fuel",
    "decks.wheelie", "decks.slide", "decks.spin", "decks.combo",
    "decks.inputs", "decks.media", "decks.carousel",
};

constexpr int DECKS_LIST = 6;   // ScrollView::DECKS

void startDecks(PluginHost& host, const char* dir) {
    REQUIRE(host.loaded());
    host.startup(dir);
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the mouse stand-in");
    host.showSettings(true);
    host.setActiveTab("Decks");
    host.draw();
    for (const char* id : kDeckRows) host.hoverSettingsRow(id);   // spend the "New" rows
    host.draw();
    REQUIRE(host.settingsScroll(DECKS_LIST).laidOut);
    host.stepScroll(DECKS_LIST, -50);
}

}  // namespace

TEST_CASE("decks tab: every deck's switch in view at once, two to a line, under fixed cards") {
    PluginHost host(dllPath());
    startDecks(host, "Z:\\tmp\\mxbmrp3-tests\\decks_tab_list\\");

    const PluginHost::ScrollView top = host.settingsScroll(DECKS_LIST);
    CHECK(top.maxRest == doctest::Approx(0.0f));   // nothing to scroll to
    CHECK(host.settingsOverflowRows() == doctest::Approx(0.0));
    for (const char* id : kDeckRows) CHECK_MESSAGE(drawn(host, id), id << " is out of view");
    // What every deck reads, and how the carousel turns: fixed, above
    for (const char* id : { "decks.slanted", "decks.title", "decks.bars", "decks.stamps", "decks.opacity",
                            "decks.scale", "decks.carousel_auto" })
        CHECK_MESSAGE(drawn(host, id), id << " is missing");
    // No labels' card: a switches-only list has no labels, and a "Decks"
    // heading over them was a card with nothing in it, the panel's only one.
    // The sidebar's own "Decks" tab name sits left of the content column.
    float carouselX = -1.0f;
    for (const PluginHost::DrawnText& t : host.lastStrings())
        if (t.text == "Carousel") carouselX = t.x;
    REQUIRE(carouselX >= 0.0f);
    for (const PluginHost::DrawnText& t : host.lastStrings())
        CHECK_MESSAGE(!(t.text == "Decks" && t.x == doctest::Approx(carouselX)), "an empty Decks card is back");
    // A group's decks in pairs, on one line: Race beside Standings
    float rx = 0.0f, ry = 0.0f, sx = 0.0f, sy = 0.0f;
    REQUIRE(host.settingsRegionCenter("decks.race", &rx, &ry));
    REQUIRE(host.settingsRegionCenter("decks.standings", &sx, &sy));
    CHECK(sy == doctest::Approx(ry));
    CHECK(sx > rx);
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("decks tab: a Find result lands on its deck, and the index holds every deck") {
    PluginHost host(dllPath());
    startDecks(host, "Z:\\tmp\\mxbmrp3-tests\\decks_tab_find\\");

    std::vector<std::string> lines;
    REQUIRE(host.settingsFind("carousel", &lines) > 0);
    bool found = false;
    for (const std::string& l : lines) {
        if (l.rfind("Decks|", 0) == 0 && l.find("|decks.carousel|") != std::string::npos) found = true;
    }
    CHECK_MESSAGE(found, "the Find index lost a deck");

    host.draw();
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("decks.carousel", &x, &y));   // the result row
    host.clickAt(x, y);
    CHECK(host.activeTab() == "Decks");
    CHECK(host.settingsFindLanding() == "decks.carousel");
    CHECK(drawn(host, "decks.carousel"));
    host.injectMouse(false);
    host.shutdown();
}
