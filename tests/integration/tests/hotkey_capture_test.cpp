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
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

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

    host.shutdown();
}
