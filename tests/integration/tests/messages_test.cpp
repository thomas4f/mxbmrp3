// ============================================================================
// tests/integration/tests/messages_test.cpp
// System messages end to end (core/system_messages.h): the toasts the
// achievement card shows for things that happen out of sight, and the Version
// widget's startup popups with their Dismiss countdown. Pins:
//
//   1. A hotkey that HIDES a HUD posts "<HUD> hidden" naming the key back and
//      pointing at the HUD's tab; the same key showing it again posts nothing
//      and withdraws the card. (A HUD that appears is its own confirmation.)
//   2. The hide-all hotkey's card is drawn THROUGH hide-all -- the one message
//      it would otherwise hide -- and nothing else is.
//   3. [Messages] enabled=0 drops the toasts and survives a save/load.
//   4. A fresh install opens on the welcome popup; the settings menu opening is
//      its answer, recorded in [Messages] so the next launch is quiet. The
//      shipped ` key has no glyph in the game font, so it is named by place,
//      and the default setup notice under the popup waits until it is gone.
//   5. The countdown counts down, and running out is not an answer: the
//      welcome comes back next launch.
//   6. "Updated to" is told once, as one fixed line pointing at the menu (no
//      per-release text to remember), and the Version widget goes back to the
//      player's own visibility after it. (There is deliberately no crash
//      popup: see SystemMessages::onStartup.)
//   7. A tab's Reset takes a second "Confirm?" click, and says what it did.
//   8. The welcome stays up on track too, until it is answered, with Dismiss
//      as its only button: it never hides by itself (it stepped aside while
//      the player rode, and the game draws nothing in its pits, so a fresh
//      MX Bikes install never showed it), and it has no Open Settings button,
//      so the player learns the real settings button instead (Thomas, 1.32).
//   9. A popup's forced visibility never reaches a profile: a save, or a
//      profile switch, while it is up stores the player's own value. (1.32
//      review: a welcome left unanswered at exit saved the widget on, and the
//      next Dismiss left the Version widget on for good.)
//  10. A game started with every HUD switched off says so once, at the first
//      Draw, with the way into settings; never again that session (hiding may
//      be deliberate), and never when one HUD is on. A widget under the
//      saved Widgets master switch draws nothing, so it does not count as on.
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <windows.h>

#include <cstdio>
#include <string>

static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\messages\\";
static const std::string kIniPath = "Z:\\tmp\\mxbmrp3-tests\\messages\\mxbmrp3\\mxbmrp3_settings.ini";

namespace {

enum { POPUP_NONE = 0, POPUP_UPDATE, POPUP_UPDATED, POPUP_WELCOME };

// The settings folder exists once a startup has run; a case that writes its
// INI before the first startup makes it.
void cleanSaveDir() {
    std::remove(kIniPath.c_str());
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\messages\\mxbmrp3", nullptr);
}

// Whether the Version widget drew this exact line.
bool versionSays(PluginHost& host, const char* text) {
    for (const auto& row : host.hudStringRows("version_widget")) {
        if (row.text == text) return true;
    }
    return false;
}

// A session to draw in: the widgets update on the Draw path. No setup name,
// so it also raises the default setup notice.
void onTrack(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 0);
    host.runInit(1);
}

// Startup that is NOT a fresh install and owes nothing, so a test about toasts
// is not sharing the card's corner with the welcome popup.
void startQuiet(PluginHost& host) {
    host.startup(kSaveWin);
    host.messagesStartup(/*fresh=*/false, "1.0", "1.0");
}

}  // namespace

