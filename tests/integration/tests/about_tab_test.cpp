// ============================================================================
// tests/integration/tests/about_tab_test.cpp
// THE ABOUT PAGE, AND THE UPDATE TAG THAT REPLACED THE FOOTER'S NOTICE.
//
// The About page is the first tab that is NOT in the tab list: the sidebar is
// around thirty rows and at or near what sets the panel's height, so a page you
// read once is reached from the footer's About button instead of costing a row
// (TabDescriptor::hidden). That split is the thing worth pinning, because the
// two halves fail independently and both fail QUIETLY:
//
//   * listed anyway -> the sidebar grows a row and the panel with it, which no
//     other gate would call wrong;
//   * hidden AND unselectable -> the page exists, the button does nothing, and
//     nothing anywhere says so.
//
// The footer button also absorbed the update notice, which used to be a green
// chip on the version string. That moved to a tag on the Updates row, and its
// rules are the interesting part: keyed on the VERSION, so a newer release
// re-arms it by itself, and kept apart from the SKIP-version state so that
// reading about an update never silently skips it.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <string>
#include <vector>

TEST_CASE("About is reachable from the footer but absent from the tab list") {
    // The dir run_tests.sh pre-creates for this test is named after the TEST, and it
    // is the only one that exists -- a save to any other path silently writes
    // nothing, which is how the persistence case below first "passed" its save and
    // then failed its load.
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\about_tab\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE_MESSAGE(host.hasAbout(), "MXBMRP3_Test_ClickAbout not exported (test build?)");
    host.showSettings(true);

    SUBCASE("the tab list does not carry it") {
        const std::vector<std::string> tabs = host.settingsTabNames();
        REQUIRE(tabs.size() > 5u);
        for (const std::string& t : tabs) {
            CHECK_MESSAGE(t != "About",
                          "About is in the sidebar list -- it is meant to be reached "
                          "from the footer button, and a listed row costs panel height "
                          "on every tab");
        }
    }

    SUBCASE("the footer button opens it anyway") {
        // Hidden is not the same as unavailable: the tab must still be selectable,
        // or the button below has nowhere to go. This is the half a `hidden` check
        // in isTabAvailable() would have broken, silently.
        host.clickAbout();
        host.draw();
        CHECK(host.activeTab() == "About");
    }

    SUBCASE("it renders content, not an empty page") {
        host.clickAbout();
        host.draw();
        // COUNTED, not matched. This looked for a "Version:" row until the page's
        // copy was rewritten and that row went away -- a test coupled to prose that
        // its author is still choosing, which fails on an edit rather than on a
        // defect. What is actually worth pinning is that clicking About lands on a
        // page with something on it, and that survives any rewrite.
        int rows = 0;
        for (const auto& r : host.hudStringRows("settings_hud")) {
            if (!r.text.empty()) ++rows;
        }
        // Comfortably above the tab list + footer the panel draws on every tab, so
        // an About page that rendered nothing at all still fails.
        CHECK_MESSAGE(rows > 40, "the About page drew only " << rows << " strings");
    }

    host.shutdown();
}

TEST_CASE("the Updates tab's tag follows the available version") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\about_tab\\";   // see above
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE(host.hasAbout());
    REQUIRE(host.hasWhatsNew());
    host.showSettings(true);

    // A FRESH START PER SUBCASE. doctest re-runs this body for each subcase below,
    // but the DLL's state is process-global: the first subcase dismisses the tag for
    // 9.9.9, so without this the second one announces a version that has already
    // been seen and the tag never lights.
    host.updateTagReset();

    // Nothing available -> no tag. The starting point matters: a tag that is always
    // lit would pass every check below.
    REQUIRE_FALSE(host.updateTagLive());

    host.updateSetAvailable("9.9.9");
    CHECK(host.updateTagLive());

    SUBCASE("opening the Updates tab clears it, and it stays cleared") {
        REQUIRE(host.openSettingsTab("Updates"));
        // Cleared on CLOSE, like the what's-new tab tag: the tag should still be
        // there while the player is looking at the tab it points to.
        CHECK(host.updateTagLive());
        host.showSettings(false);
        CHECK_FALSE(host.updateTagLive());

        host.showSettings(true);
        CHECK_FALSE(host.updateTagLive());
    }

    SUBCASE("a NEWER version re-arms it") {
        REQUIRE(host.openSettingsTab("Updates"));
        host.showSettings(false);
        REQUIRE_FALSE(host.updateTagLive());

        // The seen state is a version string, not a flag -- so this needs no code
        // anywhere to notice that a new release happened.
        host.updateSetAvailable("9.9.10");
        CHECK(host.updateTagLive());
    }

    SUBCASE("the dismissal survives a save and reload") {
        REQUIRE(host.openSettingsTab("Updates"));
        host.showSettings(false);
        REQUIRE_FALSE(host.updateTagLive());
        REQUIRE_MESSAGE(host.isDirty(),
                        "seeing the tag left the settings clean -- updateTagSeen will "
                        "never be written, so the tag returns on the next launch");
        host.save();

        // Wipe the seen-version in MEMORY, then load. Re-announcing 9.9.9 would not
        // do it -- the tag is keyed on the version string, so a version already seen
        // stays seen, and the first attempt at this case failed on exactly that.
        host.updateTagReset();
        REQUIRE(host.updateTagLive());
        host.loadSettings(saveWin);
        CHECK_FALSE(host.updateTagLive());
    }

    host.shutdown();
}
