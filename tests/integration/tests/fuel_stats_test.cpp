// ============================================================================
// tests/integration/tests/fuel_stats_test.cpp
// The fuel achievements: Carbon Footprint (litres burnt, lifetime), Long Walk
// Home (the tank emptied out on track) and Running on Fumes (a race finished
// with the tank all but dry).
//
// WHY THIS IS NOT A UNIT TEST. The plugin already had per-lap fuel accounting,
// in FuelWidget - which only runs while that widget is enabled and the player
// is viewing their own bike. An achievement cannot depend on a HUD being
// switched on, so the accounting moved into StatsManager beside the odometer,
// and what this pins is that it is fed from the real RunTelemetry callback and
// survives the odometer's ~100m flush coalescing.
//
// The odometer clock is injected (MXBMRP3_Test_StatsSetNowUs) and stepped by a
// fixed amount per tick, the same technique odometer_test.cpp uses and for the
// same reason: the flush that hands the burnt litres to the exploration sum
// rides the odometer's ~100m mark, so the distance has to be exact for the
// crossing to land on a known tick rather than "somewhere around there".
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

static constexpr int PRACTICE = 1;
static constexpr int RACE1 = 6;
static const char* kSaveWin = "Z:\\tmp\\mxbmrp3-tests\\fuelstats\\";

namespace {

// 30 m/s ticks 100ms apart = 3m each, so 34 ticks clears the 100m flush mark.
struct Rider {
    PluginHost& host;
    long long t = 1'000'000;
    void tick(float fuelL, float speedMs = 30.0f) {
        t += 100'000;
        host.statsSetNowUs(t);
        TelemetryRow row;
        row.speed = speedMs;
        row.fuel = fuelL;
        host.telemetryFrame(row);
    }
    // Ride on at a steady level until anything still unflushed has gone
    // through. Burns nothing itself, but keeps clearing the ~100m mark the
    // flush rides on, so a total read after this is settled.
    void settle(float fuelL) { for (int i = 0; i < 40; ++i) tick(fuelL); }
};

}  // namespace

TEST_CASE("fuel: litres burnt accumulate from the tank falling, and only from falling") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    Rider r{ host };
    // The first tick only takes a reference - there is no previous level to
    // difference against, so it burns nothing.
    r.tick(8.0f);
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(0.0));

    // 40 ticks down to 6.0 L is 2 L burnt - but the sum only sees 34 ticks of
    // it yet. That is the coalescing, not a loss: the flush rides the
    // odometer's ~100m mark (34 ticks at 3m) rather than feeding the
    // exploration sum, and an achievement evaluation with it, at 100Hz.
    for (int i = 1; i <= 40; ++i) r.tick(8.0f - 0.05f * i);
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(1.7).epsilon(0.01));
    // Ride on and the remainder lands: nothing is dropped, only deferred.
    r.settle(6.0f);
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(2.0).epsilon(0.01));

    // A refuel in the pits: the level goes UP. That is not negative burn, and
    // it is not burn at all - it re-references and nothing is counted.
    const double afterBurn = host.achievementValue("fuel_burnt");
    r.tick(8.0f);
    r.settle(8.0f);
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(afterBurn));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("fuel: the tank running dry is an edge, and a new bike is not a burn") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    Rider r{ host };
    r.tick(8.0f);
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(0.0));
    // A tenth of a tank left is low, not dry.
    r.tick(0.8f);
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(0.0));
    // Half a percent of the tank is the FUMES band, not empty. This is the gap
    // the two rows live either side of: running this low and carrying on is not
    // running out, and at one point it was, because both constants read 1%.
    r.tick(0.04f);
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(0.0));
    // A splash left is still not empty. Empty is zero, and nothing above it
    // counts - the row used to accept a fraction of the tank as near enough,
    // which is how it came to mean the same thing as finishing on fumes.
    r.tick(0.004f);
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(0.0));
    // Zero litres: dry.
    r.tick(0.0f);
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(1.0));
    // Still dry on later ticks: a flag, not a count, and no repeat feed.
    r.settle(0.0f);
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(1.0));

    host.runDeinit();
    host.eventDeinit();

    // A different bike with a full tank must not read as a burn of the whole
    // difference against the last bike's empty one.
    const double burnt = host.achievementValue("fuel_burnt");
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Other 250", "MX2", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/6.0f);
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();
    r.settle(6.0f);
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(burnt));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

