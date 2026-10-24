// ============================================================================
// core/test_hooks_dash.cpp
// The MXBMRP3_Test_* exports for the Dash (dash_test).
//
// SPLIT OUT OF core/test_hooks.cpp, which is at its file budget.
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "../hud/dash_hud.h"

extern "C" {

// The Dash's page up right now (DashHud::Page; a notice takeover's
// Notices included). The pages carry no title, so a test reads which one is up
// here rather than from the drawn strings.
__declspec(dllexport) int MXBMRP3_Test_DashPage() {
    return static_cast<int>(HudManager::getInstance().getDashHud().getShownPage());
}

// Where the Dash's strip icons start among its quads (DashHud::getFirstIconQuad),
// so a test finds them past a themed panel's quads and the page's.
__declspec(dllexport) int MXBMRP3_Test_DashFirstIcon() {
    return HudManager::getInstance().getDashHud().getFirstIconQuad();
}

// Points on the Dash's Delta Trace page at its last draw, as
// MXBMRP3_Test_DeltaTracePoints counts the Delta Trace HUD's.
__declspec(dllexport) int MXBMRP3_Test_DashDeltaPoints() {
    return HudManager::getInstance().getDashHud().getDeltaPlot().plottedPoints();
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
