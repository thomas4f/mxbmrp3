// ============================================================================
// tests/integration/tests/timing_reference_test.cpp
// Timing HUD progressive reference selection. Each gap chip shows ONE value: the
// live +/- delta while frozen, otherwise the reference (target) TIME for the split
// the rider is driving toward — S1 while in sector 1, S1+S2 in sector 2, the whole
// lap when idle. The rendered chip text isn't in /api/state, so this drives the
// selection directly via the MXBMRP3_Test_Timing* hooks:
//   - cumulativeReferenceMs(type, split) must sum the reference's sectors correctly
//     (the progressive requirement), and
//   - currentTargetSplit() must track the lap timer's TRACK-POSITION sector, which is
//     correct from the first flying lap (the reported bug: the old CurrentLapData-split
//     path left the very first sector-1 stuck on the whole-lap reference).
// Self-contained doctest; see run_tests.sh.
// ============================================================================
#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"
#include "integration_main.h"
#include "plugin_host.h"

#include <cstdlib>   // std::abs(int)

namespace {
// GapTypeFlags (timing_hud.h): PB=1, IDEAL=2, OVERALL=4, ALLTIME=8, RECORD=16, LASTLAP=32.
constexpr int GAP_PB = 1, GAP_OVERALL = 4, GAP_ALLTIME = 8, GAP_LASTLAP = 32;
constexpr int SPLIT_LAP = -1;     // full-lap target
constexpr int RACE = 6;
}

TEST_CASE("timing reference: cumulative target sums the reference's sectors progressively") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_ref\\");

    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);   // Race
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();                               // spectate state
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);  // display rider = #4

    // One completed lap for #4: accumulated splits S1=30.000, S1+S2=61.000, lap=90.000
    // => sectors 30.000 / 31.000 / 29.000. best=2 marks it the overall best (populates the
    // Overall reference's sectors too). classify carries the standings best lap for Overall's
    // whole-lap scan.
    host.classify(RACE, 200000, { { .num = 4, .best = 90000, .laps = 1, .gap = 0 } });
    // lapNum=1: the handler stores it 0-indexed, and getBestLapEntry/getOverallBestLap only
    // accept entries with lapNum >= 0. best=2 marks the overall best (populates Overall's sectors).
    host.raceLap(RACE, 4, /*lapNum=*/1, /*lapTime=*/90000, /*best=*/2, /*split0=*/30000, /*split1=*/61000);

    // Progressive: heading to S1 -> S1 target; to S2 -> S1+S2; whole lap -> lap time.
    for (int gap : { GAP_PB, GAP_OVERALL, GAP_LASTLAP }) {
        CAPTURE(gap);
        CHECK(host.timingReferenceMs(gap, 0) == 30000);        // S1
        CHECK(host.timingReferenceMs(gap, 1) == 61000);        // S1+S2
        CHECK(host.timingReferenceMs(gap, SPLIT_LAP) == 90000); // whole lap
        // Strictly increasing as the target advances (a valid progressive reference).
        CHECK(host.timingReferenceMs(gap, 0) < host.timingReferenceMs(gap, 1));
        CHECK(host.timingReferenceMs(gap, 1) < host.timingReferenceMs(gap, SPLIT_LAP));
    }

    host.shutdown();
}

TEST_CASE("timing reference: live target tracks the lap-timer sector from the first lap") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_ref_live\\");

    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();                          // draw state 1 = spectate (no on-track pause gate)
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);  // display rider = #4
    host.classify(RACE, 200000, { { .num = 4, .best = 90000, .laps = 1, .gap = 0 } });
    host.raceLap(RACE, 4, /*lapNum=*/1, 90000, /*best=*/2, 30000, 61000);   // references to compare against

    // Idle: no lap timer running yet (no track position fed) -> full-lap target (-1). This is
    // the case the user confirmed already worked (sitting / out-lap shows the whole-lap record).
    CHECK(host.timingTargetSplit() == -1);
    CHECK(host.timingReferenceMs(GAP_PB, -999) == 90000);   // -999 = live sector -> full lap

    // Drive the track-position monitor across the S/F line (large negative wrap) to start the
    // FIRST flying lap. The monitor needs a prior sample, so feed one before the crossing; it
    // runs on the collect/Draw path, so pump draw() after each update. currentSector resets to
    // 0 (before S1) on the crossing.
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();  // prime the monitor
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();  // S/F wrap -> anchor, sector 0

    // First flying lap, sector 1: the live target must be S1 (0), NOT the whole lap. The old
    // CurrentLapData path returned -1 here because the split accumulators were still empty.
    CHECK(host.timingTargetSplit() == 0);
    CHECK(host.timingReferenceMs(GAP_PB, -999) == 30000);   // live -> S1 target

    // Cross S1 (RaceSplit, splitIndex 0) -> now in sector 2, heading to S2.
    host.raceSplit(RACE, 4, /*lapNum=*/0, /*splitIndex=*/0, /*splitTimeMs=*/31000);
    CHECK(host.timingTargetSplit() == 1);
    CHECK(host.timingReferenceMs(GAP_PB, -999) == 61000);   // live -> S1+S2 target

    host.shutdown();
}

