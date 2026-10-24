// ============================================================================
// tests/integration/tests/event_log_pb_test.cpp
// The Event Log's personal-best rows: the local player's Session PB and
// All-time PB laps reach the log (and so /api/state), one row per lap naming
// the higher of the two, and nobody else's PBs do.
//
// The rules pinned here, each a way the row could be noise instead of news:
//  - an all-time PB is also a session PB, so the lap logs AllTimePB alone;
//  - the first clean lap of a session is trivially a session best, so it
//    logs nothing (same rule as the Session PB notice);
//  - another rider improving their own best logs nothing (a full grid would
//    flood the feed);
//  - an invalid lap logs nothing.
//
// Its own file for its own save dir: the all-time PB depends on what the
// stats file already holds, and run_tests.sh pre-creates one clean dir per
// test file.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"
#include <cstdio>   // std::remove
#include <string>

namespace {

// EventLogType values (append-only enum, core/event_log_types.h); the HTTP
// snapshot emits the raw integer.
constexpr int SESSION_PB_TYPE  = 19;
constexpr int ALLTIME_PB_TYPE  = 20;

constexpr int PLAYER = 10;
constexpr int RIVAL  = 22;

static const char* const SAVE_WIN = "Z:\\tmp\\mxbmrp3-tests\\event_log_pb\\";
static const char* const STATS_PATH =
    "Z:\\tmp\\mxbmrp3-tests\\event_log_pb\\mxbmrp3\\mxbmrp3_stats.json";

int countType(const nlohmann::json& d, int type) {
    int n = 0;
    for (const auto& e : d.value("events", nlohmann::json::array()))
        if (e.value("type", -1) == type) ++n;
    return n;
}

// Detail (the lap time) of the newest event of a type, or "" if none.
std::string lastDetail(const nlohmann::json& d, int type) {
    std::string out;
    for (const auto& e : d.value("events", nlohmann::json::array()))
        if (e.value("type", -1) == type) out = e.value("detail", std::string());
    return out;
}

}  // namespace

TEST_CASE("event log: the player's session PB and all-time PB laps are logged, one row each") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE_WIN);
    std::remove(STATS_PATH);

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Bike A", "MX1", /*trackId=*/"TestTrack");
    host.raceEvent("TestTrack");

    // --- Session 1: no stats on file, so the first clean lap is an all-time PB.
    host.session(/*session=*/1, /*numLaps=*/0, /*lengthMs=*/600000);
    host.addEntry(PLAYER, "Alice");           // first active entry -> the local player
    host.addEntry(RIVAL, "Bob");
    host.runInit(1);

    host.raceLap(1, PLAYER, 1, 90000, /*best=*/2);
    auto d = host.snapshot();
    CHECK(countType(d, ALLTIME_PB_TYPE) == 1);
    CHECK(countType(d, SESSION_PB_TYPE) == 0);    // the higher one only
    CHECK(lastDetail(d, ALLTIME_PB_TYPE) == "1:30.000");
    CHECK(hasEvent(d, "All-time PB"));

    // A rival's first lap and their improvement: their PBs are not ours to log.
    host.raceLap(1, RIVAL, 1, 95000, /*best=*/1);
    host.raceLap(1, RIVAL, 2, 94000, /*best=*/1);
    // The player's slower lap is no PB at all.
    host.raceLap(1, PLAYER, 2, 92000, /*best=*/0);
    d = host.snapshot();
    CHECK(countType(d, ALLTIME_PB_TYPE) == 1);
    CHECK(countType(d, SESSION_PB_TYPE) == 0);
    host.runDeinit();                          // flushes stats to disk

    // --- Session 2: the all-time PB (1:30.000) is now on file.
    host.session(/*session=*/2, /*numLaps=*/0, /*lengthMs=*/600000);
    host.runInit(2);
    const int sessionPbBefore = countType(host.snapshot(), SESSION_PB_TYPE);

    // First clean lap of the session: a session best by default, not news.
    host.raceLap(2, PLAYER, 1, 95000, /*best=*/1);
    CHECK(countType(host.snapshot(), SESSION_PB_TYPE) == sessionPbBefore);

    // Improves on it, still slower than the all-time PB: Session PB.
    host.raceLap(2, PLAYER, 2, 93000, /*best=*/1);
    d = host.snapshot();
    CHECK(countType(d, SESSION_PB_TYPE) == sessionPbBefore + 1);
    CHECK(lastDetail(d, SESSION_PB_TYPE) == "1:33.000");

    // An invalid lap that would have been faster logs nothing.
    host.raceLap(2, PLAYER, 3, 91000, /*best=*/1, -1, -1, /*invalid=*/true);
    CHECK(countType(host.snapshot(), SESSION_PB_TYPE) == sessionPbBefore + 1);

    // Beats the all-time PB: All-time PB, and no Session PB row for the same lap.
    const int allTimeBefore = countType(host.snapshot(), ALLTIME_PB_TYPE);
    host.raceLap(2, PLAYER, 4, 89000, /*best=*/2);
    d = host.snapshot();
    CHECK(countType(d, ALLTIME_PB_TYPE) == allTimeBefore + 1);
    CHECK(countType(d, SESSION_PB_TYPE) == sessionPbBefore + 1);
    CHECK(lastDetail(d, ALLTIME_PB_TYPE) == "1:29.000");

    host.runDeinit();
    host.shutdown();
}
