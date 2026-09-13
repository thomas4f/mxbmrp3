// ============================================================================
// tests/integration/tests/preview_test.cpp
// POSITIONING PREVIEW: a HUD that draws nothing until something happens fills
// itself with placeholder content while its own settings tab is open, so it can
// be dragged into place.
//
// The rule has two halves and the second is the one worth pinning: a DISABLED
// HUD is never previewed. Opening a tab is how you find its switch, not a
// request to see the thing -- and a preview that ignored the switch would put a
// HUD on screen that the player had deliberately turned off.
//
// Driven through the ACHIEVEMENT TOAST, whose empty state is both real and easy
// to reach: drain whatever the startup earned and no card is up, which is how it
// sits for all but a few seconds of a session. Two other candidates were tried
// and are the wrong instrument, which is worth knowing before reaching for them:
// the RADAR's auto-hide fades its background to transparent rather than dropping
// it, so nothing about its geometry moves; and NOTICES has the default-setup
// warning up for the whole of a harness session, so it is never idle here.
//
// The other six are wired to the same one mechanism (HudManager::setPreviewHud
// off SettingsHud::activeTabHud), so this pins the mechanism, not each of them.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdio>
#include <string>

static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\preview\\";
static const std::string kIniPath =
    "Z:\\tmp\\mxbmrp3-tests\\preview\\mxbmrp3\\mxbmrp3_settings.ini";

static void cleanSaveDir() { std::remove(kIniPath.c_str()); }

TEST_CASE("preview: a HUD draws placeholder content while its tab is open, unless it is off") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.hasWhatsNew());        // openSettingsTab
    REQUIRE(host.hasScreenEdges());     // hudScreenEdges lives behind this hook

    REQUIRE(host.setHudVisible("achievement_widget", true));
    // Drain the cards the startup earned (the default HUD setup moves a row or
    // two), so the widget is in the state it spends a session in: nothing up.
    for (int guard = 0; guard < 16; ++guard) {
        host.draw();
        if (!host.achievementToastShowing()) break;
        const PluginHost::ScreenEdges c = host.hudScreenEdges("achievement_widget");
        host.clickAt(static_cast<float>(c.l + c.r) * 0.5e-6f,
                     static_cast<float>(c.t + c.b) * 0.5e-6f);
        host.injectMouse(false);
        host.showSettings(false);
    }
    REQUIRE_FALSE(host.achievementToastShowing());
    host.draw();

    // WIDTH ON SCREEN, not the quad count: the panel-rect hook reports the planned
    // box whether or not anything was drawn in it, so it reads the same either
    // side. A HUD with nothing to show clears its bounds to zero (setBounds), and
    // a zero-width box is exactly the thing a player cannot grab.
    auto widthOf = [&] { const PluginHost::ScreenEdges e = host.hudScreenEdges("achievement_widget");
                         return e.r - e.l; };
    const int idle = widthOf();
    CHECK(idle == 0);

    // Its tab open: it draws, so there is something on screen to drag.
    host.showSettings(true);
    REQUIRE(host.openSettingsTab("Achievements"));
    host.draw();
    const int previewing = widthOf();
    INFO("idle width " << idle << ", previewing " << previewing);
    CHECK(previewing > 0);

    // ANOTHER tab: the preview is one HUD at a time, and this is not it.
    REQUIRE(host.openSettingsTab("Map"));
    host.draw();
    CHECK(widthOf() == idle);

    // AND THE SWITCH STILL WINS. Back on the Achievements tab with the toast
    // turned off, nothing is drawn: opening the tab is how you reach the
    // checkbox, not a request to see a HUD you have disabled.
    REQUIRE(host.openSettingsTab("Achievements"));
    REQUIRE(host.setHudVisible("achievement_widget", false));
    host.draw();
    CHECK(widthOf() == 0);

    // Switched back on with the tab still open, it previews again.
    REQUIRE(host.setHudVisible("achievement_widget", true));
    host.draw();
    CHECK(widthOf() == previewing);

    host.showSettings(false);
    host.draw();
    CHECK(widthOf() == idle);

    host.runDeinit();
    host.shutdown();
}