TEST_CASE("timing live timer: resets to placeholder on pit exit until next S/F") {
    // The live elapsed time should read like a fresh track entry (placeholder) after leaving
    // the pits, not keep ticking the dead in-progress lap, until the next S/F crossing
    // re-anchors it. Elapsed is wall-clock, so assert the PLACEHOLDER (-1) vs RUNNING (>=0)
    // condition via MXBMRP3_Test_ElapsedLapTime, not an exact value.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_pit\\");

    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);   // display rider = #4
    host.classify(RACE, 200000, { { .num = 4, .laps = 1, .gap = 0, .pit = 0 } });   // on track

    // Before any S/F crossing there's no anchor -> placeholder (like first track entry).
    CHECK(host.elapsedLapTime() == -1);

    // Cross S/F (large negative wrap) to start the lap and anchor the timer.
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();  // prime the monitor
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();  // wrap -> anchor
    CHECK(host.elapsedLapTime() >= 0);    // timer running

    // Enter the pits (pit 0 -> 1): the lap is now dead but the timer keeps its anchor.
    host.classify(RACE, 205000, { { .num = 4, .laps = 1, .gap = 0, .pit = 1 } });
    CHECK(host.elapsedLapTime() >= 0);    // still anchored while in the pits

    // Leave the pits (pit 1 -> 0): the live timer must drop back to the placeholder.
    host.classify(RACE, 210000, { { .num = 4, .laps = 1, .gap = 0, .pit = 0 } });
    CHECK(host.elapsedLapTime() == -1);   // placeholder until the next S/F

    // Crossing S/F again re-anchors it (track monitoring was preserved, not reset).
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
    CHECK(host.elapsedLapTime() >= 0);

    host.shutdown();
}

TEST_CASE("timing INVALID: shown for a cut lap, suppressed for a pit out-lap") {
    // The time cell flashes "INVALID" when a genuinely timed lap is invalidated (e.g. a cut).
    // But a lap that passed through the pits isn't a timed lap - the live timer is dropped on
    // pit exit and re-anchors at the S/F crossing where the lap "completes" - so there's no
    // timing to invalidate and INVALID must NOT show. The rendered text isn't in /api/state,
    // so read the render predicate via MXBMRP3_Test_TimingInvalidShown.
    SUBCASE("timer running, cut lap -> INVALID shown") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_inv_cut\\");
        host.eventInit("TestTrack", "Thomas");
        host.raceEvent("TestTrack", /*type=*/2);
        host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(4, "Thomas");
        host.draw();
        host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);
        host.classify(RACE, 200000, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } });  // on track

        // Timer running the whole lap (anchored at S/F), then an invalid lap completes.
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();  // prime
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();  // S/F -> anchor
        host.raceLap(RACE, 4, /*lapNum=*/1, /*lapTime=*/90000, /*best=*/0,
                     /*split0=*/30000, /*split1=*/61000, /*invalid=*/true);
        CHECK(host.timingInvalidShown());   // genuinely timed lap, invalidated -> tell the player

        host.shutdown();
    }

    SUBCASE("pit in and out, invalid out-lap -> INVALID suppressed") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_inv_pit\\");
        host.eventInit("TestTrack", "Thomas");
        host.raceEvent("TestTrack", /*type=*/2);
        host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(4, "Thomas");
        host.draw();
        host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);
        host.classify(RACE, 200000, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } });  // on track

        // Anchor the timer, then explicitly enter and leave the pits (pit exit drops the anchor).
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
        host.classify(RACE, 205000, { { .num = 4, .laps = 0, .gap = 0, .pit = 1 } });  // enter pits
        host.classify(RACE, 210000, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } });  // leave pits

        // Crossing S/F completes the (invalid) out-lap. There's no timing to invalidate -> no INVALID.
        host.raceLap(RACE, 4, /*lapNum=*/1, /*lapTime=*/95000, /*best=*/0,
                     /*split0=*/32000, /*split1=*/64000, /*invalid=*/true);
        CHECK_FALSE(host.timingInvalidShown());   // pit out-lap -> just start counting the new lap

        // And a subsequent genuinely-timed cut lap (no pit) still flags INVALID: the flag was
        // consumed by the out-lap completion, so the fresh lap starts clean.
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
        host.raceLap(RACE, 4, /*lapNum=*/2, /*lapTime=*/91000, /*best=*/0,
                     /*split0=*/30500, /*split1=*/61500, /*invalid=*/true);
        CHECK(host.timingInvalidShown());

        host.shutdown();
    }
}

