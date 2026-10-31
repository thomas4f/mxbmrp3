// ============================================================================
// tests/integration/tests/messages_test.cpp
// System messages end to end (core/system_messages.h): the toasts the
// achievement card shows for things that happen out of sight, and the startup
// messages the Dash's card tells. Pins:
//
//   1. A hotkey that HIDES a HUD posts "<HUD> hidden" naming the key back and
//      pointing at the HUD's tab; the same key showing it again posts nothing
//      and withdraws the card. (A HUD that appears is its own confirmation.)
//   2. The hide-all hotkey's card is drawn THROUGH hide-all -- the one message
//      it would otherwise hide -- and nothing else is.
//   3. [Messages] enabled=0 drops the toasts and survives a save/load.
//   4. A fresh install opens the Dash on the welcome; the settings menu
//      opening is its answer, recorded in [Messages] so the next launch is
//      quiet. The shipped ` key has no glyph in the game font, so it is named
//      by place. A notice up first (the default setup notice) outranks it.
//   5. "Updated to" and the update go after five seconds on the card, which
//      for "Updated to" is told for good and for the update only this launch;
//      the welcome waits for its answer, riding or not.
//   6. A Dash step answers the message up, and for the update that skips the
//      version (the old popup's Dismiss); the menu opening lands on the tab
//      the message names. (There is deliberately no crash message: see
//      SystemMessages::onStartup.)
//   7. A tab's Reset takes a second "Confirm?" click, and says what it did.
//   8. A click on the Dash's card while a message shows opens the menu on the
//      tab it names, as the Version widget's "View in Settings" did, switched
//      off or not, and the press that opens it reaches no control under the
//      card; a click beside the card, or on it with no message, opens nothing.
//  10. A game started with every HUD switched off says so once, at the first
//      Draw, with the way into settings; never again that session (hiding may
//      be deliberate), and never when one HUD is on. A widget under the
//      saved Widgets master switch draws nothing, so it does not count as on.
//  11. Out-of-sight writes say how they went: the Benchmark profiler switched
//      off toasts its report's folder from "Documents\" on (the full path
//      does not fit the card), a stats write that fails says so once per run
//      of failures, an unreadable stats file is copied aside before anything
//      can overwrite it and the first Draw says so, and a web overlay whose
//      port is taken says so at the first Draw.
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

enum { MSG_NONE = 0, MSG_UPDATE, MSG_UPDATED, MSG_WELCOME };
// DashHud::Page: the page a fresh install opens on, and the card a message holds.
enum { PAGE_TIMING = 1, PAGE_NOTICES = 15 };

// The settings folder exists once a startup has run; a case that writes its
// INI before the first startup makes it.
void cleanSaveDir() {
    std::remove(kIniPath.c_str());
    CreateDirectoryA("Z:\\tmp\\mxbmrp3-tests\\messages\\mxbmrp3", nullptr);
}

// Whether the Dash drew this exact line.
bool dashSays(PluginHost& host, const char* text) {
    for (const auto& row : host.hudStringRows("dash_hud")) {
        if (row.text == text) return true;
    }
    return false;
}

