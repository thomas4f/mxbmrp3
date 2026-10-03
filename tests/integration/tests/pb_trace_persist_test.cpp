// ============================================================================
// tests/integration/tests/pb_trace_persist_test.cpp
// The all-time PB gap trace outlives the session (core/pb_trace_store.h).
//
// THE GAP THIS CLOSES. The live gap and the Gap Bar's ghost are measured against
// a 1000-point table of the PB lap, and that table used to be reset with the
// session: a new track, a new session in the same event, or a restart meant no
// gap and no ghost until the next PB was set in it. The stats file kept the PB
// TIME across all of those, so the Timing panel could show an all-time gap at
// the splits and nothing between them.
//
// Case 1 sets a PB in one plugin lifetime, then loads the plugin again on the
// same save path and drives the first flying lap of a fresh session: the
// all-time reference is there from the first sample after S1 and reads ~0 on
// the PB's pace, while the session and last-lap references are still empty --
// the three are told apart through MXBMRP3_Test_LiveGapRef.
//
// Case 3 is the GRID START: the gate drop binds the timer before the player's
// first position sample, and that bind plants the trace like any other; but
// nothing is read against it during the opening lap, whose clock runs from the
// gate and would show the grid run as a gap. Lap 2 reads it.
//
// Case 4 is a trace that LAGS the PB, as a user's file was found: the stats
// held 67205 ms, the trace 67813 ms, and the Gap Bar set to All-time showed
// them ahead of a PB they were not beating. A trace whose lap time is not the
// PB's is not planted, so the all-time reference reads nothing rather than a
// slower lap.
//
// Case 5 is the other direction: a trace FASTER than the stats PB, after the
// stats file was deleted or restored from a backup. The next PB the stats file
// stores is slower than the trace, and the trace must follow it (the PB and
// its trace match, or no trace is shown); the store used to keep the faster
// lap, which left the all-time reference off until the old time was beaten.
//
// Case 2 is the PB SCOPE. Under the default class scope a different bike in
// the same class is planted with the class's fastest trace (StatsManager names
// the bike; the store is keyed by bike and knows nothing of classes); under
// bike scope, seeded through the INI, the same bike gets nothing.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include "ini.h"             // readFile
#include <cstdio>    // std::remove
#include <fstream>
#include <string>

namespace {
constexpr int RACE1 = 6;
constexpr int REF_SESSION = 0, REF_ALLTIME = 1, REF_LAST = 2;   // PbGapTracker::Ref
constexpr const char* SAVE = "Z:\\tmp\\mxbmrp3-tests\\pb_trace_persist\\";

void beginSession(PluginHost& host, const char* bike, const char* category) {
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, bike, category, /*trackId=*/"testtrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice", bike, category);   // the player: the display rider whose timer runs
}

// A 60 s lap (or lapMs) sampled at 0.40 (20 s) and 0.70 (40 s) that completes as the PB.
void setPbLap(PluginHost& host, int lapMs = 60000) {
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, /*lapNum=*/1, /*splitIndex=*/0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, lapMs, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceLap(RACE1, 10, /*lapNum=*/1, lapMs, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
}

// The first flying lap of a fresh session, read at 0.40 after a 20 s S1: the
// all-time reference's pace, so ~0 if it is there.
void firstLapToS1(PluginHost& host) {
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
}
}  // namespace

TEST_CASE("the all-time PB trace is written with the PB and planted on the next lifetime") {
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host, "Test 450", "MX1");
        int gap = 0;
        CHECK_FALSE(host.liveGapRef(REF_ALLTIME, gap));   // nothing on disk yet
        setPbLap(host);
        // The lap just set is all three references at once.
        host.raceSplit(RACE1, 10, 2, 0, 20000);
        host.raceTrackPosition({ { 10, 0.40f } });
        REQUIRE(host.liveGapRef(REF_SESSION, gap));
        REQUIRE(host.liveGapRef(REF_ALLTIME, gap));
        CHECK_MESSAGE((gap >= -1500 && gap <= 1500), "all-time gap on the PB's own pace, got " << gap);
        REQUIRE(host.liveGapRef(REF_LAST, gap));
        host.shutdown();
    }
    // The file the PB wrote, beside the stats file.
    {
        std::ifstream f("Z:\\tmp\\mxbmrp3-tests\\pb_trace_persist\\mxbmrp3\\mxbmrp3_pb_traces.json");
        REQUIRE_MESSAGE(f.is_open(), "no mxbmrp3_pb_traces.json after the PB");
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        CHECK(text.find("testtrack|Test 450") != std::string::npos);
        CHECK(text.find("\"lapTimeMs\": 60000") != std::string::npos);
    }
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host, "Test 450", "MX1");
        firstLapToS1(host);
        int gap = 0;
        REQUIRE_MESSAGE(host.liveGapRef(REF_ALLTIME, gap), "the persisted PB was not planted");
        CHECK_MESSAGE((gap >= -1500 && gap <= 1500), "expected ~0 on the persisted PB's pace, got " << gap);
        CHECK_FALSE(host.liveGapRef(REF_SESSION, gap));   // no PB in THIS session yet
        CHECK_FALSE(host.liveGapRef(REF_LAST, gap));      // no lap completed in it either
        host.shutdown();
    }
}