TEST_CASE("timing freeze: first flying lap after a garage/pit start still freezes") {
    // Regression: at the START of a session the rider sits in the garage/pit (pit==1) BEFORE
    // ever crossing S/F. That pre-lap pit sit must NOT mark the first genuine flying lap as
    // pit-interrupted. Unlike a mid-race pit, the out-lap FROM the garage produces no
    // lap-completion event, so a pit flag latched during the garage sit is never consumed - it
    // carries into the first real lap and suppresses its freeze (reported: "the Timing HUD
    // didn't freeze when I crossed S/F after my first valid lap"). The flag must only latch
    // while a lap is actually being timed (the lap timer is anchored). Freeze state isn't in
    // /api/state, so read MXBMRP3_Test_TimingFrozen.
    SUBCASE("valid first lap -> frozen") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_garage_valid\\");
        host.eventInit("TestTrack", "Thomas");
        host.raceEvent("TestTrack", /*type=*/2);
        host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(4, "Thomas");
        host.draw();
        host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);

        // Sit in the garage/pit BEFORE any S/F crossing (no lap-timer anchor yet), then ride out.
        host.classify(RACE, 200000, { { .num = 4, .laps = 0, .gap = 0, .pit = 1 } });  // in garage
        host.classify(RACE, 205000, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } });  // rode out

        // First S/F crossing anchors the lap timer (starts lap 1) - no lap completes here.
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();

        // Complete the first flying lap (valid) - it must FREEZE to hold the official time.
        host.raceLap(RACE, 4, /*lapNum=*/1, /*lapTime=*/90000, /*best=*/1,
                     /*split0=*/30000, /*split1=*/61000, /*invalid=*/false);
        CHECK(host.timingFrozen());              // was false before the fix (stale pit flag)
        CHECK_FALSE(host.timingInvalidShown());  // valid lap -> never INVALID

        host.shutdown();
    }

    SUBCASE("invalid first lap -> INVALID shown (garage sit didn't suppress it)") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_garage_invalid\\");
        host.eventInit("TestTrack", "Thomas");
        host.raceEvent("TestTrack", /*type=*/2);
        host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
        host.addEntry(4, "Thomas");
        host.draw();
        host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);

        host.classify(RACE, 200000, { { .num = 4, .laps = 0, .gap = 0, .pit = 1 } });  // in garage
        host.classify(RACE, 205000, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } });  // rode out
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();

        // A genuinely-timed first lap that was cut -> INVALID must show (the pre-lap garage sit
        // must not have marked it pit-interrupted, which would suppress both freeze and INVALID).
        host.raceLap(RACE, 4, /*lapNum=*/1, /*lapTime=*/90000, /*best=*/0,
                     /*split0=*/30000, /*split1=*/61000, /*invalid=*/true);
        CHECK(host.timingFrozen());
        CHECK(host.timingInvalidShown());

        host.shutdown();
    }
}

