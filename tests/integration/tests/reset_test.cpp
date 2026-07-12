// ============================================================================
// tests/integration/tests/reset_test.cpp
// Settings "Reset All" SCOPE — the property the #212 / #214 reset bugs got wrong:
// resetting to factory defaults must revert per-profile HUD settings while
// leaving global sections (Rumble/Hotkeys/…) untouched.
//
// Single process, no Python, no file-diff orchestration: start the plugin, let
// it write a default settings.ini, perturb a few known anchor keys on disk, pull
// them into live state via the LoadSettings hook, Reset All, re-save, and assert
// the HUD anchors reverted to their captured defaults while the global anchors
// kept the perturbed value. Self-contained doctest; see run_tests.sh.
//
// NOTE (documented gap): only Reset ALL is asserted here. Per-profile / per-HUD
// resets clear the *profile diff*, not the shared base section — so perturbing a
// base-section value doesn't exercise them (the "m_hudDefaults is not a clean
// factory snapshot" property in CLAUDE.md). See tests/integration/API_COVERAGE.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"

TEST_CASE("reset all: per-profile HUDs revert, global sections untouched") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\reset\\";
    const std::string iniPath = "Z:\\tmp\\mxbmrp3-tests\\reset\\mxbmrp3\\mxbmrp3_settings.ini";

    // Anchors with unambiguous scope. HUD keys (per-profile) must revert on Reset
    // All; global-section keys must not. All default to 0/1 so perturb() flips them.
    const std::vector<ini::Anchor> resetAnchors = {
        { "StandingsHud", "classicLayout" },
        { "StandingsHud", "playerRowHighlightBrand" },
        { "MapHud",       "showTitle" },
    };
    const std::vector<ini::Anchor> stayAnchors = {
        { "Rumble",  "enabled" },
        { "Rumble",  "rumble_when_crashed" },
        { "Hotkeys", "standings_key" },
    };
    std::vector<ini::Anchor> all = resetAnchors;
    all.insert(all.end(), stayAnchors.begin(), stayAnchors.end());

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    host.save();                                   // guarantee a default INI on disk

    const std::string defText = ini::readFile(iniPath);
    REQUIRE_MESSAGE(!defText.empty(), "no settings.ini written at " << iniPath);
    const ini::Map D = ini::parse(defText);

    // Perturb the anchors on disk, then pull the perturbed file into live state.
    const std::string pertText = ini::perturb(defText, all);
    ini::writeFile(iniPath, pertText);
    const ini::Map P = ini::parse(pertText);
    host.loadSettings(saveWin);

    // Reset everything to factory defaults, then re-save from live state.
    host.resetAll();
    host.save();
    const ini::Map R = ini::parse(ini::readFile(iniPath));

    // HUD anchors: the perturbation must have taken (guards a vacuous test), and
    // Reset All must have reverted them to the captured default.
    for (const auto& a : resetAnchors) {
        ini::Key k{ a.section, a.key };
        INFO("reset anchor [" << a.section << "] " << a.key);
        REQUIRE(D.count(k)); REQUIRE(P.count(k)); REQUIRE(R.count(k));
        REQUIRE(D.at(k) != P.at(k));               // perturbation actually changed it
        CHECK(R.at(k) == D.at(k));                 // reverted to factory default
    }
    // Global anchors: out of Reset All's scope — must keep the perturbed value.
    for (const auto& a : stayAnchors) {
        ini::Key k{ a.section, a.key };
        INFO("stay anchor [" << a.section << "] " << a.key);
        REQUIRE(D.count(k)); REQUIRE(P.count(k)); REQUIRE(R.count(k));
        REQUIRE(D.at(k) != P.at(k));
        CHECK(R.at(k) == P.at(k));                 // untouched by the reset
    }

    host.shutdown();
}