// Whether a line the Dash drew starts with `text` (a long one is cut to the card).
bool dashStarts(PluginHost& host, const char* text) {
    for (const auto& row : host.hudStringRows("dash_hud")) {
        if (row.text.rfind(text, 0) == 0) return true;
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

// On track with a setup loaded: no default setup notice over the Dash.
void onTrackQuiet(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.session(1, 0, 0);
}

// Startup that is NOT a fresh install and owes nothing, so a test about toasts
// has no startup message on the Dash.
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
        // [Messages] may be owed "Updated to", which is the Dash's, not a toast.)
        CHECK(host.startupMessage() != MSG_WELCOME);
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
    host.keepStartupMessages();
    host.startup(kSaveWin);
    onTrackQuiet(host);
    host.draw();
    REQUIRE(host.startupMessage() == MSG_WELCOME);
    CHECK(dashSays(host, "WELCOME"));
    CHECK(dashSays(host, "The key under Esc opens settings"));

    host.setSettingsKey(0xC0 /*VK_OEM_3*/, 1 /*CTRL*/);
    host.draw();
    CHECK(dashStarts(host, "Press Ctrl + the key under Esc"));

    host.setSettingsKey(0x77 /*VK_F8*/, 0);
    host.draw();
    CHECK(dashSays(host, "Press F8 to open settings"));

    host.setSettingsKey(0, 0);
    host.draw();
    CHECK(dashStarts(host, "Click the menu button"));
    host.shutdown();
}

TEST_CASE("messages: a fresh install opens the Dash on the welcome, and opening settings answers it for good") {
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupMessages();
        host.startup(kSaveWin);
        onTrackQuiet(host);
        host.draw();
        int onCard = -1;
        CHECK(host.startupMessage(&onCard) == MSG_WELCOME);
        CHECK(onCard == MSG_WELCOME);
        CHECK(host.dashPage() == PAGE_NOTICES);

        host.showSettings(true);
        host.draw();
        CHECK(host.startupMessage() == MSG_NONE);
        host.showSettings(false);
        host.draw();
        host.startupMessage(&onCard);
        CHECK(onCard == MSG_NONE);
        CHECK(host.dashPage() == PAGE_TIMING);   // the rider's page is back
        REQUIRE(host.save());
        host.shutdown();
    }
    const std::string text = ini::readFile(kIniPath);
    CHECK(text.find("welcomed=1") != std::string::npos);
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupMessages();
        host.startup(kSaveWin);
        onTrackQuiet(host);
        host.draw();
        CHECK(host.startupMessage() == MSG_NONE);
        host.shutdown();
    }
}

// The default setup notice (no setup loaded) is up first: it keeps the card,
// and the welcome takes it once the notice is gone.
TEST_CASE("messages: a notice outranks the welcome on the card") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.keepStartupMessages();
    host.startup(kSaveWin);
    onTrack(host);
    host.draw();
    int onCard = -1;
    CHECK(host.startupMessage(&onCard) == MSG_WELCOME);
    CHECK(onCard == MSG_NONE);
    CHECK(dashSays(host, "DEFAULT SETUP"));
    host.shutdown();
}

TEST_CASE("messages: \"Updated to\" goes after its time on the card; the welcome waits") {
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrackQuiet(host);
        host.messagesStartup(/*fresh=*/false, "1.34", "1.33");
        host.draw();
        int onCard = -1;
        REQUIRE(host.startupMessage(&onCard) == MSG_UPDATED);
        CHECK(onCard == MSG_UPDATED);
        CHECK(dashSays(host, "UPDATED TO 1.34"));
        CHECK(dashSays(host, "Click here to see what's new"));

        host.startupMessageAdvance(4000);
        host.draw();
        CHECK(host.startupMessage() == MSG_UPDATED);
        host.startupMessageAdvance(1500);
        host.draw();
        CHECK(host.startupMessage() == MSG_NONE);
        host.draw();
        CHECK(host.dashPage() == PAGE_TIMING);   // the rider's page is back
        REQUIRE(host.save());
        host.shutdown();
    }
    CHECK(ini::readFile(kIniPath).find("lastLine=1.34") != std::string::npos);   // told once

    // The welcome never times out: however long it is up, riding or not, it is
    // still there; only an answer ends it.
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupMessages();
        host.startup(kSaveWin);
        onTrackQuiet(host);
        host.draw();
        REQUIRE(host.startupMessage() == MSG_WELCOME);
        host.startupMessageAdvance(60000);
        host.runStart();
        for (float speed : {0.0f, 10.0f, 30.0f}) {
            host.telemetry(speed);
            host.draw();
            int onCard = -1;
            CHECK(host.startupMessage(&onCard) == MSG_WELCOME);
            CHECK(onCard == MSG_WELCOME);
        }
        host.runStop();
        host.shutdown();
    }
}

