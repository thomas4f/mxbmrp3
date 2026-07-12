// ============================================================================
// tests/integration/tests/stats_test.cpp
// Personal-best lap persistence (StatsManager). The player's lap/PB stats are
// written to <savePath>\mxbmrp3\mxbmrp3_stats.json — NOT in /api/state — so this
// drives real player-gated RaceLap callbacks and asserts the JSON on disk, the
// same save path the plugin uses in game.
//
// The PB path: RaceLap for the player's race number → StatsManager.recordLap
// (updates bestLapTimeMs when faster) + updatePersonalBest (stores the all-time PB).
// Persistence is DEFERRED — a PB is set at lap completion (on the s/f line, on track),
// and the plugin never writes while riding — so the stats file does NOT reflect a PB
// mid-session; it's flushed on the track->off-track transition (RunDeinit), same as
// settings. This asserts both: nothing persisted mid-ride, and the final (faster-wins,
// slower-rejected) PB written on leave-track. Player = first active RaceAddEntry after
// EventInit, so #10 is added first. Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile
#include "nlohmann/json.hpp"
#include <cstdio>   // std::remove
#include <limits>

static constexpr int RACE1 = 6;

// Return the single track/bike stats object (these tests run one bike on one
// track, so there's exactly one entry). Null json if the file/section is absent.
static nlohmann::json onlyTrackBike(const std::string& statsPath) {
    const std::string txt = ini::readFile(statsPath);
    if (txt.empty()) return nlohmann::json();
    auto j = nlohmann::json::parse(txt, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("trackBike")) return nlohmann::json();
    for (auto& item : j["trackBike"].items()) return item.value();   // first (only)
    return nlohmann::json();
}

TEST_CASE("stats: PB deferred off-track, flushed on RunDeinit; only a faster lap replaces it") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\stats\\";
    const std::string statsPath =
        "Z:\\tmp\\mxbmrp3-tests\\stats\\mxbmrp3\\mxbmrp3_stats.json";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    std::remove(statsPath.c_str());          // start clean: the ONLY writer below is leave-track

    host.eventInit("TestTrack", "Alice");   // sets the stats track/bike context
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");              // first active entry → the local player
    host.addEntry(22, "Bob");
    host.runInit(RACE1);                     // start the player's session (stats timers)

    // A PB lap (best=2 → overall best) updates the in-memory best but is DEFERRED — a PB is set
    // at lap completion, on track, and the plugin never writes while riding. So the stats file
    // must NOT reflect it yet.
    host.raceLap(RACE1, /*raceNum=*/10, /*lap=*/1, /*lapTimeMs=*/90000, /*best=*/2);
    {
        auto tb = onlyTrackBike(statsPath);
        CHECK_FALSE(tb.is_object());   // nothing written on track
    }

    // More laps (in memory): a faster lap wins, a slower one is rejected.
    host.raceLap(RACE1, 10, 2, 88000, 2);
    host.raceLap(RACE1, 10, 3, 92000, 2);

    // Top speed: a running max over telemetry. A non-finite speed (a bad physics
    // sample) must be clamped to 0 before it reaches the PERSISTED top speed /
    // odometer — the finiteOrZero write guard (CLAUDE.md). Drive 50 m/s, then +Inf
    // (must be ignored, not poison the value), then 30 m/s → top speed stays 50.
    host.telemetry(50.0f);
    host.telemetry(std::numeric_limits<float>::infinity());
    host.telemetry(30.0f);

    // Still nothing on disk while on track.
    CHECK_FALSE(onlyTrackBike(statsPath).is_object());

    // Leaving the track (RunDeinit) flushes stats — now the file reflects the final state.
    host.runDeinit();
    {
        auto tb = onlyTrackBike(statsPath);
        REQUIRE_MESSAGE(tb.is_object(), "no stats written on leave-track at " << statsPath);
        CHECK(tb.value("bestLapTimeMs", -1) == 88000);            // faster won,
        REQUIRE(tb.contains("personalBest"));
        CHECK(tb["personalBest"].value("lapTime", -1) == 88000);  // slower lap rejected
        // Top speed captured and finite — the +Inf sample didn't corrupt it.
        double top = tb.value("topSpeedMs", -1.0);
        CHECK(std::isfinite(top));
        CHECK(top == doctest::Approx(50.0));
    }

    host.shutdown();   // clean teardown (stats already flushed on RunDeinit above)
}
