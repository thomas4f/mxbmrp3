// ============================================================================
// tests/integration/tests/trackpos_test.cpp
// RaceTrackPosition -> real-time gap computation. The live gap is INTERNAL
// plugin state (the in-game StandingsHud "live gap" mode); it is NOT in the
// /api/state JSON, so this reads it directly via the MXBMRP3_Test_GetRealTimeGap
// hook rather than the snapshot — testing the plugin internal as an internal.
//
// The algorithm (PluginData::updateRealTimeGaps): the leader stamps a timestamp
// at each centerline position it passes; a follower's gap is how much later it
// reaches that same position. So a deterministic gap needs (a) the leader to
// have stamped a position on a prior batch, and (b) the follower to arrive there
// on a later batch, with the session clock advanced between them. The clock
// comes from RaceClassification (setSessionTime), so each batch is preceded by a
// classify() at the intended time. Lap-based race (sessionLength=0) => the timer
// counts up, so gap = follower.time - leaderStampedTime.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("race track position: real-time leader gap, and it tracks live") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\trackpos\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);   // lap-based: clock counts up
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    // Leader #10, follower #22, both on lap 1. classify sets the order (leader
    // first) and the session clock; the classification gap here is irrelevant to
    // the live gap under test.
    const std::vector<ClassRow> grid = {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
    };

    // Batch 1 @ t=1000: leader stamps position 0.20. Follower behind at a spot the
    // leader hasn't stamped, so it has no gap yet.
    host.classify(RACE1, 1000, grid);
    host.raceTrackPosition({ { 10, 0.20f }, { 22, 0.10f } });

    // Batch 2 @ t=2000: leader moves to 0.40; follower reaches 0.20 — where the
    // leader was at t=1000 — so its live gap is 2000-1000 = 1000ms.
    host.classify(RACE1, 2000, grid);
    host.raceTrackPosition({ { 10, 0.40f }, { 22, 0.20f } });

    CHECK(host.realTimeGap(10) == 0);        // leader is always 0
    CHECK(host.realTimeGap(22) == 1000);     // 1.0s back on the road

    // Batch 3 @ t=4000: leader at 0.60; follower reaches 0.40 (leader was there at
    // t=2000). It took the follower 2000ms to cover ground the leader did in
    // 1000ms, so the gap has GROWN to 4000-2000 = 2000ms — proving it's live.
    host.classify(RACE1, 4000, grid);
    host.raceTrackPosition({ { 10, 0.60f }, { 22, 0.40f } });

    CHECK(host.realTimeGap(10) == 0);
    CHECK(host.realTimeGap(22) == 2000);

    // Batch 4 @ t=6000: #22 is now a full lap down (classification reports
    // gapLaps=1). A live time-gap across different laps is meaningless — the HUD
    // must fall back to the official "+1L", so the live gap is forced to 0 rather
    // than left showing the last same-lap value (or a bogus cross-lap time). Even
    // though #22 is still in the batch and physically "ahead" on the road, the
    // lapped guard wins.
    const std::vector<ClassRow> lapped = {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 0, .gapLaps = 1 },
    };
    host.classify(RACE1, 6000, lapped);
    host.raceTrackPosition({ { 10, 0.80f }, { 22, 0.70f } });

    CHECK(host.realTimeGap(10) == 0);
    CHECK(host.realTimeGap(22) == 0);        // lapped → no live gap, use +1L

    host.shutdown();
}
