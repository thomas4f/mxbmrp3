// ============================================================================
// tests/integration/tests/pb_gap_test.cpp
// The live gap to PB is PluginData's, computed against the central lap timer.
//
// WHAT MOVED. GapBarHud used to own this: its own wall-clock anchor (a copy of
// LapTimer's without the grid-start grace), its own S/F detection, and a
// setLiveGap() call that wrote the result INTO PluginData so LapLogHud's gap row
// could read it -- a HUD writing the central store against the one-way flow every
// other HUD follows, and the reason the Gap Bar had to keep updating while
// hidden. PluginData now owns a PbGapTracker (core/pb_gap_tracker.h) driven from
// the same transitions as the lap timer (plugin_data_lap_timer.cpp), and both
// HUDs read it. These cases drive the REAL callbacks through the DLL and read the
// result back through MXBMRP3_Test_LiveGapToPb; the engine's arithmetic is the
// unit half in tests/unit/test_pb_gap_tracker.cpp.
//
// HOW A HEADLESS TEST GETS ELAPSED TIME. The timer is wall-clock anchored, so a
// lap cannot be "driven" in real time; but an official split re-anchors the
// timer at the split's accumulated time, so a split of 20000 followed by a
// track-position sample records that position at ~20000 ms. Lap 1 lays down
// samples at 0.40 (20 s) and 0.70 (40 s) and completes as a PB; lap 2 crosses S1
// 5 s slower and S2 5 s faster, and the gap read at the same positions must say
// so. The harness's own latency only adds a few ms in the same direction on both
// laps, hence the wide tolerances.
//
// THE TWO ORDERS AT THE LINE. The position wrap and the RaceLap arrive in either
// order. Case 1 sends the wrap FIRST, with the standings lap count already bumped
// so the timer re-anchors on it (the order the review found unguarded): that
// sample belongs to the new lap, and the reference the RaceLap then commits must
// not carry it -- read at 0.02 afterwards there must be NO gap, where the
// polluted table read a gap of exactly the elapsed time.
//
// THE GRID START. Case 2 drops the gate (PRE_START -> IN_PROGRESS arms the watch,
// a held classification then a racing one fires it): lap 1 runs from the green
// flag and must NOT become the reference, however good; lap 2, measured from the
// line, is the first that does, and lap 3 on its pace reads ~0, not the grid run.
//
// THE PIT EXIT. Case 3 pits mid-lap and comes back out in the real callback order
// (RunStop, RunDeinit, RunInit, RunStart, then the first position sample at the
// pit exit, then the classification whose pit flag clears). The dead lap's anchor
// used to survive until that classification, so the sample in between read the
// paused timer against the reference most of a lap on: -60 s, valid, for one
// frame, in an in-game capture. RunInit is where the plugin learns the player
// is leaving the pits, and RunInit must already have invalidated the anchor.
//
// THE PIT BOX. Case 4 is the other half of that fix. The RunInit invalidate
// dropped the anchor but kept the position baseline, so the first sample after
// a re-entry was a delta from where the rider LEFT the track: pit from the menu
// at 0.70, back on at a box at 0.02, and the -0.68 jump read as an S/F crossing
// -- the timer re-anchored at the box, the tracker fenced an observed lap
// there, and from 0.05 on the gap to PB was "valid", wrong by the box-to-line
// offset, until the classification's pit flag healed it. The re-entry must
// read NOTHING until the real line; the forward jump in case 3 never showed it.
//
// THE CUT LAP. Case 5: in a race an invalid lap keeps its time (the API note in
// race_lap_handler), and the Last Lap reference took it -- a cut lap, faster
// than the rider can go, became the pace the next lap was read against. An
// invalid lap ends without a time, so the reference stays the last valid one.
//
// THE LAP LOG'S OWN REFERENCE. Case 6: the Lap Log's gap row used to read the
// Gap Bar's Reference setting, so changing one HUD silently changed the other.
// It now has its own (default Session PB). With the Lap Log on Last lap and the
// Gap Bar left on Session PB, the row must show the last-lap gap.
//
// THE FREEZE (official_gap_freeze.h). Cases 7-9: after a split or the line,
// the Lap Log's gap row and the Gap Bar hold the OFFICIAL gap for the freeze
// duration, then go live; Off never holds. The held gap is against the HUD's
// own Reference: until 1.31's pre-release it was always the session PB's, so
// on Last lap or All-time PB the readout jumped to another number at every
// split. The all-time case is the line that SETS the all-time PB: the stats
// already hold this lap by then, so the gap must come from the PB cached
// before it (else +0.000). The PB-set line is told by lap identity, not by
// the time equalling the PB's: a lap that TIES the PB to the millisecond was
// read against the PB before it (-2.000 for +0.000), and a split equal to the
// PB's against the previous PB's sector. Cases 10-11: where the HUD shows no
// live gap it holds none -- a cut lap's line (the old Gap Bar held +0.000) and
// a split on the out-lap from the pits.
//
// THE PIT ENTRY. Case 12: riding into the pit lane voids the lap, and the
// Timing panel already treated it so, but the gap kept reading on against the
// dead lap all the way down the pit lane. It now goes blank at the pit flag,
// stays blank through the exit, and the next line starts over.
// ============================================================================
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"
#include <cstdlib>   // std::abs, std::strtod
#include <filesystem>
#include <fstream>
#include <string>

