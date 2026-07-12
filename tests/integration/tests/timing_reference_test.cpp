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
    // The Timing panel is a stack of grid-aligned bands: the big time row occupies one
    // lineHeightLarge band and each comparison row one lineHeightNormal band, with no outer
    // padding. So a time-only panel is exactly lineHeightLarge tall (identical to the Notices
    // and Gap Bar boxes) and each comparison row adds exactly one lineHeightNormal — the whole
    // panel lands on the vertical snap grid at every row count. The rendered geometry isn't in
    // /api/state, so read it via MXBMRP3_Test_TimingGeometry.
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

    SUBCASE("time only") {
        host.timingConfig(/*gapEnabled=*/false, /*primaryGap=*/0, /*secondaryMask=*/0);
        host.draw();
        PluginHost::TimingGeom g = host.timingGeometry();
        REQUIRE(g.height > 0);       // panel actually rendered (guards a vacuous all-zero pass)
        // Just the big time row: one lineHeightLarge band.
        CHECK(approxEq(g.height, g.lineLarge));
    }
    SUBCASE("time plus two comparison rows") {
        host.timingConfig(/*gapEnabled=*/false, /*primaryGap=*/0, /*secondaryMask=*/GAP_PB | GAP_ALLTIME);
        host.draw();
        PluginHost::TimingGeom g = host.timingGeometry();
        REQUIRE(g.height > 0);
        // Time band + two comparison bands: height == lineHeightLarge + 2*lineHeightNormal.
        CHECK(approxEq(g.height, g.lineLarge + 2 * g.lineNormal));
        // And it is TALLER than the time-only panel by exactly the two rows
        // (a positive sanity check that rows extend the box downward).
        CHECK(g.height > g.lineLarge);
    }

    host.shutdown();
}
