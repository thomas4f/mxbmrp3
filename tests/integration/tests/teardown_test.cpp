// ============================================================================
// tests/integration/tests/teardown_test.cpp
// Shutdown / teardown crash guard.
//
// Motivation: the analytics dashboard reported access-violation crashes in the
// shipped DLL at shutdown (e.g. mxbmrp3.dlo+0x28616 — a std::map/std::set node
// traversed with a dangling link), reproduced by mxbmrp3_replay.exe. The existing
// golden replay test DOES call shutdown(), but it replays the *slimmed* tape with
// the web server OFF, so no background worker is live and no snapshot rebuild
// races the teardown — exactly the gap this test closes.
//
// Here we bring the teardown path under load: start the HTTP/SSE server (a live
// client keeps frequent Standings snapshots rebuilding), replay a real busy
// 24-rider race so standings/track-position churn hard, hammer the snapshot path
// right up to the edge, then shut down. The critical part is the tail: shutdown()
// runs the ordered manager teardown and ~PluginHost then FreeLibrary()s the DLL
// (static/singleton destruction). A fault in either kills the wine process with a
// non-zero exit and fails this test. Reaching the final assert = clean teardown.
//
// COVERAGE BOUNDARY (read before assuming a green here clears a shutdown crash):
// this cross-build compiles OUT Discord, Steam friends and the records provider,
// and while analytics_manager IS now compiled in (for the wiring test), its
// beacon/worker threads never start here (GAME_HAS_ANALYTICS=0 → plugin_manager
// doesn't init/shutdown it). So this test can only catch a teardown fault in the
// CORE + HTTP path that ships in every build — it CANNOT reproduce a crash that
// lives in one of those excluded managers, or in analytics's live thread teardown
// (which never runs here). It is a regression guard for the tested path, not proof
// those managers tear down cleanly. Confirm those against a symbolized real dump.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

TEST_CASE("teardown: shutdown + unload after a busy session with the web server live is clean") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\teardown\\");

    // Web server + a registered client: HttpServer's listen/SSE threads are live
    // and Standings-change snapshot rebuilds are NOT gated out (hasActiveClients).
    const bool http = host.startHttp();

    // A real, captured 24-rider race: heavy standings / track-position churn.
    const int applied =
        host.replayTape("Z:\\tmp\\mxbmrp3-tests\\fixtures\\race_farm14_24riders.tape");
    CHECK(applied > 0);

    // Keep the client active and drive the render + snapshot path right up to the
    // teardown, so background reads overlap the shutdown that follows.
    for (int i = 0; i < 25; ++i) {
        host.draw();
        (void)host.rawState();
    }

    // The part under test: ordered manager teardown, then DLL unload (static dtors).
    host.shutdown();

    // If we got here the wine process didn't fault during teardown.
    CHECK(applied > 0);
    if (!http) MESSAGE("note: HTTP server did not come up; teardown still exercised");
}

// Regression: the game unloads the DLL WITHOUT calling the Shutdown() export.
//
// This is a real shutdown path (observed crash mxbmrp3.dlo+0x2e0a6: an access
// violation reading a freed unordered_map bucket array in
// SettingsManager::serializeSettings() -> m_hudDefaults.find(), v1.27.0.280).
// When Shutdown() is never called, HudManager::shutdown() never runs explicitly,
// so the teardown falls to ~HudManager during static (DLL-detach) destruction.
// There, the auto-save backstop reached the SettingsManager singleton — which is
// constructed lazily from HudManager::initialize() and therefore destroyed FIRST
// (reverse construction order) — walking its already-freed container. The fix
// makes ~HudManager skip the cross-singleton auto-save; the orchestrated
// Shutdown() path (the case above) still saves.
//
// The whole point is that we DO NOT call host.shutdown(): ~PluginHost FreeLibrary()s
// the DLL, running the C++ static destructors. Auto-save is on by default, so
// ~HudManager's backstop would fire; reaching the final assert = it didn't fault.
//
// COVERAGE BOUNDARY: whether the fiasco actually triggers depends on the static
// construction/destruction ORDER, which differs between the shipped MSVC build
// (where it crashed) and this mingw cross-build (Discord/Steam/records compiled
// out, so fewer singletons and a different first-getInstance() order — it does
// NOT reproduce the fault here). So this is a guard for the unload-without-
// Shutdown() PATH (nothing else exercises it) plus a live assertion that the
// fixed ~HudManager takes the no-cross-singleton branch — not a proven
// fail-before-fix in this build. The order-independent guarantee is in the code:
// ~HudManager calls shutdownInternal(allowSave=false), which never touches
// SettingsManager/UiConfig/PluginManager.
TEST_CASE("teardown: DLL unload WITHOUT Shutdown() (auto-save backstop) is clean") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\teardown_noshutdown\\");

        // Populate real session/standings state and drive a few frames so the
        // settings caches are fully built, exactly as in a live session.
        const int applied =
            host.replayTape("Z:\\tmp\\mxbmrp3-tests\\fixtures\\race_farm14_24riders.tape");
        CHECK(applied > 0);
        for (int i = 0; i < 10; ++i) host.draw();

        // Deliberately NO host.shutdown() here.
    }  // <-- ~PluginHost -> FreeLibrary -> static dtors -> ~HudManager backstop

    // Reached only if the wine process survived the DLL unload.
    CHECK(true);
}
