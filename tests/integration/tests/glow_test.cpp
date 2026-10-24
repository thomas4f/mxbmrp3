// ============================================================================
// tests/integration/tests/glow_test.cpp
// The glow (Appearance > Glow, BaseHud::addGlow): one soft halo quad behind
// each light that calls for action, in the light's own colour, drawn before the
// lights so none lies over a neighbour.
//   - the RPM widget's lit segments glow, and only those; the red ones (and
//     red low fuel, WRONG WAY, RIDER ONCOMING, the rev limiter) at full strength, the rest at
//     UiConfig::GLOW_CALM of it
//   - a warning notice glows behind its slab, on the Notices HUD and the Dash
//   - every Dash Fuel bar glows, the unlit and fading ones as dim as they are
//     drawn
//   - the Dash RPM page's lit lights glow
//   - a fading RPM light glows full from its first step: at the shift point
//     the first red one is the act-now cue
//   - the Gear widget's red digit glows from the shift point, and the limiter
//     circle draws at a fixed 80% (at full strength the red digit vanished on it)
//   - [Display] glowIntensity / glowWidth scale the halo's opacity and reach,
//     and survive a save clamped (a hand-edited inf or 9 cannot stick)
//
// SWITCHING IS THE CASE THAT BIT on the Direct GL experiment this came from:
// the Notices panel rebuilds only when its notice changes, so a switch mid-
// warning kept the old halo. HudManager rebuilds every HUD when the glow
// changes; the notice case flips it live (no settings reload) to pin that.
//
// The harness ships no assets, so the glow texture is staged through the
// user-override sync before startup; without it nothing glows (the shipped
// fallback for an install missing the file). The test build ships the glow
// off (UiConfig::GLOW_DEFAULT), so every other test's quad counts stand.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int SHIFT = 10500;
constexpr int LIMITER = 12000;
constexpr unsigned GLOW_ALPHA = 114;   // RpmWidget::GLOW_OPACITY 0.45 of 255: a critical light
constexpr unsigned CALM_ALPHA = 57;    // half that (UiConfig::GLOW_CALM): a warning
constexpr unsigned NOTICE_ALPHA = 102; // NoticesHud::GLOW_OPACITY 0.4 of 255: a critical notice

unsigned alpha(unsigned long c) { return static_cast<unsigned>((c >> 24) & 0xFF); }
// A lit RPM light, whole or fading up: above the unlit 0x3F.
constexpr unsigned ON_ALPHA = 0x3F + 8;

std::string stageGlow(const char* name) {
    const std::string save = std::string("Z:\\tmp\\mxbmrp3-tests\\") + name + "\\";
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests", nullptr);
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\textures").c_str(), nullptr);
    REQUIRE_MESSAGE(CopyFileA(MXB_REPO_DATA_DIR "/textures/glow_1.tga",
                              (save + "mxbmrp3\\textures\\glow_1.tga").c_str(), FALSE) != 0,
                    "could not stage glow_1.tga - the case would be vacuous");
    return save;
}

void setUp(PluginHost& host, const std::string& save, const std::string& ini) {
    REQUIRE(host.loaded());
    REQUIRE(host.startup(save.c_str()) >= 0);
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasGlow());
    REQUIRE_MESSAGE(host.textureSprite("glow") > 0, "the staged glow texture did not register");
    host.writeSettingsFile(save.c_str(), "[Settings]\nversion=10\n\n[Messages]\ndashTried=1\n\n" + ini);
    host.loadSettings(save.c_str());
    host.setMotion(0);
    host.eventInit("GlowTrack", "Player", 1600.0f, 2, "Test 450", "MX1", "", 0, "", 8.0f, SHIFT, LIMITER);
    host.drawWithState(0);
}

void ride(PluginHost& host, int rpm, float fuel = 0.0f) {
    TelemetryRow row;
    row.gear = 3;
    row.rpm = rpm;
    row.speed = 20.0f;
    row.fuel = fuel;
    host.telemetryFrame(row);
    host.drawWithState(0);
    host.drawWithState(0);
}

// A HUD's quads with the glow off, then on at the given scales, without a
// settings reload in between.
struct Pass { std::vector<PluginHost::QuadRect> rects; std::vector<unsigned long> colors; };
Pass capture(PluginHost& host, const char* hud) {
    Pass p;
    p.rects = host.hudQuadRects(hud);
    for (size_t i = 0; i < p.rects.size(); ++i) p.colors.push_back(host.quadColor(hud, static_cast<int>(i)));
    return p;
}
Pass withGlow(PluginHost& host, const char* hud, bool on, float intensity = 1.0f, float width = 1.0f) {
    host.setGlow(on, intensity, width);
    host.drawWithState(0);
    host.drawWithState(0);
    return capture(host, hud);
}