static constexpr int RACE1 = 6;
static constexpr int SESSION_IN_PROGRESS = 16;
static constexpr int SESSION_PRE_START = 256;
static constexpr int CLASSIFICATION_GATE_HOLD = 0x20;

// The Lap Log's gap row: its one signed reading ("-5.000" or "-0:05.000"; the
// empty history rows' "-" placeholders are not). Returns how many such
// rows were drawn; `shownMs` is the last one's value.
static int signedRows(PluginHost& host, const char* hud, int& shownMs) {
    int signedRows = 0;
    for (const auto& row : host.hudStringRows(hud)) {
        const std::string& t = row.text;
        if (t.size() < 2 || (t[0] != '+' && t[0] != '-') || t[1] < '0' || t[1] > '9') continue;
        const size_t colon = t.find(':');
        const double secs = colon == std::string::npos
            ? std::strtod(t.c_str() + 1, nullptr)
            : std::strtod(t.c_str() + 1, nullptr) * 60.0 + std::strtod(t.c_str() + colon + 1, nullptr);
        shownMs = static_cast<int>(secs * 1000.0 + 0.5) * (t[0] == '-' ? -1 : 1);
        ++signedRows;
    }
    return signedRows;
}
static int lapLogGapRows(PluginHost& host, int& shownMs) { return signedRows(host, "lap_log_hud", shownMs); }

TEST_CASE("live gap to PB: reference from the first PB lap, gap against the timer's clock") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\pb_gap\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");   // the player: the display rider whose timer runs

    int gap = 0;
    CHECK_FALSE(host.liveGapToPb(gap));    // nothing to compare against yet

    // --- Lap 1: the reference. S/F crossing (0.95 -> 0.05) anchors the timer and
    // marks the lap start as observed; each split re-anchors at its official time.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, /*lapNum=*/1, /*splitIndex=*/0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });      // sampled at ~20000 ms
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });      // sampled at ~40000 ms
    CHECK_FALSE(host.liveGapToPb(gap));    // still no reference: the lap has not completed

    // The line, wrap first: the standings now say lap 1 is done, so the timer
    // re-anchors on the wrap sample at 0.02 -- a sample of the NEW lap at ~0 ms.
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceLap(RACE1, 10, /*lapNum=*/1, 60000, /*best=*/1, 20000, 40000);   // lapNum is 1-based here
    host.raceTrackPosition({ { 10, 0.02f } });   // the wrap closes the line window (no gap is shown between the two ends of a lap)
    CHECK_MESSAGE(host.liveGapToPb(gap) == false,
                  "the wrap sample at 0.02 leaked into the committed reference (gap " << gap << ")");

    // --- Lap 2: 5 s slower at S1 ...
    host.raceSplit(RACE1, 10, 2, 0, 25000);
    host.raceTrackPosition({ { 10, 0.40f } });
    REQUIRE(host.liveGapToPb(gap));
    CHECK_MESSAGE((gap >= 4000 && gap <= 7000), "expected ~+5000 behind at 0.40, got " << gap);

    // ... and 5 s faster at S2.
    host.raceSplit(RACE1, 10, 2, 1, 35000);
    host.raceTrackPosition({ { 10, 0.70f } });
    REQUIRE(host.liveGapToPb(gap));
    CHECK_MESSAGE((gap <= -3000 && gap >= -7000), "expected ~-5000 ahead at 0.70, got " << gap);

    // A slower lap 2 leaves the reference alone: lap 3 still measures against lap 1.
    host.raceLap(RACE1, 10, 2, 61000, /*best=*/0, 25000, 35000);
    host.raceTrackPosition({ { 10, 0.02f } });   // the wrap closes the line window (no gap is shown between the two ends of a lap)
    host.raceSplit(RACE1, 10, 3, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    REQUIRE(host.liveGapToPb(gap));
    CHECK_MESSAGE((gap >= -1500 && gap <= 1500), "expected ~0 on the reference pace, got " << gap);
}

