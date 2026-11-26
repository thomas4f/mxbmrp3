// ============================================================================
// tests/integration/tests/default_positions_test.cpp
// What a fresh install draws (the Map and the Toggles strip, both master
// switches off), where every other HUD, widget and deck opens
// (hud/default_slots.h, DeckHud::resetToDefaults), the v11 migration that moves
// an upgraded INI there; an upgraded file keeps its own on/off.
//
// THE BUG THIS PINS. Until 1.33 each default-off HUD had a spot of its own
// choosing, and switching one on from the Toggles strip showed it wherever that
// was: twelve sat partly or wholly under a HUD that ships on (Timing, Notices,
// Version and Radar under the Dash, Charts entirely under the Map, a right-column
// cascade starting under the Map and running into Gear and Speed), the Gamepad
// hung off the bottom of the screen, and the right column ran under the Toggles
// strip. Nothing asserted a default position against any other panel, so each
// HUD added made the pile a little worse.
//
// So every default-off HUD is switched on alone at its fresh default and must be
// on screen and clear of every HUD that ships on -- a property over the whole
// list, not a per-panel spot check -- and the groups' alignment lines are pinned
// against the drawn panels they derive from, which is what the cell numbers in
// default_slots.h cannot see for themselves. UNTHEMED, which a fresh install is
// and the stored defaults are derived for (see center_stack.h).
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#include "deck_ids.h"
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using Edges = PluginHost::ScreenEdges;

// What a fresh install draws: nothing else, so a new player starts from a clean
// screen and finds the rest on the strip.
const char* const kShippedOn[] = { "map_hud", "toggle_strip_widget" };

// On, under the Widgets switch that ships off: one click on the strip's
// Widgets icon shows them. Every other HUD clears them too.
const char* const kWidgetSet[] = {
    "position_widget", "lap_widget", "gear_widget", "speed_widget", "rpm_widget",
};

// On, under the Decks switch that ships off. Placed where the widget set sits
// (Thomas, option A: a family each, overlapping only with both switches on),
// and the Carousel where the Dash opens (Thomas, option A again: it carries
// the notices, as the Dash does in the classic layout).
const char* const kStarterDecks[] = {
    "timing_deck", "race_deck", "speed_deck", "fuel_deck", "carousel_deck",
};

// Every HUD and widget a player can switch on that ships off. Benchmark is left
// out (developer mode only).
const char* const kShippedOff[] = {
    "standings_hud", "dash_hud", "friends_hud", "radar_hud", "lap_log_hud", "ideal_lap_hud", "telemetry_hud", "performance_hud",
    "delta_trace_hud", "records_hud", "session_charts_hud", "fmx_hud", "stats_hud",
    "event_log_hud", "stream_chat_hud", "rumble_hud", "time_widget", "clock_widget",
    "session_widget", "crash_widget", "speedo_widget", "tacho_widget", "timing_hud",
    "gap_bar_hud", "bars_widget", "notices_hud", "fuel_widget",
    "gamepad_widget", "lean_widget", "slide_widget", "wheelie_widget", "gforce_widget", "compass_widget",
    "tyre_temp_widget", "ecu_widget", "prestige_widget", "pitboard_hud", "now_playing_widget",
    "stance_widget",
};

// The default-off HUDs that draw nothing here without content: no notice up, no
// prestige level, no pit board to show. Every other one must draw, so a HUD that
// stops drawing cannot pass by being skipped.
bool contentGated(const char* name) {
    static const char* const kGated[] = { "notices_hud", "prestige_widget", "pitboard_hud",
                                          "stance_widget", "friends_hud" };
    for (const char* g : kGated)
        if (std::string(g) == name) return true;
    return false;
}

std::vector<TrackSegmentRow> circleTrack(int segs = 64, float trackLen = 1600.0f) {
    std::vector<TrackSegmentRow> v(segs);
    const float radius = trackLen / (2.0f * 3.14159265f);
    for (int i = 0; i < segs; ++i) {
        v[i].type = 1;
        v[i].length = trackLen / segs;
        v[i].radius = radius;
    }
    return v;
}

