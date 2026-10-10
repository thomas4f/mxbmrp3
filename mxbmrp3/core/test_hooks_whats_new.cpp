// ============================================================================
// core/test_hooks_whats_new.cpp
// The MXBMRP3_Test_* export that counts the "New" row bands a marker's tab
// draws (whats_new_test), split out of core/test_hooks.cpp, which is at its
// file budget.
//
// Same rules as its parent: the whole file is gated on MXBMRP3_TEST_BUILD and
// mxbmrp3/CMakeLists.txt removes it from every shipping target's source list,
// so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "../hud/settings_hud.h"
#include "../hud/settings/whats_new.h"

extern "C" {

// Open marker `index`'s tab and compare the row bands it drew with the live
// markers on it: 1 when there is one band per live marked row. A band on
// anything else (a cycler's arrows carry the row's id) covered the next cell.
__declspec(dllexport) int MXBMRP3_Test_WhatsNewBandsMatch(int index) {
    if (index < 0 || index >= WhatsNew::MARKER_COUNT) return -1;
    const int tab = WhatsNew::MARKERS[index].tabId;
    SettingsHud& sh = HudManager::getInstance().getSettingsHud();
    sh.show();
    sh.testClickTab(tab);
    sh.update();
    int live = 0;
    for (int i = 0; i < WhatsNew::MARKER_COUNT; ++i) {
        if (WhatsNew::MARKERS[i].tabId == tab && WhatsNew::isLive(WhatsNew::MARKERS[i])) ++live;
    }
    return sh.testWhatsNewBands() == live ? 1 : 0;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
