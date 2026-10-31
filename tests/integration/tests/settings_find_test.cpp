// ============================================================================
// tests/integration/tests/settings_find_test.cpp
// THE SETTINGS FIND BOX: AN INDEX OF THE REAL ROWS, PAGES THAT FIT, AND A JUMP.
//
// The index is not a table anyone maintains: it is every tab laid out once with
// the drawing thrown away, collected through the shared row helper
// (SettingsLayoutContext::addRowTooltip) and the value helpers that follow it.
// So what is worth pinning is what a unit test cannot see:
//
//   * real rows come back, with the value the row draws and the section it sits
//     under, and Achievements (a catalogue, not settings) does not;
//   * a HUD's name finds that HUD's rows, which are labelled just "Visible" or
//     "Scale";
//   * a broad query's pages never overflow the panel -- the Find page is kept out
//     of the tallest-tab measure, so nothing else would notice;
//   * a result opens its tab with that row lit, and the light goes out when the
//     pointer moves; the box keeps the query, and clicking it shows the results
//     again;
//   * the page is a view of the box, not a place: closing the menu returns to the
//     tab the box was opened from.
//
// Typing itself is GetAsyncKeyState polling and is not reachable headless; the
// hook sets the query as if typed and Enter pressed. The match and ranking are
// tests/unit/test_settings_find.cpp's.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cctype>
#include <string>
#include <vector>

namespace {
struct Hit { std::string tab, label, value, id, section; };

std::vector<Hit> parse(const std::vector<std::string>& lines) {
    std::vector<Hit> out;
    for (const std::string& l : lines) {
        Hit h;
        size_t a = l.find('|'), b = l.find('|', a + 1), c = l.find('|', b + 1), d = l.find('|', c + 1);
        if (a == std::string::npos || b == std::string::npos || c == std::string::npos ||
            d == std::string::npos) continue;
        h.tab = l.substr(0, a);
        h.label = l.substr(a + 1, b - a - 1);
        h.value = l.substr(b + 1, c - b - 1);
        h.id = l.substr(c + 1, d - c - 1);
        h.section = l.substr(d + 1);
        out.push_back(h);
    }
    return out;
}

bool hasWord(std::string s, const char* w) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s.find(w) != std::string::npos;
}

void start(PluginHost& host, const char* dir) {
    REQUIRE(host.loaded());
    host.startup(dir);
    host.showSettings(true);
    host.setActiveTab("Standings");
    host.draw();
}
}  // namespace

TEST_CASE("settings find: real rows, with their values, name hits first") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_find_rows\\");

    std::vector<std::string> lines;
    const int n = host.settingsFind("gap", &lines);
    REQUIRE_MESSAGE(n >= 0, "MXBMRP3_Test_SettingsFind not exported, or the index came back empty");
    CHECK(host.activeTab() == "Find");
    const std::vector<Hit> hits = parse(lines);
    REQUIRE(static_cast<int>(hits.size()) == n);

    // Gap Bar's Gap reference: a real row, by name, carrying the value its row draws.
    bool gapBarRef = false;
    for (const Hit& h : hits) {
        if (h.tab == "Gap Bar" && h.id == "gap_bar.reference") {
            gapBarRef = true;
            CHECK(!h.value.empty());
            CHECK(!h.section.empty());   // the heading it sits under ("Content")
        }
    }
    CHECK(gapBarRef);

    // Name hits first: once a hit lacks the word in its label, none after has it.
    bool inDescriptions = false;
    for (const Hit& h : hits) {
        if (!hasWord(h.label, "gap")) inDescriptions = true;
        else CHECK_MESSAGE(!inDescriptions, h.tab << " > " << h.label << " is a name hit after a description hit");
    }

    // Every row has an id to land on, and none comes from Achievements (the tab
    // itself may: it is a result, its catalogue is not).
    for (const Hit& h : hits) {
        CHECK(!h.id.empty());
        CHECK((h.tab != "Achievements" || h.id == "achievements"));
    }
}