void freshSaveDir(const char* saveWin) {
    const std::string dir = std::string(saveWin) + "mxbmrp3";
    std::filesystem::create_directories(dir);
    std::remove((dir + "\\mxbmrp3_settings.ini").c_str());
}

// On track with a field, a track for the Map, and the cursor up so the Toggles
// strip draws.
void onTrack(PluginHost& host) {
    host.eventInit("Southwick", "Thomas", 1600.0f);
    host.raceEvent("Southwick", 2);
    host.session(6, 10, 0);
    host.addEntry(4, "Thomas");
    for (int n = 10; n < 20; ++n) host.addEntry(n, "Rider");
    host.runInit(6);
    host.trackCenterline(circleTrack(), { 1.0f, 400.0f, 1200.0f, 0.0f });
    host.settleStartupMessages();
    host.injectMouse(true, 0.45f, 0.5f, 0); host.draw();
    host.injectMouse(true, 0.5f, 0.5f, 0); host.draw();
    host.draw();
}

// THE ACCEPTED OVERLAPS. The right column has 24 cells under the Map above Gear
// and Speed, and four of its members are taller, so they end inside both: Charts
// with its one default chart, Performance and Stats (27 cells, one into Gear)
// and Records (32, six into Gear). FMX (26) ends on Gear's top edge.
// Thomas chose one column over a second one under Standings (2026-10-23: HUDs
// previewed from the Toggles strip opening left, then right, then left were hard
// to follow), as he chose it for Charts before (2026-10-22): an overlap is the
// player's to move. Named here, with how many cells each reaches into Gear (FMX's
// none is pinned, not accepted), so it is a decision, not a hole: any other overlap fails, and the groups case
// pins the reach.
struct TallMember { const char* name; int intoGear; };
const TallMember kTallRightColumn[] = {
    { "session_charts_hud", 1 }, { "performance_hud", 1 }, { "stats_hud", 1 },
    { "fmx_hud", 0 }, { "records_hud", 6 },
};

bool inList(const char* name, const char* const* list, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (std::string(list[i]) == name) return true;
    return false;
}

bool acceptedOverlap(const char* off, const char* on) {
    if (std::string(on) != "gear_widget" && std::string(on) != "speed_widget") return false;
    for (const TallMember& m : kTallRightColumn)
        if (m.intoGear > 0 && std::string(off) == m.name) return true;
    return false;
}

bool drawn(const Edges& e) { return e.r > e.l && e.b > e.t; }
bool overlaps(const Edges& a, const Edges& b) {
    return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;
}
std::string str(const Edges& e) {
    return "[" + std::to_string(e.l) + ".." + std::to_string(e.r) + " x " +
           std::to_string(e.t) + ".." + std::to_string(e.b) + "]";
}

// One HUD switched on alone, drawn, measured, and switched back off.
Edges alone(PluginHost& host, const char* name) {
    if (!host.setHudVisibleBoth(name, true)) return {};
    host.draw();
    host.draw();
    const Edges e = host.hudScreenEdges(name);
    host.setHudVisibleBoth(name, false);
    return e;
}

}  // namespace