TEST_CASE("timing lap timer: grid (standing) start counts from the gate drop, not the first S/F") {
    // In a standing start the grid sits BEFORE the S/F line, and the official splits accumulate
    // from the race start (they include the grid->S/F run). The live timer used to anchor only
    // at the first S/F crossing, so it read ~0 there and then JUMPED forward by the grid->S/F
    // time when the first official split resynced it (reported).
    //
    // The green-flag STATE flip (PRE_START->IN_PROGRESS RaceSessionState) is NOT the start: after
    // it the race sits in a variable gate hold during which the CLASSIFICATION reports Complete
    // (0x20); the gate drop is the moment the classification flips to IN_PROGRESS (0x10) - that is
    // the true start (mirrors the recorded tape). Anchoring there makes the live time span the grid
    // run and match the splits, without counting the gate hold ("timer started too soon"). Elapsed
    // time is wall-clock, so assert the anchor STATE (grid-start grace / running vs placeholder).
    constexpr int PRE_START = 256, IN_PROGRESS = 16, CLS_HOLD = 32;  // 0x20 gate-hold classification state

    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_grid\\");
    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(RACE, /*numLaps=*/0, /*lengthMs=*/180000, /*state=*/PRE_START);  // timed race, PRE_START
    host.addEntry(4, "Thomas");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);          // display rider = #4
    host.classify(RACE, 30, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/PRE_START);

    // Green flag: PRE_START -> IN_PROGRESS RaceSessionState. Arms the gate-drop watch but does NOT
    // anchor yet - the gate hasn't dropped (would be "too soon" otherwise).
    host.raceSessionState(RACE, /*state=*/IN_PROGRESS);
    CHECK_FALSE(host.lapTimerFromRaceStart());
    CHECK(host.elapsedLapTime() == -1);

    // Gate hold: the classification reports Complete (0x20) with a counting-down gate clock. No anchor.
    host.classify(RACE, 9970, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/CLS_HOLD);
    host.classify(RACE, 8000, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/CLS_HOLD);
    CHECK_FALSE(host.lapTimerFromRaceStart());
    CHECK(host.elapsedLapTime() == -1);

    // Gate DROP: the classification flips to IN_PROGRESS (0x10) and the race clock starts. The
    // timer anchors here, BEFORE any S/F crossing - now running from the real start on the grid.
    host.classify(RACE, 179999, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/IN_PROGRESS);
    CHECK(host.lapTimerFromRaceStart());
    CHECK(host.elapsedLapTime() >= 0);   // running from the gate drop (was -1 before the fix)

    // Ride the grid -> S/F run. The first S/F crossing must NOT reset the anchor to 0 (that
    // would drop the grid->S/F time); the grid-start grace holds through lap 1.
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.95f } }); host.draw();  // on the grid (init monitor)
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.98f } }); host.draw();  // approaching S/F
    host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();  // crossed S/F #1 (wrap)
    CHECK(host.lapTimerFromRaceStart());   // still anchored from the race start (not reset to 0)
    CHECK(host.elapsedLapTime() >= 0);

    // Lap 1 completes -> the grace ends; lap 2 onward anchors normally at each S/F.
    host.raceLap(RACE, 4, /*lapNum=*/1, /*lapTime=*/95000, /*best=*/1,
                 /*split0=*/32000, /*split1=*/64000, /*invalid=*/false);
    CHECK_FALSE(host.lapTimerFromRaceStart());
    CHECK(host.elapsedLapTime() >= 0);     // lap 2 now anchored at 0

    host.shutdown();
}