// A player types the HUD's name: "map", "map scale", "timing". The first result
// is the HUD's tab itself; its rows are labelled just "Scale" / "Visible", so only
// the tab's name can find them.
TEST_CASE("settings find: a HUD's name finds the HUD first, then its own rows") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_find_tab\\");

    std::vector<std::string> lines;
    REQUIRE(host.settingsFind("map", &lines) > 0);
    std::vector<Hit> first = parse(lines);
    REQUIRE(!first.empty());
    CHECK(first[0].tab == "Map");
    CHECK(first[0].label == "Map");
    CHECK(first[0].id == "map");
    CHECK(!first[0].value.empty());   // what its Visible row shows
    host.draw();
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("map", &x, &y));
    host.clickAt(x, y);
    CHECK(host.activeTab() == "Map");
    CHECK(host.settingsFindLanding().empty());   // a tab has no row to light

    // Hotkeys and Achievements: no rows in the index, but the tabs are found.
    REQUIRE(host.settingsFind("hotkeys", &lines) > 0);
    CHECK(parse(lines)[0].tab == "Hotkeys");
    REQUIRE(host.settingsFind("achievements", &lines) > 0);
    CHECK(parse(lines)[0].id == "achievements");

    REQUIRE(host.settingsFind("map scale", &lines) > 0);
    std::vector<Hit> hits = parse(lines);
    bool mapScale = false;
    for (const Hit& h : hits) {
        if (h.tab == "Map" && h.id == "common.scale") mapScale = true;
        CHECK_MESSAGE(!(h.tab == "Radar" && h.id == "common.scale"), "Radar's Scale matched \"map scale\"");
    }
    CHECK(mapScale);

    REQUIRE(host.settingsFind("timing", &lines) > 0);
    hits = parse(lines);
    bool timingVisible = false;
    for (const Hit& h : hits) {
        if (h.tab == "Timing" && h.id == "common.visible") timingVisible = true;
    }
    CHECK(timingVisible);
}

// A player types the app, not the HUD: "spotify" finds the Now Playing widget
// through its tooltip, as well as the General switch that feeds it.
TEST_CASE("settings find: a player's word for a HUD finds it through its tooltip") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_find_spotify\\");

    std::vector<std::string> lines;
    REQUIRE(host.settingsFind("spotify", &lines) > 0);
    bool widget = false, general = false;
    for (const Hit& h : parse(lines)) {
        if (h.tab == "Widgets" && h.id == "widgets.now_playing") widget = true;
        if (h.tab == "General" && h.id == "general.now_playing") general = true;
    }
    CHECK_MESSAGE(widget, "\"spotify\" does not find Widgets > Now Playing");
    CHECK_MESSAGE(general, "\"spotify\" does not find General > Now playing");
}

TEST_CASE("settings find: every page of a broad query fits the panel") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_find_fit\\");
    REQUIRE(host.hasSettingsOverflow());

    const int n = host.settingsFind("e");
    REQUIRE(n > 40);   // several pages
    host.draw();
    int pages = 1;
    for (; pages < 50; ++pages) {
        CHECK_MESSAGE(host.settingsOverflowRows() == doctest::Approx(0.0),
                      "Find page " << pages << " overflows the panel");
        float nx = 0.0f, ny = 0.0f;
        if (!host.settingsRegionCenter("pager.next", &nx, &ny)) break;
        host.clickAt(nx, ny);
    }
    CHECK(pages > 2);
}

TEST_CASE("settings find: a result opens its tab with the row lit until the pointer moves") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_find_jump\\");

    REQUIRE(host.settingsFind("gap reference") > 0);
    host.draw();
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("gap_bar.reference", &x, &y));
    host.clickAt(x, y);
    CHECK(host.activeTab() == "Gap Bar");
    CHECK(host.settingsFindLanding() == "gap_bar.reference");

    host.injectMouse(true, x + 0.01f, y, 0);
    host.draw();
    CHECK(host.settingsFindLanding().empty());

    // The box keeps the query on the result's tab (it read as cleared while
    // still holding it), and clicking it brings the results back.
    bool held = false;
    for (const auto& r : host.hudStringRows(PluginHost::HUD_SETTINGS)) {
        if (r.text == "gap refer") held = true;   // the field's 9 visible characters
    }
    CHECK_MESSAGE(held, "the Find box looks empty after a jump while it still holds the query");
    float fx = 0.0f, fy = 0.0f;
    REQUIRE(host.settingsRegionCenter("find.field", &fx, &fy));
    host.clickAt(fx, fy);
    CHECK(host.activeTab() == "Find");
    CHECK(host.settingsRegionCenter("gap_bar.reference", &x, &y));
}

TEST_CASE("settings find: the field opens the page, and closing the menu goes back") {
    PluginHost host(dllPath());
    start(host, "Z:\\tmp\\mxbmrp3-tests\\settings_find_field\\");

    float x = 0.0f, y = 0.0f;
    REQUIRE_MESSAGE(host.settingsRegionCenter("find.field", &x, &y), "no Find field in the title band");
    host.clickAt(x, y);
    CHECK(host.activeTab() == "Find");
    if (host.hasHotkeyCapture()) CHECK(host.hotkeyCapturing());

    host.showSettings(false);
    host.showSettings(true);
    host.draw();
    CHECK(host.activeTab() == "Standings");
}