TEST_CASE("default positions: every default-off HUD opens on screen, clear of every HUD that ships on") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\default_positions\\";
    freshSaveDir(saveWin);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    onTrack(host);
    // The widget set is one click away, so it is checked against like the Map
    host.setMasterEnabled(PluginHost::Master::Widgets, true);
    host.draw();

    std::vector<std::pair<const char*, Edges>> on;
    for (const char* name : kShippedOn) {
        REQUIRE_MESSAGE(host.hudVisible(name) == 1, name << " no longer ships on: update kShippedOn");
        const Edges e = host.hudScreenEdges(name);
        CAPTURE(name);
        REQUIRE_MESSAGE(drawn(e), name << " drew nothing, so nothing could be checked against it");
        on.emplace_back(name, e);
    }
    for (const char* name : kWidgetSet) {
        REQUIRE_MESSAGE(host.hudVisible(name) == 1, name << " left the widget set: update kWidgetSet");
        const Edges e = host.hudScreenEdges(name);
        CAPTURE(name);
        REQUIRE(drawn(e));
        for (const char* shipped : kShippedOn) CHECK_FALSE(overlaps(e, host.hudScreenEdges(shipped)));
        on.emplace_back(name, e);
    }

    int measured = 0;
    for (const char* name : kShippedOff) {
        const int vis = host.hudVisible(name);
        if (vis < 0) continue;   // not registered on this game (Tyre Temp, ECU)
        CAPTURE(name);
        CHECK_MESSAGE(vis == 0, name << " ships on now: move it to kShippedOn");
        const Edges e = alone(host, name);
        if (!drawn(e)) {
            CHECK_MESSAGE(contentGated(name), name << " drew nothing alone: add it to contentGated() "
                                                       "if it needs content to draw");
            continue;
        }
        ++measured;
        INFO(name << " " << str(e));
        CHECK(e.l >= 0);
        CHECK(e.t >= 0);
        CHECK(e.r <= 1000000);
        CHECK(e.b <= 1000000);
        for (const auto& [onName, o] : on) {
            if (acceptedOverlap(name, onName)) continue;
            INFO("ships on: " << onName << " " << str(o));
            CHECK_FALSE(overlaps(e, o));
        }
    }
    CHECK(measured >= 27);   // every list HUD and widget that draws without content
    host.shutdown();
}

// A new install draws the Map and the strip and nothing else: the widget set and
// the starter decks are on, but held by their switches, which ship off.
TEST_CASE("default positions: a fresh install draws only the Map and the Toggles strip") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\default_positions_clean\\";
    freshSaveDir(saveWin);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    onTrack(host);
    for (const char* name : kShippedOn) {
        CAPTURE(name);
        CHECK(drawn(host.hudScreenEdges(name)));
    }
    for (const char* name : kShippedOff) {
        if (host.hudVisible(name) < 0) continue;
        CAPTURE(name);
        CHECK(host.hudVisible(name) == 0);
    }
    for (const char* name : kWidgetSet) {
        CAPTURE(name);
        CHECK(host.hudVisible(name) == 1);
        CHECK(host.hudHeldBack(name) == 1);
    }
    for (const char* name : kDeckIds) {
        if (host.hudVisible(name) < 0) continue;
        CAPTURE(std::string(name));
        const bool starter = inList(name, kStarterDecks, std::size(kStarterDecks));
        CHECK(host.hudVisible(name) == (starter ? 1 : 0));
        CHECK(host.hudHeldBack(name) == 1);
    }

    // One click each: the widget set, then the starter decks
    host.setMasterEnabled(PluginHost::Master::Widgets, true);
    host.setMasterEnabled(PluginHost::Master::Decks, true);
    host.draw();
    host.draw();
    for (const char* name : kWidgetSet) {
        CAPTURE(name);
        CHECK(host.hudHeldBack(name) == 0);
        CHECK(drawn(host.hudScreenEdges(name)));
    }
    // Where they open is the last case's (it draws them with deck art)
    for (const char* name : kStarterDecks) {
        CAPTURE(name);
        CHECK(host.hudHeldBack(name) == 0);
    }
    host.shutdown();
}

// No migration: an upgraded file states the Widgets switch (every file since
// it shipped does), so its widgets stay as they were, and no released file has
// a Decks key or a deck, so its starter decks stay held like a new install's.
TEST_CASE("default positions: an upgraded file keeps its widgets and gets the decks switched off") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\default_positions_masters\\";
    freshSaveDir(saveWin);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    host.writeSettingsFile(saveWin, "[Settings]\nversion=12\n\n[Messages]\ndashTried=1\n\n"
                                    "[Global]\nwidgetsEnabled=1\n\n"
                                    "[StandingsHud]\nvisible=1\n\n[PositionWidget]\nvisible=1\n");
    host.loadSettings(saveWin);
    onTrack(host);
    CHECK(drawn(host.hudScreenEdges("standings_hud")));
    CHECK(host.hudHeldBack("position_widget") == 0);
    for (const char* name : kStarterDecks) {
        CAPTURE(name);
        CHECK(host.hudHeldBack(name) == 1);
    }
    host.shutdown();
}

