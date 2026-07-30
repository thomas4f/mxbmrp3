// ============================================================================
// tests/integration/tests/http_gating_test.cpp
// Pins HttpServer::onDataChanged's two-class change gating.
//
// THE RULE (CLAUDE.md "Maintenance Invariants"). Change types split in two:
//   - FREQUENT (Standings, EventLog) fire many times per second on a full grid.
//     While nothing is consuming — no SSE client, no /api/state poll in the last
//     5s — they must NOT rebuild the JSON snapshot. buildJsonSnapshot() runs on
//     the game thread and reaches ~30-40KB on a full grid; doing it per
//     RaceTrackPosition batch for an audience of nobody is the expensive
//     mistake this gate exists to prevent.
//   - RARE (SessionData, RaceEntries, SpectateTarget) are transitions and must
//     rebuild ALWAYS, client or not. The plugin receives NO callbacks while the
//     player sits in menus, so a transition skipped because nobody was watching
//     could never be rebuilt — a client connecting a minute later would be
//     served a snapshot describing a session that already ended, with no
//     rebuild opportunity ever arriving.
//
// WHY IT NEEDED A TEST AND A HOOK. The two halves pull in opposite directions,
// so "optimize the rare types behind the gate too" reads as an obvious cleanup
// and silently breaks the menus case — the exact shape of bug that survives
// review. Nothing caught it: the invariant was labelled Convention.
//
// And it cannot be asserted from outside. A gated frequent change leaves the
// PREVIOUS snapshot in place, which for an unchanged-looking field is
// indistinguishable from a fresh rebuild — /api/state looks identical either
// way. The observable that separates them is the rebuild COUNT, which is the
// SSE sequence: onDataChanged bumps it once per actual rebuild. Hence the typed
// depth hook (TESTING.md principle 2: reach for the white box only when the
// value genuinely never surfaces).
//
// SHAPE. One linear test case, not subcases: the server must be started
// exactly once (a second start() on a live server is a no-op, and every
// repetition would leak a server thread), and the run must end with an explicit
// shutdown() — PluginHost's destructor only FreeLibrary()s, so unloading the
// DLL out from under a running server thread hangs. The client-active check
// comes last because registering a client is irreversible for 5s.
//
// The server is started via startHttpNoClient(): the ordinary startHttp() polls
// /api/state to wait out the bind race, and that poll registers as a client for
// 5s — which would disable the very gate under test.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <chrono>
#include <thread>

