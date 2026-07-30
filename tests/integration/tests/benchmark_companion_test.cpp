// ============================================================================
// tests/integration/tests/benchmark_companion_test.cpp
// The visibility-gate invariant applied to the benchmark profiler's collection
// switch. Sibling of telemetry_companion_test.cpp — same shape, different
// producer.
//
// THE BUG THIS PINS, reported from real use: with the profiler enabled only on
// the companion window, its on/off did not behave and the tables never
// populated; the only way to get data was to also enable it in-game.
//
// PluginData's benchmark metrics have an `active` flag that switches the whole
// instrumentation on. BenchmarkWidget latched it from setVisible() — an
// override on the GAME-surface toggle. Enabling the widget on the companion
// goes through BaseHud::setCompanionVisible(), which is NOT virtual and never
// reaches that override, so `active` stayed false. Meanwhile update() gates
// rendering on isVisibleAnySurface(), which is correct — hence the exact symptom:
// the widget appears and stays empty forever.
//
// So the producer gate (`active`) and the consumer gate (does it draw?) were
// asking different questions. That is the same defect isTelemetryHistoryNeeded()
// exists to prevent, and the reason the fix derives `active` from
// isVisibleAnySurface() per frame rather than from any setter: any-surface
// visibility ALSO changes when the companion window itself opens or closes, and
// no setter runs then. The last subcase below is the one that would still fail
// if this were fixed in setCompanionVisible() instead.
//
// WHY IT NEEDS A TYPED HOOK. Visibility cannot see this — the widget renders on
// either surface either way, and its numbers never reach /api/state. `active` is
// the only observable that separates "showing data" from "showing an empty
// frame". (TESTING.md principle 2: white box only when the value genuinely never
// surfaces.)
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

TEST_CASE("benchmark collection follows visibility on ANY surface") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\benchmark_companion\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasBenchmarkSurfaces(),
                    "MXBMRP3_Test_Benchmark* hooks not exported (test build?)");
    host.startup(saveWin);
    REQUIRE_MESSAGE(host.bmExists(),
                    "BenchmarkWidget was not built — developer mode off in this build, "
                    "so this test would pass vacuously");
    host.session(/*session=*/1, /*numLaps=*/0);

    // The sync runs in update(), which HudManager calls every frame regardless of
    // visibility — so a frame has to be pumped for a toggle to take effect.
    auto settle = [&host]() { host.draw(); host.draw(); };

    SUBCASE("hidden on both surfaces: collection off (the optimization works)") {
        host.companionWindow(false);
        host.bmSetVisible(false);
        settle();

        CHECK_MESSAGE(!host.bmMetricsActive(),
                      "instrumentation still collecting with the profiler hidden "
                      "everywhere — the whole point of the gate is to cost nothing "
                      "when nobody is looking");
    }

    SUBCASE("visible in game: collecting (the ordinary single-window case)") {
        host.companionWindow(false);
        host.bmSetVisible(true);
        settle();

        CHECK_MESSAGE(host.bmMetricsActive(),
                      "no collection for a profiler visible in game");
    }

    SUBCASE("visible ONLY on the OPEN companion window: collecting (the regression)") {
        // The reported setup: companion window open, profiler switched on there
        // and off in game.
        host.companionWindow(true);
        host.bmSetVisible(false);
        host.bmSetCompanionVisible(true);
        settle();

        CHECK_MESSAGE(host.bmMetricsActive(),
                      "no collection for a profiler visible ONLY on the companion "
                      "window — bm.active is being latched from the game-surface "
                      "toggle again, so the tables render empty (see CLAUDE.md "
                      "Maintenance Invariants)");

        host.companionWindow(false);
    }

    SUBCASE("companion window CLOSING stops collection, with no toggle touched") {
        // The case a setter-side fix would miss. Nothing calls setVisible() or
        // setCompanionVisible() here: only the window goes away, which changes
        // isVisibleAnySurface() underneath the widget. If collection is latched on
        // a setter edge it stays stuck on, profiling forever on a HUD nobody can
        // see — the 480fps budget paying for data with no reader.
        host.companionWindow(true);
        host.bmSetVisible(false);
        host.bmSetCompanionVisible(true);
        settle();
        REQUIRE(host.bmMetricsActive());

        host.companionWindow(false);
        settle();

        CHECK_MESSAGE(!host.bmMetricsActive(),
                      "collection still on after the companion window closed and the "
                      "profiler is hidden in game — the sync is edge-latched on the "
                      "setters instead of derived from isVisibleAnySurface()");
    }
}
