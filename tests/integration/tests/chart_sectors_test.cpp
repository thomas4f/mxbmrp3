// ============================================================================
// tests/integration/tests/chart_sectors_test.cpp
// Session Charts at SECTOR resolution (ELEM_SECTOR_POINTS).
//
// The position / trace / gap charts sampled once per completed lap. Every rider's sector
// times are already in the plugin — RaceLap carries the whole lap's splits, and RaceSplit
// fires live for EVERY rider mid-lap — so the same curves can be drawn 3-4x denser, which
// is what turns "the gap grew on lap 7" into "the gap grew in sector 2 of lap 7".
//
// No new ranking logic was needed: positionsPerLap()/gapToLeaderPerLap() rank riders by
// comparing the same ARRAY INDEX across the field, so feeding them a series indexed by
// lap*sectors + sector is all it takes (pinned in tests/unit/test_session_charts_math.cpp).
// What this file pins is the three DECISIONS that live in the HUD, none of which the unit
// tests can see:
//
//   1. the live in-progress lap extends the series mid-lap (the whole point of using
//      RaceSplit rather than waiting for lap completion),
//   2. sector points are races-only — off-race the charts rank by best lap so far, which
//      has no sector analogue,
//   3. a hole in the sector data falls the WHOLE field back to per-lap, because mixed
//      resolutions would rank one rider's sector against another's lap.
//
// The default is OFF, so the per-lap rendering (and stripchart_parity_test's golden
// primitive checksums) is untouched.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <vector>

namespace {
constexpr int RACE = 6;
constexpr int PRACTICE = 1;
// MX Bikes is a 3-sector game (2 splits + the finish).
constexpr int SECTORS = 3;
}  // namespace