TEST_CASE("messages: hiding a HUD by hotkey toasts the way back; showing it withdraws the card") {
    cleanSaveDir();
    // Standings ships unbound; in the game a toggle only fires from a binding.
    REQUIRE(ini::writeFile(kIniPath, "[Hotkeys]\nstandings_key=112\nstandings_mod=0\n"));
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasMessages());
    startQuiet(host);
    onTrack(host);
    REQUIRE(host.setHudVisibleBoth("standings_hud", true));
    host.draw();
    const unsigned int before = host.messagesPosted();

    REQUIRE(host.injectHotkey("standings"));
    host.draw();
    CHECK(host.hudVisible("standings_hud") == 0);
    CHECK(host.messagesPosted() == before + 1);
    const std::string last = host.lastMessage();
    // Names the key back, and wears the shared "hidden" glyph.
    CHECK(last.rfind("Standings hidden|Press F1 to show it again|eye-slash|", 0) == 0);
    CHECK(last.find("|0") == last.size() - 2);                      // info, not warning
    host.draw();
    CHECK(host.messageShowing() == "Standings hidden");

    // Back on: no card for something that appeared, and the hidden card goes.
    REQUIRE(host.injectHotkey("standings"));
    host.draw();
    host.draw();
    CHECK(host.hudVisible("standings_hud") == 1);
    CHECK(host.messagesPosted() == before + 1);
    CHECK(host.messageShowing().empty());
    host.shutdown();
}

TEST_CASE("messages: the hide-all card shows through hide-all, and only that card") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    onTrack(host);
    host.draw();

    REQUIRE(host.injectHotkey("all_elements"));
    host.draw();
    host.draw();
    CHECK(host.messageShowing() == "All HUDs hidden");
    CHECK(host.lastMessage().find("|eye-slash|") != std::string::npos);
    CHECK(host.lastMessage().rfind("All HUDs hidden|", 0) == 0);

    // Showing them again: no card of its own, and the hidden one is withdrawn.
    const unsigned int posted = host.messagesPosted();
    REQUIRE(host.injectHotkey("all_elements"));
    host.draw();
    host.draw();
    CHECK(host.messagesPosted() == posted);
    CHECK(host.messageShowing().empty());
    host.shutdown();
}

TEST_CASE("messages: [Messages] enabled=0 drops toasts and survives a save") {
    cleanSaveDir();
    REQUIRE(ini::writeFile(kIniPath, "[Messages]\nenabled=0\nwelcomed=1\n"));
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(kSaveWin);
        onTrack(host);
        REQUIRE(host.setHudVisibleBoth("standings_hud", true));
        host.draw();
        REQUIRE(host.injectHotkey("standings"));
        host.draw();
        host.draw();
        CHECK(host.hudVisible("standings_hud") == 0);
        CHECK(host.messageShowing().empty());
        // Not a fresh install, so no welcome. (No lastLine: an install older than
        // [Messages] may be owed "Updated to", which is a popup, not a toast.)
        CHECK(host.versionPopup() != POPUP_WELCOME);
        REQUIRE(host.save());
        host.shutdown();
    }
    const std::string text = ini::readFile(kIniPath);
    CHECK(text.find("[Messages]") != std::string::npos);
    CHECK(text.find("enabled=0") != std::string::npos);
}

// The welcome names how to open settings as the player has it bound. The `
// has no glyph in the game font, so it is named by place even with modifiers
// (a Ctrl+` binding once printed "Press Ctrl+ to open settings"), and with no
// keyboard key at all it points at the menu button, which is three bars, not a gear.
TEST_CASE("messages: the welcome names the settings key as bound") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasSetSettingsKey());
    host.keepStartupPopups();
    host.startup(kSaveWin);
    onTrack(host);
    host.draw();
    REQUIRE(host.versionPopup() == POPUP_WELCOME);

    host.setSettingsKey(0xC0 /*VK_OEM_3*/, 1 /*CTRL*/);
    host.draw();
    CHECK(versionSays(host, "Welcome! Press Ctrl + the key under Esc to open settings"));

    host.setSettingsKey(0x77 /*VK_F8*/, 0);
    host.draw();
    CHECK(versionSays(host, "Welcome! Press F8 to open settings"));

    host.setSettingsKey(0, 0);
    host.draw();
    CHECK(versionSays(host, "Welcome! Click the menu button to open settings"));
    host.shutdown();
}

