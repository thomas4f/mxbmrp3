// ============================================================================
// tests/integration/tests/ui_icons_off_test.cpp
// UI ICONS OFF MEANS TEXT, ON THE SETTINGS BUTTON TOO.
//
// Appearance > UI icons switches every UI icon to its text stand-in. The
// settings button once lost its stand-in and drew its menu/close glyph
// whatever the setting said, the one icon left on screen with UI icons off.
// Pinned on the strings the button draws: "[=]" closed and "[x]" open with
// icons off, and no text at all with them on (the glyph is a quad).
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>
#include <vector>

namespace {

std::vector<std::string> buttonTexts(PluginHost& host, bool settingsOpen) {
    host.showSettings(settingsOpen);
    host.draw();
    host.draw();
    std::vector<std::string> texts;
    for (const auto& r : host.hudStringRows("settings_button")) texts.push_back(r.text);
    return texts;
}

}  // namespace

TEST_CASE("UI icons off: the settings button draws [=] / [x] instead of its glyph") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\ui_icons_off\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.startup(saveWin) >= 0);
    REQUIRE(host.hasStringRows());

    // Icons on (the default): the glyph is a quad, so the button draws no text.
    CHECK(buttonTexts(host, true).empty());

    host.writeSettingsFile(saveWin, "[Settings]\nversion=10\n\n[Display]\ntitleIcons=0\n");
    host.loadSettings(saveWin);

    const auto open = buttonTexts(host, true);
    REQUIRE(open.size() == 1);
    CHECK(open[0] == "[x]");

    const auto closed = buttonTexts(host, false);
    REQUIRE(closed.size() == 1);
    CHECK(closed[0] == "[=]");
}
