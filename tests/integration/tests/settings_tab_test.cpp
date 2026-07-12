// ============================================================================
// tests/integration/tests/settings_tab_test.cpp
// The settings menu remembers which tab was open. The last-focused tab is saved
// to the INI ([Profiles] activeTab, by name) and restored on load, so reopening
// the menu next session lands on the tab the player left it on. This drives the
// real save/load path (MXBMRP3_Test_Save / MXBMRP3_Test_LoadSettings) through the
// same accessors the plugin uses, and asserts:
//   1. a chosen tab round-trips through save -> load (restored over a different
//      live value, proving the load actually wrote it);
//   2. an unknown / unavailable tab name is ignored (keeps the current tab), so a
//      tape saved on one game build won't strand the menu on an empty tab.
// Self-contained doctest; see run_tests.sh / TESTING.md.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

TEST_CASE("settings tab: the focused tab persists across save/load") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_tab\\");

    // Pick a non-default tab and persist it.
    host.setActiveTab("Standings");
    CHECK(host.activeTab() == "Standings");
    host.save();

    // Move the live tab elsewhere, then load from disk: the stored tab must win,
    // proving the restore happened (not just a stale in-memory value).
    host.setActiveTab("Map");
    REQUIRE(host.activeTab() == "Map");
    host.loadSettings("Z:\\tmp\\mxbmrp3-tests\\settings_tab\\");
    CHECK(host.activeTab() == "Standings");

    // A second tab round-trips too (not hard-wired to one value).
    host.setActiveTab("Director");
    host.save();
    host.setActiveTab("General");
    host.loadSettings("Z:\\tmp\\mxbmrp3-tests\\settings_tab\\");
    CHECK(host.activeTab() == "Director");

    host.shutdown();
}

TEST_CASE("settings tab: an unknown or unavailable tab name is ignored") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_tab\\");

    host.setActiveTab("Standings");
    REQUIRE(host.activeTab() == "Standings");

    // A name that maps to no tab must be a no-op — the current tab stays put rather
    // than landing on a broken/empty selection (models a renamed/removed tab in an
    // older INI, or a game-gated tab absent on this build).
    host.setActiveTab("NoSuchTab");
    CHECK(host.activeTab() == "Standings");
    host.setActiveTab("");
    CHECK(host.activeTab() == "Standings");

    host.shutdown();
}
