// ============================================================================
// tests/integration/tests/sessionstate_test.cpp
// RaceSessionState transitions and the derived behaviour they drive:
//   - green flag (pre-start -> in-progress) snapshots the START GRID, so the
//     positions-gained column (posDeltaStart) is measured from it;
//   - state changes log "started" / "ended" events.
//
// This is a plugin-logic test, so it observes state via the DIRECT snapshot
// (host.snapshot()) — no HTTP server, no socket, no rebuild-gating. See the
// layering note in TESTING.md. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

// SPluginsRaceSessionState_t bits.
static constexpr int PRE_START  = 256;
static constexpr int IN_PROGRESS = 16;
static constexpr int RACE_OVER  = 512;
static constexpr int RACE1 = 6;

TEST_CASE("race session state: green snapshots the grid; started/ended events") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\sessionstate\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    // Start the session in PRE_START so the green flag below is a real transition.
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0, /*state=*/PRE_START);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(7,  "Carol");

    // Grid: Alice P1, Bob P2, Carol P3.
    host.classify(RACE1, 0, {
        { .num = 10, .best = 90000, .laps = 0, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 0, .gap = 500 },
        { .num = 7,  .best = 92000, .laps = 0, .gap = 900 },
    }, /*sessionState=*/PRE_START);

    // --- Green flag: pre-start -> in-progress. Snapshots the start grid. --------
    host.raceSessionState(RACE1, IN_PROGRESS);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(hasEvent(d, "started"));
    }

    // Bob passes Alice for the lead; Carol holds P3. posDeltaStart is measured
    // from the grid snapshotted at green: gridPos - currentPos.
    host.classify(RACE1, 60000, {
        { .num = 22, .best = 91000, .laps = 1, .gap = 0 },     // grid P2 -> now P1: +1
        { .num = 10, .best = 90000, .laps = 1, .gap = 800 },   // grid P1 -> now P2: -1
        { .num = 7,  .best = 92000, .laps = 1, .gap = 2000 },  // grid P3 -> now P3:  0
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(riderByNum(d, 22).value("posDeltaStart", -99) == 1);
        CHECK(riderByNum(d, 10).value("posDeltaStart", -99) == -1);
        CHECK(riderByNum(d, 7).value("posDeltaStart", -99) == 0);
    }

    // --- Race over: logs the "ended" event. -----------------------------------
    host.raceSessionState(RACE1, RACE_OVER);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(hasEvent(d, "ended"));
    }

    host.shutdown();
}