// A PIT STOP IS NOT A NEW EVENT, which is where this one came from: the tank
// reference was dropped in setTankCapacity(), and that only fires at EventInit.
// The fuel load is part of the setup, so a rider who comes back out with less in
// the tank than they went in with used to have the difference differenced as
// burn - up to a whole tank of it, per stop, for free.
TEST_CASE("fuel: a smaller fuel load out of the pits is not a burn") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);
    host.runStart();

    Rider r{ host };
    r.tick(8.0f);
    r.tick(6.0f);
    r.settle(6.0f);
    const double burnt = host.achievementValue("fuel_burnt");
    CHECK(burnt == doctest::Approx(2.0).epsilon(0.01));

    // In to the pits and straight back out on a two-litre setup. Same event,
    // same bike, so no EventInit: only the run lifecycle says the level in the
    // tank is no longer this session's.
    host.runStop();
    host.runDeinit();
    host.runInit(PRACTICE);
    host.runStart();
    r.tick(2.0f);
    r.settle(2.0f);
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(burnt));

    // And burning from the new load still counts, so the reference was dropped
    // rather than the accounting stopped.
    r.tick(1.5f);
    r.settle(1.5f);
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(burnt + 0.5).epsilon(0.02));

    host.runStop();
    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

// THE ROWS ARE TWO DIFFERENT MOMENTS, and zero litres is the other one's. Zero
// satisfies "at or under 1% of the tank" arithmetically, so without a lower bound
// a bike that ran out on the last straight and coasted over the line earned Long
// Walk Home and then Running on Fumes as well -- which is the overlap separating
// them was for. FIRST in this file: both rows are flags kept in the shared stats
// file, so this is the only point at which Fumes can be observed still unset.
TEST_CASE("fuel: a tank empty at the flag is Long Walk Home, and not Fumes") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);
    REQUIRE(host.achievementValue("fumes") == doctest::Approx(0.0));

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Dry 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(RACE1, 1, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    Rider r{ host };
    r.tick(8.0f);
    r.tick(0.0f);                                   // out, on the last straight
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(1.0));
    CHECK(host.achievementValue("fumes") == doctest::Approx(0.0));
    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("fuel: Running on Fumes reads the tank at the flag, and needs a known tank") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // A race finished with plenty left: no row.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(RACE1, 1, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    Rider r{ host };
    r.tick(8.0f);
    r.tick(4.0f);
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    CHECK(host.achievementValue("fumes") == doctest::Approx(0.0));
    host.runDeinit();
    host.eventDeinit();

    // The same race brought home on a twentieth of a litre out of eight - 0.6%
    // of the tank, inside the 1% fumes band and clear of the 0.1% that counts
    // as empty. A fifth of a litre (2.5%) used to earn this and no longer does.
    // The point of the gap is asserted below: this is Fumes WITHOUT Long Walk
    // Home, two rows for two different moments.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack",
                   /*serverType=*/0, /*serverName=*/"", /*maxFuel=*/8.0f);
    host.raceEvent("TestTrack");
    host.session(RACE1, 1, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    r.tick(8.0f);
    r.tick(0.05f);
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    CHECK(host.achievementValue("fumes") == doctest::Approx(1.0));
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(0.0));
    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}

TEST_CASE("fuel: with consumption off nothing moves, and an unknown tank is not an empty one") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(kSaveWin);

    // maxFuel 0 is what the API reports when there is nothing to say. A level
    // of 0.0 alongside it must read as "cannot be known", never as a dry tank -
    // otherwise every player without fuel consumption earns both rows for free
    // on their first lap.
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", "TestTrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, 1, 0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();
    Rider r{ host };
    r.settle(0.0f);
    host.raceLap(RACE1, 10, 1, 90000);
    host.classify(RACE1, 100000, { { .num = 10, .best = 90000, .laps = 1, .gap = 0 } });
    CHECK(host.achievementValue("ran_dry") == doctest::Approx(0.0));
    CHECK(host.achievementValue("fumes") == doctest::Approx(0.0));
    CHECK(host.achievementValue("fuel_burnt") == doctest::Approx(0.0));

    host.runDeinit();
    host.eventDeinit();
    host.shutdown();
}
