// ============================================================================
// tests/integration/tests/trackpos_stale_test.cpp
// Real-time gap vs the ~10-closest RaceTrackPosition window. The game only
// reports the vehicles nearest the camera each batch, but PluginData retains the
// last-seen position for EVERY rider. If updateRealTimeGaps recomputes from those
// retained (stale) positions for riders outside the current batch, the live gap
// drifts into garbage as the session clock advances — the "partner/target rider
// isn't in the data the API sent" bug.
//
// The sharp case is the LEADER dropping out of the batch (e.g. spectating a
// midfield battle) while RaceClassification advances its lap: the stale leader
// position would get stamped as this lap's timing baseline, corrupting the gap of
// an active follower that reaches that point. The fix gates the whole computation
// on the active-batch set (m_activeTrackPosRiders), freezing every gap at its last
// live value when the leader isn't reporting.
//
// Read directly via MXBMRP3_Test_GetRealTimeGap (the live gap is in-game-only, not
// in /api/state). Lap-based race (clock counts up) so gap = follower.time -
// leaderStampedTime. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("track position: a rider outside the batch doesn't get a stale live gap") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\trackpos_stale\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);   // lap-based: clock counts up
    host.addEntry(10, "Alice");   // leader
    host.addEntry(22, "Bob");     // follower

    // --- Phase 1: both in the batch on lap 1 — establish Bob's live gap = 1000 --
    const std::vector<ClassRow> lap1 = {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
    };
    host.classify(RACE1, 1000, lap1);
    host.raceTrackPosition({ { 10, 0.20f }, { 22, 0.10f } });   // leader stamps pos 0.20 @ 1000
    host.classify(RACE1, 2000, lap1);
    host.raceTrackPosition({ { 10, 0.40f }, { 22, 0.20f } });   // Bob reaches 0.20 → gap 2000-1000

    REQUIRE(host.realTimeGap(10) == 0);
    REQUIRE(host.realTimeGap(22) == 1000);

    // --- Phase 2: the LEADER drops out of the batch; both advance to lap 2 -----
    // Classification still lists #10 as leader (and advances its lap), but the
    // game stops reporting it (camera moved to Bob). Bob is now on lap 2 at 0.40 —
    // exactly the last position the leader was *seen* at (0.40), so the buggy path
    // would stamp that stale position as the lap-2 baseline and hand Bob a bogus
    // 6000-2000 = 4000ms gap that straddles the lap boundary. With the fix, the
    // leader isn't in the batch, so no baseline is stamped and Bob's gap FREEZES
    // at its last live value (1000).
    const std::vector<ClassRow> lap2 = {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 2, .gap = 500 },
    };
    host.classify(RACE1, 6000, lap2);
    host.raceTrackPosition({ { 22, 0.40f } });   // leader #10 absent from this batch

    CHECK(host.realTimeGap(22) == 1000);          // frozen, NOT a stale-derived 4000

    // --- Phase 3: leader returns to the batch — live gaps resume normally ------
    // Sanity that gating on the batch doesn't wedge the computation: once the
    // leader reports again, a fresh baseline is stamped and Bob updates live.
    host.classify(RACE1, 7000, lap2);
    host.raceTrackPosition({ { 10, 0.60f }, { 22, 0.40f } });   // leader stamps 0.60 @ 7000
    host.classify(RACE1, 9000, lap2);
    host.raceTrackPosition({ { 10, 0.80f }, { 22, 0.60f } });   // Bob reaches 0.60 → 9000-7000

    CHECK(host.realTimeGap(10) == 0);
    CHECK(host.realTimeGap(22) == 2000);          // live again

    host.shutdown();
}
