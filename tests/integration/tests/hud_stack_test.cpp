// ============================================================================
// tests/integration/tests/hud_stack_test.cpp
// The HUD stacking order (core/hud_manager_stack.cpp): dragging a HUD puts it
// on top of every other HUD, the settings menu and the pointer always stay
// above it, the drag pick follows the same order (the HUD on top is the one a
// click lands on), and the order is saved with the profile.
//
// Driven through the real frame path with the injected mouse, the same way
// hidden_input_test.cpp drags.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {

bool centerOf(PluginHost& host, const char* name, float* x, float* y) {
    const PluginHost::ScreenEdges e = host.hudScreenEdges(name);
    if (e.r <= e.l || e.b <= e.t) return false;
    *x = static_cast<float>(e.l + e.r) * 0.5e-6f;
    *y = static_cast<float>(e.t + e.b) * 0.5e-6f;
    return true;
}

// Right-drag a HUD by a small step from its centre.
void nudge(PluginHost& host, const char* name) {
    float x = 0.0f, y = 0.0f;
    REQUIRE(centerOf(host, name, &x, &y));
    host.dragAt(x, y, x + 0.01f, y + 0.01f);
}

}  // namespace

TEST_CASE("hud stack: the last HUD dragged goes on top, under the menu, and is saved") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\hud_stack\\";
    {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasScreenEdges() && host.hasHudStack()),
                    "build without the hooks");
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisible("standings_hud", true));
    REQUIRE(host.setHudVisible("lap_log_hud", true));
    // Far apart, so each drag lands on the HUD it aims at.
    REQUIRE(host.setHudOffset("standings_hud", 0.0f, 0.0f));
    REQUIRE(host.setHudOffset("lap_log_hud", 0.5f, 0.5f));
    host.draw();
    host.draw();

    // Never dragged: registration order, standings under the lap log.
    REQUIRE(host.hudDrawPosition("standings_hud") >= 0);
    CHECK(host.hudDrawPosition("standings_hud") < host.hudDrawPosition("lap_log_hud"));
    CHECK(host.hudStackRank("standings_hud") == 0);

    nudge(host, "standings_hud");
    CHECK(host.hudDrawPosition("standings_hud") > host.hudDrawPosition("lap_log_hud"));
    CHECK(host.hudStackRank("standings_hud") == 1);
    // Nothing a user drags covers the menu, the Toggles strip or the pointer.
    CHECK(host.hudDrawPosition("settings_hud") > host.hudDrawPosition("standings_hud"));
    CHECK(host.hudDrawPosition("toggle_strip_widget") > host.hudDrawPosition("standings_hud"));
    CHECK(host.hudDrawPosition("pointer_widget") > host.hudDrawPosition("settings_hud"));

    // The drag pick follows the order: stacked exactly on the lap log, the
    // standings (now on top) is the one a drag at their shared centre moves.
    REQUIRE(host.setHudOffset("lap_log_hud", 0.0f, 0.0f));
    host.draw();
    const PluginHost::ScreenEdges lapBefore = host.hudScreenEdges("lap_log_hud");
    const PluginHost::ScreenEdges stBefore = host.hudScreenEdges("standings_hud");
    nudge(host, "standings_hud");
    CHECK(host.hudScreenEdges("lap_log_hud").l == lapBefore.l);
    CHECK(host.hudScreenEdges("standings_hud").l != stBefore.l);

    // Saved with the profile.
    REQUIRE(host.save());
    host.injectMouse(false);
    host.shutdown();
    }

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.draw();
    host.draw();
    CHECK(host.hudStackRank("standings_hud") == 1);
    CHECK(host.hudStackRank("lap_log_hud") == 0);
    CHECK(host.hudDrawPosition("standings_hud") > host.hudDrawPosition("lap_log_hud"));

    // Dragging the other one swaps them; the ranks stay small (renumbered 1..k).
    REQUIRE(host.setHudOffset("lap_log_hud", 0.5f, 0.5f));
    host.draw();
    nudge(host, "lap_log_hud");
    CHECK(host.hudDrawPosition("lap_log_hud") > host.hudDrawPosition("standings_hud"));
    CHECK(host.hudStackRank("standings_hud") == 1);
    CHECK(host.hudStackRank("lap_log_hud") == 2);
    host.injectMouse(false);
    host.shutdown();
}

TEST_CASE("hud stack: a drag on the companion window raises the HUD too") {
    // Same pick loop, other surface: the HUD dragged there goes on top, and
    // only its companion position moves.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hud_stack_companion\\");
    REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasScreenEdges() && host.hasHudStack()),
                    "build without the hooks");
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisible("standings_hud", true));
    REQUIRE(host.setHudVisible("lap_log_hud", true));
    REQUIRE(host.setHudOffset("standings_hud", 0.0f, 0.0f));
    REQUIRE(host.setHudOffset("lap_log_hud", 0.5f, 0.5f));
    host.companionWindow(true);   // opens with the game layout
    host.draw();
    host.draw();
    float x = 0.0f, y = 0.0f;
    REQUIRE(centerOf(host, "standings_hud", &x, &y));
    const PluginHost::ScreenEdges gameBefore = host.hudScreenEdges("standings_hud");
    const PluginHost::FrameStats framesBefore = host.surfaceFrameStats();
    REQUIRE(framesBefore.companionQuads > 0);   // the companion frame is built

    host.forceActiveSurface(1 /*Companion*/);
    host.dragAt(x, y, x + 0.01f, y + 0.01f);
    host.forceActiveSurface(0 /*Game*/);
    host.draw();
    CHECK(host.hudStackRank("standings_hud") == 1);
    CHECK(host.hudDrawPosition("standings_hud") > host.hudDrawPosition("lap_log_hud"));
    CHECK(host.hudScreenEdges("standings_hud").l == gameBefore.l);   // game place kept
    // ...and the companion's moved: its frame shifted right with the drag, the
    // game's did not.
    const PluginHost::FrameStats framesAfter = host.surfaceFrameStats();
    REQUIRE(framesAfter.companionQuads == framesBefore.companionQuads);
    CHECK(framesAfter.companionSumX > framesBefore.companionSumX + 0.005);
    CHECK(framesAfter.gameSumX == doctest::Approx(framesBefore.gameSumX));
    host.injectMouse(false);
    host.companionWindow(false);
    host.shutdown();
}
