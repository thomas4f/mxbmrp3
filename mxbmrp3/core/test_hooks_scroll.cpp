// ============================================================================
// core/test_hooks_scroll.cpp
// The MXBMRP3_Test_* exports for the settings panel's scrolling lists
// (hud/settings/scroll_view.h): move a list, and read where it stands
// and what it last laid out (settings_scroll_test, achievements_test,
// prestige_test).
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "../hud/settings_hud.h"
#include "mouse_wheel.h"

#include <windows.h>

#include <cstdio>

extern "C" {

// Move list `id` (ScrollView::Id) `steps` steps (ScrollView::step; positive =
// down the list), as the track's shortest move would, and rebuild.
__declspec(dllexport) void MXBMRP3_Test_StepScroll(int id, int steps) {
    if (id < 0 || id >= ScrollView::COUNT) return;
    HudManager::getInstance().getSettingsHud().testStepScroll(id, steps);
}

// List `id` (ScrollView::Id): out[0..9] = shown, target, maxRest, contentLines,
// viewLines, the view's x, y, w, h (build space) and the thumb's height (0 =
// no bar). Returns 1 when the last real build laid it out. groupOut, when
// given, receives the achievements list's top group.
__declspec(dllexport) int MXBMRP3_Test_SettingsScroll(int id, float* out, char* groupOut, int cap) {
    if (id < 0 || id >= ScrollView::COUNT) return 0;
    const SettingsHud& hud = HudManager::getInstance().getSettingsHud();
    const ScrollView::State& s = hud.testScroll(id);
    if (out) {
        const float v[10] = { s.shown, s.target, s.maxRest, s.contentLines, s.viewLines,
                              s.viewX, s.viewY, s.viewW, s.viewH, s.thumbH };
        for (int i = 0; i < 10; ++i) out[i] = v[i];
    }
    if (groupOut && cap > 0) std::snprintf(groupOut, static_cast<size_t>(cap), "%s", hud.testAchievementsTopGroup());
    return s.laidOut ? 1 : 0;
}

// ---- The mouse wheel, on a stand-in game window ----------------------------
// The harness owns the stand-in (its procedure must outlive this DLL, so an
// unload can be watched from outside it); the wheel is pointed at it here.

// Point the wheel at `hwnd` (an HWND) in place of the game window.
__declspec(dllexport) void MXBMRP3_Test_WheelWindow(void* hwnd) {
    MouseWheel::getInstance().testSetWindow(static_cast<HWND>(hwnd));
}

// bit 0: attached to the stand-in; bit 1: the module would be pinned
// (core/mouse_wheel.h; the test build records the pin without taking it).
__declspec(dllexport) int MXBMRP3_Test_WheelState() {
    const MouseWheel& w = MouseWheel::getInstance();
    return (w.testAttached() ? 1 : 0) | (w.testPinned() ? 2 : 0);
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