// A race with the player (#4) at mid-lap and #7 riding backward ahead: the
// rider oncoming warning, which the Notices HUD and the Dash both show.
void oncomingScene(PluginHost& host) {
    host.raceEvent("GlowTrack", 2);
    host.session(6, 3, 0);
    host.addEntry(4, "Player");
    host.addEntry(22, "Crashed");
    host.addEntry(7, "Backward");
    host.runInit(6);
    host.classify(6, 300000, { { .num = 4,  .laps = 2, .gap = 0 },
                               { .num = 22, .laps = 2, .gap = 500 },
                               { .num = 7,  .laps = 2, .gap = 3000 } });
    auto positions = [&](float wrongWay, int crashed) {
        host.raceTrackPosition({ { .num = 4,  .trackPos = 0.500f, .posZ = 120.0f },
                                 { .num = 22, .trackPos = 0.530f, .crashed = crashed, .posZ = 150.0f },
                                 { .num = 7,  .trackPos = wrongWay, .posZ = 250.0f } });
    };
    positions(0.640f, 0);
    positions(0.635f, 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    positions(0.630f, 1);
    host.drawWithState(0);
    host.drawWithState(0);
}
}  // namespace

TEST_CASE("glow: the RPM widget's lit segments glow behind the strip, scaled by the INI") {
    PluginHost host(dllPath());
    setUp(host, stageGlow("glow_rpm"), "[RpmWidget]\nvisible=1\n");
    ride(host, SHIFT);   // green and amber lit, the first red on: not the limiter

    const Pass plain = withGlow(host, "rpm_widget", false);
    const Pass lit = withGlow(host, "rpm_widget", true);
    REQUIRE(plain.rects.size() >= 15);
    const size_t segFrom = plain.rects.size() - 15;   // the strip's 15 segments, last
    int litSegments = 0;
    for (size_t i = segFrom; i < plain.rects.size(); ++i)
        if (alpha(plain.colors[i]) > ON_ALPHA) ++litSegments;
    REQUIRE(litSegments > 0);
    REQUIRE(litSegments < 15);
    REQUIRE(lit.rects.size() == plain.rects.size() + static_cast<size_t>(litSegments));

    // One halo per lit segment, all of them before the first segment, each in
    // its segment's colour and reaching past it all round: the first red one (lit
    // at the shift point, the last lit, still fading up) at full strength, green
    // and amber calm.
    for (int k = 0; k < litSegments; ++k) {
        const size_t g = segFrom + static_cast<size_t>(k);
        const PluginHost::QuadRect& seg = plain.rects[segFrom + static_cast<size_t>(k)];
        CHECK(alpha(lit.colors[g]) == (k == litSegments - 1 ? GLOW_ALPHA : CALM_ALPHA));
        CHECK((lit.colors[g] & 0xFFFFFFul) == (plain.colors[segFrom + static_cast<size_t>(k)] & 0xFFFFFFul));
        CHECK(lit.rects[g].l < seg.l);
        CHECK(lit.rects[g].r > seg.r);
        CHECK(lit.rects[g].t < seg.t);
        CHECK(lit.rects[g].b > seg.b);
    }
    // The segments themselves are untouched.
    for (size_t i = segFrom; i < plain.rects.size(); ++i)
        CHECK(lit.colors[i + static_cast<size_t>(litSegments)] == plain.colors[i]);

    // [Display] glowIntensity / glowWidth: twice as strong, twice as far.
    const Pass strong = withGlow(host, "rpm_widget", true, 2.0f, 2.0f);
    REQUIRE(strong.rects.size() == lit.rects.size());
    CHECK(alpha(strong.colors[segFrom]) == GLOW_ALPHA);   // a calm one, doubled
    const PluginHost::QuadRect& seg0 = plain.rects[segFrom];
    CHECK((strong.rects[segFrom].b - seg0.b) > 1.9 * (lit.rects[segFrom].b - seg0.b));

    // Off again: no halo left behind.
    const Pass off = withGlow(host, "rpm_widget", false);
    CHECK(off.rects.size() == plain.rects.size());
    host.shutdown();
}

TEST_CASE("glow: a warning notice glows on the Notices HUD and the Dash, switched live") {
    PluginHost host(dllPath());
    setUp(host, stageGlow("glow_notice"), "[NoticesHud]\nvisible=1\n\n[DashHud]\nvisible=1\n");
    oncomingScene(host);
    REQUIRE_MESSAGE(host.hudScreenEdges(PluginHost::HUD_NOTICES).b > 0,
                    "no warning notice is up - the case would be vacuous");

    // Switched on mid-warning: the halo comes up behind the slab.
    const Pass onNotices = withGlow(host, "notices_hud", true);
    const Pass onDash = capture(host, "dash_hud");
    // Switched off mid-warning, with no notice change to rebuild on: gone.
    const Pass offNotices = withGlow(host, "notices_hud", false);
    const Pass offDash = capture(host, "dash_hud");
    CHECK(onNotices.rects.size() == offNotices.rects.size() + 1);
    CHECK(onDash.rects.size() == offDash.rects.size() + 1);
    // Rider oncoming is critical: the halo at full strength (NoticesHud::GLOW_OPACITY).
    auto atFull = [](const Pass& p) {
        return std::count_if(p.colors.begin(), p.colors.end(),
                             [](unsigned long c) { return alpha(c) == NOTICE_ALPHA; });
    };
    CHECK(atFull(onNotices) == atFull(offNotices) + 1);
    host.shutdown();
}

