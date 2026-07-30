// ============================================================================
// tests/integration/tests/plugin_thread_flush_test.cpp
// Regression for PluginThread::flush()'s two liveness holes and the sentinel
// lifetime that fixing them exposed.
//
// THE WINDOW. threadMain() moves the WHOLE batch off m_queue (batch.swap) before
// executing any of it. A worker that dies in that window destroys an in-flight
// flush() sentinel with the unwound batch, so set_value() is never called:
//
//   1. flush() waited on an UNBOUNDED fut.wait(), so the calling thread parked
//      forever. That thread is the game thread, and reconcileEnabled()'s abort
//      self-heal runs on it — so nothing could ever reap the worker and recover.
//   2. Its second phase then spun on (m_run && !m_idle). An aborted worker leaves
//      m_run true (only stop() clears it) and m_idle false, so that spin was a
//      second unbounded hang behind the first.
//   3. Bounding the wait means flush() can now return while the sentinel is STILL
//      queued — and stop()'s leftover drain runs it afterwards. With the original
//      capture-by-reference to a local std::promise, that late set_value() would
//      touch a destroyed object (the `danglingLifetime` cppcheck flagged here).
//      The sentinel is heap-owned and captured by value so it stays valid.
//
// The abort hook can't drive this: it throws BEFORE the batch runs but the test
// must also observe the abandoned sentinel being run later, so the worker has to
// survive. MXBMRP3_Test_PluginThreadSwallowBatches gives exactly that — batches
// taken off the queue and discarded, then resumable.
//
// This test is a WATCHDOG: on the unfixed code it does not fail an assertion, it
// hangs, and run_tests.sh's per-test timeout kills it. That is the honest shape
// for a liveness bug — the pass condition is "flush() returned at all".
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

#include <chrono>
#include <thread>

TEST_CASE("plugin thread: flush() is bounded when the worker never drains") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\plugin_thread_flush\\");

    host.pluginThreadEnable();
    REQUIRE(host.pluginThreadEnabled());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    // Baseline: with a healthy worker the sentinel round-trips promptly. This also
    // proves the bound below is measuring the fault, not just a slow machine.
    {
        const auto t0 = std::chrono::steady_clock::now();
        host.pluginThreadFlush();
        const auto healthyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        CHECK(healthyMs < 2000);
    }

    // Now make the worker swallow every batch: it dequeues and discards unrun, so
    // no flush() sentinel can ever complete.
    REQUIRE(host.pluginThreadSwallowBatches(true));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // THE ASSERTION THAT MATTERS: this returns. Before the fix it never did.
    // The bound is generous (the internal timeout is 5s, twice, and Wine is slow)
    // — the point is termination, not latency.
    const auto t0 = std::chrono::steady_clock::now();
    host.pluginThreadFlush();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    CHECK(elapsed < 30000);

    // It must actually have WAITED rather than no-opped: a flush that returned
    // instantly would mean enabled() went false and the test proved nothing about
    // the sentinel path.
    CHECK(elapsed > 1000);

    // Let the worker run again and drain the abandoned sentinel(s). With the
    // original by-reference capture this is the use-after-free: set_value() on a
    // promise whose flush() frame is long gone. Shared ownership keeps it valid,
    // so this is simply a no-op that nobody is waiting on.
    REQUIRE(host.pluginThreadSwallowBatches(false));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // The plugin is still healthy: the worker drains queued work normally again
    // and PluginData computes standings as usual.
    host.classify(6, 300000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 3, .gap = 1500 },
    });
    host.pluginThreadFlush();
    host.draw();
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        checkStandings(d, {
            { 1, 10, "Alice", "Leader" },
            { 2, 22, "Bob",   "+1.500" },
        });
    }

    host.shutdown();
}
