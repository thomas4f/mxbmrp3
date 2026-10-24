// ============================================================================
// core/test_hooks_glow.cpp
// The MXBMRP3_Test_* export for the glow: the Appearance switch and the two
// INI-only scales, set live as the settings menu would (glow_test).
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "ui_config.h"

extern "C" {

__declspec(dllexport) void MXBMRP3_Test_SetGlow(int on, float intensity, float width) {
    UiConfig& ui = UiConfig::getInstance();
    ui.setGlow(on != 0);
    ui.setGlowIntensity(intensity);
    ui.setGlowWidth(width);
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
