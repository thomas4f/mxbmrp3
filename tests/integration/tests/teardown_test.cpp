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
// COVERAGE BOUNDARY (read before assuming a green here clears a shutdown crash).
// The thing that makes a teardown crash a teardown crash is a LIVE background
// thread meeting a destructor, so the boundary is best stated as which
// thread-owning subsystems actually have a thread running when we shut down:
//
//   COVERED — a live thread is running and its join is exercised below:
//     HttpServer (listen + SSE), the Steam friend-scan worker, the RecordsHud
//     fetch worker, and the AnalyticsManager custom-event worker. Steam and
//     analytics are compiled in but inert in production terms (no Steam client
//     under Wine; capture mode makes analytics' sends no-ops) — that does not
//     matter here, because the THREADING LIFECYCLE is identical and it is the
//     lifecycle that faults. Records runs its real worker against a stubbed
//     transport.
//
//   NOT COVERED — DiscordManager. It is the one subsystem compiled out of this
//     build (GAME_HAS_DISCORD is 0 under MXBMRP3_TEST_BUILD), and the blocker is
//     specific: its shutdown casts m_connectionThread.native_handle() to a Win32
//     HANDLE for CancelSynchronousIo, and mingw's posix-threads std::thread hands
//     back a pthread_t. A Discord teardown fault cannot reproduce here; confirm
//     it against a symbolized real dump.
//
// Also uncovered by construction: SEH is MSVC-only, and the static
// construction/destruction ORDER differs from the shipped build (see the second
// case's own boundary note). This is a regression guard for the paths it drives,
// not proof that every manager tears down cleanly.
//
// KNOWN HAZARD, deliberately not covered by a running test: a worker thread that
// is STILL ALIVE when the DLL is unloaded without Shutdown() deadlocks the
// process rather than crashing it. See the analytics case at the bottom of this
// file for the mechanism, the two workers it was reproduced with, and why fixing
// it is a design call rather than a test change. It is also carried as a
// Maintenance Invariant in CLAUDE.md ("A worker thread must not outlive the
// DLL") — a hazard that only exists in a test comment is one the next person to
// add a background thread will never read.
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
// This is a real shutdown path with TWO observed crash generations:
//
// 1. v1.27.0.280 (mxbmrp3.dlo+0x2e0a6): an access violation reading a freed
//    unordered_map bucket array in SettingsManager::serializeSettings() ->
//    m_hudDefaults.find(). When Shutdown() is never called, the teardown falls
//    to ~HudManager during static (DLL-detach) destruction, whose auto-save
//    backstop reached the SettingsManager singleton — constructed lazily and
//    therefore destroyed FIRST (reverse construction order) — walking its
//    already-freed container. Fixed by making ~HudManager skip the
//    cross-singleton auto-save (shutdownInternal(allowSave=false)); the
//    orchestrated Shutdown() path (the case above) still saves.
//
// 2. v1.27.7.44 (analytics backtraces, map-resolved): the SAME fiasco one level
//    up. ~PluginManager ran the full orchestrated shutdown() from
//    _execute_onexit_table, faulting inside StatsManager::save() and inside
//    PluginData::getStanding() (via tryRecordRaceFinish) — PluginManager is the
//    first singleton constructed, so its destructor runs last, after those
//    singletons are already destructed. Fixed by making ~PluginManager inert on
//    this path (it only uninstalls the SEH filter); every singleton's own
//    destructor backstop handles its own teardown.
//
// The whole point is that we DO NOT call host.shutdown(): ~PluginHost FreeLibrary()s
// the DLL, running the C++ static destructors. Auto-save is on by default, so
// ~HudManager's backstop would fire; the replayed race also leaves live
// standings state that generation-2's tryRecordRaceFinish would have walked.
// Reaching the final assert = nothing faulted.
//
// COVERAGE BOUNDARY: whether the fiasco actually triggers depends on the static
// construction/destruction ORDER, which differs between the shipped MSVC build
// (where both generations crashed) and this mingw cross-build (Discord/Steam/
// records compiled out, so fewer singletons and a different first-getInstance()
// order — it does NOT reproduce the faults here). So this is a guard for the
// unload-without-Shutdown() PATH (nothing else exercises it) plus a live
// assertion that the fixed destructors take their inert branches — not a proven
// fail-before-fix in this build. The order-independent guarantee is in the code:
// ~HudManager calls shutdownInternal(allowSave=false) and ~PluginManager never
// runs the orchestrated shutdown() (it only uninstalls the SEH filter).
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

        // Deliberately NO host.shutdown() here — and say so to ~PluginHost, which
        // otherwise shuts down before unloading (added after four tests forgot
        // and blueflag_test crashed under load). This case's SUBJECT is that
        // path, so it opts out explicitly; without the call the safety net would
        // quietly turn this into a copy of the case above.
        host.skipShutdownOnDestroy();
    }  // <-- ~PluginHost -> FreeLibrary -> static dtors -> ~HudManager backstop

    // Reached only if the wine process survived the DLL unload.
    CHECK(true);
}

