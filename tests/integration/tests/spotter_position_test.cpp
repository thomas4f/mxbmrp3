// ============================================================================
// tests/integration/tests/spotter_position_test.cpp
// Where the Spotter's subtitle card opens (low centre, hud/default_slots.h), and
// the v12 migration that carries an upgraded INI onto it.
//
// THE BUG THIS PINS. Until 1.33 the card opened at cells 20,24, upper left of
// centre, while its own comment said "lower-center": it stored its LEFT edge and
// is as wide as its cue, so no stored offset could centre it -- a short cue sat
// left of a long one. It is centre-anchored now (offsetX is its centre), which
// changes what an upgraded INI's offsetX means, so the migration either moves a
// card still on the old spot to the new one, or re-anchors one the player
// dragged by half the preview card (SpotterWidget::previewCardWidth). The
// dragged cases are what pin that static against the drawn card: if the two
// drift apart, the preview no longer lands where the player put it.
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

void freshSaveDir(const char* saveWin) {
    const std::string dir = std::string(saveWin) + "mxbmrp3";
    std::filesystem::create_directories(dir);
    std::remove((dir + "\\mxbmrp3_settings.ini").c_str());
}

bool drawn(const Edges& e) { return e.r > e.l && e.b > e.t; }
bool overlaps(const Edges& a, const Edges& b) {
    return a.l < b.r && b.l < a.r && a.t < b.b && b.t < a.b;
}
std::string str(const Edges& e) {
    return "[" + std::to_string(e.l) + ".." + std::to_string(e.r) + " x " +
           std::to_string(e.t) + ".." + std::to_string(e.b) + "]";
}

// On track, and the Spotter tab open: with no cue live the card draws only as
// the positioning preview, which is the card a player drags into place.
void onTrackPreviewing(PluginHost& host) {
    host.eventInit("Southwick", "Thomas", 1600.0f);
    host.raceEvent("Southwick", 2);
    host.session(6, 10, 0);
    host.addEntry(4, "Thomas");
    host.runInit(6);
    host.settleStartupMessages();
    host.spotterSubtitles(true);
    host.injectMouse(true, 0.45f, 0.5f, 0); host.draw();
    host.injectMouse(true, 0.5f, 0.5f, 0); host.draw();
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Spotter"));
    host.draw();
    host.draw();
}

// The preview's edges after loading `ini` into the running plugin: one plugin for
// every file, the way a player's RELOAD_CONFIG reads one.
Edges previewAfterLoading(PluginHost& host, const char* saveWin, const char* ini) {
    host.writeSettingsFile(saveWin, ini);
    host.loadSettings(saveWin);
    host.draw();
    host.draw();
    return host.hudScreenEdges("spotter_widget");
}

}  // namespace

TEST_CASE("spotter position: a fresh install opens the card at low centre, clear of every HUD that ships on") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\spotter_position_fresh\\";
    freshSaveDir(saveWin);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasLayoutCells());
    // Shipped-on edges with the menu shut (the menu draws over everything).
    host.eventInit("Southwick", "Thomas", 1600.0f);
    host.raceEvent("Southwick", 2);
    host.session(6, 10, 0);
    host.addEntry(4, "Thomas");
    host.runInit(6);
    host.settleStartupMessages();
    host.injectMouse(true, 0.45f, 0.5f, 0); host.draw();
    host.injectMouse(true, 0.5f, 0.5f, 0); host.draw();
    host.draw();
    std::vector<std::pair<const char*, Edges>> on;
    for (const char* name : kShippedOn) {
        const Edges e = host.hudScreenEdges(name);
        if (drawn(e)) on.emplace_back(name, e);
    }
    // No cue, no preview: the card draws nothing and eats no clicks.
    host.spotterSubtitles(true);
    host.draw();
    CHECK_FALSE(drawn(host.hudScreenEdges("spotter_widget")));

    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Spotter"));
    host.draw();
    host.draw();
    const Edges e = host.hudScreenEdges("spotter_widget");
    const auto cells = host.layoutCells();
    const int cellH = static_cast<int>(cells.cellH * 1e6);
    INFO("spotter " << str(e));
    REQUIRE(drawn(e));
    CHECK(std::abs((e.l + e.r) - 1000000) <= 4);           // centred
    CHECK(std::abs(e.t - 84 * cellH) <= cellH / 20);       // DefaultSlots::LOW_CENTRE_TOP_CELL
    CHECK(e.b <= 1000000);                                 // on screen
    const Edges gear = host.hudScreenEdges("gear_widget");
    CHECK(e.t >= gear.b + 2 * cellH - cellH / 20);         // two cells under the widget strip
    for (const auto& [onName, o] : on) {
        INFO("ships on: " << onName << " " << str(o));
        CHECK_FALSE(overlaps(e, o));
    }
    host.shutdown();
}

TEST_CASE("spotter position: an upgraded INI moves a card on the old spot, and keeps a dragged one where it was") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\spotter_position_upgrade\\";
    freshSaveDir(saveWin);
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasScreenEdges());
    REQUIRE(host.hasLayoutCells());
    const auto cells = host.layoutCells();
    onTrackPreviewing(host);
    const Edges fresh = host.hudScreenEdges("spotter_widget");
    REQUIRE(drawn(fresh));

    // Still on the 1.32 default (cells 20,24): moves to the fresh spot, in the
    // base section and a profile.
    {
        char ini[512];
        std::snprintf(ini, sizeof(ini),
                      "[Settings]\nversion=11\n\n[Messages]\ndashTried=1\n\n"
                      "[SpotterWidget]\nvisible=1\noffsetX=%f\noffsetY=%f\n\n"
                      "[SpotterWidget:Race]\noffsetX=%f\noffsetY=%f\n",
                      20 * cells.cellW, 24 * cells.cellH, 20 * cells.cellW, 24 * cells.cellH);
        const Edges e = previewAfterLoading(host, saveWin, ini);
        INFO("moved " << str(e) << " fresh " << str(fresh));
        REQUIRE(drawn(e));
        CHECK(std::abs(e.l - fresh.l) <= 2);
        CHECK(std::abs(e.t - fresh.t) <= 2);
    }

    // Dragged, at two scales: the preview's left edge stays where the player
    // put it. Within half a cell, the most fitPanelToGrid's rounding of the drawn
    // box can move it (previewCardWidth is the unrounded width).
    for (const char* scale : { "1.000000", "1.500000" }) {
        CAPTURE(scale);
        const std::string ini = std::string("[Settings]\nversion=11\n\n[Messages]\ndashTried=1\n\n"
                                            "[SpotterWidget]\nvisible=1\noffsetX=0.300000\n"
                                            "offsetY=0.400000\nscale=") + scale + "\n";
        const Edges e = previewAfterLoading(host, saveWin, ini.c_str());
        INFO("dragged " << str(e));
        REQUIRE(drawn(e));
        CHECK(std::abs(e.l - 300000) <= 2750);   // half a cell (0.0055 wide)
        CHECK(std::abs(e.t - 400000) <= 2);
    }

    // Written by this build (v12): offsetX already means the centre, so nothing shifts.
    {
        const Edges e = previewAfterLoading(
            host, saveWin,
            "[Settings]\nversion=12\n\n[Messages]\ndashTried=1\n\n"
            "[SpotterWidget]\nvisible=1\noffsetX=0.300000\noffsetY=0.400000\n");
        INFO("current " << str(e));
        REQUIRE(drawn(e));
        CHECK(std::abs((e.l + e.r) - 600000) <= 4);
    }
    host.shutdown();
}
