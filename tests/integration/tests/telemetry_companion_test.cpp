// ============================================================================
// tests/integration/tests/telemetry_companion_test.cpp
// The PRODUCER half of the visibility-gate invariant: data that only gets
// accumulated while "someone is watching" must count the companion window as
// someone.
//
// THE BUG THIS PINS. TelemetryHud draws its graphs from PluginData's history
// buffers, and filling those on every RunTelemetry callback is expensive (~200
// deque ops/sec at the 100Hz physics rate), so PluginData gates the append on
// HudManager::isTelemetryHistoryNeeded(). That predicate read isVisible() — the
// GAME surface only — while TelemetryHud::update() rebuilds on
// isVisibleAnySurface(). A telemetry HUD enabled only on the companion window
// therefore rebuilt happily, from buffers nobody was filling: permanently empty
// graphs, with no error anywhere.
//
// WHY IT NEEDED ITS OWN TEST. check_visibility_gates.sh now covers the call site,
// but a lint pins the SPELLING, not the behaviour — it cannot tell that the
// producer and consumer gates ask the same question, only that a particular
// identifier appears. And no black-box assertion can see it either: the history
// buffers never reach /api/state, so "graphs are empty because nothing was
// recorded" and "graphs are empty because the HUD is hidden" look identical from
// outside. Hence the typed depth hook (TESTING.md principle 2: reach for the
// white box only when the value genuinely never surfaces).
//
// The three cases below are the three surface configurations that matter; the
// companion-only one is the regression. Self-contained doctest; see
// run_tests.sh / TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

// Feed n RunTelemetry frames with a live RPM/throttle signal. Values vary per
// frame so a buffer that IS filling can't be mistaken for one holding a single
// repeated sample.
static void pumpTelemetry(PluginHost& host, int n) {
    for (int i = 0; i < n; ++i) {
        TelemetryRow r;
        r.speed    = 20.0f + static_cast<float>(i);
        r.gear     = 3;
        r.rpm      = 6000 + i * 10;
        r.throttle = 0.25f + 0.01f * static_cast<float>(i % 50);
        host.telemetryFrame(r);
    }
}

TEST_CASE("telemetry history accumulates whenever the HUD is visible on ANY surface") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\telemetry_companion\\";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasTelemetrySurfaces(),
                    "MXBMRP3_Test_Telemetry* hooks not exported (test build?)");
    host.startup(saveWin);

    // A session gives limiterRPM / numberOfGears sane values, so the normalizing
    // maths in the append path runs the same way it does in game.
    host.session(/*session=*/1, /*numLaps=*/0);

    SUBCASE("hidden on both surfaces: nothing is recorded (the optimization works)") {
        host.tmClearCompanion();      // back to mirroring the game surface
        host.tmSetVisible(false);
        host.tmClearHistory();

        pumpTelemetry(host, 120);

        CHECK_MESSAGE(host.tmHistoryDepth() == 0,
                      "history accumulated while the HUD was hidden everywhere — "
                      "the gate that saves ~200 deque ops/sec has stopped working");
    }

    SUBCASE("visible in game: recorded (the ordinary single-window case)") {
        host.tmClearCompanion();
        host.tmSetVisible(true);
        host.tmClearHistory();

        pumpTelemetry(host, 120);

        CHECK_MESSAGE(host.tmHistoryDepth() > 0,
                      "no telemetry history recorded for a HUD visible in game");
    }

    SUBCASE("visible ONLY on the OPEN companion window: still recorded (the regression)") {
        // The real user setup: companion window open, telemetry moved onto it and
        // switched off in game. setCompanionVisible() snapshots and decouples, so
        // the game surface stays hidden.
        host.companionWindow(true);
        host.tmSetVisible(false);
        host.tmSetCompanionVisible(true);
        host.tmClearHistory();

        pumpTelemetry(host, 120);

        CHECK_MESSAGE(host.tmHistoryDepth() > 0,
                      "no telemetry history recorded for a HUD visible ONLY on the "
                      "companion window — isTelemetryHistoryNeeded() is reading the "
                      "game-surface flag again (see CLAUDE.md Maintenance Invariants)");

        // And it really is the companion keeping it alive: drop the override so the
        // HUD mirrors the (hidden) game surface again, and accumulation must stop.
        host.tmClearCompanion();
        host.tmClearHistory();
        pumpTelemetry(host, 120);

        CHECK_MESSAGE(host.tmHistoryDepth() == 0,
                      "history still accumulating after the companion override was "
                      "cleared and the HUD is hidden on both surfaces");

        host.companionWindow(false);
    }

    SUBCASE("companion-visible but the window is CLOSED: nothing is recorded") {
        // The other half of the contract, and the reason the fix is
        // isVisibleAnySurface() rather than "OR the companion flag": a companion
        // preference for a window nobody has opened is not a viewer, so the
        // optimization must still apply. Without this case the regression check
        // above would also pass for a naive always-accumulate gate.
        host.companionWindow(false);
        host.tmSetVisible(false);
        host.tmSetCompanionVisible(true);
        host.tmClearHistory();

        pumpTelemetry(host, 120);

        CHECK_MESSAGE(host.tmHistoryDepth() == 0,
                      "history accumulated for a companion-visible HUD while the "
                      "companion window is closed — nothing is displaying it");
    }

    SUBCASE("becoming visible on the COMPANION clears the stale buffers") {
        // The CONSUMER half of the same edge. Accumulation already asks
        // isVisibleAnySurface(); the reset that gives the graph a fresh start was
        // keyed off setVisible() -- the game toggle -- which setCompanionVisible()
        // never reaches (it is not virtual). A HUD switched on for the first time on
        // the companion window therefore drew leftover samples from an earlier
        // in-game stint, with new ones mixed into the same ring.
        // Settle the view state FIRST. The first draw after startup flips
        // spectate<->on-track, and PluginData clears telemetry on that transition
        // (clearTelemetryData) for reasons unrelated to visibility. Written without
        // this, the harness wiped the buffers itself and the case passed with the
        // bug present — a tautology that advertised coverage it did not have.
        host.companionWindow(false);
        host.draw();
        host.draw();

        host.tmClearCompanion();
        host.tmSetVisible(true);
        host.tmClearHistory();
        pumpTelemetry(host, 120);
        REQUIRE(host.tmHistoryDepth() > 0);          // a stint worth of samples

        host.tmSetVisible(false);                    // hidden everywhere
        host.draw();

        // POSITIVE CONTROL, and the reason this case is trustworthy: the samples must
        // still be there at this point. If anything else clears them the final CHECK
        // would pass no matter what the show edge does, which is exactly how the first
        // version of this test fooled a mutation run.
        REQUIRE_MESSAGE(host.tmHistoryDepth() > 0,
                        "buffers emptied before the companion show edge — something "
                        "other than the edge under test is clearing them, so the "
                        "assertion below cannot discriminate");

        // Now switch it on for the companion only. The show edge must clear.
        host.companionWindow(true);
        host.tmSetCompanionVisible(true);
        host.draw();

        CHECK_MESSAGE(host.tmHistoryDepth() == 0,
                      "stale telemetry survived the companion show edge — the reset is "
                      "keyed off the game toggle again, so a companion-only HUD opens "
                      "on someone else's samples");

        host.companionWindow(false);
        host.tmClearCompanion();
    }

    host.shutdown();
}