TEST_CASE("grid-start grace: active from the green flag until the first split, off for pit starts") {
    // The standing-start grace suppresses the launch-shuffle false positives (the player's
    // wrong-way notice and the grid-crowd "hazard ahead") from the green flag, THROUGH the
    // variable gate hold and the launch, until the display rider clears the first split. It is
    // sector-based (no fixed duration) and covers races AND grid qualifying; pit starts never
    // enter it. Both consumers read the same PluginData::isInGridStartGrace predicate.
    constexpr int PRE_START = 256, IN_PROGRESS = 16, CLS_HOLD = 32;

    SUBCASE("grid start: on through gate hold + launch, off after S1") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\gridgrace\\");
        host.eventInit("TestTrack", "Thomas");
        host.raceEvent("TestTrack", /*type=*/2);
        host.session(RACE, /*numLaps=*/0, /*lengthMs=*/180000, /*state=*/PRE_START);
        host.addEntry(4, "Thomas");
        host.draw();
        host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);

        // Pre-start on the grid: watch not armed yet -> not in the grace.
        host.classify(RACE, 30, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/PRE_START);
        CHECK_FALSE(host.inGridStartGrace());

        // Green flag arms the watch: the gate hold is already inside the grace.
        host.raceSessionState(RACE, /*state=*/IN_PROGRESS);
        CHECK(host.inGridStartGrace());
        host.classify(RACE, 9970, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/CLS_HOLD);
        CHECK(host.inGridStartGrace());   // still awaiting the gate drop

        // Gate drop: anchored at the start, sector 0 -> still in grace through the launch.
        host.classify(RACE, 179999, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/IN_PROGRESS);
        CHECK(host.inGridStartGrace());

        // Grid -> S/F#1: the S/F crossing during the grace keeps sector 0 -> still in grace.
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.95f } }); host.draw();
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
        CHECK(host.inGridStartGrace());

        // Cross the first split (S1) -> the grace ends; wrong-way / hazards can show again.
        host.raceSplit(RACE, 4, /*lapNum=*/0, /*splitIndex=*/0, /*splitTimeMs=*/30000);
        CHECK_FALSE(host.inGridStartGrace());

        host.shutdown();
    }

    SUBCASE("pit start: never in the grace") {
        PluginHost host(dllPath());
        REQUIRE(host.loaded());
        host.startup("Z:\\tmp\\mxbmrp3-tests\\pitgrace\\");
        host.eventInit("TestTrack", "Thomas");
        host.raceEvent("TestTrack", /*type=*/2);
        // A pit-start session arrives already IN_PROGRESS (no PRE_START->IN_PROGRESS transition),
        // so the gate-drop watch is never armed.
        host.session(RACE, /*numLaps=*/0, /*lengthMs=*/300000, /*state=*/IN_PROGRESS);
        host.addEntry(4, "Thomas");
        host.draw();
        host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);
        host.classify(RACE, 299970, { { .num = 4, .laps = 0, .gap = 0, .pit = 0 } }, /*sessionState=*/IN_PROGRESS);
        CHECK_FALSE(host.inGridStartGrace());

        // Even after an S/F crossing anchors the lap timer, it is not a race-start anchor, so the
        // grace stays off.
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.92f } }); host.draw();
        host.raceTrackPosition({ { .num = 4, .trackPos = 0.03f } }); host.draw();
        CHECK_FALSE(host.inGridStartGrace());

        host.shutdown();
    }
}

