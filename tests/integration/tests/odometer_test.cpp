// ============================================================================
// tests/integration/tests/odometer_test.cpp
// Odometer / distance accumulation (StatsManager::updateTelemetry). Distance is
// integrated as speed × the WALL-CLOCK gap between telemetry ticks, so this
// injects the odometer clock (MXBMRP3_Test_StatsSetNowUs) and steps it 100ms
// per tick — making every dt, and therefore the expected distance, exact.
//
// Invariants pinned:
//  - distance integrates correctly over clock-spaced ticks (bike odometer +
//    session trip), read live via MXBMRP3_Test_StatsOdometerState;
//  - the ~100m dirty-coalescing: accumulating distance does NOT mark the stats
//    dirty until ~100m has built up (avoids per-frame save-scheduling), then
//    the unsaved accumulator resets;
//  - a +Inf/NaN speed sample is rejected (the finiteOrZero guard) instead of
//    poisoning the PERSISTED odometer — one bad physics sample must not corrupt
//    the stats file with no recovery path;
//  - a tick gap over 0.5s contributes nothing (menu/pause gaps don't inflate
//    the odometer);
//  - the accumulated total is persisted, finite, on the leave-track flush
//    (RunDeinit) — the same no-save-while-riding contract as stats_test.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile
#include "nlohmann/json.hpp"
#include <cstdio>   // std::remove
#include <cmath>
#include <limits>

static constexpr int PRACTICE = 1;

TEST_CASE("stats: odometer integrates clock-spaced ticks, coalesces dirty at ~100m, rejects non-finite speed") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\odometer\\";
    const std::string statsPath =
        "Z:\\tmp\\mxbmrp3-tests\\odometer\\mxbmrp3\\mxbmrp3_stats.json";

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    REQUIRE(host.hasStatsOdometer());
    std::remove(statsPath.c_str());   // start from a zero odometer

    host.eventInit("TestTrack", "Alice");   // sets the stats track/bike context ("Test 450")
    host.raceEvent("TestTrack");
    host.session(PRACTICE, 0, 480000);
    host.addEntry(10, "Alice");
    host.runInit(PRACTICE);

    // 30 m/s ticks 100ms apart = 3m per tick. 3 doesn't divide 100, so the
    // ~100m coalescing threshold is crossed unambiguously mid-batch (at 102m) —
    // float rounding noise can't move the crossing to a different tick.
    long long t = 1'000'000;   // µs; arbitrary epoch on the simulated steady clock
    auto tick = [&](float speedMs) {
        t += 100'000;
        host.statsSetNowUs(t);
        host.telemetry(speedMs);
    };

    // First sample only anchors the odometer timestamp (no interval yet) but
    // does record top speed → dirty; save to establish a known-clean baseline
    // so the only dirty-setter left below is the distance coalescing.
    host.statsSetNowUs(t);
    host.telemetry(30.0f);
    host.statsSave();
    CHECK(host.statsOdometerState().dirty == 0);

    // 15 ticks → 45m: accumulates in the odometer AND the session trip, but
    // stays below the ~100m mark, so the stats must NOT be dirty yet.
    for (int i = 0; i < 15; ++i) tick(30.0f);
    {
        auto s = host.statsOdometerState();
        CHECK(s.bikeOdometer == doctest::Approx(45.0).epsilon(0.001));
        CHECK(s.sessionTrip == doctest::Approx(45.0).epsilon(0.001));
        CHECK(s.unsaved == doctest::Approx(45.0).epsilon(0.001));
        CHECK(s.dirty == 0);
    }

    // A +Inf then a NaN sample (bad physics frames): each is clamped to 0 —
    // its interval contributes no distance and the accumulated value stays
    // finite instead of becoming +Inf/NaN forever.
    tick(std::numeric_limits<float>::infinity());
    tick(std::numeric_limits<float>::quiet_NaN());
    {
        auto s = host.statsOdometerState();
        CHECK(std::isfinite(s.bikeOdometer));
        CHECK(s.bikeOdometer == doctest::Approx(45.0).epsilon(0.001));
        CHECK(s.dirty == 0);
    }

    // 20 more ticks → 105m total. The unsaved accumulator crosses 100m at
    // 102m (tick 19), which marks dirty ONCE and resets it — the last tick
    // leaves exactly one 3m remainder.
    for (int i = 0; i < 20; ++i) tick(30.0f);
    {
        auto s = host.statsOdometerState();
        CHECK(s.bikeOdometer == doctest::Approx(105.0).epsilon(0.001));
        CHECK(s.dirty == 1);
        CHECK(s.unsaved == doctest::Approx(3.0).epsilon(0.01));
    }

    // A 2s gap (menu/pause — no telemetry flows) must add nothing: deltas over
    // 0.5s are discarded, not integrated.
    t += 2'000'000;
    host.statsSetNowUs(t);
    host.telemetry(30.0f);
    CHECK(host.statsOdometerState().bikeOdometer == doctest::Approx(105.0).epsilon(0.001));

    // Leave-track flush persists the odometer — finite and matching what
    // accumulated, on the bike AND the track/bike distance (and the top speed
    // untouched by the Inf sample).
    host.statsSetNowUs(-1);   // restore the real clock before teardown
    host.runDeinit();
    {
        const std::string txt = ini::readFile(statsPath);
        REQUIRE_MESSAGE(!txt.empty(), "no stats written on leave-track at " << statsPath);
        auto j = nlohmann::json::parse(txt, nullptr, /*allow_exceptions=*/false);
        REQUIRE(j.is_object());
        double odo = j["bikes"]["Test 450"].value("odometer", -1.0);
        CHECK(std::isfinite(odo));
        CHECK(odo == doctest::Approx(105.0).epsilon(0.001));
        for (auto& item : j["trackBike"].items()) {
            CHECK(item.value().value("totalDistanceM", -1.0) == doctest::Approx(105.0).epsilon(0.001));
            CHECK(item.value().value("topSpeedMs", -1.0) == doctest::Approx(30.0));
        }
    }

    host.shutdown();
}
