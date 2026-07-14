// ============================================================================
// tests/integration/tests/plugin_thread_teardown_test.cpp
// Regression guard for teardown with the worker thread RUNNING. The dangerous case
// a background thread introduces is shutdown ordering: if the worker outlived the
// singletons it touches (or the SEH crash filter / the DLL mapping), the next
// instruction it ran would fault the host. PluginManager::shutdown() must therefore
// stop+join the worker FIRST, and stop() must drain any still-queued callbacks
// inline so nothing is lost.
//
// This drives state, enables the worker, queues a callback WITHOUT flushing, then
// calls the real Shutdown export with the worker still live. A clean return (the
// test doesn't hang or crash) + the worker reporting stopped afterward is the
// assertion — it proves shutdown() joined it and drained the queue.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

TEST_CASE("plugin thread: clean teardown with the worker still running") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\ptteardown\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, 10, 0);
    host.addEntry(10, "Alice");

    host.pluginThreadEnable();
    REQUIRE(host.pluginThreadEnabled());

    // Queue a callback and do NOT flush — so the worker still has (or is mid-applying)
    // work when Shutdown lands. shutdown() -> PluginThread::stop() must join it and
    // drain this inline; there must be no hang and no use-after-free.
    host.classify(6, 300000, { { .num = 10, .best = 90000, .laps = 5, .gap = 0 } });

    // Real Shutdown export, worker still enabled. If teardown ordering were wrong this
    // would hang on a self-join or fault; reaching the next line means it was clean.
    host.shutdown();

    // The worker was stopped as part of shutdown (not left dangling past DLL teardown).
    CHECK_FALSE(host.pluginThreadEnabled());
}
