// ============================================================================
// tests/integration/tests/smoke_test.cpp
// Lifecycle smoke test: load the cross-compiled plugin DLL and drive the core
// exports the way the game does — Startup -> DrawInit -> Draw -> Shutdown —
// proving the plugin actually *runs* (managers init, settings load, HTTP thread
// starts) under Wine, not just that it links. The cheapest, first-to-fail check.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

typedef int (*PFN_DrawInit)(int*, char**, int*, char**);

TEST_CASE("smoke: Startup -> DrawInit -> Draw -> Shutdown") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());

    // Startup returns the telemetry-rate enum (>= 0 on success).
    int rate = host.startup("Z:\\tmp\\mxbmrp3-tests\\smoke\\");
    CHECK(rate >= 0);

    // DrawInit reports sprite/font counts the plugin registered.
    if (auto DrawInit = host.sym<PFN_DrawInit>("DrawInit")) {
        int ns = 0, nf = 0; char* sn = nullptr; char* fn = nullptr;
        DrawInit(&ns, &sn, &nf, &fn);
        CHECK(ns >= 0);
        CHECK(nf >= 0);
    }

    // Draw must return without crashing and hand back non-negative primitive counts.
    host.draw();

    // A minimally-populated frame must actually emit primitives: an event with
    // one entry always renders at least the version widget / enabled HUD chrome,
    // so zero quads here means the render pipeline silently died (the survival
    // checks above can't catch that).
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.classify(6, 1000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    host.draw();
    CHECK(host.lastGameQuads() > 0);
    CHECK(host.lastGameStrings() > 0);

    host.shutdown();
    CHECK(true);  // reaching here means the full lifecycle survived
}
