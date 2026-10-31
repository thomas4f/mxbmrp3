// ============================================================================
// tests/integration/tests/impact_vignette_test.cpp
// The impact vignette through the real callbacks: a hard hit in RunTelemetry
// puts its one quad in the game frame, and nothing else does.
//
// WHAT MUST NEVER HAPPEN is the screen darkening when it should not: with the
// helmet off (the default), with the setting Off, while riding normally, or in
// a spectate or replay view, where the player is not the one taking the hit.
// Each of those is checked against the same hit, and the positive control (the
// helmet on and the setting at its default, a hard hit, on track) is what
// makes them mean anything. The envelope's timing is pinned in
// tests/unit/test_impact_vignette.cpp.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>

namespace {

constexpr int ON_TRACK = 0, SPECTATE = 1;

// Quads in the game frame after one telemetry sample at `g`. The plugin's
// frame, not the engine handoff: the vignette's first frame can be alpha 0
// (it fades in), which the handoff drops as drawing nothing.
int quadsAfterHit(PluginHost& host, float g, int view) {
    TelemetryRow r;
    r.accelY = g;
    host.telemetryFrame(r);
    host.drawWithState(view);
    return host.gameFrameQuads();
}

}  // namespace

TEST_CASE("impact vignette: only a hard hit, with the helmet on, on track, darkens the screen") {
    const std::string save = "Z:\\\\tmp\\\\mxbmrp3-tests\\\\impact_vignette\\\\";
    CreateDirectoryA("Z:\\\\tmp\\\\mxbmrp3-tests", nullptr);
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\\\textures").c_str(), nullptr);
    REQUIRE_MESSAGE(CopyFileA(MXB_REPO_DATA_DIR "/textures/impact_vignette_1.tga",
                              (save + "mxbmrp3\\\\textures\\\\impact_vignette_1.tga").c_str(),
                              FALSE) != 0,
                    "could not stage the vignette art - the case would be vacuous");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasImpactVignette());
    host.startup(save.c_str());
    host.showAllHuds(false);
    host.showSettings(false);
    // Always shown in the plugin (Off is the strength); the harness's hide-all
    // just hid it with everything else.
    REQUIRE(host.setHudVisible("impact_vignette", true));
    host.drawWithState(ON_TRACK);
    const int bare = quadsAfterHit(host, 1.0f, ON_TRACK);
    CHECK_MESSAGE(quadsAfterHit(host, 15.0f, ON_TRACK) == bare,
                  "the vignette drew with the helmet off, its default");

    // The helmet on brings the vignette with it, at its default strength.
    REQUIRE(host.setHudVisible("helmet_overlay", true));
    host.drawWithState(ON_TRACK);
    const int base = quadsAfterHit(host, 1.0f, ON_TRACK);
    CHECK_MESSAGE(quadsAfterHit(host, 4.5f, ON_TRACK) == base,
                  "hard braking or cornering load darkened the screen");
    // Two frames in the view first: changing view wipes the bike telemetry
    // (PluginData::setDrawState), hit included, so a hit sent on the switching
    // frame would never reach the HUD and the case would pass vacuously.
    host.drawWithState(SPECTATE);
    CHECK_MESSAGE(quadsAfterHit(host, 15.0f, SPECTATE) == base,
                  "the vignette drew in a spectate view");
    host.drawWithState(ON_TRACK);
    CHECK_MESSAGE(quadsAfterHit(host, 200.0f, ON_TRACK) == base,
                  "a spawn or reset glitch (past PlausibleG::MAX) flashed the screen");
    CHECK_MESSAGE(quadsAfterHit(host, 15.0f, ON_TRACK) == base + 1,
                  "a hard hit did not draw the vignette's one quad");

    host.setImpactVignette(0.0f);
    host.drawWithState(ON_TRACK);
    CHECK_MESSAGE(host.lastGameQuads() == base,
                  "switching it Off mid-fade left the vignette on screen");

    host.shutdown();
}

TEST_CASE("impact vignette: crashed, the hit that crashed you fades out and later knocks add nothing") {
    const std::string save = "Z:\\\\tmp\\\\mxbmrp3-tests\\\\impact_vignette_crash\\\\";
    CreateDirectoryA("Z:\\\\tmp\\\\mxbmrp3-tests", nullptr);
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\\\textures").c_str(), nullptr);
    REQUIRE_MESSAGE(CopyFileA(MXB_REPO_DATA_DIR "/textures/impact_vignette_1.tga",
                              (save + "mxbmrp3\\\\textures\\\\impact_vignette_1.tga").c_str(),
                              FALSE) != 0,
                    "could not stage the vignette art - the case would be vacuous");

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasImpactVignette());
    host.startup(save.c_str());
    host.showAllHuds(false);
    host.showSettings(false);
    REQUIRE(host.setHudVisible("impact_vignette", true));
    REQUIRE(host.setHudVisible("helmet_overlay", true));
    host.eventInit("TestTrack", "Me");
    host.raceEvent("TestTrack");
    host.addEntry(5, "Me");   // first entry after EventInit = the player
    host.drawWithState(ON_TRACK);
    host.setImpactVignette(1.0f);
    const int base = quadsAfterHit(host, 1.0f, ON_TRACK);

    // The hit, then the crash: its vignette carries on fading.
    CHECK(quadsAfterHit(host, 15.0f, ON_TRACK) == base + 1);
    host.raceTrackPosition({ { .num = 5, .trackPos = 0.5f, .crashed = 1 } });
    CHECK_MESSAGE(quadsAfterHit(host, 1.0f, ON_TRACK) == base + 1,
                  "the crash cut the running vignette off");

    // Let it clear (the fade defaults to 1.5 s), still crashed.
    host.setImpactVignette(0.0f);
    host.drawWithState(ON_TRACK);
    host.setImpactVignette(1.0f);
    REQUIRE(quadsAfterHit(host, 1.0f, ON_TRACK) == base);
    CHECK_MESSAGE(quadsAfterHit(host, 15.0f, ON_TRACK) == base,
                  "a knock while crashed (the bike tumbling) drew the vignette");

    // Back on the bike, hits show again.
    host.raceTrackPosition({ { .num = 5, .trackPos = 0.5f, .crashed = 0 } });
    CHECK(quadsAfterHit(host, 15.0f, ON_TRACK) == base + 1);

    // The flag rising in the same frame as the hit that threw the rider: that
    // hit still shows.
    host.setImpactVignette(0.0f);
    host.drawWithState(ON_TRACK);
    host.setImpactVignette(1.0f);
    REQUIRE(quadsAfterHit(host, 1.0f, ON_TRACK) == base);
    host.raceTrackPosition({ { .num = 5, .trackPos = 0.5f, .crashed = 1 } });
    CHECK_MESSAGE(quadsAfterHit(host, 15.0f, ON_TRACK) == base + 1,
                  "the hit that crashed the rider was dropped with the flag");
    host.shutdown();
}