TEST_CASE("messages: a Dash step answers the message; an update for this launch only") {
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrackQuiet(host);
        host.updateSetAvailable("9.9.9");
        host.draw();
        int onCard = -1;
        REQUIRE(host.startupMessage(&onCard) == MSG_UPDATE);
        CHECK(onCard == MSG_UPDATE);
        CHECK(dashSays(host, "UPDATE AVAILABLE"));
        CHECK(dashSays(host, "Click here to get 9.9.9"));

        REQUIRE(host.injectHotkey("dash_next"));
        host.draw();
        CHECK(host.startupMessage() == MSG_NONE);
        CHECK(host.dashPage() == PAGE_TIMING);   // the step answered it, and turned no page
        host.shutdown();
    }
    // The next launch is told again: no version is skipped by a page key.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrackQuiet(host);
        host.updateSetAvailable("9.9.9");
        host.draw();
        CHECK(host.startupMessage() == MSG_UPDATE);
        host.shutdown();
    }

    // "Updated to" answered the same way is told for good.
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrackQuiet(host);
        host.messagesStartup(/*fresh=*/false, "1.34", "1.33");
        host.draw();
        REQUIRE(host.startupMessage() == MSG_UPDATED);
        REQUIRE(host.injectHotkey("dash_prev"));
        host.draw();
        CHECK(host.startupMessage() == MSG_NONE);
        REQUIRE(host.save());
        host.shutdown();
    }
    CHECK(ini::readFile(kIniPath).find("lastLine=1.34") != std::string::npos);
}

TEST_CASE("messages: the menu opening lands on the tab the message names") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    onTrackQuiet(host);
    host.updateSetAvailable("9.9.9");
    host.draw();
    REQUIRE(host.startupMessage() == MSG_UPDATE);
    host.showSettings(true);
    host.setActiveTab("Standings");   // where it was left
    host.draw();
    CHECK(host.activeTab() == "Updates");
    CHECK(host.startupMessage() == MSG_NONE);
    host.showSettings(false);
    host.shutdown();
}

// The card's centre, where a player clicks it.
static void clickDash(PluginHost& host) {
    const PluginHost::ScreenEdges e = host.hudScreenEdges("dash_hud");
    REQUIRE(e.r > e.l);
    host.clickAt(static_cast<float>(e.l + e.r) * 0.5e-6f, static_cast<float>(e.t + e.b) * 0.5e-6f);
    host.injectMouse(false);
}

TEST_CASE("messages: a click on the Dash's card opens the tab the message names") {
    for (bool dashOn : {true, false}) {
        CAPTURE(dashOn);
        cleanSaveDir();
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        REQUIRE_MESSAGE((host.hasInjectedMouse() && host.hasScreenEdges()), "build without the hooks");
        startQuiet(host);
        onTrackQuiet(host);
        REQUIRE(host.setHudVisible("dash_hud", dashOn));
        host.draw();
        // No message: the card is the rider's page, and a click on it is nothing.
        if (dashOn) {
            clickDash(host);
            CHECK_FALSE(host.settingsVisible());
        }

        host.updateSetAvailable("9.9.9");
        host.draw();
        REQUIRE(host.startupMessage() == MSG_UPDATE);
        CHECK(host.dashBorrowed() == (dashOn ? 0 : 1));
        // Beside the card: nothing.
        const PluginHost::ScreenEdges e = host.hudScreenEdges("dash_hud");
        host.clickAt(static_cast<float>(e.r) * 1e-6f + 0.05f, static_cast<float>(e.t + e.b) * 0.5e-6f);
        host.injectMouse(false);
        CHECK_FALSE(host.settingsVisible());
        CHECK(host.startupMessage() == MSG_UPDATE);

        clickDash(host);
        CHECK(host.settingsVisible());
        CHECK(host.activeTab() == "Updates");
        host.draw();
        CHECK(host.startupMessage() == MSG_NONE);   // the menu opening answered it
        host.showSettings(false);
        host.shutdown();
    }

    // The card dragged over a menu control: the press that opens the menu is
    // not a press on the control drawn under it (SettingsHud::m_openingPress),
    // so the menu stays on Updates rather than switching to the tab under it.
    {
        cleanSaveDir();
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrackQuiet(host);
        host.showSettings(true);
        host.draw();
        float gx = 0.0f, gy = 0.0f;
        REQUIRE(host.settingsRegionCenter("general", &gx, &gy));
        host.showSettings(false);
        // A move is a pure translation: centre the card on the General tab.
        REQUIRE(host.setHudOffset("dash_hud", 0.0f, 0.0f));
        host.updateSetAvailable("9.9.9");
        host.draw();
        REQUIRE(host.startupMessage() == MSG_UPDATE);
        const PluginHost::ScreenEdges e = host.hudScreenEdges("dash_hud");
        REQUIRE(host.setHudOffset("dash_hud", gx - static_cast<float>(e.l + e.r) * 0.5e-6f,
                                  gy - static_cast<float>(e.t + e.b) * 0.5e-6f));
        host.draw();
        host.clickAt(gx, gy);
        host.injectMouse(false);
        host.draw();
        CHECK(host.settingsVisible());
        CHECK(host.activeTab() == "Updates");
        host.showSettings(false);
        host.shutdown();
    }

    // "Updated to" opens the menu too (on What's New, wherever that is).
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    onTrackQuiet(host);
    host.messagesStartup(/*fresh=*/false, "1.34", "1.33");
    host.draw();
    REQUIRE(host.startupMessage() == MSG_UPDATED);
    clickDash(host);
    CHECK(host.settingsVisible());
    host.draw();
    CHECK(host.startupMessage() == MSG_NONE);
    host.showSettings(false);
    host.shutdown();
}

