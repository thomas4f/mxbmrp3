// ============================================================================
// tests/integration/tests/director_lock_test.cpp
// Auto-director rider lock (hold) release rules. The lock pins the director to a
// rider so it won't auto-cut away; it must survive ordinary data churn but be
// released when the field resets under it. This asserts the invariant that a new
// session (session-generation bump) releases the lock, with a negative control
// that a plain standings update does NOT. The director's shot cadence is
// wall-clock and only runs while spectating, so this deliberately tests the
// release *rules* (via the DirectorToggleLock / DirectorIsLocked hooks), not
// timing-dependent cutting. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

static constexpr int RACE = 6;   // PiBoSo Race1 session enum

TEST_CASE("director lock: a session change releases it, a standings update does not") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_lock\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.directorSetEnabled(true);

    // First session establishes the director's session-generation baseline.
    host.session(RACE, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(RACE, 100000, {
        { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 1, .gap = 1500 },
    });

    // Lock on.
    host.directorToggleLock();
    REQUIRE(host.directorIsLocked());

    // Negative control: a plain standings update must NOT release the lock.
    host.classify(RACE, 120000, {
        { .num = 22, .best = 91000, .laps = 1, .gap = 0 },
        { .num = 10, .best = 90000, .laps = 1, .gap = 800 },
    });
    CHECK(host.directorIsLocked());

    // A new session (generation bump) resets the field and releases the lock.
    host.session(RACE, /*numLaps=*/10);
    CHECK_FALSE(host.directorIsLocked());

    host.shutdown();
}

TEST_CASE("director lock: the camera rotates through the enabled pool (TV -> onboards -> wrap)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\director_lock\\");

    // Pure cycle logic (no session/spectate needed): with the default camera config
    // (Front + Helmet onboards enabled) the pool is [AUTO(0), Front, Helmet]. AUTO is 0
    // (first CameraRole). Assert the structural cycle: TV -> onboard -> onboard -> wrap to
    // TV, and any off-pool role (e.g. a battle Trackside / Free-Roam) starts fresh at TV.
    constexpr int AUTO = 0;

    int c1 = host.directorNextLockedCamera(AUTO);
    CHECK(c1 != AUTO);                                  // TV -> an onboard

    int c2 = host.directorNextLockedCamera(c1);
    CHECK(c2 != AUTO);
    CHECK(c2 != c1);                                    // -> the other enabled onboard

    int c3 = host.directorNextLockedCamera(c2);
    CHECK(c3 == AUTO);                                  // wraps back to the TV shot

    // An off-pool role (not AUTO and not an enabled onboard) restarts at TV.
    CHECK(host.directorNextLockedCamera(99) == AUTO);

    host.shutdown();
}
