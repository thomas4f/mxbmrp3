// ============================================================================
// tests/integration/tests/reset_tab_test.cpp
// PER-TAB "Reset <tab>" — the property every one of them is supposed to have:
// a tab's Reset restores everything that tab can change, and nothing else.
//
// WHY IT EXISTS. reset_test.cpp covers Reset ALL, against six named anchors, and
// says in its own header that the per-tab resets are a documented gap. They were,
// for their whole life: nothing ever pressed one. Meanwhile most tabs reset by
// replaying a whole INI section (which cannot drift), but the tabs whose settings
// do not map 1:1 to a section reset by HAND — a list of setters, or a list of
// widget names — and every one of those lists had fallen behind the tab it
// belonged to. The General tab was not restoring Grid Snap, Screen Clamp, Direct
// GL Rendering or Analytics; the Widgets tab was skipping the Crashes widget and
// the pointer's menu-only toggle; the Spotter tab was leaving its widget's
// opacity and scale where the user had dragged them.
//
// A list of settings in this test would rot exactly as those lists did, so there
// isn't one. The tab enumerates itself (MXBMRP3_Test_SettingsAnyTabName), the
// tab's own click regions enumerate what it can change, and the settings FILE —
// saved before and after — is the oracle. A control added to a tab is covered the
// day it is added, without touching this file.
//
// THE ONE MAINTAINED LIST is preservedByDesign() below: what a per-tab Reset is
// supposed to leave alone (visibility, master switches, the analytics opt-out).
// It is small, it is about intent rather than inventory, and every entry says why.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

#include <string>
#include <vector>

namespace {

// Keys a per-tab Reset deliberately does NOT restore. Everything else must come
// back, so a new setting is covered by default.
bool preservedByDesign(const std::string& tab, const ini::Key& k) {
    const std::string& section = k.first;
    const std::string& key = k.second;

    // A record of what the player has already been SHOWN, not a setting they chose.
    // Every reset path preserves these (see replayGlobalDefaults).
    if (key == "whatsNewSeen" || key == "updateTagSeen") return true;
    // INI-only power-user flag; no reset path touches it.
    if (key == "developerMode") return true;
    // Which page the menu is on. The sweep opens each tab in turn, so this differs
    // by construction; it is navigation state, not a setting.
    if (section == "Profiles" && key == "activeTab") return true;

    // Visibility: a per-tab reset must not hide the element the user is
    // positioning (resetHudsToFactoryDefaults' keepVisibility). The Widgets tab is
    // the deliberate exception — its rows expose per-widget "Visible" toggles, so
    // Reset restores those too, and it is NOT excused here.
    if (tab != "Widgets" && (key == "visible" || key == "companionVisible")) return true;

    // Master switches. Each tab's reset restores that tab's tuning while leaving
    // the on/off alone; the full "Reset all settings" turns them off instead.
    if (tab == "Rumble"   && section == "Rumble"   && key == "enabled") return true;
    if (tab == "Director" && section == "Director" && key == "enabled") return true;
    if (tab == "Spotter"  && section == "Spotter"  &&
        (key == "enabled" || key == "subtitles")) return true;
    if (tab == "Updates"  && section == "Updates"  && key == "updateMode") return true;
    if (tab == "Widgets"  && key == "widgetsEnabled") return true;
    // Consent, not tuning: replaying the factory value would turn a player's
    // opt-out back ON from a button that says "Reset General". See resetTabGeneral.
    if (tab == "General"  && section == "General"  && key == "analytics") return true;

    return false;
}

std::string describe(const ini::Key& k) { return "[" + k.first + "] " + k.second; }

// True when a and b agree on every key that is not preserved-by-design for `tab`.
bool differsOnlyBy(const ini::Map& a, const ini::Map& b, const std::string& tab) {
    for (const auto& e : a) {
        if (preservedByDesign(tab, e.first)) continue;
        const auto it = b.find(e.first);
        if (it == b.end() || it->second != e.second) return false;
    }
    for (const auto& e : b) {
        if (preservedByDesign(tab, e.first)) continue;
        if (!a.count(e.first)) return false;
    }
    return true;
}

// Human-readable diff, capped: doctest prints whole maps otherwise, and these have
// hundreds of keys.
std::string diffText(const ini::Map& a, const ini::Map& b, size_t maxLines = 12) {
    std::string out;
    size_t n = 0;
    for (const auto& e : a) {
        const auto it = b.find(e.first);
        if (it != b.end() && it->second == e.second) continue;
        if (++n > maxLines) { out += " ...more"; break; }
        out += " " + describe(e.first) + ": " + e.second + " vs " +
               (it == b.end() ? std::string("<absent>") : it->second) + ";";
    }
    for (const auto& e : b) {
        if (a.count(e.first)) continue;
        if (++n > maxLines) { out += " ...more"; break; }
        out += " " + describe(e.first) + ": <absent> vs " + e.second + ";";
    }
    return out;
}

}  // namespace

