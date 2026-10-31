// ============================================================================
// core/test_hooks_find.cpp
// The MXBMRP3_Test_* export for the settings Find box (settings_find_test,
// companion_demo's "find" mode): show the results for a query as if it had been
// typed and Enter pressed, and read the hits back.
//
// Same rules as core/test_hooks.cpp: the whole file is gated on
// MXBMRP3_TEST_BUILD and mxbmrp3/CMakeLists.txt removes it from every shipping
// target's source list, so these exports cannot exist in a released DLL.
// ============================================================================
#include "../game/game_config.h"

#if defined(MXBMRP3_TEST_BUILD)

#include "hud_manager.h"
#include "../hud/settings_hud.h"

#include <cstdio>
#include <string>

extern "C" {

// Open the Find page on `query` (empty: close it) and draw it; the hits come back
// as "Tab|label|value|tooltipId|section" lines, name hits first, into `out`. Returns the
// hit count, or -1 when the index is empty.
__declspec(dllexport) int MXBMRP3_Test_SettingsFind(const char* query, char* out, int cap) {
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    settings.testFindQuery(query ? query : "");
    const SettingsFind::State& f = settings.testFind();
    std::string joined;
    for (int i : f.hits) {
        const SettingsFind::Entry& e = f.index[static_cast<size_t>(i)];
        joined += settings.testTabNameForIndex(e.tab);
        joined += '|'; joined += e.label;
        joined += '|'; joined += e.value;
        joined += '|'; joined += e.tooltipId;
        joined += '|'; joined += e.section;
        joined += '\n';
    }
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", joined.c_str());
    return f.index.empty() ? -1 : static_cast<int>(f.hits.size());
}

// The row a Find result opened, if it is still lit and the built tab carries it:
// its tooltip id into `out`, 1; else 0.
__declspec(dllexport) int MXBMRP3_Test_SettingsFindLanding(char* out, int cap) {
    SettingsHud& settings = HudManager::getInstance().getSettingsHud();
    const std::string& id = settings.testFind().landTooltipId;
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", id.c_str());
    return !id.empty() && settings.testHasRegionWithTooltip(id.c_str()) ? 1 : 0;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