TEST_CASE("default positions: the groups line up on the HUDs that ship on") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\default_positions_groups\\";
    freshSaveDir(saveWin);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasLayoutCells());
    // Friends draws nothing until a friend is online, unless it is set to Always.
    host.writeSettingsFile(saveWin, "[Settings]\nversion=11\n\n[Messages]\ndashTried=1\n\n"
                                    "[FriendsHud]\nshowMode=0\n");
    host.loadSettings(saveWin);
    host.setMasterEnabled(PluginHost::Master::Widgets, true);   // Gear marks the column's floor
    onTrack(host);
    const auto cells = host.layoutCells();
    const int cellW = static_cast<int>(cells.cellW * 1e6), cellH = static_cast<int>(cells.cellH * 1e6);
    // A twentieth of a cell: the cell numbers in default_slots.h are rounded.
    const int tolW = cellW / 20, tolH = cellH / 20;

    const Edges map = host.hudScreenEdges("map_hud");
    const Edges strip = host.hudScreenEdges("toggle_strip_widget");
    const Edges gear = host.hudScreenEdges("gear_widget");
    REQUIRE(drawn(map));
    REQUIRE(drawn(strip));
    REQUIRE(drawn(gear));

    // The Map one cell clear of the Toggles strip (DefaultSlots::RIGHT_EDGE_CELLS).
    INFO("map " << str(map) << " strip " << str(strip));
    CHECK(std::abs((strip.l - map.r) - cellW) <= tolW);

    // RIGHT COLUMN: right edges on the Map's, two cells under it, above Gear. The
    // circle track is the square case, the Map's tallest box (DefaultSlots).
    for (const char* name : { "friends_hud", "telemetry_hud", "delta_trace_hud", "rumble_hud",
                              "event_log_hud", "stream_chat_hud", "lap_log_hud", "ideal_lap_hud",
                              "speedo_widget", "tacho_widget" }) {
        const Edges e = alone(host, name);
        CAPTURE(name);
        INFO(str(e));
        REQUIRE(drawn(e));
        CHECK(std::abs(e.r - map.r) <= tolW);
        CHECK(std::abs((e.t - map.b) - 2 * cellH) <= tolH);
        CHECK(e.b <= gear.t - 2 * cellH + tolH);
    }
    // The taller members too, reaching no further into Gear and Speed than
    // acceptedOverlap() says.
    for (const TallMember& m : kTallRightColumn) {
        const char* name = m.name;
        const int intoGear = m.intoGear;
        const Edges e = alone(host, name);
        CAPTURE(name);
        INFO(str(e) << " gear " << str(gear));
        REQUIRE(drawn(e));
        CHECK(std::abs(e.r - map.r) <= tolW);
        CHECK(std::abs((e.t - map.b) - 2 * cellH) <= tolH);
        CHECK(e.b <= gear.t + intoGear * cellH + tolH);
    }

    // UNDER THE DASH: the Gap Bar and Timing share one top (Notices too, which
    // draws nothing here without a notice; center_stack_theme_test measures it), and
    // Timing is the taller box, so the centre group below keys off its bottom.
    const Edges gapBar = alone(host, "gap_bar_hud");
    const Edges timing = alone(host, "timing_hud");
    REQUIRE(drawn(gapBar));
    REQUIRE(drawn(timing));
    INFO("gap bar " << str(gapBar) << " timing " << str(timing));
    CHECK(timing.t == gapBar.t);
    CHECK(timing.b >= gapBar.b);

    // CENTRE: centred, two cells under Timing, ending above the widget strip's
    // line (Gear's top) so it covers none of the strip's default-off widgets.
    for (const char* name : { "radar_hud" }) {
        const Edges e = alone(host, name);
        CAPTURE(name);
        INFO(str(e) << " timing " << str(timing) << " gear " << str(gear));
        REQUIRE(drawn(e));
        CHECK(std::abs((e.l + e.r) - 1000000) <= 4);
        CHECK(std::abs((e.t - timing.b) - 2 * cellH) <= tolH);
        CHECK(e.b <= gear.t - 2 * cellH + tolH);
    }

    // LOW LEFT: the Gamepad in the bottom-left corner, on Standings' left edge,
    // its bottom on the screen's last whole cell, a cell clear of Standings above.
    const Edges standings = alone(host, "standings_hud");
    const Edges pad = alone(host, "gamepad_widget");
    REQUIRE(drawn(standings));
    REQUIRE(drawn(pad));
    INFO("gamepad " << str(pad) << " standings " << str(standings));
    CHECK(std::abs(pad.l - standings.l) <= tolW);
    CHECK(pad.b <= 1000000);
    CHECK(1000000 - pad.b < cellH);
    CHECK(pad.t >= standings.b + cellH - tolH);
    host.shutdown();
}

