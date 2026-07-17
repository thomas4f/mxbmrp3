// ============================================================================
// tests/integration/tests/plugin_thread_abort_test.cpp
// Regression for the worker-death self-heal. If an exception ESCAPES
// threadMain() (bad_alloc-class failure, past the per-command guards), the old
// code left enabled() true forever: every game callback kept enqueueing
// closures into a queue nobody drained (unbounded growth inside the host, HUD
// frozen), reconcileEnabled() couldn't recover (desired == running), and
// flush() hung on its sentinel.
//
// The fix: the dying worker flags the abort and clears enabled() (routing falls
// back inline immediately); the next draw()'s reconcileEnabled() joins the dead
// thread, drains the stranded backlog in order, and LATCHES threaded mode off
// so a persistent failure can't respawn a worker every frame.
//
// Drives it via the MXBMRP3_Test_PluginThreadAbortWorker fault-injection hook
// (an escaping throw on the worker's next wakeup). Self-contained doctest.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

#include <chrono>
#include <thread>

TEST_CASE("plugin thread: a worker killed by exception self-heals to sync mode") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\plugin_thread_abort\\");

    host.pluginThreadEnable();
    REQUIRE(host.pluginThreadEnabled());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.pluginThreadFlush();   // worker alive and fully caught up

    // Kill the worker with an exception that escapes its loop.
    REQUIRE(host.pluginThreadAbortWorker());

    // The dying worker clears enabled() itself, so routing falls back to inline
    // execution without waiting for a draw. Give the thread a moment to unwind.
    for (int i = 0; i < 500 && host.pluginThreadEnabled(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK_FALSE(host.pluginThreadEnabled());

    // The next draw runs reconcileEnabled() on the "game thread": it must join
    // the dead worker, drain the backlog, and latch threaded mode OFF even
    // though the [Advanced] flag is still 1 — not restart it (crash-loop) and
    // not std::terminate on the un-joined thread.
    host.draw();
    CHECK_FALSE(host.pluginThreadEnabled());

    // Callbacks now run inline and the plugin keeps computing state normally
    // (no flush needed — this is the synchronous path again).
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 3, .gap = 1500 },
    });
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        checkStandings(d, {
            { 1, 10, "Alice", "Leader" },
            { 2, 22, "Bob",   "+1.500" },
        });
    }

    // The latch holds across further draws: the worker stays down until the
    // user explicitly turns the flag off (which re-arms a future opt-in).
    for (int i = 0; i < 3; ++i) host.draw();
    CHECK_FALSE(host.pluginThreadEnabled());

    host.shutdown();
}