TEST_CASE("timing panel: height is a whole number of grid bands") {
    // The Timing panel is a stack of grid-aligned bands -- the big time row occupies one
    // lineHeightLarge band and each comparison row one lineHeightNormal band -- inside the
    // same panelPaddingYCells every other panel has, top and bottom. Each comparison row
    // adds exactly one lineHeightNormal, so the panel lands on the vertical snap grid at
    // every row count. The rendered geometry isn'"'"'t in /api/state, so read it via
    // MXBMRP3_Test_TimingGeometry.
    //
    // This asserted `height == lineLarge` -- no padding at all -- which was true until
    // the panel took the standard padding and stopped being true without anyone
    // noticing. paddingV comes back from the same hook, so the expectation is written
    // from it rather than assumed away again.
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_pad\\");
    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" } }, /*curSelectionIndex=*/0);

    // The panel renders in the default ALWAYS mode (contentVisible() is unconditional there), so
    // it sizes from a placeholder time even without lap data — geometry doesn't need lap history.
    // Values from the hook are ×1e6 integer-quantized; allow a couple of units for the rounding
    // of the separately-quantized pieces vs. the quantized total.
    auto approxEq = [](int a, int b) { return std::abs(a - b) <= 3; };

    // The assertions are on the CONTENT STACK, not on `height == 2 * padding + rows`.
    // That form asked two questions at once — what a row costs, and what the panel's
    // chrome is — and only the first is this test's. It broke on the second when the
    // panel moved to the box model: the height became boxPanelPadding's while the
    // padding it was checked against was still the legacy panelPaddingYCells, and a
    // real row-height regression would have been indistinguishable from that. The
    // panel's own height also carries the box model's ceil-to-whole-cell remainder,
    // which no exact sum of rows can predict.
    SUBCASE("time only") {
        host.timingConfig(/*gapEnabled=*/false, /*primaryGap=*/0, /*secondaryMask=*/0);
        host.draw();
        PluginHost::TimingGeom g = host.timingGeometry();
        REQUIRE(g.height > 0);       // panel actually rendered (guards a vacuous all-zero pass)
        // Just the big time row. ONE NORMAL ROW, not a lineHeightLarge band: the big
        // time is drawn at the large font but its INK is about half that cell, so the
        // band reserved twice the height it showed. See BaseHud::bigValueRowHeight --
        // the same row the Position/Lap/Time/Clock values and the GapBar and Notices
        // boxes now use.
        CHECK(approxEq(g.contentBot - g.contentTop, g.lineNormal));
        // ...and it is a NORMAL row, not the large one it used to reserve.
        CHECK(g.lineLarge > g.lineNormal);
        CHECK(g.contentBot - g.contentTop < g.lineLarge);
        // The stack sits inside the panel, chrome above it and below it.
        CHECK(g.contentTop > 0);
        CHECK(g.contentBot <= g.height);
    }
    SUBCASE("time plus two comparison rows") {
        host.timingConfig(/*gapEnabled=*/false, /*primaryGap=*/0, /*secondaryMask=*/GAP_PB | GAP_ALLTIME);
        host.draw();
        PluginHost::TimingGeom g = host.timingGeometry();
        REQUIRE(g.height > 0);
        // Time band + two comparison bands -- three normal rows now that the time
        // band is one (see the time-only case above), plus the seam the box model
        // leaves between the two sibling section cards.
        const int stack = g.contentBot - g.contentTop;
        CHECK(stack >= 3 * g.lineNormal);
        // Each comparison row costs exactly ONE normal row: the two-row stack minus
        // the one-row stack is 2 * lineNormal and nothing else. The seam is constant
        // across both (one seam either way once there are two sections), so this is
        // the exact statement the old `2 * padding + 3 * rows` was reaching for.
        host.timingConfig(/*gapEnabled=*/false, /*primaryGap=*/0, /*secondaryMask=*/GAP_PB);
        host.draw();
        PluginHost::TimingGeom one = host.timingGeometry();
        CHECK(approxEq(stack - (one.contentBot - one.contentTop), g.lineNormal));
        // And rows extend the box DOWNWARD -- the panel grew with them.
        CHECK(g.height > one.height);
    }

    host.shutdown();
}

