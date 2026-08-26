// ============================================================================
// tests/integration/tests/position_widget_test.cpp
// PositionWidget must be woken by the change types that can move its readout.
//
// WHY THIS FILE EXISTS. The widget shows "P3 / 22", and it used to notice a change
// to either half by RECOMPUTING BOTH EVERY FRAME and comparing against a cached
// copy. That poll made it the most expensive widget in the plugin -- 1.41us/frame
// across a stint in which it rebuilt ZERO times, ten times its peers' idle cost --
// because getDisplayPositionForRaceNum() serves a lazily-rebuilt cache and this was
// its only per-frame caller, so it alone paid to rebuild it on every frame a
// classification had landed.
//
// The poll was removed and replaced by a subscription: Standings for the position,
// RaceEntries for the denominator, SpectateTarget for whose position is shown. That
// is a claim, and this file is what makes it one that fails loudly. THE FAILURE MODE
// IT GUARDS IS SILENT IN EVERY OTHER TEST: a widget that stops updating leaves the
// plugin's computed state entirely correct -- the snapshot, /api/state and the
// standings all still say P3 of 22 -- and only the pixels are stale. So the
// assertion has to be about the panel's own rebuild, which is what the profiler's
// per-HUD counter records and MXBMRP3_Test_HudRebuildCount exposes.
//
// The counter only advances while the profiler is collecting, hence the
// bmSetVisible(true) in each case.
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

namespace {

constexpr int RACE = 6;   // PiBoSo Race1 session enum
const char* kPanel = "position_widget";   // the profiler's name for it

// A running race with two riders, the player among them, and the widget on screen.
void openSession(PluginHost& host) {
    host.showAllHuds(true);     // the widget early-outs when hidden on both surfaces
    host.bmSetVisible(true);    // start the profiler: the rebuild counter is its
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/5, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.classify(RACE, 100000, {
        { .num = 10, .best = 90000, .laps = 1, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 1, .gap = 1500 },
    });
    host.draw();
}

}  // namespace

TEST_CASE("position widget: a new entry rebuilds it, with no classification") {
    // THE CASE THE REMOVED POLL USED TO COVER. A rider joining changes the
    // denominator ("P3 / 22" -> "P3 / 23") and fires RaceEntries -- NOT Standings.
    // Before the subscription was widened, only the per-frame recompute noticed;
    // drop RaceEntries from handlesDataType() and the readout freezes with the old
    // total until something else happens to dirty the widget.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\position_widget_entries\\");

    openSession(host);

    const int before = host.hudRebuildCount(kPanel);
    REQUIRE_MESSAGE(before >= 0,
                    "MXBMRP3_Test_HudRebuildCount absent or panel unregistered -- "
                    "rebuild the test DLL");

    host.addEntry(33, "Carol");
    host.draw();

    CHECK_MESSAGE(host.hudRebuildCount(kPanel) > before,
                  "the entry count changed and the widget did not rebuild -- its "
                  "denominator is now stale on screen while every other reading of "
                  "the plugin's state stays correct");

    host.shutdown();
}

TEST_CASE("position widget: a classification rebuilds it") {
    // The other half of the readout. Standings was always subscribed; this pins it
    // so a future tidy-up of handlesDataType() cannot take both halves at once.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\position_widget_standings\\");

    openSession(host);

    const int before = host.hudRebuildCount(kPanel);
    REQUIRE(before >= 0);

    // Bob takes the lead: the player's position moves 1 -> 2.
    host.classify(RACE, 200000, {
        { .num = 22, .best = 89000, .laps = 2, .gap = 0 },
        { .num = 10, .best = 90000, .laps = 2, .gap = 900 },
    });
    host.draw();

    CHECK(host.hudRebuildCount(kPanel) > before);

    host.shutdown();
}

TEST_CASE("position widget: idle frames do not rebuild it") {
    // The other direction, and the reason the poll had to go rather than just get
    // cheaper: with nothing changing, the widget must do NOTHING. A rebuild-per-frame
    // here would be a correctness-neutral regression that only a profile would show.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\position_widget_idle\\");

    openSession(host);

    const int before = host.hudRebuildCount(kPanel);
    REQUIRE(before >= 0);

    for (int i = 0; i < 30; ++i) host.draw();

    CHECK(host.hudRebuildCount(kPanel) == before);

    host.shutdown();
}
