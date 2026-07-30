// ============================================================================
// tests/integration/tests/spectate_cameras_test.cpp
// SpectateCameras — the callback the auto-director steers the camera through.
//
// API_COVERAGE.md carried this as a fuzz-only "gap": the callback fuzzer proved
// the opaque-blob walk doesn't fault, nothing proved the director ever got the
// camera it asked for. Both halves of the callback matter to a broadcast:
//   - it reports when the caster has taken manual control (Orbit / Free /
//     Free-Roam), which pauses the director entirely; and
//   - it turns a semantic role request into an index in THIS track's list.
//
// The resolution logic itself is pure and unit-tested case-by-case in
// tests/unit/test_camera_resolve.cpp (~1s, no Wine). What can only be proved
// here is the WIRING: that the real DLL export consumes a request posted through
// the director's own entry point, writes the index back through *piSelect, and
// returns the "I changed it" flag the game acts on.
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

// SpectateHandler::CameraRole as int, in declaration order.
static constexpr int ROLE_AUTO       = 0;
static constexpr int ROLE_TRACKSIDE  = 1;
static constexpr int ROLE_START      = 2;
static constexpr int ROLE_HELMET     = 4;
static constexpr int ROLE_FORKS      = 7;
static constexpr int ROLE_FREE_ROAM  = 8;

// A representative in-game list. The order is deliberately NOT the role order —
// selecting by index instead of by name would pass on a list that happened to
// line up and fail on every real track.
static const std::vector<std::string> kCameras = {
    "Auto", "Trackside", "Start", "Helmet 1", "Helmet 2",
    "Front Fender", "Rear Fender", "Forks", "Orbit", "Free-Roam",
};

TEST_CASE("SpectateCameras resolves a director camera request to an index") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\cameras\\");

    // No request pending: the callback must not touch the selection.
    int select = 0;
    CHECK(host.spectateCameras(kCameras, /*curSelection=*/0, &select) == 0);
    CHECK(select == 0);

    // The director asks for Trackside. The game learns about it on the next
    // callback (it fires ~140/s during spectate, so this lands within a frame).
    host.requestSpectateCamera(ROLE_TRACKSIDE);
    CHECK(host.spectateCameras(kCameras, /*curSelection=*/0, &select) == 1);
    CHECK(select == 1);

    host.requestSpectateCamera(ROLE_FORKS);
    CHECK(host.spectateCameras(kCameras, /*curSelection=*/0, &select) == 1);
    CHECK(select == 7);

    host.requestSpectateCamera(ROLE_HELMET);
    CHECK(host.spectateCameras(kCameras, /*curSelection=*/0, &select) == 1);
    CHECK(select == 3);

    host.shutdown();
}

TEST_CASE("a camera request is consumed exactly once") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\cameras-oneshot\\");

    int select = 0;
    host.requestSpectateCamera(ROLE_START);
    REQUIRE(host.spectateCameras(kCameras, /*curSelection=*/0, &select) == 1);
    REQUIRE(select == 2);

    // At ~140 calls/second a request that isn't consumed would re-select on every
    // single frame, pinning the camera and making the caster's manual override
    // impossible. The second call must report no change.
    select = 2;
    CHECK(host.spectateCameras(kCameras, /*curSelection=*/2, &select) == 0);
    CHECK(select == 2);

    host.shutdown();
}

TEST_CASE("SpectateCameras reports no change when already on the wanted camera") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\cameras-noop\\");

    int select = 1;
    host.requestSpectateCamera(ROLE_TRACKSIDE);
    // Camera is already index 1. Returning 1 here would make the game re-apply
    // the same cut every frame.
    CHECK(host.spectateCameras(kCameras, /*curSelection=*/1, &select) == 0);
    CHECK(select == 1);

    host.shutdown();
}

TEST_CASE("Free-Roam leaves the camera alone when the track exposes none") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\cameras-freeroam\\");

    const std::vector<std::string> noManual = { "Auto", "Trackside", "Helmet 1" };

    // Free-Roam is the director's gamepad takeover. Falling back to Auto would
    // hand the caster a camera they cannot fly, so the request is dropped instead.
    int select = 1;
    host.requestSpectateCamera(ROLE_FREE_ROAM);
    CHECK(host.spectateCameras(noManual, /*curSelection=*/1, &select) == 0);
    CHECK(select == 1);

    // Every other role does fall back — here to Auto, by name.
    host.requestSpectateCamera(ROLE_FORKS);
    CHECK(host.spectateCameras(noManual, /*curSelection=*/1, &select) == 1);
    CHECK(select == 0);

    host.shutdown();
}

TEST_CASE("SpectateCameras detects the caster taking manual control") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\cameras-manual\\");

    // On Auto: the director is free to cut.
    host.spectateCameras(kCameras, /*curSelection=*/0);
    CHECK_FALSE(host.manualCameraActive());

    // The caster grabs Orbit. The director must pause entirely — otherwise it
    // yanks the shot they are composing by hand.
    host.spectateCameras(kCameras, /*curSelection=*/8);
    CHECK(host.manualCameraActive());

    // Free-Roam counts too.
    host.spectateCameras(kCameras, /*curSelection=*/9);
    CHECK(host.manualCameraActive());

    // Back to a director-owned camera: the flag clears.
    host.spectateCameras(kCameras, /*curSelection=*/1);
    CHECK_FALSE(host.manualCameraActive());

    host.shutdown();
}

TEST_CASE("a camera-list change re-resolves a stale manual flag") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\cameras-relist\\");

    host.spectateCameras(kCameras, /*curSelection=*/8);   // Orbit
    REQUIRE(host.manualCameraActive());

    // A new session/track ships a different list, and index 8 may be anything or
    // nothing. The handler re-resolves on a COUNT change as well as a selection
    // change, so the flag can't survive from the previous track — a stale one
    // would leave the director permanently paused for the rest of the broadcast.
    const std::vector<std::string> shorter = { "Auto", "Trackside", "Helmet 1" };
    host.spectateCameras(shorter, /*curSelection=*/8);
    CHECK_FALSE(host.manualCameraActive());

    host.shutdown();
}

TEST_CASE("SpectateCameras survives degenerate camera lists") {
    // The blob has no element size, so the plugin's walk is bounded by its own
    // cap. None of these may fault or select out of range.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\cameras-degenerate\\");

    int select = -1;
    host.requestSpectateCamera(ROLE_TRACKSIDE);
    CHECK(host.spectateCameras({}, /*curSelection=*/0, &select) == 0);

    // A single unnamed camera: falls back to index 0 rather than off the end.
    select = 0;
    host.requestSpectateCamera(ROLE_TRACKSIDE);
    host.spectateCameras({ "Auto" }, /*curSelection=*/-1, &select);
    CHECK(select == 0);

    // An out-of-range current selection must not be read as a camera name.
    host.resetCameraTracking();
    host.spectateCameras(kCameras, /*curSelection=*/99);
    CHECK_FALSE(host.manualCameraActive());

    host.shutdown();
}
