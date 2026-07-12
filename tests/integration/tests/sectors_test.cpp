// ============================================================================
// tests/integration/tests/sectors_test.cpp
// Best-sectors board (the overlay's per-sector "who's fastest where" carousel).
// Each rider's per-sector best comes from their completed-lap splits
// (RaceLap -> updateIdealLap -> bestSectorN); the snapshot emits, per sector, the
// riders ranked fastest-first as `sectors:[{s, riders:[{num, ms}]}]`. Drive three
// riders whose sector strengths differ (each wins a different sector) and assert
// the ranking per sector.
//
// Reads state via the direct snapshot(), so it sidesteps a real gotcha: an
// IdealLap change alone does NOT rebuild the *cached HTTP* snapshot (only
// Standings/EventLog do — in a live race classifications stream continuously). A
// test going through the server would have to force a rebuild; the direct
// snapshot just reads current state. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "assertions.h"

static constexpr int PRACTICE = 1;   // non-race: the best-sectors board's home

// The riders array for sector `s` from the snapshot (empty if the sector absent).
static nlohmann::json sectorRiders(const nlohmann::json& d, int s) {
    for (const auto& sec : d.value("sectors", nlohmann::json::array()))
        if (sec.value("s", -1) == s) return sec.value("riders", nlohmann::json::array());
    return nlohmann::json::array();
}

TEST_CASE("best sectors: per-sector fastest-first ranking from lap splits") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\sectors\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(PRACTICE, /*numLaps=*/0, /*lengthMs=*/600000);
    host.addEntry(10, "Alice");
    host.addEntry(22, "Bob");
    host.addEntry(7,  "Carol");

    const std::vector<ClassRow> grid = {
        { .num = 10, .best = 90000, .laps = 1 },
        { .num = 22, .best = 91000, .laps = 1 },
        { .num = 7,  .best = 92000, .laps = 1 },
    };
    host.classify(PRACTICE, 120000, grid);

    // Accumulated splits (S1, S1+S2); the third sector is lapTime - split1. Each
    // rider owns a different sector:
    //   #10  s1=30000 s2=31000 s3=29000   (best S3)
    //   #22  s1=29000 s2=32000 s3=30000   (best S1)
    //   #7   s1=31000 s2=30000 s3=31000   (best S2)
    host.raceLap(PRACTICE, 10, 1, 90000, 0, 30000, 61000);
    host.raceLap(PRACTICE, 22, 1, 91000, 0, 29000, 61000);
    host.raceLap(PRACTICE,  7, 1, 92000, 0, 31000, 61000);

    auto d = host.snapshot();
    REQUIRE(d.is_object());

    // Expected fastest-first ranking per sector: (num, ms).
    const std::vector<std::pair<int, std::vector<std::pair<int, int>>>> want = {
        { 1, { { 22, 29000 }, { 10, 30000 }, { 7, 31000 } } },
        { 2, { { 7,  30000 }, { 10, 31000 }, { 22, 32000 } } },
        { 3, { { 10, 29000 }, { 22, 30000 }, { 7, 31000 } } },
    };
    for (const auto& [s, riders] : want) {
        INFO("sector " << s);
        const auto got = sectorRiders(d, s);
        REQUIRE(got.size() == riders.size());
        for (size_t i = 0; i < riders.size(); ++i) {
            INFO("  rank " << i);
            CHECK(got[i].value("num", -1) == riders[i].first);
            CHECK(got[i].value("ms", -1) == riders[i].second);
        }
    }

    host.shutdown();
}