TEST_CASE("live gap to PB: a grid start's opening lap is never the reference") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\pb_gap_grid\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0, SESSION_PRE_START);
    host.addEntry(10, "Alice");
    host.raceSessionState(RACE1, SESSION_IN_PROGRESS);                 // arms the gate-drop watch
    host.classify(RACE1, 0, { { .num = 10, .laps = 0 } }, CLASSIFICATION_GATE_HOLD);
    host.classify(RACE1, 0, { { .num = 10, .laps = 0 } }, SESSION_IN_PROGRESS);   // the gate drops: t0

    int gap = 0;
    // The grid run to the line, then lap 1 measured from the gate: S1 at 24 s
    // carries the 4 s grid run the official split includes.
    host.raceTrackPosition({ { 10, 0.96f } });
    host.raceTrackPosition({ { 10, 0.98f } });
    host.raceTrackPosition({ { 10, 0.02f } });      // first S/F: the timer keeps the gate anchor
    host.raceSplit(RACE1, 10, 1, 0, 24000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 44000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 64000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 64000, /*best=*/1, 24000, 44000);
    host.raceTrackPosition({ { 10, 0.02f } });   // the wrap closes the line window (no gap is shown between the two ends of a lap)
    CHECK_MESSAGE(host.liveGapToPb(gap) == false,
                  "the gate-measured opening lap became the reference (gap " << gap << ")");

    // Lap 2, measured from the line, is the first usable reference.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 2, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    CHECK_FALSE(host.liveGapToPb(gap));
    host.raceSplit(RACE1, 10, 2, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 124000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });   // the wrap closes the line window (no gap is shown between the two ends of a lap)
    REQUIRE(host.liveGapToPb(gap));

    // Lap 3 on lap 2's pace reads ~0 -- not the -4 s a gate-measured reference gives.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 3, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    REQUIRE(host.liveGapToPb(gap));
    CHECK_MESSAGE((gap >= -1500 && gap <= 1500), "expected ~0 on the reference pace, got " << gap);
}

TEST_CASE("live gap to PB: leaving the pits reads nothing until the next line, from RunInit on") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\pb_gap_pit\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // A reference lap, then a lap under way with a valid read.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.raceTrackPosition({ { 10, 0.88f } });      // the reference needs samples where the
    host.raceTrackPosition({ { 10, 0.90f } });      // pit exit will be read (~40 s here)
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceTrackPosition({ { 10, 0.05f } });      // a slot the reference covers: valid
    int gap = 0;
    REQUIRE(host.liveGapToPb(gap));

    // Into the pits a few metres past the line, and back out most of a lap on --
    // the game's order, with NO classification yet. The old anchor is paused at
    // ~0 s; the reference at 0.88 is ~40 s, so a stale read is ~-40 s.
    host.runStop();
    host.runDeinit();
    host.runInit(RACE1);
    host.runStart();
    host.raceTrackPosition({ { 10, 0.88f } });
    CHECK_MESSAGE(host.liveGapToPb(gap) == false,
                  "the pit-exit sample read the dead lap's anchor (gap " << gap << ")");
    host.raceTrackPosition({ { 10, 0.90f } });
    CHECK_FALSE(host.liveGapToPb(gap));

    // The line re-anchors and the game reports the out-lap as a 0 ms RaceLap (the
    // capture's order): the partial run counts for nothing, and the next full lap
    // reads against the reference.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceLap(RACE1, 10, 2, 0, /*best=*/0, 0, 0);
    host.raceTrackPosition({ { 10, 0.03f } });
    host.raceSplit(RACE1, 10, 2, 0, 21000);
    host.raceTrackPosition({ { 10, 0.40f } });
    REQUIRE(host.liveGapToPb(gap));
    CHECK_MESSAGE((gap >= 0 && gap <= 2500), "expected ~+1000 behind at 0.40, got " << gap);
}

