// ============================================================================
// tests/integration/tests/hidden_input_test.cpp
// A HUD the frame is not drawing takes no input. The hide-all-HUDs hotkey and
// the Widgets toggle hide at DRAW time and leave every HUD's own visibility
// flag alone -- which is what the input pass used to read, so a hidden HUD
// could still be dragged with the right button and its click targets still
// fired (a crash-counter reset, a standings row) for as long as the cursor was
// up: the settings menu open, or the easter egg running. The Direct GL prompt
// had the same gap for the settings menu (gl_render_test.cpp).
//
// HudManager::isHeldBack is now the one answer both passes read; this drives
// the injected mouse through the real frame path and asserts a hidden HUD
// stays put while the same drag moves it once it is back on screen.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {

// The centre of a HUD's panel in UI space, from the fixed-point edges.
bool centerOf(PluginHost& host, const char* name, float* x, float* y) {
    const PluginHost::ScreenEdges e = host.hudScreenEdges(name);
    if (e.r <= e.l || e.b <= e.t) return false;
    *x = static_cast<float>(e.l + e.r) * 0.5e-6f;
    *y = static_cast<float>(e.t + e.b) * 0.5e-6f;
    return true;
}

// Right-drag `name` by a fixed step; true if its panel moved.
bool dragMoves(PluginHost& host, const char* name) {
    float x = 0.0f, y = 0.0f;
    REQUIRE(centerOf(host, name, &x, &y));
    const PluginHost::ScreenEdges before = host.hudScreenEdges(name);
    host.dragAt(x, y, x + 0.15f, y + 0.10f);
    const PluginHost::ScreenEdges after = host.hudScreenEdges(name);
    return after.l != before.l || after.t != before.t;
}

}  // namespace

TEST_CASE("hidden input: the hide-all hotkey takes the HUDs out of the input pass too") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hidden_input\\");
    if (!host.hasInjectedMouse() || !host.hasScreenEdges()) { MESSAGE("build without the hooks"); return; }
    // Only the HUD under test on screen: at the default layout a HUD that is
    // on top of another takes the drag, and the search is topmost-first.
    const char* standings = PluginHost::hudName(PluginHost::HUD_STANDINGS);
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisible(standings, true));
    host.draw();
    REQUIRE_MESSAGE(dragMoves(host, standings), "the stand-in mouse cannot drag at all");

    host.setHudsEnabled(false);                        // the hotkey: everything off screen
    CHECK_MESSAGE(!dragMoves(host, standings), "a hidden HUD followed the mouse");
    host.setHudsEnabled(true);
    CHECK(dragMoves(host, standings));
    host.injectMouse(false);
}

TEST_CASE("hidden input: the Widgets toggle holds back the widgets and only the widgets") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hidden_input\\");
    if (!host.hasInjectedMouse() || !host.hasScreenEdges()) { MESSAGE("build without the hooks"); return; }
    const char* speed = PluginHost::hudName(PluginHost::HUD_SPEED);
    const char* standings = PluginHost::hudName(PluginHost::HUD_STANDINGS);
    host.setEveryHudVisible(false);                    // see above: two HUDs that do not overlap
    REQUIRE(host.setHudVisible(speed, true));
    REQUIRE(host.setHudVisible(standings, true));
    host.draw();
    REQUIRE(dragMoves(host, speed));
    REQUIRE(dragMoves(host, standings));

    host.setWidgetsEnabled(false);
    CHECK_MESSAGE(!dragMoves(host, speed), "a hidden widget followed the mouse");
    CHECK_MESSAGE(dragMoves(host, standings), "a full HUD was held back by the widgets toggle");
    host.setWidgetsEnabled(true);
    CHECK(dragMoves(host, speed));
    host.injectMouse(false);
}
