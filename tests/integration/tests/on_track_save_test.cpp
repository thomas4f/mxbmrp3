// ============================================================================
// tests/integration/tests/on_track_save_test.cpp
// Nothing persisted is written while the player is riding, record or not; and
// leaving the track writes the stats and the PB traces TOGETHER.
//
// WHY THIS EXISTS. For a while a "milestone" (a PB, a lifetime record, a tier
// earned live) wrote the stats file mid-ride, at most once per 30 s, so a game
// crash could not take it. That traded a frame hitch on track for crash
// safety, and it saved the PB without the gap-bar trace recorded with it: the
// trace was written only at the next pit stop, so a crash in between left a PB
// on disk whose reference lap was an older one. The decision (2026-09-30):
// crash loss is accepted, disjoint saves are not. Both files are written at
// the same three points -- leaving for the pits (RunStop), leaving the run
// (RunDeinit), and Shutdown() -- and the disk I/O is on the AtomicFileWriter
// thread.
//
// The saves that happen in place -- the crash widget's tally reset (clickable
// while riding), a breakout score, a prestige -- write the stats file at once,
// and through 1.31.0's pre-release they wrote it ALONE: a PB set on the lap
// before a tally reset reached disk without its trace. StatsManager::save()
// now writes both; the second case pins it.
//
// The harness runs the writer inline (PluginHost turns the worker off), so a
// write that happened is on disk when the call returns: "no file yet" below
// means no write, not a write still queued.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile

#include <cstdio>
#include <string>

namespace {
constexpr int RACE1 = 6;
constexpr const char* SAVE = "Z:\\tmp\\mxbmrp3-tests\\on_track_save\\";
const std::string STATS = "Z:\\tmp\\mxbmrp3-tests\\on_track_save\\mxbmrp3\\mxbmrp3_stats.json";
const std::string TRACES = "Z:\\tmp\\mxbmrp3-tests\\on_track_save\\mxbmrp3\\mxbmrp3_pb_traces.json";

// A 60 s lap sampled at 0.40 (20 s) and 0.70 (40 s) that completes as the PB,
// so it is both a stats record and a stored trace (pb_trace_persist_test.cpp).
void setPbLap(PluginHost& host) {
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, /*lapNum=*/1, /*splitIndex=*/0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceLap(RACE1, 10, /*lapNum=*/1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
}
}  // namespace

TEST_CASE("on track: a PB, a record and a tier write nothing; leaving the track writes stats and traces together") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    REQUIRE(host.hasStatsOdometer());
    REQUIRE(host.hasAchievements());
    std::remove(STATS.c_str());
    std::remove(TRACES.c_str());

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", /*trackId=*/"testtrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice", "Test 450", "MX1");
    host.runInit(RACE1);

    long long t = 1'000'000;   // µs on the simulated steady clock
    auto ride = [&](int ticks, float speedMs) {
        for (int i = 0; i < ticks; ++i) {
            t += 100'000;
            host.statsSetNowUs(t);
            host.telemetry(speedMs);
        }
    };

    // Everything the old milestone path wrote on: a top-speed record on the first
    // frame, a PB lap with its trace, a tier earned live (Tinkerer, threshold 1)
    // -- then minutes of simulated riding, far past the old 30 s interval.
    ride(1, 30.0f);
    setPbLap(host);
    host.configReloaded();
    CHECK(host.achievementTier("config_reloads") == 1);
    t += 10LL * 60 * 1'000'000;
    ride(50, 40.0f);

    CHECK_MESSAGE(ini::readFile(STATS).empty(), "the stats file was written while riding");
    CHECK_MESSAGE(ini::readFile(TRACES).empty(), "the PB traces were written while riding");

    // Into the pits: both files, in the same call.
    host.runStop();
    const std::string stats = ini::readFile(STATS);
    const std::string traces = ini::readFile(TRACES);
    CHECK(stats.find("\"config_reloads\"") != std::string::npos);
    CHECK(stats.find("60000") != std::string::npos);
    CHECK(traces.find("testtrack|Test 450") != std::string::npos);
    CHECK(traces.find("\"lapTimeMs\": 60000") != std::string::npos);

    host.statsSetNowUs(-1);   // real clock back before teardown
    host.runDeinit();
    host.shutdown();
}

TEST_CASE("an in-place stats save on track (crash tally reset) writes the PB trace with it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Before startup: the first case left a 60 s trace on disk, and a store that
    // already holds it would (rightly) not take the same lap again.
    std::remove(STATS.c_str());
    std::remove(TRACES.c_str());
    host.startup(SAVE);
    REQUIRE(host.hasCrashTally());

    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", /*trackId=*/"testtrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice", "Test 450", "MX1");
    host.runInit(RACE1);

    host.telemetry(30.0f);   // on track, as the first case's first frame
    setPbLap(host);
    // One crash (the tally follows the crashed flag's rising edge on the
    // track-position row; crash_widget_test.cpp), so the reset has work to do.
    for (int crashed : { 1, 0 }) {
        host.raceTrackPosition({ TrackRow{ 10, 0.25f, crashed } });
        TelemetryRow r;
        r.time = crashed ? 1.0f : 1.5f;
        host.telemetryFrame(r);
    }
    REQUIRE(host.crashTally() == 1);
    REQUIRE(ini::readFile(STATS).empty());

    // The reset persists at once, still on track: the PB goes to disk, and its
    // trace must go with it.
    host.crashTallyReset();
    const std::string stats = ini::readFile(STATS);
    const std::string traces = ini::readFile(TRACES);
    CHECK(stats.find("60000") != std::string::npos);
    CHECK_MESSAGE(traces.find("\"lapTimeMs\": 60000") != std::string::npos,
                  "the stats file was written with the PB but without its trace");

    host.runDeinit();
    host.shutdown();
}
