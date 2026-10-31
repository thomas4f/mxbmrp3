// ============================================================================
// tests/integration/tests/standings_show_mode_test.cpp
// THE STANDINGS' SHOW MODE BRINGS THE HUD UP AT THE RIGHT CROSSINGS, AND ONLY
// THEN.
//
// Show mode is the Pitboard's (pitboard_splits_test.cpp), read through the
// same SplitCrossingDetector and HoldTimer: Always, At laps (the line) and At
// splits (each split and the line). Pins:
//   - At splits comes up at a split for the Freeze, then goes down;
//   - At laps stays down at a split and comes up at the line;
//   - Always, the default, is up with no crossing at all (no change for anyone
//     who never touches the setting);
//   - with the Standings tab open it draws anyway, so it can be dragged.
//
// "Up" is read as the HUD having strings at all: a hidden HUD builds none.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr int RACE1 = 6;

bool standingsUp(PluginHost& host) {
    return !host.hudStringRows("standings_hud").empty();
}

// A one-rider race with the Standings on `showMode` (empty = not written) and a 1 s hold.
void setUp(PluginHost& host, const char* name, const char* showMode) {
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    const std::string saveWin = std::string("Z:\\tmp\\mxbmrp3-tests\\") + name + "\\";
    host.startup(saveWin.c_str());
    {
        std::filesystem::create_directories(saveWin + "mxbmrp3");
        std::ofstream ini(saveWin + "mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[StandingsHud]\nvisible=1\nfreezeDuration=1000\n";
        if (showMode[0]) ini << "showMode=" << showMode << "\n";
    }
    host.loadSettings(saveWin.c_str());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    host.classify(RACE1, 0, { { .num = 10, .laps = 0, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.drawWithState(0);
}

// Let the 1 s hold run out.
void waitOutHold(PluginHost& host) {
    Sleep(1100);
    host.drawWithState(0);
}

void crossLine(PluginHost& host) {
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);
}

}  // namespace

TEST_CASE("standings show mode: Always (the default) is up with no crossing") {
    PluginHost host(dllPath());
    setUp(host, "standings_show_always", "");
    CHECK(standingsUp(host));
    host.shutdown();
}

TEST_CASE("standings show mode: At splits comes up at a split for the Freeze, then goes down") {
    PluginHost host(dllPath());
    setUp(host, "standings_show_splits", "SPLITS");
    CHECK_FALSE(standingsUp(host));

    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.drawWithState(0);
    CHECK(standingsUp(host));

    waitOutHold(host);
    CHECK_FALSE(standingsUp(host));

    crossLine(host);
    CHECK_MESSAGE(standingsUp(host), "At splits also comes up at the line");
    host.shutdown();
}

TEST_CASE("standings show mode: At laps stays down at a split and comes up at the line") {
    PluginHost host(dllPath());
    setUp(host, "standings_show_laps", "LAPS");
    CHECK_FALSE(standingsUp(host));

    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.drawWithState(0);
    CHECK_FALSE(standingsUp(host));

    crossLine(host);
    CHECK(standingsUp(host));

    waitOutHold(host);
    CHECK_FALSE(standingsUp(host));
    host.shutdown();
}

TEST_CASE("standings show mode: the HUD draws while its tab is open, then hides again") {
    PluginHost host(dllPath());
    setUp(host, "standings_show_preview", "SPLITS");
    REQUIRE(host.hasWhatsNew());  // openSettingsTab
    CHECK_FALSE(standingsUp(host));

    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Standings"));
    host.drawWithState(0);
    CHECK_MESSAGE(standingsUp(host), "a HUD up for seconds a lap must draw while its tab is open");

    host.showSettings(false);
    host.drawWithState(0);
    CHECK_FALSE(standingsUp(host));
    host.shutdown();
}