TEST_CASE("live gap to PB: re-entering at a pit box behind the exit point is not a line crossing") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\pb_gap_pitbox\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // A reference lap with samples where the re-entry will be read.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    int gap = 0;
    REQUIRE(host.liveGapToPb(gap));

    // Most of a lap on, into the pits from the menu (no classification pit flag
    // ever comes), and back out at a box just past the line: a jump of -0.68 from
    // the last sample, which is NOT a lap. Nothing may read until the real line.
    host.raceSplit(RACE1, 10, 2, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceTrackPosition({ { 10, 0.70f } });
    host.runStop();
    host.runDeinit();
    host.runInit(RACE1);
    host.runStart();
    host.raceTrackPosition({ { 10, 0.02f } });
    CHECK_FALSE(host.liveGapToPb(gap));
    host.raceTrackPosition({ { 10, 0.05f } });
    CHECK_FALSE(host.liveGapToPb(gap));
    host.raceTrackPosition({ { 10, 0.40f } });
    CHECK_MESSAGE(host.liveGapToPb(gap) == false,
                  "the re-entry at the pit box was read as an S/F crossing (gap " << gap << ")");

    // The real line anchors the timer, the game reports the out-lap as a 0 ms
    // RaceLap, and the next lap reads against the reference.
    host.raceTrackPosition({ { 10, 0.70f } });
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceLap(RACE1, 10, 2, 0, /*best=*/0, 0, 0);
    host.raceTrackPosition({ { 10, 0.03f } });
    host.raceSplit(RACE1, 10, 2, 0, 21000);
    host.raceTrackPosition({ { 10, 0.40f } });
    REQUIRE(host.liveGapToPb(gap));
    CHECK_MESSAGE((gap >= 0 && gap <= 2500), "expected ~+1000 behind at 0.40, got " << gap);
}

TEST_CASE("live gap to PB: a cut lap is not the Last Lap reference") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\pb_gap_cut\\");

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // Lap 1, valid: the Last Lap reference.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);

    // Lap 2, cut: 15 s faster and flagged invalid, its time kept as a race does.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 2, 0, 15000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 2, 1, 30000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 105000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 45000, /*best=*/0, 15000, 30000, /*invalid=*/true);

    // Lap 3 on lap 1's pace reads ~0 against the Last Lap: the cut lap did not
    // become the reference (against it, 0.40 would read +5 s behind).
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 3, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    int gap = 0;
    REQUIRE(host.liveGapRef(/*last lap*/ 2, gap));
    CHECK_MESSAGE((gap >= -1500 && gap <= 1500),
                  "the cut lap became the Last Lap reference (gap " << gap << ")");
}

