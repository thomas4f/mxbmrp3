// ============================================================================
// tests/integration/tests/motion_test.cpp
// Motion's collect side (core/hud_manager_motion.cpp): HUDs fade and slide in
// and out on the copy collectSurface hands the renderer.
//
// WHAT MUST NEVER HAPPEN is Motion changing a frame it has no business
// changing. Off is the frame the plugin always drew: a HUD hidden is gone the
// very next frame and nothing in the frame is touched. And with Motion on, a
// HUD that is settled is not touched either - so every steady frame, which is
// nearly every frame, is exactly the frame Off draws. Both are checked on a
// fingerprint of everything drawn (MXBMRP3_Test_GameFrameHash), not a
// count, because a fade that leaks shows as an alpha byte, not a missing quad.
// (Drawn fields rather than raw bytes: a string's buffer past its terminator is
// whatever it held, and differs between equal frames.)
//
// The positive controls are what make those mean anything: with Motion on, a
// hidden HUD keeps drawing (its ghost, from the last frame it handed over)
// while it fades, and a shown one comes in changed and settles to the steady
// frame; the settings menu's tab body fades on a tab switch; and the Map's
// outline, under its fill, fades faster than the fill so it never shows through.
// The timing itself is pinned in tests/unit/test_motion.cpp.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <vector>

namespace {

constexpr int RACE = 6;   // PiBoSo Race1 session enum
constexpr int OFF = 0, NORMAL = 2;
constexpr long long T0 = 1000000000ll;   // a fixed Motion clock, in microseconds

void openSession(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(RACE, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
                                  { .num = 22, .best = 91000, .laps = 1, .gap = 1000 } });
    host.draw();
}

struct Shot { unsigned long long hash; int quads; int strings; };

Shot shot(PluginHost& host) {
    host.draw();
    return { host.gameFrameHash(), host.lastGameQuads(), host.lastGameStrings() };
}

// Advance Motion's clock by stepMs per frame for `frames` frames.
Shot advance(PluginHost& host, long long& now, int frames, int stepMs) {
    Shot s{};
    for (int i = 0; i < frames; ++i) {
        now += stepMs * 1000ll;
        host.setMotionNowUs(now);
        s = shot(host);
    }
    return s;
}

// Standings on, everything else off, settled. Returns the shown and the
// hidden steady frames, both taken with Motion OFF.
void baseline(PluginHost& host, const char* dir, Shot& shown, Shot& hidden) {
    host.startup(dir);
    host.setMotion(OFF);
    host.setMotionNowUs(T0);
    host.showAllHuds(false);
    host.showSettings(false);
    openSession(host);
    REQUIRE(host.setHudVisible("standings_hud", true));
    host.draw();
    shown = shot(host);
    REQUIRE(host.setHudVisible("standings_hud", false));
    hidden = shot(host);
    REQUIRE(shown.quads > hidden.quads);   // the standings draw something
    REQUIRE(host.setHudVisible("standings_hud", true));
    REQUIRE(shot(host).hash == shown.hash);
}

// A circle the map can draw.
std::vector<TrackSegmentRow> circleTrack(int segs = 64, float trackLen = 1600.0f) {
    std::vector<TrackSegmentRow> v(static_cast<size_t>(segs));
    for (auto& seg : v) {
        seg.type = 1;
        seg.length = trackLen / static_cast<float>(segs);
        seg.radius = trackLen / (2.0f * 3.14159265f);
        seg.angle = 0.0f;
    }
    return v;
}

}  // namespace

TEST_CASE("motion: Off hides and shows on the very next frame, and touches nothing") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE_MESSAGE(host.hasMotion(), "MXBMRP3_Test_SetMotion / GameFrameHash not exported");
    Shot shown{}, hidden{};
    baseline(host, "Z:\\tmp\\mxbmrp3-tests\\motion_off\\", shown, hidden);

    long long now = T0;
    REQUIRE(host.setHudVisible("standings_hud", false));
    CHECK(advance(host, now, 1, 4).hash == hidden.hash);
    REQUIRE(host.setHudVisible("standings_hud", true));
    CHECK(advance(host, now, 1, 4).hash == shown.hash);

    host.shutdown();
}

TEST_CASE("motion: a settled frame with Motion on is the Off frame, to the byte") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    Shot shown{}, hidden{};
    baseline(host, "Z:\\tmp\\mxbmrp3-tests\\motion_settled\\", shown, hidden);

    // Switched on over a HUD already showing: it starts settled, no fade in.
    long long now = T0;
    host.setMotion(NORMAL);
    CHECK(advance(host, now, 1, 4).hash == shown.hash);
    CHECK(advance(host, now, 3, 16).hash == shown.hash);

    host.shutdown();
}

