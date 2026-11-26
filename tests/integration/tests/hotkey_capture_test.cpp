// ============================================================================
// tests/integration/tests/hotkey_capture_test.cpp
// The hotkey-capture lockout: a capture must never outlive the settings menu.
//
// THE BUG THIS PINS. Clicking a bind row on the Hotkeys tab arms a capture, and
// an armed capture deliberately swallows the whole keyboard -- HotkeyManager
// routes update() into updateCapture() instead of checkTriggeredActions(), and
// HudManager::processHotkeys returns early -- because ANY key must be bindable,
// including the ones already bound. The only cancel was the ESC handled inside
// SettingsHud::handleInput, which needs the panel open.
//
// So arming a capture and then closing the panel with the button, rather than
// completing or cancelling it, killed every hotkey in the plugin. Including
// TOGGLE_SETTINGS, so the menu could not be reopened to undo it. A streamer hit
// exactly this on air, hand-edited the ini looking for a way back, and
// restarted the game before it worked again: "literally pressing every single
// button on my keyboard, it's not going to come up."
//
// Not reachable by clicking here -- the bind rows build their click regions from
// a live layout -- so the capture is armed through a test hook and the close is
// the real SettingsHud::hide(). That is the seam the fix is on.
//
// ALSO PINNED HERE: the plugin reads no keys while the game isn't focused, and
// a key held while focus returns never fires a hotkey.
//   1. Unfocused frames make no key read at all: not the hotkeys, not the
//      mouse buttons, not the settings key's modifier check.
//   2. Focused frames read keys again.
//   3. A bound key held down while the player comes back (Alt+Tab back with
//      it still down) does nothing; releasing it and pressing it again fires
//      it as usual.
// HotkeyManager used to keep reading the bound keys in the background only to
// remember "was it down", so (3) could not misfire. One fresh baseline on the
// first focused frame (HotkeyManager::update) does that now, and every key
// read goes through InputManager::isKeyDown, which reads nothing unfocused;
// check_banned_constructs.sh keeps it the only key read in the plugin.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <windows.h>

// HotkeyAction::TOGGLE_STANDINGS — any action does; the lockout is not per-row.
static constexpr int ACTION_TOGGLE_STANDINGS = 0;

TEST_CASE("hotkeys: closing the settings menu disarms an in-progress capture") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hotkey_capture\\");
    REQUIRE(host.hasHotkeyCapture());

    CHECK_FALSE(host.hotkeyCapturing());

    SUBCASE("the close button is the path that used to strand it") {
        host.showSettings(true);
        host.hotkeyStartCapture(ACTION_TOGGLE_STANDINGS);
        CHECK(host.hotkeyCapturing());

        host.showSettings(false);          // SettingsHud::hide(), as the button calls it
        CHECK_FALSE(host.hotkeyCapturing());
    }

    SUBCASE("a capture armed with the menu already closed cannot survive a frame") {
        // The belt to hide()'s braces: whatever route closed the panel, the
        // draw path treats "capturing while the menu is shut" as impossible.
        host.hotkeyStartCapture(ACTION_TOGGLE_STANDINGS);
        CHECK(host.hotkeyCapturing());

        host.eventInit("TestTrack", "Alice");
        host.session(1, 0, 480000);
        host.runInit(1);
        host.runStart();
        host.draw();
        CHECK_FALSE(host.hotkeyCapturing());
    }

    SUBCASE("reopening and completing a capture still works normally") {
        host.showSettings(true);
        host.hotkeyStartCapture(ACTION_TOGGLE_STANDINGS);
        CHECK(host.hotkeyCapturing());
        host.showSettings(false);
        CHECK_FALSE(host.hotkeyCapturing());

        // ...and the seam is reusable: the next open arms cleanly.
        host.showSettings(true);
        host.hotkeyStartCapture(ACTION_TOGGLE_STANDINGS);
        CHECK(host.hotkeyCapturing());
        host.showSettings(false);
        CHECK_FALSE(host.hotkeyCapturing());
    }

    // Switching tabs disarms it too: a chat channel field left capturing on a
    // tab no longer shown swallowed every key and committed on Enter.
    SUBCASE("switching settings tabs disarms an in-progress capture") {
        host.showSettings(true);
        host.hotkeyStartCapture(ACTION_TOGGLE_STANDINGS);
        REQUIRE(host.hotkeyCapturing());
        REQUIRE(host.openSettingsTab("General"));   // handleTabClick, the sidebar's path
        CHECK_FALSE(host.hotkeyCapturing());
        host.showSettings(false);
    }

    host.shutdown();
}

// The Hotkeys tab sets two bindings to a row, which left no room for the clear
// button that used to follow each field: a RIGHT-click on a field clears it
// (SettingsHud::handleRightClick), while a left-click still arms a capture.
static constexpr int ACTION_TOGGLE_SETTINGS = 15;   // HotkeyAction::TOGGLE_SETTINGS, bound by default