TEST_CASE("live gap to PB: the Lap Log's gap row reads its own Reference, not the Gap Bar's") {
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\pb_gap_laplog\\";
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup(saveWin);
    {
        std::filesystem::create_directories("Z:\\tmp\\mxbmrp3-tests\\pb_gap_laplog\\mxbmrp3");
        // Lap Log on, measuring against the last lap; the Gap Bar keeps its default.
        std::ofstream ini("Z:\\tmp\\mxbmrp3-tests\\pb_gap_laplog\\mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[LapLogHud]\nvisible=1\nreference=2\n";
    }
    host.loadSettings(saveWin);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // Lap 1, the session PB: 20 s at 0.40. Lap 2, the last lap: 25 s at 0.40.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 2, 0, 25000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 2, 1, 45000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 125000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 65000, /*best=*/0, 25000, 45000);

    // Lap 3 on lap 1's pace: ~0 to the session PB, ~-5 s to the last lap.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 3, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(/*on track*/ 0);

    int sessionGap = 0, lastGap = 0;
    REQUIRE(host.liveGapRef(/*session PB*/ 0, sessionGap));
    REQUIRE(host.liveGapRef(/*last lap*/ 2, lastGap));
    REQUIRE(std::abs(lastGap - sessionGap) >= 3000);   // the two references differ

    int shownMs = 0;
    const int signedRows = lapLogGapRows(host, shownMs);
    REQUIRE_MESSAGE(signedRows == 1, "expected one gap row on the Lap Log, found " << signedRows);
    CHECK_MESSAGE(std::abs(shownMs - lastGap) <= 500,
                  "the gap row shows " << shownMs << " ms; last lap " << lastGap
                  << ", session PB " << sessionGap << " (the Gap Bar's reference)");
    host.shutdown();
}

// Three laps: 60 s (S1 20 s, S2 40 s; the PB), 65 s (S1 25 s), then lap 3
// crosses S1 at 23.456 s: +3.456 to the session PB, -1.544 to the last lap.
// The rider is then put at 0.70, where the PB lap was at 40 s, so the live
// reading (~-16.5 s) is nowhere near either frozen one.
static void rideToLap3S1(PluginHost& host, const char* name, const std::string& iniBody) {
    const std::string saveWin = std::string("Z:\\tmp\\mxbmrp3-tests\\") + name + "\\";
    host.startup(saveWin.c_str());
    {
        const std::string dir = saveWin + "mxbmrp3";
        std::filesystem::create_directories(dir);
        std::ofstream ini(dir + "\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n" << iniBody;
    }
    host.loadSettings(saveWin.c_str());

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(/*on track*/ 0);
    host.raceSplit(RACE1, 10, 2, 0, 25000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 2, 1, 45000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 125000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 65000, /*best=*/0, 25000, 45000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);

    host.raceTrackPosition({ { 10, 0.40f } });   // read live on the way to S1
    host.drawWithState(0);
    host.raceSplit(RACE1, 10, 3, 0, 23456);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
}

TEST_CASE("freeze: the Lap Log's gap row holds the official split gap against its Reference, then goes live") {
    SUBCASE("Session PB: +3.456 until the freeze runs out") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        rideToLap3S1(host, "freeze_ll_session", "[LapLogHud]\nvisible=1\nreference=0\nfreezeDuration=1000\n");
        int shownMs = 0;
        REQUIRE(lapLogGapRows(host, shownMs) == 1);
        CHECK_MESSAGE(shownMs == 3456, "the frozen row shows " << shownMs << " ms, not the official +3456");

        Sleep(1100);
        host.drawWithState(0);
        REQUIRE(lapLogGapRows(host, shownMs) == 1);
        int liveGap = 0;
        REQUIRE(host.liveGapRef(/*session PB*/ 0, liveGap));
        CHECK_MESSAGE(std::abs(shownMs - liveGap) <= 500,
                      "after the freeze the row shows " << shownMs << " ms; live " << liveGap);
        host.shutdown();
    }

    SUBCASE("Last lap: -1.544, not the session PB's +3.456") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        rideToLap3S1(host, "freeze_ll_last", "[LapLogHud]\nvisible=1\nreference=2\nfreezeDuration=3000\n");
        int shownMs = 0;
        REQUIRE(lapLogGapRows(host, shownMs) == 1);
        CHECK_MESSAGE(shownMs == -1544, "the frozen row shows " << shownMs << " ms, not the last-lap -1544");
        host.shutdown();
    }

    SUBCASE("Off: the row stays live") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        rideToLap3S1(host, "freeze_ll_off", "[LapLogHud]\nvisible=1\nfreezeDuration=0\n");
        int shownMs = 0, liveGap = 0;
        REQUIRE(lapLogGapRows(host, shownMs) == 1);
        REQUIRE(host.liveGapRef(0, liveGap));
        CHECK(std::abs(shownMs - liveGap) <= 500);
        CHECK(shownMs != 3456);
        host.shutdown();
    }
}

