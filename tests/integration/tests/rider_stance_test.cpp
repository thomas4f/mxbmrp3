// ============================================================================
// tests/integration/tests/rider_stance_test.cpp
// The rider sit/stand state, read off the real RunTelemetry export.
//
// MX Bikes Beta 21f appends one int to the bike telemetry, m_iRiderStanding:
// 1 = standing, 0 = seated. PiBoSo publishes no header with it, and players on
// older builds still send the plain struct, so the plugin has to handle both
// at once. Three contracts, read through the
// MXBMRP3_Test_BikeTelemetry hook (telemetry is not in /api/state):
//   1. an older build (188 bytes) leaves the stance UNKNOWN (-1), even right
//      after a 21f frame said "standing" - a stale 1 would be a lie;
//   2. the 21f 192-byte frame lands as seated/standing;
//   3. spectating (RaceVehicleData, which has no stance) shows it as unknown,
//      so a caster never sees the player's own stance on another rider. The
//      telemetry clear when the camera leaves the player is what does it;
//      this pins that the stance is part of what gets cleared.
// test_rider_stance.cpp (unit) pins the byte-level reader; recorder_test pins
// the tape carrying the int.
//
// The Stance widget (hud/stance_widget.h) reads it: nothing at all until the
// game has sent a stance (older builds never do), then two icons in the
// Gear widget's box, the lit one following the stance; the Widgets tab offers its
// row only from then on. Its INI-only mode and icon (flip / standing, by name) are
// pinned by the last case.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"
#include <cmath>
#include <cstdio>
#include <string>

TEST_CASE("RunTelemetry: rider stance is read only when the game sends it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\rider-stance\\");
    host.eventInit("TestTrack", "Alice");

    // (1) An older build: the plain struct. Telemetry is live, stance unknown.
    host.telemetry(12.0f);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.valid);
        CHECK(t.riderStance == -1);
    }

    // (2) Beta 21f: seated, then standing.
    host.telemetryWithStance(0);
    CHECK(host.bikeTelemetry().riderStance == 0);
    host.telemetryWithStance(1);
    CHECK(host.bikeTelemetry().riderStance == 1);

    // A value outside the two measured ones is unknown, not "standing".
    host.telemetryWithStance(2);
    CHECK(host.bikeTelemetry().riderStance == -1);

    // (1 again) Back to a frame without the int: the last "standing" must not
    // linger - the field resets to unknown on every frame that lacks it.
    host.telemetryWithStance(1);
    host.telemetry(12.0f);
    CHECK(host.bikeTelemetry().riderStance == -1);

    host.shutdown();
}

TEST_CASE("RaceVehicleData: another rider's stance is unknown") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\rider-stance-spectate\\");
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(6, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");

    // The player's own frame said standing...
    host.telemetryWithStance(1);
    REQUIRE(host.bikeTelemetry().riderStance == 1);

    // ...then the camera goes to #22, whose frames come from RaceVehicleData.
    // That struct has no stance, so the display must not keep the player's.
    host.spectateVehicles({ { 10, "Alice" }, { 22, "Bob" } }, /*curSelection=*/1);
    host.raceVehicleData(22, /*speedMs=*/20.0f, /*gear=*/3, /*rpm=*/8000,
                         /*throttle=*/0.5f, /*frontBrake=*/0.0f, /*leanDeg=*/0.0f);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.rpm == 8000);           // the frame did land
        CHECK(t.riderStance == -1);
    }

    host.shutdown();
}

namespace {
const char* const kStance = "stance_widget";

// Copy the shipped icons the widget names into the user-asset folder the plugin
// syncs at Startup (the discovery path twitch_chat_test explains).
void stageStanceIcons(const char* saveWin) {
    const std::string save = saveWin;
    CreateDirectoryA(save.c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3").c_str(), nullptr);
    CreateDirectoryA((save + "mxbmrp3\\icons").c_str(), nullptr);
    for (const char* icon : { "play", "caret-up" }) {
        char src[256], dst[256];
        snprintf(src, sizeof(src), MXB_REPO_DATA_DIR "/icons/%s.tga", icon);
        snprintf(dst, sizeof(dst), "%smxbmrp3\\icons\\%s.tga", saveWin, icon);
        REQUIRE_MESSAGE(CopyFileA(src, dst, FALSE) != 0, "shipped icon missing: " << std::string(src));
    }
}

void stanceSetUp(PluginHost& host, const char* saveWin, const char* extra = "") {
    REQUIRE(host.loaded());
    stageStanceIcons(saveWin);
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasQuadRects());
    REQUIRE(host.hasInkHooks());
    const std::string ini = std::string("[Settings]\nversion=10\n\n[StanceWidget]\nvisible=1\nbackgroundOpacity=1\n") + extra;
    host.writeSettingsFile(saveWin, ini.c_str());
    host.loadSettings(saveWin);
    host.eventInit("StanceTrack", "Player");
    host.runInit(1);
}

// The pair's two icons are the widget's last two quads, up then down.
struct Pair { PluginHost::QuadRect up, down; unsigned long upColor = 0, downColor = 0; };
Pair pair(PluginHost& host) {
    Pair p;
    const auto q = host.hudQuadRects(kStance);
    REQUIRE(q.size() >= 2);
    const int n = static_cast<int>(q.size());
    p.up = q[n - 2];
    p.down = q[n - 1];
    p.upColor = host.quadColor(kStance, n - 2);
    p.downColor = host.quadColor(kStance, n - 1);
    return p;
}

bool rowOffered(PluginHost& host) {
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Widgets"));
    host.stepScroll(4, 50);   // ScrollView::WIDGETS, to the bottom: Movement is last
    host.draw();
    float x = 0.0f, y = 0.0f;
    const bool offered = host.settingsRegionCenter("widgets.stance", &x, &y);
    host.showSettings(false);
    host.draw();
    return offered;
}
}  // namespace

