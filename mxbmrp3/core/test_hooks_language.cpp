// ============================================================================
// core/test_hooks_language.cpp
// The MXBMRP3_Test_* exports for a language change while the game runs
// (i18n_test): switch the language the way the Appearance tab's Language row
// does, and read back every name the active settings tab's lists would offer.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "i18n.h"
#include "../hud/settings_hud.h"

#include <cstdio>
#include <string>

extern "C" {

// What the Language row's step does: pick a pack by folder ("" = English).
__declspec(dllexport) void MXBMRP3_Test_SetLanguage(const char* folder) {
    I18n::setLanguage(folder ? folder : "");
}

// Every name of every named list (dropdown) on the active settings tab, one per
// line, as the list would draw them now. Returns the line count.
__declspec(dllexport) int MXBMRP3_Test_SettingsListNames(char* out, int cap) {
    std::string names;
    HudManager::getInstance().getSettingsHud().testListNames(names);
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", names.c_str());
    int lines = 0;
    for (char c : names) lines += c == '\n';
    return lines;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
