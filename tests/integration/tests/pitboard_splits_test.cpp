// ============================================================================
// tests/integration/tests/pitboard_splits_test.cpp
// THE PITBOARD'S AT SPLITS BOARD COMES UP AT EVERY CROSSING, AND ONLY THEN.
//
// The board reads crossings through SplitCrossingDetector (split_crossing.h),
// the detector the Timing panel and the gap freezes share. Before that it kept
// its own split caches and keyed the line on the lap TIME changing, so:
//   - two laps in a row with the same time never brought the board up for the
//     second one (the time had not "changed");
//   - a race lap through the pits, which keeps a time but is invalid, came up
//     with that time and INVALID, where the Timing panel shows nothing for a pit
//     lap and the Lap Log says PIT.
// And GP Bikes' third split never showed at all; that one is the detector's
// NUM_SPLITS loop, which this MX Bikes build cannot reach (two splits), so it
// is pinned by the unit test of timeToCrossing and the shared loop the gap
// freezes already exercise.
//
// Also pins the At Splits hold itself (the release review's item 6): the board
// is up for the Freeze setting's duration after a crossing, then down.
//
// "Up" is read as the HUD having strings at all: a hidden board builds none.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr int RACE1 = 6;

bool boardUp(PluginHost& host) {
    return !host.hudStringRows("pitboard_hud").empty();
}

bool boardShows(PluginHost& host, const char* text) {
    for (const auto& row : host.hudStringRows("pitboard_hud")) {
        if (row.text == text) return true;
    }
    return false;
}

// A one-rider race with the Pitboard on At Splits (the default) and a 1 s hold.
void setUp(PluginHost& host, const char* name) {
    REQUIRE(host.loaded());
    REQUIRE(host.hasStringRows());
    const std::string saveWin = std::string("Z:\\tmp\\mxbmrp3-tests\\") + name + "\\";
    host.startup(saveWin.c_str());
    {
        std::filesystem::create_directories(saveWin + "mxbmrp3");
        std::ofstream ini(saveWin + "mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[PitboardHud]\nvisible=1\nfreezeDuration=1000\n";
    }
    host.loadSettings(saveWin.c_str());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.drawWithState(0);
}

// Let the 1 s hold run out.
void waitOutHold(PluginHost& host) {
    Sleep(1100);
    host.drawWithState(0);
}

}  // namespace

TEST_CASE("pitboard: a split brings the board up for the Freeze, then it goes down") {
    PluginHost host(dllPath());
    setUp(host, "pitboard_split_hold");
    CHECK_FALSE(boardUp(host));

    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.drawWithState(0);
    CHECK(boardUp(host));
    CHECK((boardShows(host, "20.0") || boardShows(host, "0:20.0")));  // Compact Times or not

    waitOutHold(host);
    CHECK_FALSE(boardUp(host));

    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.drawWithState(0);
    CHECK((boardShows(host, "40.0") || boardShows(host, "0:40.0")));
    host.shutdown();
}

TEST_CASE("pitboard: a second lap with the same time brings the board up again") {
    PluginHost host(dllPath());
    setUp(host, "pitboard_same_time");

    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);
    REQUIRE(boardShows(host, "1:00.0"));
    waitOutHold(host);
    REQUIRE_FALSE(boardUp(host));

    host.classify(RACE1, 120000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 60000, /*best=*/0, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);
    CHECK_MESSAGE(boardUp(host), "the line of a lap that equals the one before must show the board");
    CHECK(boardShows(host, "1:00.0"));
    host.shutdown();
}

TEST_CASE("pitboard: a lap through the pits keeps the board down, without INVALID") {
    PluginHost host(dllPath());
    setUp(host, "pitboard_pit_lap");

    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceTrackPosition({ { 10, 0.40f } });
    waitOutHold(host);
    REQUIRE_FALSE(boardUp(host));

    // Into the pit lane and back out, then the line: an invalid lap with a time.
    host.classify(RACE1, 80000, { { .num = 10, .laps = 1, .gap = 0, .pit = 1 } });
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
    host.classify(RACE1, 90000, { { .num = 10, .laps = 1, .gap = 0, .pit = 0 } });
    host.raceTrackPosition({ { 10, 0.88f } });
    host.drawWithState(0);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.classify(RACE1, 135000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 75000, /*best=*/0, 25000, 50000, /*invalid=*/true);
    host.drawWithState(0);

    CHECK_MESSAGE(!boardUp(host), "a pit lap is not a timed lap: the board stays down");
    CHECK_FALSE(boardShows(host, "INVALID"));
    host.shutdown();
}

TEST_CASE("pitboard: a cut lap still comes up, with INVALID") {
    PluginHost host(dllPath());
    setUp(host, "pitboard_cut_lap");

    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    waitOutHold(host);

    host.classify(RACE1, 122000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 62000, /*best=*/0, 21000, 42000, /*invalid=*/true);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);
    CHECK(boardShows(host, "1:02.0"));
    CHECK(boardShows(host, "INVALID"));
    host.shutdown();
}