TEST_CASE("reset tab: a tab's Reset restores everything that tab can change") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\reset_tab\\";
    const std::string iniPath =
        "Z:\\tmp\\mxbmrp3-tests\\reset_tab\\mxbmrp3\\mxbmrp3_settings.ini";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();

    const std::string factoryText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!factoryText.empty(), "no settings.ini written at " << iniPath);
    const ini::Map FACTORY = ini::parse(factoryText);

    host.showSettings(true);
    const std::vector<std::string> tabs = host.settingsAllTabNames();
    REQUIRE(tabs.size() > 5);

    int tabsWithSomethingToReset = 0;
    for (const std::string& tab : tabs) {
        INFO("tab " << tab);

        // Start every tab from the same file. Reset the live state first (globals
        // AND per-profile) so anything the previous tab left pinned but absent from
        // the file — a colour override, say — is cleared rather than inherited.
        host.resetEverything();
        ini::writeFile(iniPath, factoryText);
        host.loadSettings(saveWin);
        host.setActiveTab(tab.c_str());
        host.save();
        const ini::Map baseline = ini::parse(ini::readFile(iniPath));
        // Plain bool, not the comparison: doctest would otherwise stringify both
        // maps (hundreds of keys) into the failure message. diffText says it in one line.
        const bool baselineClean = differsOnlyBy(FACTORY, baseline, tab);
        REQUIRE_MESSAGE(baselineClean, "baseline is not factory before tab "
                        << tab << ":" << diffText(FACTORY, baseline));

        const int clicked = host.perturbActiveTab();
        REQUIRE(clicked >= 0);       // hook present
        host.save();
        const ini::Map perturbed = ini::parse(ini::readFile(iniPath));
        // Nothing this tab changes reaches the INI (the About page, or a tab whose
        // controls are all pagination).
        if (differsOnlyBy(FACTORY, perturbed, tab)) continue;
        ++tabsWithSomethingToReset;

        REQUIRE_MESSAGE(host.clickResetTab(),
                        "tab " << tab << " changes settings but has no Reset button");
        host.save();
        const ini::Map after = ini::parse(ini::readFile(iniPath));

        for (const auto& entry : after) {
            if (preservedByDesign(tab, entry.first)) continue;
            const auto it = FACTORY.find(entry.first);
            if (it == FACTORY.end()) {
                FAIL_CHECK("Reset " << tab << " left a key the factory file does not have: "
                                    << describe(entry.first) << " = " << entry.second);
                continue;
            }
            CHECK_MESSAGE(entry.second == it->second,
                          "Reset " << tab << " did not restore " << describe(entry.first)
                                   << ": factory " << it->second << ", after reset " << entry.second);
        }
        for (const auto& entry : FACTORY) {
            if (preservedByDesign(tab, entry.first)) continue;
            CHECK_MESSAGE(after.count(entry.first) == 1,
                          "Reset " << tab << " dropped " << describe(entry.first));
        }
    }

    // The sweep is only worth anything if tabs really did change the file: a hook
    // that silently clicked nothing would otherwise pass every case above. 26 tabs
    // perturb today; the floor is set below that so a tab going quiet is caught
    // without the number needing a touch every time a tab is added.
    CHECK(tabsWithSomethingToReset >= 20);

    host.shutdown();
}
