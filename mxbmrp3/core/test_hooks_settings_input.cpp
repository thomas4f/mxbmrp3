// ============================================================================
// core/test_hooks_settings_input.cpp
// The MXBMRP3_Test_* exports for the settings panel's heavy steps: which cycle
// arrows on the active tab fire once on release instead of repeating while
// held, and how often the Spotter tab has read its packs and SAPI voices
// (settings_heavy_steps_test).
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "../hud/settings_hud.h"

extern int g_spotterListReads;   // settings_tab_spotter.cpp
extern int g_ttsVoiceReads;      // spotter_manager_audio.cpp

extern "C" {

// CYCLE_UP arrows on the active tab that do NOT repeat while held.
__declspec(dllexport) int MXBMRP3_Test_SettingsOnceCycleCount() {
    return HudManager::getInstance().getSettingsHud().testOnceCycleCount();
}
// Times the Spotter tab has read its lists (the pack scan; the voices are a session list).
__declspec(dllexport) int MXBMRP3_Test_SpotterListReads() {
    return g_spotterListReads;
}
// Times SpotterManager has enumerated the SAPI voices (once per session).
__declspec(dllexport) int MXBMRP3_Test_TtsVoiceReads() {
    return g_ttsVoiceReads;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