TEST_CASE("charts: sector points triple the sample rate and follow the lap in progress") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\chart_sectors\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.draw();
    host.spectateVehicles({ { 10, "Alice" }, { 22, "Bob" } }, /*curSelectionIndex=*/0);
    host.classify(RACE, 1000, {
        { .num = 10, .laps = 0, .gap = 0 },
        { .num = 22, .laps = 0, .gap = 500 },
    });

    // Two completed laps each, splits at 30.0 / 61.0 of a 90.0 lap → sectors 30/31/29.
    for (int lap = 1; lap <= 2; ++lap) {
        host.raceLap(RACE, 10, lap, 90000, /*best=*/1, /*split0=*/30000, /*split1=*/61000);
        host.raceLap(RACE, 22, lap, 92000, /*best=*/1, /*split0=*/31000, /*split1=*/62000);
    }
    host.classify(RACE, 200000, {
        { .num = 10, .best = 90000, .laps = 2, .gap = 0 },
        { .num = 22, .best = 92000, .laps = 2, .gap = 4000 },
    });

    // Per-lap (the default): one sample per completed lap.
    PluginHost::ChartSeries perLap = host.chartSeries(/*sectorPoints=*/0);
    CHECK(perLap.pointsPerLap == 1);
    CHECK(perLap.points == 2);

    // Sector resolution: the same two laps, three times as many samples.
    PluginHost::ChartSeries bySector = host.chartSeries(/*sectorPoints=*/1);
    CHECK(bySector.pointsPerLap == SECTORS);
    CHECK_MESSAGE(bySector.points == 2 * SECTORS,
                  "two completed laps should give two laps' worth of sector samples");

    SUBCASE("the lap in progress extends the line before it is complete") {
        // Alice crosses split 1 of lap 3. Nothing is in the lap log yet — the lap is not
        // over — but RaceSplit has arrived, so the chart must already know about it.
        host.raceSplit(RACE, /*raceNum=*/10, /*lapNum=*/2, /*splitIndex=*/0, /*splitTimeMs=*/30000);
        PluginHost::ChartSeries s1 = host.chartSeries(/*sectorPoints=*/1);
        CHECK_MESSAGE(s1.points == 2 * SECTORS + 1,
                      "one crossed split should add exactly one sector sample");

        // Second split of the same lap: one more sample, still no completed lap.
        host.raceSplit(RACE, 10, 2, /*splitIndex=*/1, /*splitTimeMs=*/61000);
        CHECK(host.chartSeries(/*sectorPoints=*/1).points == 2 * SECTORS + 2);

        // The per-lap view is unmoved by any of it — a lap in progress is not a lap.
        CHECK(host.chartSeries(/*sectorPoints=*/0).points == 2);
    }

    SUBCASE("the snapshot carries the per-sector series the overlay draws from") {
        // The web overlay renders the same charts from laps[].s, so the field has to be in
        // /api/state — the in-game HUD reads PluginData directly and would not notice if it
        // went missing. Flattened with a fixed stride, and (unlike t) it also covers the lap
        // IN PROGRESS, so it can be longer than laps*sectors.
        auto st = host.snapshot();
        REQUIRE(st.contains("laps"));
        bool sawAlice = false;
        for (const auto& r : st["laps"]) {
            if (r.value("num", -1) != 10) continue;
            sawAlice = true;
            REQUIRE(r.contains("s"));
            const auto t2 = r["t"].get<std::vector<int>>();
            const auto s2 = r["s"].get<std::vector<int>>();
            CHECK(t2.size() == 2);
            REQUIRE(s2.size() == 2 * SECTORS);
            // Sector times sum to their lap time, so a chart can switch resolution without
            // the line moving: 30.0 + 31.0 + 29.0 == the 90.0 lap.
            CHECK(s2[0] == 30000);
            CHECK(s2[1] == 31000);
            CHECK(s2[0] + s2[1] + s2[2] == t2[0]);
        }
        CHECK(sawAlice);

        // A crossed split on the lap in progress extends `s` past the completed laps —
        // the live edge, which `t` cannot express at all.
        host.raceSplit(RACE, 10, /*lapNum=*/2, /*splitIndex=*/0, /*splitTimeMs=*/30000);
        auto st2 = host.snapshot();
        for (const auto& r : st2["laps"]) {
            if (r.value("num", -1) != 10) continue;
            CHECK(r["t"].size() == 2);                       // still two completed laps
            CHECK(r["s"].size() == 2 * SECTORS + 1);         // ...but one more sector
        }
    }

    SUBCASE("a hole in the sector data falls the whole field back to per-lap") {
        // Bob completes a lap whose splits are broken (split0 == 0, which the lap handler
        // zeroes into a sector with no time — the "misplaced split markers" case). His
        // series can't be summed past the hole, so ranking him against Alice by array index
        // would compare different points on track. The field drops to per-lap instead.
        host.raceLap(RACE, 22, /*lap=*/3, /*lapTime=*/92000, /*best=*/1,
                     /*split0=*/0, /*split1=*/0);
        host.classify(RACE, 300000, {
            { .num = 10, .best = 90000, .laps = 2, .gap = 0 },
            { .num = 22, .best = 92000, .laps = 3, .gap = 4000 },
        });
        PluginHost::ChartSeries fallback = host.chartSeries(/*sectorPoints=*/1);
        CHECK_MESSAGE(fallback.pointsPerLap == 1,
                      "one rider's broken splits must drop the whole field to per-lap");
        CHECK(fallback.points == 2);
    }

    host.shutdown();
}

TEST_CASE("charts: sector points are ignored outside a race") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\chart_sectors\\");

    // Practice ranks riders by best lap so far, not cumulative time. A partial lap cannot
    // improve a best lap, so there is nothing finer than a lap to plot.
    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack", /*type=*/1);
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
    host.addEntry(10, "Alice");
    host.draw();
    host.spectateVehicles({ { 10, "Alice" } }, /*curSelectionIndex=*/0);
    host.classify(PRACTICE, 1000, { { .num = 10, .laps = 0, .gap = 0 } });

    host.raceLap(PRACTICE, 10, 1, 90000, /*best=*/1, /*split0=*/30000, /*split1=*/61000);
    host.raceLap(PRACTICE, 10, 2, 89000, /*best=*/1, /*split0=*/29500, /*split1=*/60000);
    host.classify(PRACTICE, 200000, { { .num = 10, .best = 89000, .laps = 2, .gap = 0 } });

    PluginHost::ChartSeries s = host.chartSeries(/*sectorPoints=*/1);
    CHECK_MESSAGE(s.pointsPerLap == 1, "practice must stay per-lap even with sector points on");
    CHECK(s.points == 2);

    host.shutdown();
}