TEST_CASE("freeze: the Gap Bar holds the official split gap against its own Reference") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    // Gap Bar on Last lap; the Lap Log (on its default Session PB) shows the two are independent.
    rideToLap3S1(host, "freeze_gb_last",
                 "[GapBarHud]\nvisible=1\nreference=2\nfreezeDuration=3000\n\n[LapLogHud]\nvisible=1\n");
    int shownMs = 0;
    REQUIRE(signedRows(host, "gap_bar_hud", shownMs) == 1);
    CHECK_MESSAGE(shownMs == -1544, "the frozen Gap Bar shows " << shownMs << " ms, not the last-lap -1544");
    REQUIRE(lapLogGapRows(host, shownMs) == 1);
    CHECK_MESSAGE(shownMs == 3456, "the Lap Log (Session PB) shows " << shownMs << " ms, not +3456");
    host.shutdown();
}

TEST_CASE("freeze: the line that sets the all-time PB is held against the previous one, not +0.000") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\freeze_alltime\\";
    std::filesystem::remove_all("Z:\\tmp\\mxbmrp3-tests\\freeze_alltime");
    host.startup(saveWin);
    {
        std::filesystem::create_directories("Z:\\tmp\\mxbmrp3-tests\\freeze_alltime\\mxbmrp3");
        std::ofstream ini("Z:\\tmp\\mxbmrp3-tests\\freeze_alltime\\mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[LapLogHud]\nvisible=1\nreference=1\nfreezeDuration=3000\n";
    }
    host.loadSettings(saveWin);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // Lap 1: 60 s, the first all-time PB. Lap 2: 58 s, the new one.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(/*on track*/ 0);
    host.raceSplit(RACE1, 10, 2, 0, 19000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(0);
    host.raceSplit(RACE1, 10, 2, 1, 38000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
    host.classify(RACE1, 118000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 58000, /*best=*/1, 19000, 38000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);

    int shownMs = 0;
    REQUIRE(lapLogGapRows(host, shownMs) == 1);
    CHECK_MESSAGE(shownMs == -2000, "the line that set the all-time PB shows " << shownMs
                  << " ms, not -2000 against the PB it beat");
    host.shutdown();
}

TEST_CASE("freeze: a lap that ties the session PB to the millisecond is held at +0.000, not against the PB before it") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\freeze_tie\\";
    std::filesystem::remove_all("Z:\\tmp\\mxbmrp3-tests\\freeze_tie");
    host.startup(saveWin);
    {
        std::filesystem::create_directories("Z:\\tmp\\mxbmrp3-tests\\freeze_tie\\mxbmrp3");
        std::ofstream ini("Z:\\tmp\\mxbmrp3-tests\\freeze_tie\\mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[LapLogHud]\nvisible=1\nreference=0\nfreezeDuration=3000\n";
    }
    host.loadSettings(saveWin);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // Lap 1: 62 s. Lap 2: 60 s, the session PB (previous best 62 s). Lap 3: 60 s
    // again, S1 and the line to the millisecond -- not a new PB, and the game
    // does not flag it as one.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 21000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 42000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 62000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 62000, /*best=*/1, 21000, 42000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);
    host.raceSplit(RACE1, 10, 2, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(0);
    host.raceSplit(RACE1, 10, 2, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
    host.classify(RACE1, 122000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);

    int shownMs = 0;
    REQUIRE(lapLogGapRows(host, shownMs) == 1);
    CHECK_MESSAGE(shownMs == -2000, "the line that set the PB shows " << shownMs << " ms, not -2000");

    // Lap 3's S1 equals the PB's: 0, not -1000 against lap 1's S1.
    host.raceSplit(RACE1, 10, 3, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(0);
    REQUIRE(lapLogGapRows(host, shownMs) == 1);
    CHECK_MESSAGE(shownMs == 0, "a split equal to the PB's shows " << shownMs << " ms, not 0");

    host.raceSplit(RACE1, 10, 3, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
    host.classify(RACE1, 182000, { { .num = 10, .laps = 3, .gap = 0 } });
    host.raceLap(RACE1, 10, 3, 60000, /*best=*/0, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);
    REQUIRE(lapLogGapRows(host, shownMs) == 1);
    CHECK_MESSAGE(shownMs == 0, "a lap that ties the PB shows " << shownMs << " ms at the line, not -2000 against the PB before it");
    host.shutdown();
}

// Shows exactly what the live reading says: the live gap when there is one,
// nothing signed when there is none -- i.e. nothing is held.
static void checkShowsLive(PluginHost& host, const char* hud, int ref, const char* what) {
    int shownMs = 0, liveGap = 0;
    const int rows = signedRows(host, hud, shownMs);
    if (host.liveGapRef(ref, liveGap)) {
        REQUIRE(rows == 1);
        CHECK_MESSAGE(std::abs(shownMs - liveGap) <= 500,
                      std::string(what) << ": " << std::string(hud) << " shows " << shownMs << " ms; live " << liveGap);
    } else {
        CHECK_MESSAGE(rows == 0, std::string(what) << ": " << std::string(hud) << " holds " << shownMs << " ms with no live reading");
    }
}

TEST_CASE("freeze: an invalid lap holds nothing at the line; the next lap starts over") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\freeze_cut\\";
    host.startup(saveWin);
    {
        std::filesystem::create_directories("Z:\\tmp\\mxbmrp3-tests\\freeze_cut\\mxbmrp3");
        std::ofstream ini("Z:\\tmp\\mxbmrp3-tests\\freeze_cut\\mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[GapBarHud]\nvisible=1\nfreezeDuration=3000\n\n"
               "[LapLogHud]\nvisible=1\nfreezeDuration=3000\n";
    }
    host.loadSettings(saveWin);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // Lap 1: the PB.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);

    // Lap 2, read live all the way round, then cut: flagged invalid at the line.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceSplit(RACE1, 10, 2, 0, 21000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(/*on track*/ 0);
    host.raceSplit(RACE1, 10, 2, 1, 42000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
    host.classify(RACE1, 122000, { { .num = 10, .laps = 2, .gap = 0 } });
    host.raceLap(RACE1, 10, 2, 62000, /*best=*/0, 21000, 42000, /*invalid=*/true);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.drawWithState(0);

    // The old Gap Bar held +0.000 here for its freeze; now both read live.
    checkShowsLive(host, "gap_bar_hud", 0, "at the line of a cut lap");
    checkShowsLive(host, "lap_log_hud", 0, "at the line of a cut lap");
    host.shutdown();
}

TEST_CASE("freeze: the out-lap from the pits shows no gap at its splits, live or held") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\freeze_pit\\";
    host.startup(saveWin);
    {
        std::filesystem::create_directories("Z:\\tmp\\mxbmrp3-tests\\freeze_pit\\mxbmrp3");
        std::ofstream ini("Z:\\tmp\\mxbmrp3-tests\\freeze_pit\\mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[GapBarHud]\nvisible=1\nfreezeDuration=3000\n\n"
               "[LapLogHud]\nvisible=1\nfreezeDuration=3000\n";
    }
    host.loadSettings(saveWin);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // A PB lap, then a few metres into the next and into the pits.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.drawWithState(/*on track*/ 0);

    // Out of the pits at 0.30, then across S2 at 0.40: no lap under way, so the
    // HUDs show no gap and hold none.
    host.runStop();
    host.runDeinit();
    host.runInit(RACE1);
    host.runStart();
    host.raceTrackPosition({ { 10, 0.30f } });
    host.drawWithState(0);
    host.raceSplit(RACE1, 10, 2, 1, 95000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(0);

    // The split re-anchored the lap clock, but the lap it times began before the
    // pit visit: no gap until the line, live or held.
    int gap = 0;
    CHECK_FALSE(host.liveGapRef(0, gap));
    int shownMs = 0;
    CHECK_MESSAGE(signedRows(host, "gap_bar_hud", shownMs) == 0, "the Gap Bar shows " << shownMs << " ms on the out-lap");
    CHECK_MESSAGE(lapLogGapRows(host, shownMs) == 0, "the Lap Log shows " << shownMs << " ms on the out-lap");
    host.shutdown();
}

TEST_CASE("live gap to PB: riding into the pits blanks the gap until the next line") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* saveWin = "Z:\\tmp\\mxbmrp3-tests\\pb_gap_pit_entry\\";
    host.startup(saveWin);
    {
        std::filesystem::create_directories("Z:\\tmp\\mxbmrp3-tests\\pb_gap_pit_entry\\mxbmrp3");
        std::ofstream ini("Z:\\tmp\\mxbmrp3-tests\\pb_gap_pit_entry\\mxbmrp3\\mxbmrp3_settings.ini", std::ios::trunc);
        REQUIRE(ini.is_open());
        ini << "[Settings]\nversion=6\n\n[GapBarHud]\nvisible=1\nfreezeDuration=0\n\n"
               "[LapLogHud]\nvisible=1\nfreezeDuration=0\n";
    }
    host.loadSettings(saveWin);

    host.eventInit("TestTrack", "Alice");
    host.raceEvent("TestTrack");
    host.session(RACE1, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(10, "Alice");
    host.runInit(RACE1);
    host.runStart();

    // A PB lap, then a lap under way with a gap on screen.
    host.raceTrackPosition({ { 10, 0.95f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceSplit(RACE1, 10, 1, 0, 20000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.raceSplit(RACE1, 10, 1, 1, 40000);
    host.raceTrackPosition({ { 10, 0.70f } });
    host.raceTrackPosition({ { 10, 0.88f } });
    host.classify(RACE1, 60000, { { .num = 10, .laps = 1, .gap = 0 } });
    host.raceLap(RACE1, 10, 1, 60000, /*best=*/1, 20000, 40000);
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceTrackPosition({ { 10, 0.05f } });
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(0);
    int gap = 0;
    REQUIRE(host.liveGapToPb(gap));
    int shownMs = 0;
    REQUIRE(signedRows(host, "gap_bar_hud", shownMs) == 1);
    REQUIRE(lapLogGapRows(host, shownMs) == 1);

    // Into the pit lane: the classification's pit flag rises, the lap is void.
    host.classify(RACE1, 80000, { { .num = 10, .laps = 1, .gap = 0, .pit = 1 } });
    host.raceTrackPosition({ { 10, 0.70f } });
    host.drawWithState(0);
    CHECK_FALSE(host.liveGapToPb(gap));
    CHECK_MESSAGE(signedRows(host, "gap_bar_hud", shownMs) == 0, "the Gap Bar shows " << shownMs << " ms in the pits");
    CHECK_MESSAGE(lapLogGapRows(host, shownMs) == 0, "the Lap Log shows " << shownMs << " ms in the pits");

    // Back out without stopping: still the void lap, still blank.
    host.classify(RACE1, 90000, { { .num = 10, .laps = 1, .gap = 0, .pit = 0 } });
    host.raceTrackPosition({ { 10, 0.88f } });
    host.drawWithState(0);
    CHECK_FALSE(host.liveGapToPb(gap));
    CHECK(signedRows(host, "gap_bar_hud", shownMs) == 0);

    // The line closes the pit lap and the next one reads again.
    host.raceTrackPosition({ { 10, 0.02f } });
    host.raceLap(RACE1, 10, 2, 0, /*best=*/0, 0, 0, /*invalid=*/true);
    host.raceTrackPosition({ { 10, 0.03f } });
    host.raceSplit(RACE1, 10, 2, 0, 21000);
    host.raceTrackPosition({ { 10, 0.40f } });
    host.drawWithState(0);
    REQUIRE(host.liveGapToPb(gap));
    CHECK_MESSAGE((gap >= 0 && gap <= 2500), "expected ~+1000 behind at 0.40, got " << gap);
    host.shutdown();
}
