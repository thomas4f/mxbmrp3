// ============================================================================
// tests/integration/tests/plugin_thread_test.cpp
// Behavioral integration test for the EXPERIMENTAL plugin worker thread
// (core/plugin_thread.*). With the worker on, every game-state callback is copied
// across a queue and applied on a SEPARATE thread; Draw only serves a pre-built,
// triple-buffered frame. This test proves that path is FUNCTIONALLY EQUIVALENT to
// the synchronous path: the same synthetic race, driven through the real PiBoSo
// callbacks, must produce the same standings — just computed off the game thread.
//
// The harness drives callbacks then reads the plugin's own /api/state snapshot as
// usual, but calls host.pluginThreadFlush() first so the assertion sees the
// worker's fully-applied state (in the game there is deliberately no such barrier —
// the point is that the game thread never waits on us).
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("plugin thread: callbacks applied off-thread, standings match") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\plugin_thread\\");

    // Turn the worker on AFTER startup and confirm it actually spawned (the export is
    // only present in the test DLL; if the flag/threading were compiled out this fails
    // loudly rather than silently testing the sync path).
    host.pluginThreadEnable();
    REQUIRE(host.pluginThreadEnabled());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);

    // Entries added in reverse finishing order — same as race_test, to prove position
    // comes from the classification order, not insertion order, even when every one of
    // these callbacks is applied on the worker thread.
    host.addEntry(7,  "Carol");
    host.addEntry(22, "Bob");
    host.addEntry(10, "Alice");

    // --- Phase 1: initial grid -------------------------------------------------
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
        { .num = 7,  .best = 92500, .laps = 5, .gap = 3200 },
    });
    host.pluginThreadFlush();   // barrier: let the worker finish applying everything
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(d["session"].value("type", std::string()) == "Race 1");
        CHECK(d["session"].value("isRace", false) == true);
        CHECK(d["session"].value("time", std::string()) == "05:00");
        CHECK(d["session"].value("trackName", std::string()) == "TestTrack");
        checkStandings(d, {
            { 1, 10, "Alice", "Leader", "1:30.000" },
            { 2, 22, "Bob",   "+1.500", "1:31.000" },
            { 3,  7, "Carol", "+3.200", "1:32.500" },
        });
    }

    // --- Phase 2: an overtake, still all off-thread ----------------------------
    host.classify(6, 360000, {
        { .num = 22, .best = 90500, .laps = 6, .gap = 0 },
        { .num = 10, .best = 90000, .laps = 6, .gap = 800 },
        { .num = 7,  .best = 92500, .laps = 6, .gap = 4100 },
    });
    host.pluginThreadFlush();
    {
        auto d = host.snapshot();
        checkStandings(d, {
            { 1, 22, "Bob",   "Leader", "1:30.500" },
            { 2, 10, "Alice", "+0.800", "1:30.000" },
            { 3,  7, "Carol", "+4.100", "1:32.500" },
        });
    }

    // Clean stop joins the worker and drains any remainder inline; the plugin then
    // shuts down through the normal path.
    host.pluginThreadStop();
    CHECK_FALSE(host.pluginThreadEnabled());
    host.shutdown();
}