TEST_CASE("glow: every Dash Fuel bar glows, the unlit ones as dim as they are drawn") {
    PluginHost host(dllPath());
    setUp(host, stageGlow("glow_fuel"),
          "[DashHud]\nvisible=1\npage=fuel\nnotice_takeover=0\nstrip=bottom\n");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Player");
    ride(host, 5000, 4.96f);   // 62% of the 8 l tank: six of ten bars lit, the seventh fading up
    REQUIRE(host.dashPage() == 4);

    const Pass plain = withGlow(host, "dash_hud", false);
    const Pass lit = withGlow(host, "dash_hud", true);
    REQUIRE(lit.rects.size() == plain.rects.size() + 10);
    // After the panel, ten halos, then the ten bars as before.
    int strong = 0, faint = 0;
    for (int i = 1; i <= 10; ++i) {
        const unsigned a = alpha(lit.colors[static_cast<size_t>(i)]);
        if (a == CALM_ALPHA) ++strong;   // no laps left yet: not critical
        else if (a == 14) ++faint;       // that x RpmWidget::UNLIT_OPACITY 0.25
        CHECK(lit.colors[static_cast<size_t>(i + 10)] == plain.colors[static_cast<size_t>(i)]);
    }
    CHECK(strong == 6);
    CHECK(faint == 3);
    CHECK(alpha(lit.colors[7]) == 25);   // the fading one: as dim as it is drawn (0.4375)
    host.shutdown();
}

TEST_CASE("glow: the Dash RPM page's lit lights glow") {
    PluginHost host(dllPath());
    setUp(host, stageGlow("glow_dash_rpm"),
          "[DashHud]\nvisible=1\npage=rpm\npage_rpm=1\nnotice_takeover=0\nstrip=bottom\n");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Player");
    ride(host, SHIFT);
    REQUIRE(host.dashPage() == 8);

    const Pass plain = withGlow(host, "dash_hud", false);
    const Pass lit = withGlow(host, "dash_hud", true);
    int litLights = 0;
    for (int i = 1; i <= 15; ++i)
        if (alpha(plain.colors[static_cast<size_t>(i)]) > ON_ALPHA) ++litLights;
    REQUIRE(litLights > 0);
    REQUIRE(litLights < 15);
    CHECK(lit.rects.size() == plain.rects.size() + static_cast<size_t>(litLights));
    for (int i = 1; i <= litLights; ++i)
        CHECK(alpha(lit.colors[static_cast<size_t>(i)]) == (i == litLights ? GLOW_ALPHA : CALM_ALPHA));
    host.shutdown();
}

TEST_CASE("glow: the Gear digit glows from the shift point; the limiter circle stays at 80%") {
    PluginHost host(dllPath());
    setUp(host, stageGlow("glow_gear"), "");
    ride(host, SHIFT - 1);
    const Pass below = withGlow(host, "gear_widget", true);
    CHECK(withGlow(host, "gear_widget", false).rects.size() == below.rects.size());

    ride(host, SHIFT);
    const Pass plain = withGlow(host, "gear_widget", false);
    const Pass lit = withGlow(host, "gear_widget", true);
    REQUIRE(lit.rects.size() == plain.rects.size() + 1);
    CHECK(alpha(lit.colors.back()) == 76);   // half the gear's own 0.6: not yet the limiter

    // On the limiter the circle is drawn at 80%, whatever the Glow switch or its scales.
    ride(host, LIMITER);
    const Pass circle = withGlow(host, "gear_widget", false);
    REQUIRE(circle.rects.size() == plain.rects.size() + 1);
    CHECK(alpha(circle.colors.back()) == 204);
    // On it the digit's glow is critical: full, behind the circle.
    const Pass limiter = withGlow(host, "gear_widget", true);
    REQUIRE(limiter.rects.size() == circle.rects.size() + 1);
    CHECK(alpha(limiter.colors[limiter.colors.size() - 2]) == 153);
    CHECK(alpha(withGlow(host, "gear_widget", false, 2.0f).colors.back()) == 204);
    CHECK(alpha(withGlow(host, "gear_widget", true, 0.5f).colors.back()) == 204);
    host.shutdown();
}

TEST_CASE("glow: the switch and its INI scales save, and a hand-edited scale is clamped") {
    const std::string save = stageGlow("glow_persist");
    const std::string iniPath = save + "mxbmrp3\\mxbmrp3_settings.ini";
    PluginHost host(dllPath());
    setUp(host, save, "[Display]\nglow=0\nglowIntensity=inf\nglowWidth=9\n");
    REQUIRE(host.save());
    const ini::Map saved = ini::parse(ini::readFile(iniPath));
    CHECK(saved.at({ "Display", "glow" }) == "0");
    CHECK(saved.at({ "Display", "glowIntensity" }) == "1");   // not finite: the default
    CHECK(saved.at({ "Display", "glowWidth" }) == "3");       // UiConfig::GLOW_SCALE_MAX
    host.shutdown();
}