TEST_CASE("messages: a fresh install opens on the welcome, and opening settings answers it for good") {
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupPopups();
        host.startup(kSaveWin);
        onTrack(host);
        host.draw();
        CHECK(host.versionPopup() == POPUP_WELCOME);
        CHECK(host.hudVisible("version_widget") == 1);
        CHECK(versionSays(host, "Welcome! The key under Esc opens settings"));
        CHECK(host.hudHeldBack("notices_hud") == 1);    // under the popup

        host.showSettings(true);
        host.draw();
        CHECK(host.versionPopup() == POPUP_NONE);
        CHECK(host.hudHeldBack("notices_hud") == 0);
        host.showSettings(false);
        REQUIRE(host.save());
        host.shutdown();
    }
    const std::string text = ini::readFile(kIniPath);
    CHECK(text.find("welcomed=1") != std::string::npos);
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupPopups();
        host.startup(kSaveWin);
        onTrack(host);
        host.draw();
        CHECK(host.versionPopup() == POPUP_NONE);
        host.shutdown();
    }
}

TEST_CASE("messages: the Dismiss countdown runs down; the welcome waits for the player") {
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrack(host);
        host.messagesStartup(/*fresh=*/false, "1.32", "1.31");
        host.draw();
        int countdown = -1;
        REQUIRE(host.versionPopup(&countdown) == POPUP_UPDATED);
        CHECK(countdown == 5);

        host.versionPopupAdvance(3500);
        host.draw();
        host.versionPopup(&countdown);
        CHECK(countdown == 2);
        CHECK(versionSays(host, "Dismiss (2)"));

        host.versionPopupAdvance(2000);
        host.draw();
        CHECK(host.versionPopup() == POPUP_NONE);
        host.shutdown();
    }
    // The welcome never times out: however long it is up, it shows a plain
    // Dismiss and is still there; only an answer ends it.
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupPopups();
        host.startup(kSaveWin);
        onTrack(host);
        host.draw();
        int countdown = 0;
        REQUIRE(host.versionPopup(&countdown) == POPUP_WELCOME);
        CHECK(countdown == -1);
        CHECK(versionSays(host, "Dismiss"));
        CHECK_FALSE(versionSays(host, "Dismiss (5)"));

        host.versionPopupAdvance(60000);
        host.draw();
        CHECK(host.versionPopup() == POPUP_WELCOME);
        CHECK(versionSays(host, "Dismiss"));
        host.shutdown();
    }
}

TEST_CASE("messages: \"Updated to\" is told once, then the widget is the player's again") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    onTrack(host);
    REQUIRE(host.setHudVisibleBoth("version_widget", false));
    host.draw();
    CHECK(host.versionPopup() == POPUP_NONE);

    // A new release line, on an install that was told the last.
    host.messagesStartup(/*fresh=*/false, "1.32", "1.31");
    host.draw();
    CHECK(host.versionPopup() == POPUP_UPDATED);
    CHECK(host.hudVisible("version_widget") == 1);
    CHECK(versionSays(host, "Updated to 1.32. Open settings to see what's new"));
    host.versionPopupDismiss();
    host.draw();
    CHECK(host.versionPopup() == POPUP_NONE);
    CHECK(host.hudVisible("version_widget") == 0);    // back to the player's own setting
    host.shutdown();
}

TEST_CASE("messages: the welcome stays up while riding, with Dismiss its only button") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.keepStartupPopups();
    host.startup(kSaveWin);
    onTrack(host);
    host.draw();
    REQUIRE(host.versionPopup() == POPUP_WELCOME);
    CHECK(versionSays(host, "Dismiss"));
    CHECK_FALSE(versionSays(host, "Open Settings"));

    // On track, standing and riding: still up, still over Notices.
    host.runStart();
    for (float speed : {0.0f, 10.0f, 30.0f}) {
        host.telemetry(speed);
        host.draw();
        CHECK(host.versionPopup() == POPUP_WELCOME);
        CHECK(host.hudHeldBack("version_widget") == 0);
        CHECK(host.hudHeldBack("notices_hud") == 1);
    }
    host.runStop();
    host.draw();
    CHECK(host.versionPopup() == POPUP_WELCOME);
    CHECK(host.hudHeldBack("version_widget") == 0);
    host.shutdown();
}

