// ============================================================================
// tests/integration/tests/tooltip_coverage_test.cpp
// EVERY SETTINGS ROW HAS HOVER HELP.
//
// The panel's description box shows the hovered row's tooltip; a row built
// without one leaves the box on the tab's own text, which reads as if the row
// had nothing to say. test_tooltip_length.cpp guards that a tooltip FITS; this
// guards that there IS one: every row built through the shared row helpers
// (SettingsLayoutContext::addRowTooltip) on every tab carries an id, and that id
// has text in the tooltip table.
//
// Rows drawn without the shared helpers are outside what this can see.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <algorithm>
#include <string>
#include <vector>

TEST_CASE("every settings row on every tab carries a tooltip with text") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\tooltip_coverage\\");
    REQUIRE_MESSAGE(host.settingsUntippedRows() >= 0,
                    "MXBMRP3_Test_SettingsUntippedRows not exported (test build?)");

    host.showSettings(true);
    const std::vector<std::string> tabs = host.settingsAllTabNames();
    REQUIRE(!tabs.empty());
    host.settingsUntippedRows();   // whatever startup built

    std::vector<std::string> missing;
    for (const std::string& tab : tabs) {
        host.setActiveTab(tab.c_str());
        host.draw();
        host.settingsUntippedRows(&missing);
    }
    host.showSettings(false);

    // The panel measures every tab for its height, so one row can repeat.
    std::sort(missing.begin(), missing.end());
    missing.erase(std::unique(missing.begin(), missing.end()), missing.end());
    std::string list;
    for (const std::string& m : missing) list += "\n  " + m;
    CHECK_MESSAGE(missing.empty(), "settings rows without a tooltip:" << list);
    host.shutdown();
}
