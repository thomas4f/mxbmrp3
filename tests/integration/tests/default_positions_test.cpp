// ============================================================================
// tests/integration/tests/default_positions_test.cpp
// Where the default-off HUDs open on a fresh install (hud/default_slots.h), and
// the v11 migration that moves an upgraded INI there.
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

const char* const kShippedOn[] = {
    "standings_hud", "map_hud", "dash_hud", "position_widget", "lap_widget",
    "gear_widget", "speed_widget", "rpm_widget", "toggle_strip_widget",
};

// Every HUD and widget a player can switch on that ships off. Benchmark is left
// out (developer mode only); Friends ships on and draws only with a friend online.
const char* const kShippedOff[] = {
    "radar_hud", "lap_log_hud", "ideal_lap_hud", "telemetry_hud", "performance_hud",
    "delta_trace_hud", "records_hud", "session_charts_hud", "fmx_hud", "stats_hud",
    "event_log_hud", "stream_chat_hud", "rumble_hud", "time_widget", "clock_widget",
    "session_widget", "crash_widget", "speedo_widget", "tacho_widget", "timing_hud",
    "gap_bar_hud", "bars_widget", "notices_hud", "fuel_widget",
    "gamepad_widget", "lean_widget", "gforce_widget", "compass_widget",
    "tyre_temp_widget", "ecu_widget", "prestige_widget", "pitboard_hud", "now_playing_widget",
};

// The default-off HUDs that draw nothing here without content: no notice up, no
// prestige level, no pit board to show. Every other one must draw, so a HUD that
// stops drawing cannot pass by being skipped.
bool contentGated(const char* name) {
    static const char* const kGated[] = { "notices_hud", "prestige_widget", "pitboard_hud" };
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

    std::vector<std::pair<const char*, Edges>> on;
    for (const char* name : kShippedOn) {
        REQUIRE_MESSAGE(host.hudVisible(name) == 1, name << " no longer ships on: update kShippedOn");
        const Edges e = host.hudScreenEdges(name);
        CAPTURE(name);
        REQUIRE_MESSAGE(drawn(e), name << " drew nothing, so nothing could be checked against it");
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
    CHECK(measured >= 25);   // every list HUD and widget that draws without content
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
    const Edges standings = host.hudScreenEdges("standings_hud");
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