// A player-scoped comparison must go blank when the panel is showing someone else.
// All-Time PB comes from StatsManager, which only ever stores the LOCAL player's laps,
// and Record is fetched for the local player's own bike/class. Every other row reads the
// display rider's data, so the panel follows a spectate switch — which meant these two
// silently kept the player's reference and scored the SPECTATED rider's lap against it.
// On screen that is a gap that reads as if it were theirs; the reporter hit it as a green
// "all-time PB" notice next to a red Alltime row (the notice half is fixed separately, see
// pb_scope_test.cpp). Suppressed at the reference source, so this hook — the same
// cumulativeReferenceMs() the panel renders from — sees exactly what the panel shows.
TEST_CASE("timing reference: player-scoped rows blank out while spectating another rider") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_ref\\");

    host.eventInit("TestTrack", "Thomas");
    host.raceEvent("TestTrack", /*type=*/2);
    host.session(RACE, /*numLaps=*/10, /*lengthMs=*/0);
    host.addEntry(4, "Thomas");        // first active entry → the local player
    host.addEntry(7, "Rival");
    host.draw();
    host.spectateVehicles({ { 4, "Thomas" }, { 7, "Rival" } }, /*curSelectionIndex=*/0);

    // A completed lap for the player populates both the session PB and (via StatsManager
    // + cacheAllTimePB) the all-time reference.
    host.classify(RACE, 200000, { { .num = 4, .best = 90000, .laps = 1, .gap = 0 } });
    host.raceLap(RACE, 4, /*lapNum=*/1, /*lapTime=*/90000, /*best=*/2, /*split0=*/30000, /*split1=*/61000);

    // Watching yourself: the all-time reference is live at every split.
    REQUIRE(host.timingReferenceMs(GAP_ALLTIME, SPLIT_LAP) == 90000);
    CHECK(host.timingReferenceMs(GAP_ALLTIME, 0) == 30000);
    CHECK(host.timingReferenceMs(GAP_ALLTIME, 1) == 61000);

    // Switch the camera to #7. The all-time reference must go away at EVERY split (-1 =
    // unavailable, which the panel renders as "N/A" muted) rather than keep showing yours.
    host.spectateVehicles({ { 4, "Thomas" }, { 7, "Rival" } }, /*curSelectionIndex=*/1);
    CHECK(host.timingReferenceMs(GAP_ALLTIME, SPLIT_LAP) == -1);
    CHECK(host.timingReferenceMs(GAP_ALLTIME, 0) == -1);
    CHECK(host.timingReferenceMs(GAP_ALLTIME, 1) == -1);

    // Control: a display-rider-scoped row is NOT suppressed. #7 has their own lap, so
    // Session PB keeps working and reports THEIR time — proving the suppression is scoped
    // to the player-only references and didn't just blank the whole panel.
    host.classify(RACE, 260000, { { .num = 4, .best = 90000, .laps = 1, .gap = 0 },
                                  { .num = 7, .best = 95000, .laps = 1, .gap = 5000 } });
    host.raceLap(RACE, 7, /*lapNum=*/1, /*lapTime=*/95000, /*best=*/1, /*split0=*/32000, /*split1=*/64000);
    CHECK(host.timingReferenceMs(GAP_PB, SPLIT_LAP) == 95000);
    CHECK(host.timingReferenceMs(GAP_ALLTIME, SPLIT_LAP) == -1);   // still suppressed

    // Back to the player: the all-time reference returns (suppression is a view state, not
    // a one-way teardown of the cache).
    host.spectateVehicles({ { 4, "Thomas" }, { 7, "Rival" } }, /*curSelectionIndex=*/0);
    CHECK(host.timingReferenceMs(GAP_ALLTIME, SPLIT_LAP) == 90000);

    host.shutdown();
}

// ============================================================================
// THE READOUTS SECTION: a row each for the figures people otherwise run a whole
// widget to see, in a third section under the comparison rows.
//
// All OFF by default -- this panel sits mid-screen and is read at a glance, so
// extra rows are opt-in. Which makes the default state worth asserting too: a
// new section that quietly appeared for every existing user would be the bug.
//
// Every value is a READ of the source that already owns it (the session clock
// through formatSessionClock, fuel through FuelWidget's accumulated history), so
// what this pins is the wiring -- the rows appear, carry their labels, and the
// panel grows to hold them.
// ============================================================================
TEST_CASE("timing: the readout rows are off by default and appear when enabled") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    const char* save = "Z:\\tmp\\mxbmrp3-tests\\timing_readouts\\";
    host.startup(save);
    REQUIRE(host.hasStringRows());
    REQUIRE(host.hasScreenEdges());

    host.eventInit("TestTrack", "Player");
    host.raceEvent("TestTrack");
    host.addEntry(12, "Player");
    host.session(6, /*numLaps=*/5, /*lengthMs=*/480000);
    host.classify(6, 400000, { { .num = 12, .best = 108500, .laps = 1, .gap = 0 } });
    host.draw();

    auto labels = [&]() {
        std::string all;
        for (const auto& r : host.hudStringRows(PluginHost::HUD_TIMING)) all += "[" + r.text + "]";
        return all;
    };
    auto height = [&]() {
        const auto e = host.hudScreenEdges(PluginHost::HUD_TIMING);
        return e.b - e.t;
    };

    // Shipped defaults: the comparison rows are there, the readouts are not.
    const std::string plain = labels();
    const int plainH = height();
    INFO("default rows: " << plain);
    REQUIRE_MESSAGE(plainH > 0, "timing panel rendered nothing at defaults");
    CHECK(plain.find("[Pos]") == std::string::npos);
    CHECK(plain.find("[Fuel]") == std::string::npos);

    // Turn two on through the INI, the way the settings tab writes them.
    host.writeSettingsFile(save,
        "[Settings]\nversion=6\n\n[TimingHud]\nvisible=1\n"
        "readout_position=1\nreadout_lap=1\n");
    host.loadSettings(save);
    host.draw();

    const std::string withRows = labels();
    INFO("with readouts: " << withRows);
    CHECK(withRows.find("[Position]") != std::string::npos);
    CHECK(withRows.find("[Lap]") != std::string::npos);
    // ...and only the two that were asked for.
    CHECK(withRows.find("[Fuel]") == std::string::npos);
    CHECK(withRows.find("[Time]") == std::string::npos);
    // NOT "[Session]": the Session PB comparison row is on by default and owns
    // that word, which is why the readout is labelled "Format".
    CHECK(withRows.find("[Format]") == std::string::npos);
    // The panel grew to hold them rather than drawing over what was there.
    CHECK(height() > plainH);

    host.shutdown();
}

