// ============================================================================
// tests/integration/tests/vehicle_data_test.cpp
// RaceVehicleData — per-rider rpm/gear/lean/throttle/brake.
//
// This was a fuzz-only "gap" in API_COVERAGE.md: the callback fuzzer proved it
// doesn't crash, nothing proved it moved a needle. It matters because it is the
// ONLY telemetry source while spectating or watching a replay — RunTelemetry is
// player-only — so every telemetry-driven widget a caster sees (speed, gear,
// tacho, lean) is fed from here and nowhere else.
//
// Three contracts, none of them observable in /api/state (telemetry isn't in the
// snapshot; the overlay has no use for it), so they are read through the
// MXBMRP3_Test_BikeTelemetry hook:
//   1. it updates the DISPLAY rider only — other riders' frames arrive at the
//      same rate and must not overwrite what's on screen;
//   2. it defers to RunTelemetry when the player is on track (that source has
//      the full frame; this one has six fields);
//   3. lean is NEGATED into roll. The two use opposite sign conventions and the
//      widgets read roll, so a dropped negation mirrors the bike on screen.
//
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int RACE1 = 6;

namespace {

const std::vector<std::pair<int, std::string>> kGrid = {
    { 10, "Alice" }, { 22, "Bob" }, { 7, "Carol" },
};

void setupRace(PluginHost& host, const char* savePath) {
    host.startup(savePath);
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
}

}  // namespace

TEST_CASE("RaceVehicleData feeds the spectated rider's telemetry") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    setupRace(host, "Z:\\tmp\\mxbmrp3-tests\\vehicledata\\");

    // Camera on #22. host.draw() runs with view state 1 (SPECTATE), which is what
    // makes #22 the display rider.
    host.spectateVehicles(kGrid, /*curSelection=*/1);

    host.raceVehicleData(22, /*speedMs=*/26.5f, /*gear=*/4, /*rpm=*/9100,
                         /*throttle=*/0.75f, /*frontBrake=*/0.10f, /*leanDeg=*/-31.0f);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.valid);
        CHECK(t.speedometer == doctest::Approx(26.5f));
        CHECK(t.gear == 4);
        CHECK(t.rpm == 9100);
        CHECK(t.throttle == doctest::Approx(0.75f));
        CHECK(t.frontBrake == doctest::Approx(0.10f));
        // lean "negative = left" becomes roll "positive = right": the handler
        // negates. Dropping that mirrors the bike in every lean-driven widget.
        CHECK(t.roll == doctest::Approx(31.0f));
    }

    // Same rider, leaning the other way — the sign tracks, it isn't an abs().
    host.raceVehicleData(22, 24.0f, 3, 8000, 0.5f, 0.0f, /*leanDeg=*/+18.0f);
    CHECK(host.bikeTelemetry().roll == doctest::Approx(-18.0f));

    host.shutdown();
}

TEST_CASE("RaceVehicleData ignores riders who aren't on screen") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    setupRace(host, "Z:\\tmp\\mxbmrp3-tests\\vehicledata-other\\");

    host.spectateVehicles(kGrid, /*curSelection=*/1);   // camera on #22
    host.raceVehicleData(22, 26.5f, 4, 9100, 0.75f, 0.10f, -31.0f);

    // The game sends a frame for every rider in the batch at the same rate. Only
    // the one the camera is on may reach the display telemetry — otherwise the
    // widgets flicker between whichever rider's frame landed last.
    host.raceVehicleData(7,  10.0f, 1, 3000, 0.05f, 0.90f, +5.0f);
    host.raceVehicleData(10, 40.0f, 6, 12500, 1.0f, 0.0f, -2.0f);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.gear == 4);                                  // still #22's
        CHECK(t.rpm == 9100);
        CHECK(t.speedometer == doctest::Approx(26.5f));
        CHECK(t.roll == doctest::Approx(31.0f));
    }

    // Cutting the camera to #7 makes ITS frames the ones that count.
    host.spectateVehicles(kGrid, /*curSelection=*/2);
    host.raceVehicleData(7, 11.0f, 2, 4200, 0.20f, 0.60f, +9.0f);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.gear == 2);
        CHECK(t.rpm == 4200);
        CHECK(t.roll == doctest::Approx(-9.0f));
    }

    host.shutdown();
}

TEST_CASE("RaceVehicleData stands down while the player is on track") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    setupRace(host, "Z:\\tmp\\mxbmrp3-tests\\vehicledata-ontrack\\");

    // Seed via the spectate path so there is something to clobber.
    host.spectateVehicles(kGrid, /*curSelection=*/1);
    host.raceVehicleData(22, 26.5f, 4, 9100, 0.75f, 0.10f, -31.0f);
    REQUIRE(host.bikeTelemetry().gear == 4);

    // Back on track. Crossing the spectate/on-track boundary CLEARS telemetry on
    // purpose — showing the rider you were just watching as if it were your own
    // bike is worse than showing nothing — so the widgets start blank and wait
    // for RunTelemetry.
    host.drawWithState(/*ON_TRACK=*/0);
    {
        auto t = host.bikeTelemetry();
        REQUIRE(t.gear == 0);
        REQUIRE(t.rpm == 0);
    }

    // ... and RaceVehicleData must leave them blank. RunTelemetry is the source
    // on track and carries the FULL frame (suspension, clutch, steer, fuel, ...);
    // RaceVehicleData has six fields, so letting it write here would half-fill
    // the telemetry — a live rpm/gear beside a suspension reading that never
    // updates — which is worse than not writing at all.
    host.raceVehicleData(10, 40.0f, 6, 12500, 1.0f, 0.0f, -2.0f);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.gear == 0);
        CHECK(t.rpm == 0);
        CHECK(t.speedometer == doctest::Approx(0.0f));
        CHECK(t.throttle == doctest::Approx(0.0f));
    }

    // The real source still works, which is what makes the check above a
    // "deferred to RunTelemetry" assertion rather than "telemetry is broken".
    host.telemetry(/*speedMs=*/33.0f, /*gear=*/5);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.gear == 5);
        CHECK(t.speedometer == doctest::Approx(33.0f));
    }

    host.shutdown();
}

TEST_CASE("RaceVehicleData ignores inactive vehicles") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    setupRace(host, "Z:\\tmp\\mxbmrp3-tests\\vehicledata-inactive\\");

    host.spectateVehicles(kGrid, /*curSelection=*/1);
    host.raceVehicleData(22, 26.5f, 4, 9100, 0.75f, 0.10f, -31.0f);

    // active=0 means every field after it is unset. Reading them anyway would
    // slam the widgets to whatever garbage the struct happened to hold.
    host.raceVehicleData(22, 0.0f, 0, 0, 0.0f, 0.0f, 0.0f, /*active=*/false);
    {
        auto t = host.bikeTelemetry();
        CHECK(t.gear == 4);
        CHECK(t.rpm == 9100);
        CHECK(t.speedometer == doctest::Approx(26.5f));
    }

    host.shutdown();
}
