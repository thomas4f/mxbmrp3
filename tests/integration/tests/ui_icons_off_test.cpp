// ============================================================================
// tests/integration/tests/ui_icons_off_test.cpp
// UI ICONS OFF MEANS TEXT, ON THE MENU'S OWN BUTTON TOO.
//
// Appearance > UI icons switches every UI icon to its text stand-in. The
// settings button once lost its stand-in and drew its menu/close glyph
// whatever the setting said, the one icon left on screen with UI icons off.
// That button is now the Toggles strip's Settings icon, so the strip is pinned
// on the strings it draws: "=" closed and "x" open on its first row with icons
// off, and no "=" with them on (the glyph is a quad).
//
// AN ICON MISSING FROM THE ICONS FOLDER IS TEXT TOO: a plugin updated without
// its data folder drew a filled square per missing glyph on the Dash's strip
// (sprite 0 is the solid colour) and an empty row on this one. With UI icons on,
// the strip's switches whose icons are not there draw the same two letters as
// with them off. The harness ships no icons, so the case stages the Settings
// pair alone and every other row is a missing one.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::vector<std::string> stripTexts(PluginHost& host, bool settingsOpen) {
    host.showSettings(settingsOpen);
    // The strip shows with the cursor; away from it, so no hover name is drawn.
    host.injectMouse(true, 0.45f, 0.5f, 0); host.draw();
    host.injectMouse(true, 0.5f, 0.5f, 0); host.draw();
    host.draw();
    std::vector<std::string> texts;
    for (const auto& r : host.hudStringRows("toggle_strip_widget")) texts.push_back(r.text);
    return texts;
}

}  // namespace

TEST_CASE("UI icons off: the strip's Settings icon draws = / x instead of its glyph") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\ui_icons_off\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // The Settings pair in a discovery tree of this case's own, for the length of
    // Startup (dash_test's startDash says why not the save dir).
    {
        const std::filesystem::path tree = "Z:\\tmp\\mxbmrp3-tests\\ui_icons_off_tree";
        const std::filesystem::path icons = tree / "plugins" / "mxbmrp3_data" / "icons";
        std::filesystem::create_directories(icons);
        for (const char* icon : { "hud-menu", "hud-close" }) {
            const std::string src = std::string(MXB_REPO_DATA_DIR "/icons/") + icon + ".tga";
            REQUIRE(CopyFileA(src.c_str(), (icons / (std::string(icon) + ".tga")).string().c_str(), FALSE) != 0);
        }
        const std::filesystem::path prev = std::filesystem::current_path();
        std::filesystem::current_path(tree);
        REQUIRE(host.startup(saveWin) >= 0);
        std::filesystem::current_path(prev);   // integration_main's sentinel is CWD-relative
    }
    REQUIRE(host.hasStringRows());
    REQUIRE_MESSAGE(host.hasInjectedMouse(), "build without the hooks");

    // Icons on (the default): the Settings icon is a quad, so no "=" -- and each
    // switch whose icon is missing draws its two letters instead of nothing.
    const auto on = stripTexts(host, false);
    CHECK(std::find(on.begin(), on.end(), "=") == on.end());
    CHECK(on.size() > 1);
    for (const std::string& t : on) CHECK(t.size() == 2);

    host.writeSettingsFile(saveWin, "[Settings]\nversion=10\n\n[Display]\ntitleIcons=0\n");
    host.loadSettings(saveWin);

    const auto open = stripTexts(host, true);
    REQUIRE(open.size() > 1);
    CHECK(open[0] == "x");

    const auto closed = stripTexts(host, false);
    REQUIRE(closed.size() > 1);
    CHECK(closed[0] == "=");
    host.injectMouse(false);
}
