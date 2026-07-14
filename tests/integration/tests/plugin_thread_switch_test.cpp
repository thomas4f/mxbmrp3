// ============================================================================
// tests/integration/tests/plugin_thread_switch_test.cpp
// Proves the plugin worker thread can be toggled AT RUNTIME — legacy <-> threaded —
// without restarting the game, the way the RELOAD_CONFIG hotkey does it: flip the
// [Advanced] pluginThread flag (as a live INI reload would), and the next Draw's
// game-thread PluginThread::reconcileEnabled() starts/stops the worker to match.
//
// It also checks state stays correct across each switch: standings computed in sync
// mode, then in threaded mode after switching on, then in sync mode again after
// switching off — all against one running plugin instance.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("plugin thread: live legacy<->threaded switch via the flag + a draw") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ptswitch\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(7,  "Carol");
    host.addEntry(22, "Bob");
    host.addEntry(10, "Alice");

    // --- Start in legacy (synchronous) mode ------------------------------------
    REQUIRE_FALSE(host.pluginThreadEnabled());
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 5, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 5, .gap = 1500 },
        { .num = 7,  .best = 92500, .laps = 5, .gap = 3200 },
    });
    {
        auto d = host.snapshot();
        checkStandings(d, {
            { 1, 10, "Alice", "Leader", "1:30.000" },
            { 2, 22, "Bob",   "+1.500", "1:31.000" },
            { 3,  7, "Carol", "+3.200", "1:32.500" },
        });
    }

    // --- Switch ON at runtime: flag flips, next draw reconciles it into threaded --
    host.setPluginThreadFlag(true);
    REQUIRE_FALSE(host.pluginThreadEnabled());   // not started until a draw reconciles
    host.draw();
    REQUIRE(host.pluginThreadEnabled());         // worker now running

    // Drive an update off-thread and confirm it applied correctly.
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

    // --- Switch OFF at runtime: flag flips, next draw joins the worker back to sync-
    host.setPluginThreadFlag(false);
    host.draw();                                 // reconcile stops + joins the worker
    REQUIRE_FALSE(host.pluginThreadEnabled());

    // Back on the game thread: a further update applies synchronously (no flush).
    host.classify(6, 420000, {
        { .num = 10, .best = 89500, .laps = 7, .gap = 0 },
        { .num = 22, .best = 90500, .laps = 7, .gap = 300 },
        { .num = 7,  .best = 92500, .laps = 7, .gap = 5000 },
    });
    {
        auto d = host.snapshot();
        checkStandings(d, {
            { 1, 10, "Alice", "Leader", "1:29.500" },
            { 2, 22, "Bob",   "+0.300", "1:30.500" },
            { 3,  7, "Carol", "+5.000", "1:32.500" },
        });
    }

    host.shutdown();
}