// ---------------------------------------------------------------------------
// THE TWO TEXT READOUTS: Server and Track.
//
// Every other readout has a value of bounded length -- a position, a lap count, a
// clock, a session format, a fuel figure. These two carry free text from the session,
// and this panel's width is not theirs to grow: it is pinned to the centre stack so it
// lines up with the Notices panel above it (wantCenterStackWidth). So they truncate,
// and the thing worth pinning is that they truncate rather than overrun the label
// beside them -- an overrun is invisible to a width assertion and obvious on screen.
//
// Both values are read from the sources the Session panel already prints, so the pair
// cannot disagree about what server you are on; that shared-source rule is what the
// name check below is really asserting.
// ---------------------------------------------------------------------------
TEST_CASE("timing: the server and track readouts fit the panel") {
    PluginHost host(dllPath());
    REQUIRE(host.loaded());
    host.startup("Z:\\tmp\\mxbmrp3-tests\\timing_text_readouts\\");
    REQUIRE_MESSAGE(host.timingReadouts(0), "MXBMRP3_Test_TimingReadouts not exported");
    host.showAllHuds(true);
    // A track name comfortably longer than the panel is wide, so truncation is
    // exercised rather than merely available.
    host.eventInit("Southwick National Motocross Circuit", "Thomas");
    host.session(1, 0, 0);
    host.runInit(1);
    host.addEntry(4, "Thomas");

    const unsigned READOUT_SERVER_BIT = 1u << 5;
    const unsigned READOUT_TRACK_BIT  = 1u << 6;
    REQUIRE(host.timingReadouts(READOUT_SERVER_BIT | READOUT_TRACK_BIT));
    host.draw();

    const auto rows = host.hudStringRows("timing_hud");
    REQUIRE_MESSAGE(!rows.empty(), "Timing drew nothing");

    auto labelled = [&](const char* label) {
        for (const auto& r : rows) if (r.text == label) return true;
        return false;
    };
    CHECK(labelled("Server"));
    CHECK(labelled("Track"));

    // THE VALUE FITS THE PANEL'S BUDGET.
    //
    // Asserted on the value's LENGTH against the budget the panel itself reports, not
    // on where the string sits. Two earlier versions of this check were structurally
    // incapable of failing and both passed against a deliberately broken build:
    //   - comparing the string's x against the panel edges. The values are RIGHT-
    //     justified, so x is the ANCHOR, which is inside the panel however long the
    //     text is; an overrun runs LEFT from a point that always passes.
    //   - looking for the whole track name in the output. The row's value buffer is 24
    //     bytes and truncates silently, so the name never appears in full even with the
    //     budget disabled, and the search found nothing either way.
    const int budget = host.timingTextBudget();
    REQUIRE_MESSAGE(budget >= 4, "MXBMRP3_Test_TimingTextBudget not exported");

    bool sawServer = false, sawTrack = false;
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        const bool isServer = (rows[i].text == "Server");
        const bool isTrack  = (rows[i].text == "Track");
        if (!isServer && !isTrack) continue;
        // The value is the next string emitted on that row (label then value).
        const std::string& value = rows[i + 1].text;
        INFO((isServer ? "Server" : "Track") << " value \"" << value << "\" is "
             << value.size() << " chars against a budget of " << budget);
        CHECK(static_cast<int>(value.size()) <= budget);
        sawServer = sawServer || isServer;
        sawTrack  = sawTrack  || isTrack;
    }
    CHECK(sawServer);
    CHECK(sawTrack);

    host.shutdown();
}