// Switched off, the Dash still shows for an update or an "Updated to" (the
// latter only while the update checker is on, its own setting), never for the
// welcome; it holds back Notices/Timing where it lands on them. They open under
// the Dash, so this drags Timing up onto it the way a player can.
TEST_CASE("messages: a Dash switched off shows for the update messages, not the welcome") {
    cleanSaveDir();
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrackQuiet(host);
        REQUIRE(host.setHudVisible("dash_hud", false));
        REQUIRE(host.setHudVisible("timing_hud", true));
        REQUIRE(host.setHudOffset("timing_hud", 0.5f, static_cast<float>(host.layoutCells().cellH)));
        host.draw();
        CHECK(host.dashBorrowed() == 0);
        CHECK(host.hudHeldBack("timing_hud") == 0);

        host.updateSetAvailable("9.9.9");
        host.draw();
        CHECK(host.dashBorrowed() == 1);
        CHECK(dashSays(host, "UPDATE AVAILABLE"));
        // The message alone: no page strip (its icons start past the last quad).
        CHECK(host.dashFirstIcon() == static_cast<int>(host.hudQuadRects("dash_hud").size()));
        CHECK(host.hudVisible("dash_hud") == 0);   // its own switch untouched
        CHECK(host.hudHeldBack("timing_hud") == 1);   // the Dash lands on it
        REQUIRE(host.injectHotkey("dash_next"));    // a step answers it
        host.draw();
        CHECK(host.startupMessage() == MSG_NONE);
        CHECK(host.dashBorrowed() == 0);
        CHECK(host.hudHeldBack("timing_hud") == 0);
        host.shutdown();
    }
    for (bool checker : {true, false}) {
        cleanSaveDir();
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        host.setUpdateCheckEnabled(checker);
        onTrackQuiet(host);
        REQUIRE(host.setHudVisible("dash_hud", false));
        host.messagesStartup(/*fresh=*/false, "1.34", "1.33");
        host.draw();
        REQUIRE(host.startupMessage() == MSG_UPDATED);
        CHECK(host.dashBorrowed() == (checker ? 1 : 0));
        host.shutdown();
    }
    {
        cleanSaveDir();
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.keepStartupMessages();
        host.startup(kSaveWin);
        onTrackQuiet(host);
        REQUIRE(host.setHudVisible("dash_hud", false));
        host.draw();
        REQUIRE(host.startupMessage() == MSG_WELCOME);
        CHECK(host.dashBorrowed() == 0);
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
    REQUIRE(host.setHudVisibleBoth("toggle_strip_widget", true));  // chrome is not a HUD
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

// ---- 11. out-of-sight writes ------------------------------------------------

static const std::string kStatsPath = "Z:\\tmp\\mxbmrp3-tests\\messages\\mxbmrp3\\mxbmrp3_stats.json";

TEST_CASE("messages: the profiler switched off toasts where its report went") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    REQUIRE(host.hasBenchmarkSurfaces());
    startQuiet(host);
    onTrack(host);
    host.bmSetVisible(true);
    host.draw();
    host.draw();
    const unsigned int before = host.messagesPosted();
    host.bmSetVisible(false);
    host.draw();                                   // the falling edge: the report is written
    CHECK(host.messagesPosted() == before);        // told once the writer has landed it
    Sleep(1700);
    host.draw();
    CHECK(host.messagesPosted() == before + 1);
    const std::string last = host.lastMessage();
    INFO(last);
    // The test's save dir has no Documents folder: its last three folders.
    CHECK(last.rfind("Profiler report saved|messages\\mxbmrp3\\benchmarks|floppy-disk|13|0", 0) == 0);
    host.draw();
    CHECK(host.messageShowing() == "Profiler report saved");
    host.shutdown();
}

