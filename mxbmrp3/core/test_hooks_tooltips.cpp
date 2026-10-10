// ============================================================================
// core/test_hooks_tooltips.cpp
// The MXBMRP3_Test_* export for settings tooltip coverage: the rows the shared
// row helpers built without hover help (tooltip_coverage_test).
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
#include <vector>

extern "C" {

// The settings rows built since the last call with no tooltip, or an id with no
// text, as "tab: label" lines ('\n'-joined into `out`); the record is cleared.
// Returns how many there were.
__declspec(dllexport) int MXBMRP3_Test_SettingsUntippedRows(char* out, int cap) {
    std::vector<std::string>& rows = HudManager::getInstance().getSettingsHud().testUntippedRows();
    std::string joined;
    for (const std::string& r : rows) { joined += r; joined += '\n'; }
    if (out && cap > 0) std::snprintf(out, static_cast<size_t>(cap), "%s", joined.c_str());
    const int n = static_cast<int>(rows.size());
    rows.clear();
    return n;
}

}  // extern "C"

#endif  // MXBMRP3_TEST_BUILD