TEST_CASE("Stance widget: nothing on an older build, then the lit icon follows the stance") {
    PluginHost host(dllPath());
    stanceSetUp(host, "Z:\\tmp\\mxbmrp3-tests\\stance-widget\\");

    // An older build: switched on, yet it draws nothing and the tab hides it.
    host.telemetry(12.0f);
    host.draw();
    CHECK(host.hudQuadRects(kStance).empty());
    CHECK_FALSE(rowOffered(host));

    // Beta 21f, standing: up lit, down unlit.
    host.telemetryWithStance(1);
    host.draw();
    const Pair standing = pair(host);
    CHECK(standing.upColor != standing.downColor);
    CHECK(rowOffered(host));

    // The pair sits in the Gear widget's box, centred on its card, up above down.
    // The card rect is pre-offset; the panel's background (drawn: the setup
    // turns it on), the first quad, is the same panel in the drawn space.
    PluginHost::QuadRect card{}, panel{}, gearCard{};
    REQUIRE(host.hudCardRect(PluginHost::HUD_STANCE, &card, &panel));
    REQUIRE(host.hudCardRect(PluginHost::HUD_GEAR, &gearCard));
    CHECK((card.r - card.l) == doctest::Approx(gearCard.r - gearCard.l).epsilon(1e-4));
    CHECK((card.b - card.t) == doctest::Approx(gearCard.b - gearCard.t).epsilon(1e-4));
    CHECK(standing.up.b < standing.down.t);
    const PluginHost::QuadRect drawnPanel = host.hudQuadRects(kStance).front();
    const double dx = drawnPanel.l - panel.l, dy = drawnPanel.t - panel.t;
    const double cx = (card.l + card.r) / 2.0 + dx, cy = (card.t + card.b) / 2.0 + dy;
    CHECK((standing.up.l + standing.up.r) / 2.0 == doctest::Approx(cx).epsilon(1e-4));
    CHECK((standing.down.l + standing.down.r) / 2.0 == doctest::Approx(cx).epsilon(1e-4));
    CHECK((standing.up.t + standing.down.b) / 2.0 == doctest::Approx(cy).epsilon(1e-3));

    // Lit is the palette's ACCENT and unlit its MUTED, read from the palette
    // itself (ColorSlot::ACCENT = 9, MUTED = 3) rather than from a neighbour.
    constexpr int ACCENT = 9, MUTED = 3;
    const unsigned long accent = host.effectiveColor(ACCENT);
    const unsigned long muted = host.effectiveColor(MUTED);
    REQUIRE(accent != muted);
    CHECK(standing.upColor == accent);
    CHECK(standing.downColor == muted);

    // Seated: the colours swap.
    host.telemetryWithStance(0);
    host.draw();
    const Pair seated = pair(host);
    CHECK(seated.upColor == muted);
    CHECK(seated.downColor == accent);

    // Rebuilt when the stance changes, not on every frame. The rebuild counter
    // counts only while the profiler runs, which the Benchmark widget starts.
    host.bmSetVisible(true);
    host.draw();
    const int rebuilds = host.hudRebuildCount(kStance);
    REQUIRE_MESSAGE(rebuilds >= 0, "MXBMRP3_Test_HudRebuildCount absent or panel unregistered");
    for (int i = 0; i < 20; ++i) {
        host.telemetryWithStance(0);
        host.draw();
    }
    CHECK(host.hudRebuildCount(kStance) == rebuilds);
    host.telemetryWithStance(1);
    host.draw();
    CHECK(host.hudRebuildCount(kStance) == rebuilds + 1);
    host.bmSetVisible(false);

    // Nothing to read (a frame without it, the next session): both unlit, and
    // the widget and its row stay - the game has shown it sends one.
    host.telemetry(12.0f);
    host.draw();
    const Pair unknown = pair(host);
    CHECK(unknown.upColor == muted);
    CHECK(unknown.downColor == muted);
    host.runDeinit();
    host.eventDeinit();
    host.eventInit("StanceTrack", "Player");
    host.draw();
    CHECK(host.hudQuadRects(kStance).size() >= 2);
    CHECK(rowOffered(host));

    host.shutdown();
}