TEST_CASE("messages: a stats write that fails says so once") {
    cleanSaveDir();
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    onTrack(host);
    host.draw();
    // A target the writer cannot replace.
    std::remove(kStatsPath.c_str());
    REQUIRE(CreateDirectoryA(kStatsPath.c_str(), nullptr));
    const unsigned int before = host.messagesPosted();
    host.runStop();                                // the run's stats are the save
    Sleep(1700);
    host.draw();
    CHECK(host.messagesPosted() == before + 1);
    CHECK(host.lastMessage().rfind("Stats not saved|Could not write mxbmrp3_stats.json. Is it read-only?|"
                                   "triangle-exclamation|-1|1", 0) == 0);
    // The writer retries at the next save point; the player already knows.
    host.runInit(1);
    host.runStop();
    Sleep(1700);
    host.draw();
    CHECK(host.messagesPosted() == before + 1);
    host.shutdown();
    RemoveDirectoryA(kStatsPath.c_str());
}

TEST_CASE("messages: an unreadable stats file is copied aside before anything overwrites it") {
    cleanSaveDir();
    const char* broken = "{\"version\": 3, \"personalBests\": [ oops";
    REQUIRE(ini::writeFile(kStatsPath, broken));
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        startQuiet(host);
        onTrack(host);
        const unsigned int before = host.messagesPosted();
        host.draw();
        CHECK(host.messagesPosted() == before + 1);
        CHECK(host.lastMessage().rfind("Stats file unreadable|Started fresh. A copy is kept in the mxbmrp3 folder|"
                                       "triangle-exclamation|-1|1", 0) == 0);
        host.runDeinit();                          // a save point: the fresh file replaces the broken one
        host.shutdown();
    }
    // The copy holds the old bytes, word for word.
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("Z:\\tmp\\mxbmrp3-tests\\messages\\mxbmrp3\\mxbmrp3_stats_unreadable_*.json", &fd);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    std::string copy;
    int copies = 0;
    do {
        copy = std::string("Z:\\tmp\\mxbmrp3-tests\\messages\\mxbmrp3\\") + fd.cFileName;
        ++copies;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    CHECK(copies == 1);
    CHECK(ini::readFile(copy) == broken);
    DeleteFileA(copy.c_str());
}

TEST_CASE("messages: a web overlay whose port is taken says so at the first Draw") {
    cleanSaveDir();
    WSADATA wsa;
    REQUIRE(WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    REQUIRE(s != INVALID_SOCKET);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;                              // any free port, then hold it
    REQUIRE(bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(listen(s, 1) == 0);
    int len = sizeof(addr);
    REQUIRE(getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    const int port = ntohs(addr.sin_port);
    REQUIRE(port >= 1024);
    REQUIRE(ini::writeFile(kIniPath, "[General]\nwebServer=1\n\n[Advanced]\nwebServerPort=" +
                                     std::to_string(port) + "\n"));
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    startQuiet(host);
    onTrack(host);
    host.draw();
    const std::string want = "Web overlay not started|Could not open port " + std::to_string(port) +
                             ". Is it in use?|triangle-exclamation|0|1";
    CHECK(host.lastMessage() == want);
    host.shutdown();
    closesocket(s);
    WSACleanup();
}
