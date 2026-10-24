// ============================================================================
// core/test_hooks_impact_vignette.cpp
// The MXBMRP3_Test_* export for the impact vignette: its strength, as the
// Appearance row sets it (impact_vignette_test).
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "ui_config.h"

extern "C" {

// 0 = Off, up to 1 = 100%.
__declspec(dllexport) void MXBMRP3_Test_SetImpactVignette(float strength) {
    UiConfig::getInstance().setImpactVignette(strength);
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
