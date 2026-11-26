// ============================================================================
// core/test_hooks_settings_columns.cpp
// The MXBMRP3_Test_* export for the settings panel's column runs: how many
// beginColumns..endColumns runs emitted a row count other than the one they
// declared (settings_layout_test).
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so this export cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "../hud/settings/settings_layout.h"

extern "C" {

// Miscounted runs since the last call; the call clears it. See
// SettingsLayoutContext::beginColumns.
__declspec(dllexport) int MXBMRP3_Test_SettingsColumnMiscounts() {
    const int n = SettingsLayoutContext::s_testColumnMiscounts;
    SettingsLayoutContext::s_testColumnMiscounts = 0;
    return n;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