TEST_CASE("hotkeys: a right-click on a binding field clears it, a left-click still rebinds") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\hotkey_clear\\");
    host.showSettings(true);
    host.setActiveTab("Hotkeys");
    host.draw();
    REQUIRE(host.hotkeyHasKeyboard(ACTION_TOGGLE_SETTINGS));

    // The cell's centre falls on its keyboard field (name 10, field 11, of 29).
    float x = 0.0f, y = 0.0f;
    REQUIRE(host.settingsRegionCenter("hotkeys.settings", &x, &y));
    host.clickAt(x, y);
    CHECK(host.hotkeyCapturing());
    host.showSettings(false);            // disarms (the case above)
    host.showSettings(true);
    host.draw();

    host.rightClickAt(x, y);
    CHECK_FALSE(host.hotkeyCapturing());
    CHECK_FALSE(host.hotkeyHasKeyboard(ACTION_TOGGLE_SETTINGS));
}

namespace {
constexpr int VK_F10_KEY = 0x79;
}

TEST_CASE("hotkeys: no key read while the game isn't focused, and no hotkey from a key held through refocus") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasKeyHooks());
    REQUIRE(host.hasSetSettingsKey());
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests", nullptr);
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\keyreads", nullptr);
    host.startup("Z:\\tmp\\mxbmrp3-tests\\keyreads\\");
    host.injectKeys(true);
    host.setSettingsKey(VK_F10_KEY, 0);   // through HotkeyManager, not the ` fallback

    // A focused frame with the key up first, so the baseline that matters is
    // the one taken when focus RETURNS, not the one every startup takes.
    host.forceFocus(1);
    host.draw();
    host.draw();
    REQUIRE_FALSE(host.settingsVisible());

    // 1. Unfocused: nothing is read, even with the key held.
    host.forceFocus(0);
    host.holdKey(VK_F10_KEY, true);
    host.draw();
    host.keyReads(true);
    for (int i = 0; i < 5; ++i) host.draw();
    CHECK(host.keyReads(false) == 0);
    CHECK_FALSE(host.settingsVisible());

    // 2 + 3. Focus returns with the key still down: reads resume, nothing fires.
    host.forceFocus(1);
    host.keyReads(true);
    host.draw();
    host.draw();
    CHECK(host.keyReads(false) > 0);
    CHECK_FALSE(host.settingsVisible());

    // A real press after that opens the menu, as usual.
    host.holdKey(VK_F10_KEY, false);
    host.draw();
    host.holdKey(VK_F10_KEY, true);
    host.draw();
    host.draw();   // processed on the frame after the press
    CHECK(host.settingsVisible());

    // 1 again, where reads would otherwise happen every frame: with the menu
    // open and a capture armed (its Esc check), and with the settings key on
    // ` (its Ctrl/Shift/Alt check). Neither reads a key while unfocused.
    host.holdKey(VK_F10_KEY, false);
    host.draw();
    host.hotkeyStartCapture(ACTION_TOGGLE_STANDINGS);
    REQUIRE(host.hotkeyCapturing());
    host.forceFocus(0);
    host.draw();
    host.keyReads(true);
    for (int i = 0; i < 3; ++i) host.draw();
    CHECK(host.keyReads(false) == 0);
    host.showSettings(false);   // disarms the capture
    host.setSettingsKey(0xC0 /*VK_OEM_3*/, 0);
    host.draw();
    host.keyReads(true);
    for (int i = 0; i < 3; ++i) host.draw();
    CHECK(host.keyReads(false) == 0);

    host.injectKeys(false);
    host.forceFocus(-1);
    host.shutdown();
}

// Another program in front with the cursor over the companion window: the
// mouse stays usable there (companion_window.h), but the keys belong to the
// program in front, so none is read and no hotkey fires. This branch used to
// enable the keyboard too, so typing in a chat app with the mouse resting on
// the companion could open the settings menu.
TEST_CASE("hotkeys: no key read with the cursor over the companion while another program is in front") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasKeyHooks());
    REQUIRE(host.hasSetSettingsKey());
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests", nullptr);
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\keyreads-companion", nullptr);
    host.startup("Z:\\tmp\\mxbmrp3-tests\\keyreads-companion\\");
    host.injectKeys(true);
    host.setSettingsKey(VK_F10_KEY, 0);

    host.forceFocus(1);
    host.draw();
    host.draw();
    REQUIRE_FALSE(host.settingsVisible());

    // A press, release and press with the cursor over the companion: never read.
    host.forceFocus(2);
    host.draw();
    host.keyReads(true);
    host.holdKey(VK_F10_KEY, true);
    host.draw();
    host.draw();
    host.holdKey(VK_F10_KEY, false);
    host.draw();
    host.holdKey(VK_F10_KEY, true);
    host.draw();
    host.draw();
    CHECK(host.keyReads(false) == 0);
    CHECK_FALSE(host.settingsVisible());

    // The game back in front with the key still down: reads resume, nothing
    // fires until a fresh press.
    host.forceFocus(1);
    host.keyReads(true);
    host.draw();
    host.draw();
    CHECK(host.keyReads(false) > 0);
    CHECK_FALSE(host.settingsVisible());
    host.holdKey(VK_F10_KEY, false);
    host.draw();
    host.holdKey(VK_F10_KEY, true);
    host.draw();
    host.draw();
    CHECK(host.settingsVisible());

    host.injectKeys(false);
    host.forceFocus(-1);
    host.shutdown();
}
