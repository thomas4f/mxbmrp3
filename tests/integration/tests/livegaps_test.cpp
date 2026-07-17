// ============================================================================
// tests/integration/tests/livegaps_test.cpp
// Live-gap DATA CONTRACT for the web overlay. The overlay's battle cards can show
// real-time intervals instead of official splits; the plugin ALWAYS surfaces the
// raw per-rider live gap + a validity flag in /api/state (the on/off is a purely
// client-side overlay setting, so it's not in the plugin's contract). This pins
// that contract (via host.snapshot(), no HTTP server):
//
//   * per rider: liveGapMs (leader-relative real-time gap, 0 for the leader) and
//     liveGapValid — whether liveGapMs is trustworthy right now.
//
// liveGapValid is true for the leader (its 0 is valid data) and for any rider in
// the current ~10-closest track-position batch with a computed same-lap gap; it
// is FALSE for a rider that dropped out of the batch (stale gap → fall back to the
// official split) or one that's lapped. This is the overlay-facing companion to
// trackpos_stale_test, which pins the underlying realTimeGap value itself.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("live gaps: per-rider liveGapMs/liveGapValid contract") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\livegaps\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);   // lap-based
    host.addEntry(10, "Alice");   // leader
    host.addEntry(22, "Bob");     // follower

    // --- Phase 1: both in the batch → both have valid live gaps --------------
    const std::vector<ClassRow> lap1 = {
        { .num = 10, .laps = 1, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 500 },
    };
    host.classify(RACE1, 1000, lap1);
    host.raceTrackPosition({ { 10, 0.20f }, { 22, 0.10f } });   // leader stamps 0.20 @ 1000
    host.classify(RACE1, 2000, lap1);
    host.raceTrackPosition({ { 10, 0.40f }, { 22, 0.20f } });   // Bob reaches 0.20 → gap 1000
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());

        auto leader = riderByNum(d, 10);
        CHECK(leader.value("liveGapMs", -1) == 0);              // leader is the reference
        CHECK(leader.value("liveGapValid", false) == true);     // its 0 is valid data

        auto bob = riderByNum(d, 22);
        CHECK(bob.value("liveGapMs", -1) == 1000);              // matches the computed real-time gap
        CHECK(bob.value("liveGapValid", false) == true);
        // The JSON field mirrors the internal value read via the white-box hook.
        CHECK(bob.value("liveGapMs", -1) == host.realTimeGap(22));
    }

    // --- Phase 2: Bob drops out of the batch → his live gap is not valid ------
    // (leader still reporting, so gaps still compute for whoever IS in the batch)
    host.classify(RACE1, 4000, lap1);
    host.raceTrackPosition({ { 10, 0.60f } });                  // Bob absent
    {
        auto d = host.snapshot();
        auto bob = riderByNum(d, 22);
        CHECK(bob.value("liveGapValid", true) == false);        // stale → client uses official
        CHECK(riderByNum(d, 10).value("liveGapValid", false) == true);   // leader still valid
    }

    // --- Phase 3: Bob is a full lap down → live gap meaningless across laps ---
    const std::vector<ClassRow> lapped = {
        { .num = 10, .laps = 2, .gap = 0 },
        { .num = 22, .laps = 1, .gap = 0, .gapLaps = 1 },
    };
    host.classify(RACE1, 6000, lapped);
    host.raceTrackPosition({ { 10, 0.20f }, { 22, 0.80f } });   // both in the batch this time
    {
        auto d = host.snapshot();
        CHECK(riderByNum(d, 22).value("liveGapValid", true) == false);   // lapped → invalid
    }

    // --- Phase 4 (regression): removeRaceEntry must evict the "active" bit ----
    // Bob was in the last batch, so his active-track-pos bit is set. If he leaves
    // and a NEW rider joins reusing #22 while no batches arrive (the player could
    // be sitting in menus — no callbacks flow there to refresh the set), the
    // rejoiner must NOT inherit the stale bit. removeRaceEntry() missed this set.
    REQUIRE(host.hasActiveTrackPos(22) == 1);   // set by the phase-3 batch
    host.removeEntry(22);
    host.addEntry(22, "Carl");                  // raceNum reuse, no new batch
    CHECK(host.hasActiveTrackPos(22) == 0);     // fresh rider, no stale "active" bit

    host.shutdown();
}
