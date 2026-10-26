// ============================================================================
// tests/integration/tests/settings_control_persist_test.cpp
// EVERY SETTINGS CONTROL, CHANGED BY ITSELF, MUST REACH THE SAVED FILE AND COME
// BACK FROM IT.
//
// WHY IT EXISTS. The "Lapper ahead" switch on the Notices tab worked from #245
// (1.26) to #540 and was never saved: the bit, its default, its checkbox and its
// tooltip landed, the INI key and its save/load lines did not, and turning it off
// lasted until the next restart. Every persistence test passed throughout, because
// every one of them takes the settings FILE as its oracle - run_persist_test.sh
// flips the keys it finds in the file, reset_tab_test diffs the file around a Reset
// - and a setting that never reaches the file is invisible to all of them.
//
// So this test starts from the other end, the menu. Each control the tab emits is
// changed ALONE, from a factory start (SettingsHud::testPerturbControl - the same
// walk reset_tab_test's sweep makes, one control at a time), and then:
//   1. the saved file must differ from the factory file - a control that changes
//      nothing on disk is a setting that reverts on restart;
//   2. with the control then moved the other way, that file loaded and saved
//      again must come back identical - a key written but read back wrong, or not
//      at all, fails here.
// A control at the limit the first way pushes against (a stepper at its minimum)
// is tried the other way before it counts as unsaved. There is no list of settings
// here to fall behind: a control added to any tab is covered the day it lands.
//
// THE ONE MAINTAINED LIST is notASetting() below: controls that change menu state
// rather than a setting. Small, about intent, every entry says why.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <string>
#include <vector>

namespace {

// Keys that differ between two saves for reasons other than a setting (as in
// reset_tab_test's preservedByDesign, minus the per-tab intent that only Reset has).
bool notASettingKey(const ini::Key& k) {
    const std::string& section = k.first;
    const std::string& key = k.second;
    if (key == "whatsNewSeen" || key == "updateTagSeen") return true;   // what was shown
    if (section == "Profiles" && key == "activeTab") return true;        // navigation
    if (section == "Fingerprint") return true;                           // hash of the rest
    // Window placement recorded by the companion window itself under an X display.
    if (section == "Display" && key.rfind("companionWindow", 0) == 0) return true;
    return false;
}

// Controls that change menu state, not a setting, so nothing reaches the file by
// design. Matched on "<row tooltip>|<tooltip>" (PluginHost::perturbControl).
bool notASetting(const std::string& id) {
    // Which profile "Copy to" targets: picked in the menu, used once by the Copy
    // button (which the sweep never presses - it writes another profile).
    if (id.rfind("general.copy_profile|", 0) == 0) return true;
    // Paging through a list (achievements, server riders): navigation.
    if (id.find("|pager.") != std::string::npos) return true;
    // The channel fields: a click starts typing; the channel is saved on commit.
    if (id.find("|twitch.channel") != std::string::npos ||
        id.find("|youtube.channel") != std::string::npos) return true;
    return false;
}

std::string diffText(const ini::Map& a, const ini::Map& b, size_t maxLines = 6) {
    std::string out;
    size_t n = 0;
    for (const auto& e : a) {
        if (notASettingKey(e.first)) continue;
        const auto it = b.find(e.first);
        if (it != b.end() && it->second == e.second) continue;
        if (++n > maxLines) return out + " ...more";
        out += " [" + e.first.first + "] " + e.first.second + ": " + e.second + " vs " +
               (it == b.end() ? std::string("<absent>") : it->second) + ";";
    }
    for (const auto& e : b) {
        if (notASettingKey(e.first) || a.count(e.first)) continue;
        if (++n > maxLines) return out + " ...more";
        out += " [" + e.first.first + "] " + e.first.second + ": <absent> vs " + e.second + ";";
    }
    return out;
}

}  // namespace