TEST_CASE("motion: Normal fades a hidden HUD out from its ghost, then it is gone") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    Shot shown{}, hidden{};
    baseline(host, "Z:\\tmp\\mxbmrp3-tests\\motion_out\\", shown, hidden);

    long long now = T0;
    host.setMotion(NORMAL);
    advance(host, now, 2, 16);

    REQUIRE(host.setHudVisible("standings_hud", false));
    const Shot fading = advance(host, now, 1, 16);
    CHECK_MESSAGE(fading.quads == shown.quads,
                  "a HUD hidden with Motion on must keep drawing while it fades");
    CHECK(fading.hash != shown.hash);    // ...faded and moved, not as it was
    const Shot gone = advance(host, now, 20, 16);
    CHECK(gone.hash == hidden.hash);

    host.shutdown();
}

TEST_CASE("motion: Normal fades a shown HUD in, and it settles to the steady frame") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    Shot shown{}, hidden{};
    baseline(host, "Z:\\tmp\\mxbmrp3-tests\\motion_in\\", shown, hidden);

    long long now = T0;
    host.setMotion(NORMAL);
    REQUIRE(host.setHudVisible("standings_hud", false));
    advance(host, now, 30, 16);
    REQUIRE(advance(host, now, 1, 16).hash == hidden.hash);

    REQUIRE(host.setHudVisible("standings_hud", true));
    const Shot coming = advance(host, now, 1, 16);
    CHECK(coming.quads == shown.quads);
    CHECK(coming.hash != shown.hash);
    CHECK(advance(host, now, 30, 16).hash == shown.hash);

    host.shutdown();
}

TEST_CASE("motion: a settings tab switch fades the tab body, and settles") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\motion_tab\\");
    host.setMotion(NORMAL);
    long long now = T0;
    host.setMotionNowUs(now);
    host.showAllHuds(false);
    openSession(host);
    host.showSettings(true);
    host.setActiveTab("Appearance");
    advance(host, now, 30, 16);

    // The steady frame of the target tab, Motion off, as the reference.
    host.setActiveTab("General");
    advance(host, now, 30, 16);
    host.setMotion(OFF);
    const Shot general = advance(host, now, 1, 16);
    host.setActiveTab("Appearance");
    advance(host, now, 2, 16);
    host.setMotion(NORMAL);
    advance(host, now, 2, 16);

    host.setActiveTab("General");
    const Shot switching = advance(host, now, 1, 16);
    CHECK(switching.quads == general.quads);
    CHECK(switching.hash != general.hash);
    CHECK(advance(host, now, 30, 16).hash == general.hash);

    host.shutdown();
}

TEST_CASE("motion: the Map's outline fades on its own curve, so the track never lights up") {
    // The outline is a wide white ribbon UNDER the black fill. Faded at one alpha,
    // the part-transparent fill lets it through and the track turns grey-white
    // mid-fade. It fades on alpha cubed instead (BaseHud::m_motionUnderQuadFirst).
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMotion());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\motion_map\\");
    host.setMotion(NORMAL);
    long long now = T0;
    host.setMotionNowUs(now);
    host.showAllHuds(false);
    host.showSettings(false);
    openSession(host);
    host.trackCenterline(circleTrack());
    REQUIRE(host.setHudVisible("map_hud", true));
    const Shot shown = advance(host, now, 30, 16);
    int under = 0, over = 0;
    REQUIRE_MESSAGE(host.mapMotionLayers(under, over), "the map drew no outline - nothing is being tested");
    const int settledUnder = under, settledOver = over;

    REQUIRE(host.setHudVisible("map_hud", false));
    advance(host, now, 30, 16);
    REQUIRE(host.setHudVisible("map_hud", true));
    advance(host, now, 3, 40);   // about halfway in
    REQUIRE(host.mapMotionLayers(under, over));
    INFO("mid-fade outline " << under << " fill " << over
         << ", settled " << settledUnder << " / " << settledOver);
    CHECK(over < settledOver);   // it is fading
    CHECK(under * settledOver < over * settledUnder);   // the outline is further gone than the fill

    CHECK(advance(host, now, 30, 16).hash == shown.hash);   // and settles to the steady frame
    host.shutdown();
}