// An INI from 1.32 carries every HUD's old default position. A HUD still there
// moves to the new spot; one the player dragged stays where they put it.
TEST_CASE("default positions: an upgraded INI moves only what still sits on the old default") {
    // The new spots, from a fresh install.
    Edges freshTelemetry, freshLapLog, freshMap, freshGapBar, freshTiming, freshCharts;
    {
        const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\default_positions_fresh\\";
        freshSaveDir(saveWin);
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE(host.startup(saveWin) >= 0);
        REQUIRE(host.hasLayoutCells());
        onTrack(host);
        freshTelemetry = alone(host, "telemetry_hud");
        freshLapLog = alone(host, "lap_log_hud");
        freshGapBar = alone(host, "gap_bar_hud");
        freshTiming = alone(host, "timing_hud");
        freshCharts = alone(host, "session_charts_hud");
        freshMap = host.hudScreenEdges("map_hud");
        host.shutdown();
    }
    REQUIRE(drawn(freshTelemetry));
    REQUIRE(drawn(freshLapLog));
    REQUIRE(drawn(freshMap));
    REQUIRE(drawn(freshGapBar));
    REQUIRE(drawn(freshTiming));
    REQUIRE(drawn(freshCharts));

    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\default_positions_upgrade\\";
    freshSaveDir(saveWin);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    const auto cells = host.layoutCells();
    char ini[1536];
    // Telemetry and the Lap Log on their 1.32 defaults (cells 133,38 and 1,64; the
    // Lap Log's in the Race profile too), the Map on its old anchor, the Gap Bar and
    // Timing on the centre stack's old top (the Dash's spot now), Charts on its old
    // spot over the Map (it moves under it); Stats dragged.
    std::snprintf(ini, sizeof(ini),
                  "[Settings]\nversion=10\n\n[Messages]\ndashTried=1\n\n"
                  "[TelemetryHud]\nvisible=1\noffsetX=%f\noffsetY=%f\n\n"
                  "[LapLogHud]\nvisible=1\noffsetX=%f\noffsetY=%f\n\n"
                  "[LapLogHud:Race]\noffsetX=%f\n\n"
                  "[StatsHud]\nvisible=1\noffsetX=0.300000\noffsetY=0.400000\n\n"
                  "[GapBarHud]\nvisible=1\noffsetX=0.500000\noffsetY=%f\n\n"
                  "[TimingHud]\nvisible=1\noffsetX=0.500000\noffsetY=%f\n\n"
                  "[SessionChartsHud]\nvisible=1\noffsetX=0.731500\noffsetY=%f\n\n"
                  "[MapHud]\nanchorX=0.994125\nanchorY=0.011304\n",
                  133 * cells.cellW, 38 * cells.cellH, 1 * cells.cellW, 64 * cells.cellH,
                  1 * cells.cellW, 1 * cells.cellH, 1 * cells.cellH, 1 * cells.cellH);
    host.writeSettingsFile(saveWin, ini);
    host.loadSettings(saveWin);
    onTrack(host);
    host.draw();

    const Edges telemetry = host.hudScreenEdges("telemetry_hud");
    const Edges lapLog = host.hudScreenEdges("lap_log_hud");
    const Edges stats = host.hudScreenEdges("stats_hud");
    const Edges map = host.hudScreenEdges("map_hud");
    const Edges gapBar = host.hudScreenEdges("gap_bar_hud");
    const Edges timing = host.hudScreenEdges("timing_hud");
    const Edges charts = host.hudScreenEdges("session_charts_hud");
    INFO("gap bar " << str(gapBar) << " fresh " << str(freshGapBar));
    INFO("timing " << str(timing) << " fresh " << str(freshTiming));
    INFO("charts " << str(charts) << " fresh " << str(freshCharts));
    INFO("telemetry " << str(telemetry) << " fresh " << str(freshTelemetry));
    INFO("lap log " << str(lapLog) << " fresh " << str(freshLapLog));
    INFO("map " << str(map) << " fresh " << str(freshMap));
    REQUIRE(drawn(telemetry));
    REQUIRE(drawn(lapLog));
    REQUIRE(drawn(stats));
    CHECK(std::abs(telemetry.l - freshTelemetry.l) <= 2);
    CHECK(std::abs(telemetry.t - freshTelemetry.t) <= 2);
    CHECK(std::abs(lapLog.l - freshLapLog.l) <= 2);
    CHECK(std::abs(lapLog.t - freshLapLog.t) <= 2);
    CHECK(std::abs(map.r - freshMap.r) <= 2);
    REQUIRE(drawn(gapBar));
    CHECK(std::abs(gapBar.l - freshGapBar.l) <= 2);
    CHECK(std::abs(gapBar.t - freshGapBar.t) <= 2);
    REQUIRE(drawn(timing));
    CHECK(std::abs(timing.l - freshTiming.l) <= 2);
    CHECK(std::abs(timing.t - freshTiming.t) <= 2);
    REQUIRE(drawn(charts));
    CHECK(std::abs(charts.l - freshCharts.l) <= 2);
    CHECK(std::abs(charts.t - freshCharts.t) <= 2);
    // Dragged: left alone.
    CHECK(std::abs(stats.l - 300000) <= 2);
    CHECK(std::abs(stats.t - 400000) <= 2);
    host.shutdown();
}

