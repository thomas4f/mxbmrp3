// ============================================================================
// tests/integration/tests/autoswitch_test.cpp
// Auto-by-session profile switching. When the ProfileManager auto-switch flag is
// on, the plugin follows the game state into the matching profile bucket on each
// SessionData change: Practice/Warmup -> Practice, Qualify* -> Qualify, Race* ->
// Race (Spectate/Replay -> Spectate, not exercised here). Drive RaceSession with
// each PiBoSo session enum and assert the active profile follows; then flip the
// flag off and confirm a session change no longer overrides a manual switch.
//
// The active profile isn't in /api/state, so this reads it via the
// MXBMRP3_Test_GetActiveProfile hook. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

// ProfileType indices (profile_manager.h): Practice=0, Qualify=1, Race=2, Spectate=3.
static constexpr int PRACTICE = 0;
static constexpr int QUALIFY  = 1;
static constexpr int RACE     = 2;

// PiBoSo session enum for a normal Race event (mxbikes_adapter.h toCanonicalSession):
// 1=Practice, 4=Qualify, 6=Race1.
static constexpr int SES_PRACTICE = 1;
static constexpr int SES_QUALIFY  = 4;
static constexpr int SES_RACE     = 6;

TEST_CASE("auto profile switch: active profile follows the session type") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\autoswitch\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack", /*type=*/2);   // normal Race event (not Straight Rhythm)

    // Default active profile is Practice.
    REQUIRE(host.activeProfile() == PRACTICE);

    host.setAutoSwitch(true);

    SUBCASE("session type drives the profile") {
        // Race session -> Race profile (moves off the Practice default).
        host.session(SES_RACE, /*numLaps=*/10);
        CHECK(host.activeProfile() == RACE);

        // Qualify session -> Qualify profile.
        host.session(SES_QUALIFY, /*numLaps=*/0, /*lengthMs=*/600000);
        CHECK(host.activeProfile() == QUALIFY);

        // Practice session -> Practice profile.
        host.session(SES_PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
        CHECK(host.activeProfile() == PRACTICE);
    }

    SUBCASE("disabling auto-switch stops it overriding a manual choice") {
        // First let it resolve to Race automatically.
        host.session(SES_RACE, /*numLaps=*/10);
        REQUIRE(host.activeProfile() == RACE);

        // Turn auto-switch off and manually pick Qualify.
        host.setAutoSwitch(false);
        host.switchProfile(QUALIFY);
        REQUIRE(host.activeProfile() == QUALIFY);

        // A subsequent session change must NOT drag the profile back to a bucket.
        host.session(SES_PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
        CHECK(host.activeProfile() == QUALIFY);
    }

    host.shutdown();
}
