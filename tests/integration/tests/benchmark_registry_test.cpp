// ============================================================================
// tests/integration/tests/benchmark_registry_test.cpp
// The benchmark profiler's REGISTRY must survive a session teardown.
//
// THE BUG THIS PINS (reported from a real in-game session, v1.27.8.369): every
// exported benchmark report read "HUDs profiled: 0" with an empty per-HUD
// footprint table, no matter which HUDs were on. PluginData::clear() — called
// from RaceDeinit and EventDeinit, i.e. every time you leave a session — used to
// assign a fresh BenchmarkMetrics{}, wiping the registry:
//
//   * HUDs are registered exactly ONCE, in HudManager::initialize(). Nothing ever
//     re-registers them, so after the first session exit hudCount stayed 0 for the
//     rest of the run and every recordHudRebuild() was dropped by its
//     `index >= hudCount` bounds check. The rebuilds were still being TIMED (each
//     HUD's m_benchmarkIndex is still >= 0) — the samples just went nowhere.
//   * Callbacks re-register lazily, which looks like recovery and is actually
//     worse: each caches its slot in a function-static and only re-registers when
//     `idx >= callbackCount`. After a wipe, a stale index that still falls below
//     the new count keeps writing into whichever callback claimed that slot first,
//     so timings surface under the WRONG NAME. The reported symptom was a missing
//     "Draw" row plus implausible peaks on RaceRemoveEntry (232us) and
//     RaceVehicleData (272us) — Draw's cost wearing other callbacks' labels.
//
// Both counts are therefore asserted across a teardown. Neither needs the profiler
// to be switched on: the registry is wiring, independent of bm.active.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {

constexpr int RACE = 6;   // PiBoSo Race1 session enum

// Stand up an ordinary session so both registries are populated: HUDs at
// initialize() time, callbacks as each one first fires.
void openSession(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(RACE, 100000, {
        { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 1, .gap = 1500 },
    });
    host.draw();
}

}  // namespace

TEST_CASE("benchmark registry: HUD registrations survive RaceDeinit") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\bench_registry\\");

    openSession(host);

    // Registered once at startup, and every HUD gets a slot — so this is a real
    // count, not an artifact of whatever happens to be visible.
    const int hudsBefore = host.benchmarkHudCount();
    REQUIRE(hudsBefore > 0);

    // Leaving the race calls PluginData::clear(). The registry is not session data
    // and nothing re-registers HUDs, so losing it here is unrecoverable.
    host.raceDeinit();
    CHECK(host.benchmarkHudCount() == hudsBefore);

    // ...and again after the event teardown, the other clear() caller.
    host.eventDeinit();
    CHECK(host.benchmarkHudCount() == hudsBefore);

    host.shutdown();
}

TEST_CASE("benchmark registry: callback slots are not recycled under new names") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\bench_registry_cb\\");

    openSession(host);

    const int cbBefore = host.benchmarkCallbackCount();
    REQUIRE(cbBefore > 0);

    // Callbacks would appear to recover from a wipe by re-registering, so the
    // meaningful assertion is that the count never DROPS: a drop means the
    // already-cached indices now point at slots that later callers will re-claim
    // under their own names, which is how Draw's timings ended up labelled
    // RaceRemoveEntry / RaceVehicleData in the reported session.
    host.raceDeinit();
    CHECK(host.benchmarkCallbackCount() >= cbBefore);

    host.eventDeinit();
    CHECK(host.benchmarkCallbackCount() >= cbBefore);

    // A second session must not re-register duplicates of what is already there
    // (the count may grow as new callbacks fire for the first time, never reset).
    openSession(host);
    CHECK(host.benchmarkCallbackCount() >= cbBefore);

    host.shutdown();
}