TEST_CASE("every settings control reaches the saved file and comes back from it") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\settings_control_persist\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\settings_control_persist\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE(host.save());
    const std::string factoryText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!factoryText.empty(), "no settings.ini written at " << iniPath);

    host.showSettings(true);
    const std::vector<std::string> tabs = host.settingsAllTabNames();
    REQUIRE(tabs.size() > 5);

    // The file as `text`, loaded into a plugin reset to factory, on `tab`, a frame
    // drawn so state the HUDs settle on their next update (the Dash leaving a page
    // just switched off) is what gets saved, as it would be in game.
    auto loadOnTab = [&](const std::string& text, const std::string& tab) {
        host.resetEverything();
        REQUIRE_MESSAGE(ini::writeFile(iniPath, text), "could not write the settings file for tab " << tab);
        host.loadSettings(saveWin);
        host.setActiveTab(tab.c_str());
        host.draw();
    };
    auto saved = [&](const std::string& what) {
        host.draw();
        REQUIRE_MESSAGE(host.save(), "settings save failed " << what << ": a stale file, not the plugin");
        return ini::readFile(iniPath);
    };

    int changed = 0;
    for (const std::string& tab : tabs) {
        INFO("tab " << tab);
        loadOnTab(factoryText, tab);
        const ini::Map baseline = ini::parse(saved("for the baseline"));

        for (int index = 0;; ++index) {
            std::string id, text;
            int result = 0;
            bool reachedFile = false, usedAlternate = false;
            for (const bool alternate : {false, true}) {
                loadOnTab(factoryText, tab);
                result = host.perturbControl(index, alternate, id);
                REQUIRE(result >= 0);   // hook present
                if (result != 1) break;
                text = saved("after changing " + id);
                usedAlternate = alternate;
                if (!diffText(baseline, ini::parse(text)).empty()) { reachedFile = true; break; }
            }
            if (result == 0) break;     // past the tab's last control
            if (result == 2 || notASetting(id)) continue;
            INFO("control " << index << " (" << id << ")");
            CHECK_MESSAGE(reachedFile, "Changing " << id << " on " << tab
                          << " changed nothing in the saved file: it reverts on restart");
            if (!reachedFile) continue;
            ++changed;

            // Move the live value AWAY from what was saved (the control's other way)
            // before loading the file back. Loading into a plugin reset to factory
            // would not do: every reset path replays the same loaders, so a key with
            // no load line keeps whatever value it had, and if that is the saved
            // one the round trip passes without reading the key at all.
            const ini::Map first = ini::parse(text);
            // The move is confirmed on disk: a slider's "other way" can be the end it
            // already sits at, which moves nothing. (A change that re-lays the tab out
            // - fewer rows below - can leave no control at this index, or none that
            // moves; that one reloads from a factory reset instead.)
            bool movedAway = false;
            for (const bool alternate : {!usedAlternate, usedAlternate}) {
                std::string again_id;
                host.draw();
                if (host.perturbControl(index, alternate, again_id) != 1) break;
                if (!diffText(first, ini::parse(saved("after moving " + id + " away"))).empty()) {
                    movedAway = true;
                    break;
                }
            }
            if (movedAway) {
                REQUIRE_MESSAGE(ini::writeFile(iniPath, text), "could not write the settings file for tab " << tab);
                host.loadSettings(saveWin);
                host.setActiveTab(tab.c_str());
                host.draw();
            } else {
                loadOnTab(text, tab);
            }
            const ini::Map again = ini::parse(saved("after reloading " + id));
            const std::string drift = diffText(first, again);
            CHECK_MESSAGE(drift.empty(), "Saved " << id << " on " << tab
                          << " did not come back from the file:" << drift);
        }
    }

    // Vacuity guard: the walk must really have reached the menu's controls. Over 600
    // reach the file today; the floor sits well below so adding a tab needs no touch.
    CHECK(changed >= 400);

    host.shutdown();
}
