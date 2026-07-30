// ============================================================================
// tests/integration/tests/pb_scope_test.cpp
// The all-time-PB NOTICE must follow the active PB scope, not the per-bike write.
//
// THE BUG THIS PINS. Personal bests are stored keyed by track+bike — a PB belongs to
// the bike that set it. But what the player is SHOWN as their all-time reference
// depends on UiConfig's PBScope, and the default is PBScope::CATEGORY: the TimingHud
// "Alltime" row compares against the fastest lap across every bike in the CLASS.
// StatsManager::updatePersonalBest returned a bare bool meaning "a write happened",
// and race_lap_handler used it as if it meant "beat your all-time". Ride a second bike
// in a class you already have a faster time in and the write succeeds (that bike had no
// PB yet), so the green "ALL-TIME PB" notice fired while the Alltime row showed red
// against the class best. It also swallowed a real notice: an all-time PB deliberately
// SUPPRESSES the fastest-lap and session-PB notices, so a false one silenced the
// FASTEST LAP the player had actually earned.
//
// PersonalBestUpdate now names both facts (stored / beatsScopedBest) so a caller has to
// pick one. Storage is deliberately unchanged and still per-bike — the file assertions
// below pin that, so the fix can't be "solved" by making the store class-keyed (which
// would destroy per-bike history and break a switch back to PBScope::BIKE).
//
// Its own file, not a case in stats_test.cpp: run_tests.sh pre-creates ONE save dir per
// test FILE (named after the basename minus _test), and StatsManager can only write
// where that directory already exists — so a second save path inside one file silently
// writes nothing. Own file => own process, own plugin lifecycle, own clean save dir.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile
#include "nlohmann/json.hpp"
#include <cstdio>   // std::remove
#include <string>

static constexpr int RACE1 = 6;

static const char* const SAVE_WIN  = "Z:\\tmp\\mxbmrp3-tests\\pb_scope\\";
static const char* const STATS_PATH =
    "Z:\\tmp\\mxbmrp3-tests\\pb_scope\\mxbmrp3\\mxbmrp3_stats.json";

// The stored PB lap time for one track+bike ("trackId|bikeName" is the store's key),
// or -1 when that bike has no PB on file.
static int storedPbFor(const std::string& key) {
    const std::string txt = ini::readFile(STATS_PATH);
    if (txt.empty()) return -1;
    auto j = nlohmann::json::parse(txt, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("trackBike")) return -1;
    if (!j["trackBike"].contains(key)) return -1;
    const auto& tb = j["trackBike"][key];
    if (!tb.contains("personalBest")) return -1;
    return tb["personalBest"].value("lapTime", -1);
}

TEST_CASE("all-time PB notice follows the active PB scope, not the per-bike write") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE_WIN);
    std::remove(STATS_PATH);
    // PB scope is left at its default (CATEGORY) — the buggy path, and what players run.

    // One event/session per bike, as the game does it: you leave the track to swap bikes,
    // so each bike arrives via its own EventInit (which is what sets the stats context).
    auto rideLap = [&](const char* bike, const char* cls, int lapTimeMs, int best) {
        host.eventInit("TestTrack", "Alice", 1600.0f, 2, bike, cls, /*trackId=*/"TestTrack");
        host.raceEvent("TestTrack");
        host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(10, "Alice");           // first active entry → the local player
        host.runInit(RACE1);
        host.raceLap(RACE1, /*raceNum=*/10, /*lap=*/1, lapTimeMs, best);
        const bool notified = host.takeNewAllTimePB();
        host.runDeinit();                     // leave track → flushes stats to disk
        return notified;
    };

    // Bike A, class MX1: a 1:30 establishes the class reference.
    CHECK_MESSAGE(rideLap("Bike A", "MX1", 90000, /*best=*/2),
                  "first PB in the class should notify");

    // Bike B, SAME class: a 1:35 is that bike's first PB, but it is 5s off the class
    // best — the player must NOT be told they set an all-time PB. This is the bug.
    CHECK_MESSAGE(!rideLap("Bike B", "MX1", 95000, /*best=*/1),
                  "slower than the class best must not fire the all-time PB notice");

    // ...yet it IS still stored as Bike B's own PB. The fix is to the notice, not to
    // storage: per-bike history stays intact for a later switch to PBScope::BIKE.
    CHECK(storedPbFor("TestTrack|Bike A") == 90000);
    CHECK(storedPbFor("TestTrack|Bike B") == 95000);

    // Bike B beating the class best IS an all-time PB.
    CHECK_MESSAGE(rideLap("Bike B", "MX1", 88000, /*best=*/2),
                  "beating the class best must fire the notice");
    CHECK(storedPbFor("TestTrack|Bike B") == 88000);

    // Control, so this can't be "fixed" by never notifying on a slower lap: Bike C runs
    // a 1:40 — slower than everything above, but the first time in MX2, so it is that
    // class's all-time PB and must notify.
    CHECK_MESSAGE(rideLap("Bike C", "MX2", 100000, /*best=*/1),
                  "first PB in a different class should notify regardless of other classes");
    CHECK(storedPbFor("TestTrack|Bike C") == 100000);

    // Bike A's original 1:30 survived every swap above (nothing rewrote another bike's key).
    CHECK(storedPbFor("TestTrack|Bike A") == 90000);

    host.shutdown();
}