// The decks ship off and draw only with a deck layout, so Cabinet Dark (whose
// folder holds the shipped two) is staged first. Each opens clear of every HUD that ships on, and of
// every other deck.
TEST_CASE("default positions: every deck opens on screen, clear of every HUD that ships on") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\default_positions_decks\\";
    freshSaveDir(saveWin);
    // The sync copies the theme into the shared tree, where it would join every
    // later case's theme list (a cycle's count, the pseudo-locale's widths)
    struct SyncedThemeCleanup {
        ~SyncedThemeCleanup() {
            std::error_code ec;
            std::filesystem::remove_all("plugins/mxbmrp3_data/themes/cabinet-dark", ec);
        }
    } themeGoes;
    // Cabinet, and Slant (the widest style, its deck_ pictures beside it)
    {
        const std::filesystem::path to = std::filesystem::path(std::string(saveWin) + "mxbmrp3\\themes") / "cabinet-dark";
        std::filesystem::create_directories(to);
        for (const auto& f : std::filesystem::directory_iterator(std::filesystem::path(MXB_REPO_DATA_DIR "/themes/cabinet-dark")))
            std::filesystem::copy_file(f.path(), to / f.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing);
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    onTrack(host);

    std::vector<std::pair<const char*, Edges>> on;
    for (const char* name : kShippedOn) on.emplace_back(name, host.hudScreenEdges(name));
    int measured = 0;
    for (const char* style : { "0", "1" }) {
    INFO("style " << std::string(style));
    std::string ini = "[Settings]\nversion=11\n\n[Messages]\ndashTried=1\n";
    ini += std::string("\n[Decks]\nslanted=") + style + "\n";
    host.writeSettingsFile(saveWin, ini);
    host.loadSettings(saveWin);
    host.setMasterEnabled(PluginHost::Master::Decks, true);
    host.draw();
    std::vector<std::pair<std::string, Edges>> decks;
    for (const char* deck : kDeckIds) {
        const std::string name = deck;
        if (host.hudVisible(deck) < 0) continue;   // Combo: not on this game
        CAPTURE(name);
        const Edges e = alone(host, deck);
        REQUIRE_MESSAGE(drawn(e), name << " drew nothing alone");
        ++measured;
        INFO(name << " " << str(e));
        CHECK(e.l >= 0);
        CHECK(e.t >= 0);
        CHECK(e.r <= 1000000);
        CHECK(e.b <= 1000000);
        for (const auto& [onName, o] : on) {
            INFO("ships on: " << std::string(onName) << " " << str(o));
            CHECK_FALSE(overlaps(e, o));
        }
        for (const auto& [otherName, d] : decks) {
            INFO("deck: " << otherName << " " << str(d));
            // Slanted, side by side decks nest: each box's leaning end reaches
            // under its neighbour's by the lean, under a cell, as laid out in game
            const int tuck = style[0] == '1' ? static_cast<int>(host.layoutCells().cellW * 1e6) : 0;
            CHECK_FALSE(overlaps(Edges{ e.l, e.t, e.r - tuck, e.b }, Edges{ d.l, d.t, d.r - tuck, d.b }));
        }
        decks.emplace_back(name, e);
    }
    {
        // Each deck's slot, in whole cells (DeckHud::resetToDefaults), in both
        // styles: the layout Thomas set in game, Slanted, copied cell for cell,
        // but the Notice deck at the Notices HUD's top, centred in Regular
        struct Slot { const char* id; int x, y; };
        static const Slot kSlots[] = {
            {"timing_deck", 1, 1},     {"race_deck", 1, 11},      {"standings_deck", 1, 22},
            {"gap_deck", 1, 55},       {"laplog_deck", 1, 65},    {"rival_deck", 1, 75},
            {"wheelie_deck", 32, 55},  {"spin_deck", 32, 65},     {"slide_deck", 32, 75},
            {"notice_deck", 76, 11},   {"session_deck", 76, 76},  {"combo_deck", 87, 66},
            {"inputs_deck", 107, 76},  {"media_deck", 118, 66},   {"speed_deck", 138, 76},
            {"fuel_deck", 159, 76},    {"carousel_deck", 76, 1},
        };
        const auto cells = host.layoutCells();
        const double cellW = cells.cellW * 1e6, cellH = cells.cellH * 1e6;
        int placed = 0;
        for (const Slot& slot : kSlots) {
            for (const auto& [n, e] : decks) {
                if (n != slot.id) continue;
                INFO(n << " " << str(e));
                CHECK(std::abs(e.l - slot.x * cellW) <= cellW / 4);
                CHECK(std::abs(e.t - slot.y * cellH) <= cellH / 4);
                ++placed;
            }
        }
        CHECK(placed == static_cast<int>(decks.size()));
    }
    if (style[0] == '0') {
        // What the Notice deck's slot is FOR, not just its cell: it opens
        // where the Notices HUD does, its top on that HUD's top and centred on
        // screen. The Notices HUD draws only with a notice up, so its top is
        // read off the Timing HUD, which shares CenterStack::underDashBoxTop.
        const Edges timing = alone(host, "timing_hud");
        REQUIRE(drawn(timing));
        const double cellW = host.layoutCells().cellW * 1e6, cellH = host.layoutCells().cellH * 1e6;
        for (const auto& [n, e] : decks) {
            if (n != "notice_deck") continue;
            INFO("notice_deck " << str(e) << " timing_hud " << str(timing));
            CHECK(std::abs(e.t - timing.t) <= cellH / 4);
            CHECK(std::abs((e.l + e.r) / 2.0 - 500000.0) <= cellW / 4);
        }
        // The Carousel's slot is the Dash's: centred on screen at the top, so
        // its notices land where the classic layout's do
        int carousels = 0;
        for (const auto& [n, e] : decks) {
            if (n != "carousel_deck") continue;
            INFO("carousel_deck " << str(e));
            CHECK(std::abs((e.l + e.r) / 2.0 - 500000.0) <= cellW / 4);
            ++carousels;
        }
        CHECK(carousels == 1);
    }
    }
    // Every deck the game has, in both styles: Combo and Wheelie are the
    // bike games' (FMX's)
    CHECK(measured == 2 * (static_cast<int>(std::size(kDeckIds)) - (host.hasFmx() ? 0 : 2)));
    host.shutdown();
}
