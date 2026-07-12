// ============================================================================
// tests/integration/tests/spectate_test.cpp
// SpectateVehicles -> the spectated ("camera") rider. The game passes its rider
// list and which index the camera is on; the plugin records that rider, and the
// snapshot tags it with the `camera` chip (used by the overlay/director to mark
// who's on screen). Assert the chip lands on the selected rider and moves when
// the camera switches. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

TEST_CASE("spectate: the camera chip follows the spectated rider") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spectate\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(7,  "Carol");
    host.classify(RACE1, 300000, {
        { .num = 10, .best = 90000, .laps = 3, .gap = 0 },
        { .num = 22, .best = 91000, .laps = 3, .gap = 1500 },
        { .num = 7,  .best = 92000, .laps = 3, .gap = 3200 },
    });

    // The game's spectate list; the camera is currently on index 1 (#22).
    const std::vector<std::pair<int, std::string>> grid = {
        { 10, "Alice" }, { 22, "Bob" }, { 7, "Carol" },
    };

    host.spectateVehicles(grid, /*curSelection=*/1);   // camera on #22
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(d["session"].value("isSpectating", false) == true);
        CHECK(hasChip(riderByNum(d, 22), "camera"));
        CHECK_FALSE(hasChip(riderByNum(d, 10), "camera"));
        CHECK_FALSE(hasChip(riderByNum(d, 7),  "camera"));
    }

    // Camera cuts to #7 — the chip must move.
    host.spectateVehicles(grid, /*curSelection=*/2);
    {
        auto d = host.snapshot();
        REQUIRE(d.is_object());
        CHECK(hasChip(riderByNum(d, 7),  "camera"));
        CHECK_FALSE(hasChip(riderByNum(d, 22), "camera"));
    }

    host.shutdown();
}

// Joining mid-session to spectate, before the game reports a camera target: there
// is NO display rider — not the local player (absent), and not a fabricated
// fallback (e.g. the leader). Display-rider widgets (Lap, Position, gaps) then
// render "-", which reads as "no rider selected" rather than inventing a subject.
// The `camera` chip must land on no one. Regression for the mid-race-spectate bug
// where widgets showed a rider's data (or "lap 1/N") for a target not yet chosen.
TEST_CASE("spectate: no camera target yet -> no display rider (widgets show dash)") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\spectate_notarget\\");

    // Pure spectator joining mid-session: the game does NOT fire EventInit in this
    // case (confirmed from a real capture), so the "first entry is the local player"
    // heuristic is never armed and the player race number stays unknown (-1).
    // Deliberately skip host.eventInit() to reproduce that.
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/6);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(7,  "Carol");
    // Race already 4 laps in; #22 leads.
    host.classify(RACE1, 300000, {
        { .num = 22, .best = 90000, .laps = 4, .gap = 0 },
        { .num = 10, .best = 91000, .laps = 4, .gap = 1500 },
        { .num = 7,  .best = 92000, .laps = 4, .gap = 3200 },
    });

    // The game reports the spectate list but with NO valid selection yet (camera not
    // on a rider). curSelection = -1 leaves the spectated rider unset.
    const std::vector<std::pair<int, std::string>> grid = {
        { 22, "Bob" }, { 10, "Alice" }, { 7, "Carol" },
    };
    host.spectateVehicles(grid, /*curSelection=*/-1);

    auto d = host.snapshot();
    REQUIRE(d.is_object());
    CHECK(d["session"].value("isSpectating", false) == true);
    // No rider is the display rider: the `camera` chip lands on no one, so the
    // Lap/Position widgets have no subject and render "-" (not the leader's data).
    CHECK_FALSE(hasChip(riderByNum(d, 22), "camera"));
    CHECK_FALSE(hasChip(riderByNum(d, 10), "camera"));
    CHECK_FALSE(hasChip(riderByNum(d, 7),  "camera"));

    host.shutdown();
}