// A classification burst: the frequent path, at the rate a real full grid
// produces. Per-rider values change every call so nothing can be skipped as
// "no news" — but the SESSION CLOCK IN THE HEADER IS HELD FIXED, and that is
// load-bearing. RaceClassification carries the session time, so advancing it
// also fires a SessionData change, which is a RARE type and rebuilds
// unconditionally. Varying it would make every burst call rebuild through the
// rare path and the frequent gate would look broken when it is working
// perfectly (this cost a debugging round when the test was written).
static const int kFixedSessionTimeMs = 300000;
static void classifyBurst(PluginHost& host, int n) {
    for (int i = 0; i < n; ++i) {
        host.classify(6, kFixedSessionTimeMs, {
            { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
            { .num = 22, .best = 91000 + i, .laps = 3, .gap = 1500 + i },
        });
    }
}

TEST_CASE("http gating: frequent changes idle, rare transitions always rebuild") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasSnapshotSeq(),
                    "MXBMRP3_Test_SnapshotSeq not exported (test build?)");
    host.startup("Z:\\tmp\\mxbmrp3-tests\\http_gating\\");
    REQUIRE(host.startHttpNoClient());

    // Get into a race with a field, so a classification is genuinely meaningful
    // work to rebuild rather than a no-op on an empty grid.
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(/*session=*/6, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    classifyBurst(host, 1);

    // --- frequent changes idle while nothing is consuming --------------------
    {
        const unsigned long long before = host.snapshotSeq();
        classifyBurst(host, 40);
        CHECK_MESSAGE(host.snapshotSeq() == before,
                      "a Standings burst rebuilt the JSON snapshot with no client "
                      "connected and no recent /api/state poll — the frequent-change "
                      "gate (hasActiveClients) has stopped working, and every "
                      "RaceTrackPosition batch now serializes the whole grid on the "
                      "game thread for an audience of nobody");
    }

    // --- rare transitions rebuild anyway -------------------------------------
    {
        // RaceEntries. The plugin may get no further callbacks after this (the
        // player can sit in menus indefinitely), so this rebuild is the last
        // chance to make the change visible to a later-connecting client.
        unsigned long long before = host.snapshotSeq();
        host.addEntry(33, "Carol");
        CHECK_MESSAGE(host.snapshotSeq() > before,
                      "RaceEntries did not rebuild the snapshot. Rare transition "
                      "types must rebuild unconditionally — the plugin receives no "
                      "callbacks in menus, so a skipped transition can never be "
                      "rebuilt and a client connecting later is served stale state");

        // SessionData, via a session-state transition.
        before = host.snapshotSeq();
        host.raceSessionState(/*session=*/6, /*state=*/32);
        CHECK_MESSAGE(host.snapshotSeq() > before,
                      "a session-state transition did not rebuild the snapshot "
                      "(same reasoning as RaceEntries above)");
    }

    // --- negative control: with a client, frequent changes MUST rebuild ------
    // Without this, a gate that rebuilt for nobody ever (an inverted condition,
    // say) would still pass the first block while the live overlay froze.
    // Last, because registering a client is irreversible for 5s.
    {
        REQUIRE(host.startHttp());          // polls /api/state => counts as a client
        const unsigned long long before = host.snapshotSeq();
        classifyBurst(host, 5);
        CHECK_MESSAGE(host.snapshotSeq() > before,
                      "Standings changes stopped rebuilding the snapshot even with an "
                      "active client — the overlay would freeze");
    }

    // --- and they are COALESCED, not rebuilt one-for-one ---------------------
    // The SSE loop pushes at most once per throttleMs and explicitly skips
    // intermediate snapshots, so a rebuild per frequent change produced whole-grid
    // serializations on the game thread that no client could ever receive. A burst
    // arriving inside the window must therefore collapse to (at most) a couple of
    // rebuilds, not one each.
    //
    // Asserted as a RATIO rather than an exact count: the window is time-based, so a
    // slow runner could legitimately straddle it and take a second build. The bug this
    // guards against is one-rebuild-per-change, which is an order of magnitude away.
    {
        const int kBurst = 60;
        const unsigned long long before = host.snapshotSeq();
        classifyBurst(host, kBurst);
        const unsigned long long rebuilds = host.snapshotSeq() - before;
        CHECK_MESSAGE(rebuilds < static_cast<unsigned long long>(kBurst) / 4,
                      "a burst of " << kBurst << " Standings changes caused " << rebuilds
                      << " snapshot rebuilds — the build-side coalescing window has stopped "
                      "working, so the game thread is serializing the whole grid for pushes "
                      "the throttle will discard");
    }

    // --- but the window only DEFERS: the change still lands -------------------
    // Coalescing sets the stale flag instead of dropping the change, so the next
    // notification after the window elapses rebuilds. Without this a "coalesce" that
    // silently discarded the last change would pass the ratio check above while the
    // overlay showed stale standings until the next transition.
    {
        const unsigned long long before = host.snapshotSeq();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));  // > throttle/2
        classifyBurst(host, 1);
        CHECK_MESSAGE(host.snapshotSeq() > before,
                      "no rebuild after the coalescing window elapsed — deferred frequent "
                      "changes are being dropped rather than caught up, so the overlay "
                      "would sit on stale standings");
    }

    // --- a rare transition is never deferred ---------------------------------
    // The window must not apply to transitions: no callbacks arrive while the player
    // sits in menus, so a deferred transition could never be rebuilt. This fires one
    // immediately after a frequent change, i.e. squarely inside the window.
    {
        classifyBurst(host, 1);                        // opens/refreshes the window
        const unsigned long long before = host.snapshotSeq();
        host.addEntry(44, "Dave");                     // rare type, same instant
        CHECK_MESSAGE(host.snapshotSeq() > before,
                      "a RaceEntries transition was deferred by the coalescing window — "
                      "rare types must rebuild immediately or a transition made just "
                      "before the player enters menus is never published");
    }

    host.shutdown();
}