namespace {
// The sprite drawn upright (vertex 0 at the top) or turned over in place (vertex
// 0 at the bottom: a 180-degree turn keeps the winding, a mirror would not).
bool upright(PluginHost& host, int quad) {
    float x0 = 0, y0 = 0, x2 = 0, y2 = 0;
    REQUIRE(host.quadVertex(PluginHost::HUD_STANCE, quad, 0, &x0, &y0));
    REQUIRE(host.quadVertex(PluginHost::HUD_STANCE, quad, 2, &x2, &y2));
    return y0 < y2 && x0 < x2;
}

// A plain frame and a draw first, as the first case starts: without them the
// first stance frame of a fresh host reads back as unknown.
void settle(PluginHost& host) {
    host.telemetry(12.0f);
    host.draw();
}
}  // namespace

TEST_CASE("Stance widget: the INI picks the icon by name and pair, flip or standing-only") {
    // The pair (default) draws the named icon twice: upright, then turned over.
    {
        PluginHost host(dllPath());
        stanceSetUp(host, "Z:\\tmp\\mxbmrp3-tests\\stance-pair\\", "icon=caret-up\n");
        settle(host);
        REQUIRE(host.hasQuadSprite());
        host.telemetryWithStance(1);
        host.draw();
        const int n = static_cast<int>(host.hudQuadRects(kStance).size());
        REQUIRE(n == 3);   // panel + two icons
        const int caret = host.iconSpriteForName("caret-up");
        REQUIRE(caret > 0);
        CHECK(host.quadSprite(PluginHost::HUD_STANCE, 1) == caret);
        CHECK(host.quadSprite(PluginHost::HUD_STANCE, 2) == caret);
        CHECK(upright(host, 1));
        CHECK_FALSE(upright(host, 2));
        host.shutdown();
    }

    // Flip: one icon spanning the pair's whole height, turned over while seated,
    // upright and muted while unknown. An unknown name draws the default, play.
    {
        PluginHost host(dllPath());
        stanceSetUp(host, "Z:\\tmp\\mxbmrp3-tests\\stance-flip\\", "mode=flip\nicon=no-such-icon\n");
        settle(host);
        constexpr int ACCENT = 9, MUTED = 3;
        host.telemetryWithStance(1);
        host.draw();
        auto q = host.hudQuadRects(kStance);
        REQUIRE(q.size() == 2);   // panel + one icon
        CHECK(host.quadSprite(PluginHost::HUD_STANCE, 1) == host.iconSpriteForName("play"));
        CHECK(upright(host, 1));
        CHECK(host.quadColor(kStance, 1) == host.effectiveColor(ACCENT));
        const PluginHost::QuadRect standing = q[1];

        host.telemetryWithStance(0);
        host.draw();
        q = host.hudQuadRects(kStance);
        REQUIRE(q.size() == 2);
        CHECK_FALSE(upright(host, 1));
        CHECK(host.quadColor(kStance, 1) == host.effectiveColor(ACCENT));
        // Turned in place: the same square.
        CHECK(q[1].t == doctest::Approx(standing.t).epsilon(1e-5));
        CHECK(q[1].b == doctest::Approx(standing.b).epsilon(1e-5));

        host.telemetry(12.0f);
        host.draw();
        REQUIRE(host.hudQuadRects(kStance).size() == 2);
        CHECK(upright(host, 1));
        CHECK(host.quadColor(kStance, 1) == host.effectiveColor(MUTED));

        // The icon's square spans what the pair spans: the gear digit's ink.
        host.shutdown();
        PluginHost pairHost(dllPath());
        stanceSetUp(pairHost, "Z:\\tmp\\mxbmrp3-tests\\stance-flip-pair\\");
        settle(pairHost);
        pairHost.telemetryWithStance(1);
        pairHost.draw();
        const Pair p = pair(pairHost);
        CHECK(standing.t == doctest::Approx(p.up.t).epsilon(1e-4));
        CHECK(standing.b == doctest::Approx(p.down.b).epsilon(1e-4));
        pairHost.shutdown();
    }

    // Standing only: the icon while standing, nothing seated or unknown.
    {
        PluginHost host(dllPath());
        stanceSetUp(host, "Z:\\tmp\\mxbmrp3-tests\\stance-standing\\", "mode=standing\n");
        settle(host);
        host.telemetryWithStance(1);
        host.draw();
        REQUIRE(host.hudQuadRects(kStance).size() == 2);
        CHECK(upright(host, 1));
        host.telemetryWithStance(0);
        host.draw();
        CHECK(host.hudQuadRects(kStance).size() == 1);   // the panel alone
        host.telemetry(12.0f);
        host.draw();
        CHECK(host.hudQuadRects(kStance).size() == 1);
        host.shutdown();
    }
}
