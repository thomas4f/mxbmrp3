// ============================================================================
// tests/unit/test_settings_find.cpp
// Unit tests for hud/settings/settings_find.h -- the settings Find box's match:
// which rows a query finds, whether by name or only by description, the order
// they come back in, and the value a row's own value helper hands the index.
// Header-only, no game engine. The index filled from real tabs, the page fit and
// the jump are settings_find_test.cpp's (integration).
// ============================================================================
#include "doctest.h"

#include "hud/settings/settings_find.h"

#include <vector>

namespace {
SettingsFind::Entry entry(int tab, const char* label, const char* tooltip) {
    SettingsFind::Entry e;
    e.tab = tab;
    std::snprintf(e.label, sizeof(e.label), "%s", label);
    std::snprintf(e.tooltipId, sizeof(e.tooltipId), "t%d.%s", tab, label);
    e.tooltip = tooltip;
    return e;
}
}  // namespace

TEST_CASE("settings find: name hits come first, then description hits, each in index order") {
    const std::vector<SettingsFind::Entry> index = {
        entry(0, "Freeze", "How long Gap Bar and Lap Log hold a gap."),
        entry(1, "Gap reference", "Lap the gap is measured against."),
        entry(2, "Opacity", "Background opacity."),
        entry(3, "Show gap", "Show gap time text."),
    };
    std::vector<int> hits;
    const int names = SettingsFind::match(index, "gap", hits).names;
    CHECK(names == 2);
    REQUIRE(hits.size() == 3u);
    CHECK(hits[0] == 1);   // Gap reference
    CHECK(hits[1] == 3);   // Show gap
    CHECK(hits[2] == 0);   // Freeze, by its description
}

TEST_CASE("settings find: matching ignores case, and every word must appear") {
    const std::vector<SettingsFind::Entry> index = {
        entry(0, "Gap reference", "Lap the gap is measured against."),
        entry(1, "Gap row", "Show gap comparison row."),
        entry(2, "Live gap row", "Live gap to your PB."),
    };
    std::vector<int> hits;
    CHECK(SettingsFind::match(index, "GAP REF", hits).names == 1);
    REQUIRE(hits.size() == 1u);
    CHECK(hits[0] == 0);

    // "live" is in one label; "pb" only in that row's description: a description hit.
    CHECK(SettingsFind::match(index, "live pb", hits).names == 0);
    REQUIRE(hits.size() == 1u);
    CHECK(hits[0] == 2);
}

// Most HUD rows are labelled just "Visible", "Scale", "Opacity": typed by a
// player as "map" or "map scale", the HUD's name lives in the tab, not the label.
TEST_CASE("settings find: a tab or section name finds its rows, ranked between names and descriptions") {
    std::vector<SettingsFind::Entry> index = {
        entry(0, "Scale", "Size multiplier."),          // Map > Appearance
        entry(1, "Rotate map", "Turn the map."),        // Map > Layout: a name hit
        entry(2, "Scale", "Size multiplier."),          // Radar > Appearance
        entry(3, "Gap reference", "Lap on the map."),   // General: description only
        entry(4, "Marker scale", "Rider dot size."),    // Map > Rider Markers
    };
    index[0].tabName = "Map";
    std::snprintf(index[0].section, sizeof(index[0].section), "Appearance");
    index[1].tabName = "Map";
    index[2].tabName = "Radar";
    index[3].tabName = "General";
    index[4].tabName = "Map";
    std::snprintf(index[4].section, sizeof(index[4].section), "Rider Markers");

    std::vector<int> hits;
    SettingsFind::Counts c = SettingsFind::match(index, "map", hits);
    CHECK(c.names == 1);
    CHECK(c.places == 2);
    CHECK(c.descriptions == 1);
    REQUIRE(hits.size() == 4u);
    CHECK(hits[0] == 1);   // Rotate map, by name
    CHECK(hits[1] == 0);   // Map > Scale, by tab
    CHECK(hits[2] == 4);   // Map > Marker scale, by tab
    CHECK(hits[3] == 3);   // Gap reference, by description

    // One word in the label, one in the tab: a place hit, and not Radar's Scale.
    c = SettingsFind::match(index, "map scale", hits);
    CHECK(c.names == 0);
    CHECK(c.places == 2);
    REQUIRE(hits.size() == 2u);
    CHECK(hits[0] == 0);
    CHECK(hits[1] == 4);

    // A section name counts too.
    c = SettingsFind::match(index, "markers", hits);
    CHECK(c.names == 0);
    CHECK(c.places == 1);
    REQUIRE(hits.size() == 1u);
    CHECK(hits[0] == 4);
}

