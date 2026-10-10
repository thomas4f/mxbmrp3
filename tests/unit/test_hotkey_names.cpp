// ============================================================================
// tests/unit/test_hotkey_names.cpp
// The Hotkeys tab lays its bindings out two to a row, which leaves each action
// name a 9-character column (settings_tab_hotkeys.cpp). A longer name would run
// into its own keyboard field, so every name the tab can show is pinned here.
// ============================================================================
#include "doctest.h"
#include "core/hotkey_config.h"

#include <cstring>

TEST_CASE("HotkeysNamesFit: every action name fits the Hotkeys tab's name column") {
    for (int i = 0; i < static_cast<int>(HotkeyAction::COUNT); ++i) {
        const char* name = getActionDisplayName(static_cast<HotkeyAction>(i));
        INFO("action " << i << " = \"" << name << "\"");
        CHECK(std::strlen(name) <= 9);
    }
}