TEST_CASE("messages: a popup's forced visibility is never saved, nor taken by a profile") {
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupPopups();
        host.startup(kSaveWin);
        onTrack(host);
        host.draw();
        REQUIRE(host.versionPopup() == POPUP_WELCOME);
        CHECK(host.hudVisible("version_widget") == 1);
        // A profile switch mid-popup: the old profile captures, the new applies.
        host.switchProfile(1);
        host.draw();
        host.switchProfile(0);
        host.draw();
        CHECK(host.versionPopup() == POPUP_WELCOME);
        CHECK(host.hudVisible("version_widget") == 1);
        REQUIRE(host.save());   // left unanswered at exit
        host.shutdown();
    }
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupPopups();
        host.startup(kSaveWin);
        onTrack(host);
        host.draw();
        REQUIRE(host.versionPopup() == POPUP_WELCOME);   // still owed
        host.versionPopupDismiss();
        host.draw();
        CHECK(host.versionPopup() == POPUP_NONE);
        CHECK(host.hudVisible("version_widget") == 0);   // the default, not the popup's
        host.switchProfile(1);
        host.draw();
        CHECK(host.hudVisible("version_widget") == 0);
        host.shutdown();
    }
}

TEST_CASE("messages: a tab's Reset asks to confirm, then toasts what it restored") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    host.showSettings(true);
    host.setActiveTab("Standings");
    host.draw();
    auto hasConfirm = [&] {
        for (const auto& row : host.hudStringRows(PluginHost::HUD_SETTINGS)) {
            if (row.text == "Confirm?") return true;
        }
        return false;
    };
    CHECK_FALSE(hasConfirm());
    const unsigned int posted = host.messagesPosted();

    REQUIRE(host.clickResetTabOnce());
    host.draw();
    CHECK(hasConfirm());
    CHECK(host.messagesPosted() == posted);          // armed, nothing reset yet

    REQUIRE(host.clickResetTabOnce());
    host.draw();
    CHECK_FALSE(hasConfirm());
    CHECK(host.messagesPosted() == posted + 1);
    CHECK(host.lastMessage().rfind("Standings reset|Defaults restored in ", 0) == 0);
    host.shutdown();
}

TEST_CASE("messages: every HUD off at the first Draw is said once per session") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisibleBoth("achievement_widget", true));   // the card itself
    REQUIRE(host.setHudVisibleBoth("settings_button", true));      // chrome is not a HUD
    onTrack(host);
    const unsigned int before = host.messagesPosted();
    host.draw();
    CHECK(host.messagesPosted() == before + 1);
    // The shipped ` is named by place, as on the welcome; same glyph as every
    // other "hidden" card.
    CHECK(host.lastMessage().rfind("All HUDs hidden|The key under Esc opens settings|eye-slash|", 0) == 0);
    host.draw();
    CHECK(host.messageShowing() == "All HUDs hidden");

    // Back on track after the menus: the first Draw only, so nothing new.
    Sleep(1100);   // past PluginConstants::ENTER_TRACK_GAP_MS
    host.draw();
    CHECK(host.messagesPosted() == before + 1);
    host.shutdown();
}

TEST_CASE("messages: one HUD on at the first Draw is not \"all hidden\"") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisibleBoth("achievement_widget", true));
    REQUIRE(host.setHudVisibleBoth("lap_widget", true));
    onTrack(host);
    const unsigned int before = host.messagesPosted();
    host.draw();
    host.draw();
    CHECK(host.messagesPosted() == before);
    CHECK(host.messageShowing().empty());
    host.shutdown();
}

TEST_CASE("messages: a widget under the Widgets master switch does not count as on") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    host.setEveryHudVisible(false);
    REQUIRE(host.setHudVisibleBoth("achievement_widget", true));
    REQUIRE(host.setHudVisibleBoth("lap_widget", true));
    host.setWidgetsEnabled(false);                                 // saved master switch off
    onTrack(host);
    const unsigned int before = host.messagesPosted();
    host.draw();
    CHECK(host.messagesPosted() == before + 1);
    CHECK(host.lastMessage().rfind("All HUDs hidden|", 0) == 0);
    host.shutdown();
}