// ============================================================================
// The friend-scan worker's teardown. scanFriends() was moved off the game thread
// because it ran ~11 SEH-wrapped Steam IPC calls per friend, inline in Draw, and
// showed up as a periodic 2 ms frame spike every SCAN_INTERVAL_MS.
//
// The scan needs a Steam client, so it cannot run here — but the part that can
// take the game down doesn't need Steam at all: shutdown() must JOIN the worker
// before it clears m_friends and the m_fn* pointers the worker reads unlocked.
// Get that order wrong and you get a hang or a use-after-free at teardown, which
// is precisely what this pins. Without the join, this test times out.
// ============================================================================
TEST_CASE("teardown: the Steam friend-scan worker is joined by shutdown()") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\teardown-steam\\");

        // With no Steam client the scan short-circuits, so the worker just parks
        // on its condition variable — the state a shutdown has to interrupt.
        REQUIRE(host.steamStartWorker());
        CHECK(host.steamWorkerRunning());

        host.shutdown();

        // Joined, not merely signalled: a shutdown that returned while the thread
        // was still running would leave it reading freed Steam pointers.
        CHECK_FALSE(host.steamWorkerRunning());
    }  // ~PluginHost -> FreeLibrary; a surviving thread would fault here

    CHECK(true);
}

// ============================================================================
// The records fetch worker's teardown, with a fetch STILL IN FLIGHT.
//
// This is the documented invariant "HudManager::clear() joins RecordsHud's fetch
// thread BEFORE nulling the HUD pointers" — the worker calls
// getTimingHud().setDataDirty() when it completes, so a completion landing inside
// that window dereferences a null m_pTiming. Nothing exercised it: every other
// test either never starts a fetch or lets it finish first, and the crash needs
// the worker to be mid-flight exactly when teardown begins.
//
// The stub's delay is what makes that deterministic — the fetch is guaranteed
// unfinished when shutdown() runs, so the join is the only thing that can end it.
// ============================================================================
TEST_CASE("teardown: an in-flight records fetch is joined by shutdown()") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\teardown-records\\");

        // A slow stubbed response: the worker is still parked in it at shutdown.
        constexpr int kStubDelayMs = 3000;
        host.recordsSetFetchStub(kStubDelayMs, "[]");
        REQUIRE(host.recordsStartFetch());
        REQUIRE(host.recordsFetchState() == 1);        // FETCHING

        // Time the shutdown. Surviving it proves nothing on its own — a shutdown
        // that ABANDONED the worker would also return, and would then fault when
        // FreeLibrary pulls the code out from under a thread still sleeping in the
        // stub. Blocking for the stub's remaining delay is what actually shows the
        // join happened. Generous slack: this asserts "waited", not "waited 3s".
        const DWORD t0 = GetTickCount();
        host.shutdown();
        const DWORD elapsed = GetTickCount() - t0;
        CHECK(elapsed >= static_cast<DWORD>(kStubDelayMs / 2));

        // NOTE: do not call recordsFetchState() (or any other HUD-reading hook)
        // after shutdown() — HudManager::clear() has nulled the cached HUD
        // pointers by then, so getRecordsHud() dereferences null. An earlier
        // version of this test did exactly that and faulted here.
    }  // ~PluginHost -> FreeLibrary

    CHECK(true);
}

// ============================================================================
// The analytics custom-event worker's teardown.
//
// analytics_manager is compiled into this build for the wiring test, but nothing
// ever STARTED its threads here (GAME_HAS_ANALYTICS is 0, so plugin_manager never
// initializes it) — so ~AnalyticsManager always took the "nothing to join" branch
// and its join was dead code as far as the suite was concerned. That matters
// because analytics is a Meyers singleton with a worker that outlives the
// orchestrated shutdown: on the unload path its destructor runs during static
// teardown, which is exactly where both shipped crash generations lived.
//
// testStartEventWorker() starts the REAL worker; capture mode keeps its sends
// no-ops, so it parks on its condvar — the state shutdown() has to wake and join.
// A missed notify hangs this test rather than failing it, which is the correct
// signal: that IS the production failure (the game never exits).
//
// WHY THIS JOINS EXPLICITLY INSTEAD OF LETTING THE UNLOAD DO IT — and it is not
// a detail of this manager. Leaving the worker alive across FreeLibrary DEADLOCKS,
// reproducibly, and so does the Steam worker under the same treatment (both
// verified: the test reaches "about to unload" and never returns). The mechanism
// is the classic one — FreeLibrary holds the loader lock while it runs the static
// destructors, the destructor calls join(), and the worker needs that same lock to
// finish detaching. It is not analytics-specific and not a property of any one
// singleton: it is a property of joining ANY thread from a destructor that runs at
// DLL detach.
//
// That makes the unload-without-Shutdown() case above (which is a REAL path — it
// is where both shipped crash generations lived) safe only for as long as no
// worker thread happens to be running when it fires. Nothing enforces that today.
// Deliberately NOT "fixed" here: the options (detach instead of join at detach,
// a detach-in-progress flag, refusing to join when DllMain's lpvReserved says the
// process is exiting) change shutdown semantics for every worker in the plugin and
// that is a design call, not a test fixup. Observed under Wine; the loader-lock
// rule is the same on Windows but this has not been confirmed against the shipped
// MSVC build.
// ============================================================================
TEST_CASE("teardown: a live analytics event worker is drained and joined") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\teardown-analytics\\");

        host.analyticsPrime();                          // identity/session + capture mode
        REQUIRE(host.analyticsStartEventWorker());
        CHECK(host.analyticsEventWorkerRunning());

        // Queue work so the drain loop has something to flush on the way out,
        // rather than only ever observing the empty-queue exit.
        host.analyticsQueueCustom("teardown_probe");

        // The join under test. Stands in for what PluginManager's orchestrated
        // Shutdown() does in a shipping build (GAME_HAS_ANALYTICS is 0 here, so
        // nothing else would ever run it) — and it must happen BEFORE unload, per
        // the deadlock note above.
        host.analyticsShutdown();
        CHECK_FALSE(host.analyticsEventWorkerRunning());
    }  // ~PluginHost -> FreeLibrary, with no live worker left to join

    CHECK(true);
}