TEST_CASE("PB scope: the class's fastest trace under class scope, the bike's own under bike scope") {
    // Class scope (the default): another bike in MX1 is measured against the
    // Test 450's trace from case 1.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host, "Other 450", "MX1");
        firstLapToS1(host);
        int gap = 0;
        REQUIRE_MESSAGE(host.liveGapRef(REF_ALLTIME, gap), "class scope did not plant the class's trace");
        CHECK_MESSAGE((gap >= -1500 && gap <= 1500), "got " << gap);
        host.shutdown();
    }
    // A bike in another class has no trace to be planted with.
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host, "Test 250", "MX2");
        firstLapToS1(host);
        int gap = 0;
        CHECK_FALSE(host.liveGapRef(REF_ALLTIME, gap));
        host.shutdown();
    }
    // Bike scope, seeded before startup: the Other 450 has no trace of its own.
    {
        std::ofstream ini("Z:\\tmp\\mxbmrp3-tests\\pb_trace_persist\\mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        ini << "[Settings]\nversion=6\n\n[General]\npbScope=BIKE\n";
    }
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host, "Other 450", "MX1");
        firstLapToS1(host);
        int gap = 0;
        CHECK_FALSE(host.liveGapRef(REF_ALLTIME, gap));
        host.shutdown();
    }
}

TEST_CASE("grid start: the gate-drop bind plants the trace, the opening lap reads nothing, lap 2 reads it") {
    constexpr int SESSION_IN_PROGRESS = 16;
    constexpr int SESSION_PRE_START = 256;
    constexpr int CLASSIFICATION_GATE_HOLD = 0x20;
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    host.eventInit("TestTrack", "Alice", 1600.0f, 2, "Test 450", "MX1", /*trackId=*/"testtrack");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0, SESSION_PRE_START);
    host.addEntry(10, "Alice", "Test 450", "MX1");
    host.raceSessionState(RACE1, SESSION_IN_PROGRESS);                 // arms the gate-drop watch
    host.classify(RACE1, 0, { { .num = 10, .laps = 0 } }, CLASSIFICATION_GATE_HOLD);
    host.classify(RACE1, 0, { { .num = 10, .laps = 0 } }, SESSION_IN_PROGRESS);   // the gate drops: t0, no sample yet

    int gap = 0;
    host.raceTrackPosition({ { 10, 0.96f } });
    host.raceTrackPosition({ { 10, 0.98f } });
    host.raceTrackPosition({ { 10, 0.02f } });      // first S/F: the timer keeps the gate anchor
    host.raceSplit(RACE1, 10, 1, 0, 24000);         // 4 s of grid run inside the official split
    host.raceTrackPosition({ { 10, 0.40f } });
    CHECK_MESSAGE(host.liveGapRef(REF_ALLTIME, gap) == false,
                  "the gate-measured opening lap read the planted trace (gap " << gap << ")");
    host.raceSplit(RACE1, 10, 1, 1, 44000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 64000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 64000, /*best=*/1, 24000, 44000);
    host.raceTrackPosition({ { 10, 0.02f } });

    // Lap 2, measured from the line, reads the persisted PB on its pace.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 2, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    REQUIRE_MESSAGE(host.liveGapRef(REF_ALLTIME, gap), "the gate-drop bind did not plant the trace");
    CHECK_MESSAGE((gap >= -1500 && gap <= 1500), "expected ~0 on the persisted PB's pace, got " << gap);
    host.shutdown();
}

TEST_CASE("a trace slower than the stats PB is not planted as the all-time reference") {
    // The file case 1 wrote, rewritten the way a lost save left the user's: the
    // trace's time behind the PB the stats file holds (60000 ms). Its samples
    // still fit the longer time, so load() keeps it -- only the plant refuses it.
    const char* path = "Z:\\tmp\\mxbmrp3-tests\\pb_trace_persist\\mxbmrp3\\mxbmrp3_pb_traces.json";
    std::string text = ini::readFile(path);
    const std::string pbTime = "\"lapTimeMs\": 60000";
    const size_t at = text.find(pbTime);
    REQUIRE_MESSAGE(at != std::string::npos, "case 1's trace is missing");
    text.replace(at, pbTime.size(), "\"lapTimeMs\": 61000");
    {
        std::ofstream f(path, std::ios::trunc);
        f << text;
    }
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(SAVE);
    beginSession(host, "Test 450", "MX1");
    firstLapToS1(host);
    int gap = 0;
    CHECK_MESSAGE(host.liveGapRef(REF_ALLTIME, gap) == false,
                  "a trace slower than the PB was read as all-time (gap " << gap << ")");
    host.shutdown();
}

TEST_CASE("a new PB replaces a faster stored trace, so the PB and its trace match again") {
    const std::string dir = "Z:\\tmp\\mxbmrp3-tests\\pb_trace_persist\\mxbmrp3\\";
    const std::string tracesPath = dir + "mxbmrp3_pb_traces.json";
    // The trace at 60000 ms (case 4 may have left it at 61000), and no stats file:
    // the stats were restored from before that PB.
    std::string text = ini::readFile(tracesPath);
    const size_t at = text.find("\"lapTimeMs\": 61000");
    if (at != std::string::npos) text.replace(at, 18, "\"lapTimeMs\": 60000");
    REQUIRE(text.find("\"lapTimeMs\": 60000") != std::string::npos);
    {
        std::ofstream f(tracesPath, std::ios::trunc);
        f << text;
    }
    std::remove((dir + "mxbmrp3_stats.json").c_str());
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host, "Test 450", "MX1");
        setPbLap(host, 62000);   // the stats file's first, and so its PB
        host.shutdown();
    }
    CHECK_MESSAGE(ini::readFile(tracesPath).find("\"lapTimeMs\": 62000") != std::string::npos,
                  "the stored trace kept the faster lap the stats file no longer holds");
    {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup(SAVE);
        beginSession(host, "Test 450", "MX1");
        firstLapToS1(host);
        int gap = 0;
        CHECK_MESSAGE(host.liveGapRef(REF_ALLTIME, gap), "the PB's own trace was not planted");
        host.shutdown();
    }
}
