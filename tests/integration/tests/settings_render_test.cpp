// ============================================================================
// tests/integration/tests/settings_render_test.cpp
// The settings panel draws its content, in the configuration a user actually has.
//
// WHY THIS FILE EXISTS. A change to how the panel sizes itself shipped, passed every
// settings suite in the tree, and rendered as ONE TALL BLACK RECTANGLE WITH NO TEXT
// in game. Nothing caught it, and the reason is the configuration: every existing
// settings test drives the panel UNTHEMED and with developer mode OFF, while the
// report came from a session with a theme selected and the Developer section visible.
// The suites were not wrong, they were narrow -- they asserted click-region ordinals
// and row geometry, and none of them asked the blunt question "did any text come out".
//
// So this file asks exactly that, in the four corners of (theme on/off) x (developer
// mode on/off), and asserts nothing subtle: content strings exist, the panel is not
// absurdly tall, and no configuration is wildly different from another. It is the
// smoke test the panel did not have.
//
// The panel MEASURES EVERY TAB to size itself, so a section that only exists under
// developer mode is only measured under developer mode -- which is why that axis is
// here rather than being obviously irrelevant.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {

constexpr int RACE = 6;   // PiBoSo Race1 session enum

void openSession(PluginHost& host) {
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.classify(RACE, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    host.draw();
}

struct Shot { int quads = 0; int strings = 0; };

Shot shoot(PluginHost& host) {
    host.draw();
    host.draw();
    return { host.lastGameQuads(), host.lastGameStrings() };
}

}  // namespace

TEST_CASE("settings panel: draws content in every theme x developer-mode corner") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_render\\");
    REQUIRE(host.hasThemeGeometry());
    host.showAllHuds(false);
    openSession(host);
    host.showSettings(true);

    // A BLANK PANEL IS THE FAILURE. The bar is deliberately crude -- the panel draws
    // a tab list of ~30 rows plus a tab's worth of controls, so anything under a few
    // dozen strings means the content did not come out, whatever the geometry says.
    constexpr int kMinStrings = 40;

    Shot corners[4];
    int i = 0;
    for (bool dev : { false, true }) {
        host.setDeveloperMode(dev);
        for (bool themed : { false, true }) {
            if (themed) host.installTheme("probe", 1.0f, 1.0f, /*titleBand=*/1, /*contentCard=*/1, 1, 1);
            else        host.clearTheme();
            const Shot s = shoot(host);
            corners[i++] = s;
            CHECK_MESSAGE(s.strings >= kMinStrings,
                          "settings panel emitted only " << s.strings << " strings with "
                          << (themed ? "a theme" : "no theme") << " and developer mode "
                          << (dev ? "ON" : "OFF") << " -- a panel with no text in it is "
                          << "the failure this file exists for");
            CHECK_MESSAGE(s.quads > 0, "settings panel emitted no quads at all");
        }
    }

    // ...and no corner is wildly unlike the others. A theme adds chrome and the
    // Developer section adds a few rows; neither should double or halve the content.
    for (int k = 1; k < 4; ++k) {
        CHECK_MESSAGE(corners[k].strings * 2 > corners[0].strings,
                      "corner " << k << " emitted " << corners[k].strings
                      << " strings against the plain corner's " << corners[0].strings
                      << " -- one configuration is losing content the others keep");
    }

    host.shutdown();
}

TEST_CASE("settings panel: fits on the screen, themed or not") {
    // THE OTHER HALF OF THE REPORT: "super tall ... no matter how i scroll i cant
    // seem to reach the end". The panel does not scroll, so anything past the bottom
    // edge -- Save and Close included -- is simply unreachable.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\settings_render_fit\\");
    REQUIRE(host.hasThemeGeometry());
    REQUIRE(host.hasScreenEdges());
    host.showAllHuds(false);
    openSession(host);
    host.showSettings(true);

    for (bool dev : { false, true }) {
        host.setDeveloperMode(dev);
        for (bool themed : { false, true }) {
            if (themed) host.installTheme("probe", 1.0f, 1.0f, 1, 1, 1, 1);
            else        host.clearTheme();
            host.draw();
            host.draw();
            const auto e = host.hudScreenEdges(PluginHost::HUD_SETTINGS);
            const double h = (e.b - e.t) / 1e6;
            CHECK_MESSAGE(h <= 1.0,
                          "settings panel is " << h << " screens tall (theme "
                          << (themed ? "on" : "off") << ", developer mode "
                          << (dev ? "ON" : "OFF") << ") -- it does not scroll, so "
                          << "everything past the bottom edge is unreachable");
        }
    }

    host.shutdown();
}