TEST_CASE("settings find: a tab named by the query comes first, valued by its Visible row") {
    SettingsFind::State s;
    s.indexing = true;
    s.addTab(7, "Map", "map", "Track map.");
    s.tabName = "Map";
    s.addRow(7, "Rotate map", "map.rotation", "Turn the map.");
    s.noteValue("On");
    s.addRow(7, "Visible", "common.visible", "Show or hide.");
    s.noteValue("Off");
    s.tabVisible(0);
    s.addTab(8, "Radar", "radar", "Riders near you, as on a map.");
    s.indexing = false;
    REQUIRE(s.index.size() == 4u);
    CHECK(s.index[0].isTab);
    CHECK(std::string(s.index[0].value) == "Off");
    CHECK(std::string(s.index[3].value).empty());   // no Visible row followed

    s.query = "map";
    s.refresh();
    CHECK(s.counts.tabs == 1);
    CHECK(s.counts.names == 1);
    CHECK(s.counts.places == 1);
    REQUIRE(s.hits.size() == 4u);
    CHECK(s.hits[0] == 0);   // the Map tab
    CHECK(s.hits[1] == 1);   // Rotate map, by name
    CHECK(s.hits[2] == 2);   // Map > Visible, by tab
    CHECK(s.hits[3] == 3);   // the Radar tab, by its description: not a tab hit
}

TEST_CASE("settings find: widgets named by the query have their own group, after tabs") {
    SettingsFind::State s;
    s.indexing = true;
    s.tabName = "Appearance";
    s.addRow(1, "Speed unit", "appearance.speed_unit", "km/h or mph.");
    s.tabName = "Widgets";
    s.widgetRows = true;
    s.addRow(2, "Speed", "widgets.speed", "Current speed.");
    s.addRow(2, "Gear", "widgets.gear", "Gear and speed.");   // by description: not a widget hit
    s.indexing = false;
    std::vector<int> hits;
    const SettingsFind::Counts c = SettingsFind::match(s.index, "speed", hits);
    CHECK(c.widgets == 1);
    CHECK(c.names == 1);
    CHECK(c.descriptions == 1);
    REQUIRE(hits.size() == 3u);
    CHECK(hits[0] == 1);   // the Speed widget
    CHECK(hits[1] == 0);   // Speed unit
    CHECK(hits[2] == 2);   // Gear, by its description
}

TEST_CASE("settings find: an empty or all-space query finds nothing") {
    const std::vector<SettingsFind::Entry> index = { entry(0, "Gap", "Gap chart.") };
    std::vector<int> hits;
    CHECK(SettingsFind::match(index, "", hits).names == 0);
    CHECK(hits.empty());
    CHECK(SettingsFind::match(index, "   ", hits).names == 0);
    CHECK(hits.empty());
}

TEST_CASE("settings find: a row's value reaches only that row, trailing pad cut; its section comes along") {
    SettingsFind::State s;
    s.indexing = true;
    s.setSection("Appearance");
    s.addRow(4, "Opacity", "common.opacity", "Background opacity.");
    s.noteValue("33%      ");   // the padded field a toggle draws
    s.addRow(4, "Scale", "common.scale", "Size.");
    s.addRow(4, "No id", "", "");          // not indexed: no id to land on
    s.noteValue("On");                     // must not land on "Scale"
    s.indexing = false;
    REQUIRE(s.index.size() == 2u);
    CHECK(std::string(s.index[0].value) == "33%");
    CHECK(std::string(s.index[0].section) == "Appearance");
    CHECK(std::string(s.index[1].value).empty());

    // Outside an index pass nothing is taken.
    s.addRow(5, "Later", "x.later", "");
    s.noteValue("Off");
    CHECK(s.index.size() == 2u);
}

TEST_CASE("settings find: refresh recomputes on a new query or a rebuilt index, and resets the page") {
    SettingsFind::State s;
    s.index = { entry(0, "Gap", "Gap chart."), entry(1, "Range", "Gap range.") };
    s.query = "gap";
    s.refresh();
    CHECK(s.hits.size() == 2u);
    CHECK(s.counts.names == 1);
    s.page = 3;
    s.refresh();               // same query, same index: kept
    CHECK(s.page == 3);
    s.query = "range";
    s.refresh();
    CHECK(s.page == 0);
    CHECK(s.hits.size() == 1u);
    s.index.push_back(entry(2, "Range", "Another range."));
    s.matchedFresh = false;    // what an index rebuild does
    s.refresh();
    CHECK(s.hits.size() == 2u);
}
