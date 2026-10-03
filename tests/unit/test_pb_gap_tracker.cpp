// ============================================================================
// tests/unit/test_pb_gap_tracker.cpp
// The live gap-to-PB engine (core/pb_gap_tracker.h), formerly GapBarHud's own
// timing-point table and reachable only through the DLL under Wine. Pins:
//   - a PB lap whose start was observed becomes the reference; the gap at a
//     position is (elapsed now) - (elapsed the PB had there), sign: + = behind
//   - interpolation between the two enclosing samples, and the fallbacks when
//     one or both are missing (nearest earlier sample within 9 slots)
//   - THE LAP FENCE, in both callback orders: wrap-then-completion commits the
//     table set aside at the wrap, and the new lap's samples never touch it;
//     completion-then-wrap commits at the completion, and the strays taken at
//     0.99x before the wrap are discarded. A wrap the timer did not re-anchor
//     on records nothing until the completion brings the new lap's clock.
//     (The bug this pins: with samples taken on every position callback, the
//     wrap-first order overwrote the old lap's slot 20 with elapsed 0 a moment
//     before the completion committed it -- a red spike at every lap start.)
//     And the fence holds for the SAME order twice in a row: the completion
//     that closes a wrap-first ending leaves the next wrap nothing to shed.
//     (Told it had strays, that wrap wiped the whole lap and committed the
//     empty table -- four laps in a row in the real capture.)
//   - THE OBSERVED-START GATE: a lap sampled from mid-lap (join / spectate)
//     is never committed, however good its time -- its missing head would
//     read as "PB was here at 0 ms" and every gap would show a lap ahead
//   - THE GRID-START OPENING LAP is never committed: it is measured from the
//     gate, every later lap from the S/F line, so as a reference it would
//     offset the whole of lap 2 by the grid run -- and when it ends wrap-first
//     (the wrap before the RaceLap that ends the grace) lap 2 is still fenced
//     and committed whole
//   - THREE REFERENCES from one sampling: every committed lap is LAST_LAP, a
//     session PB is SESSION_PB, a lap the caller rates all-time best is
//     ALLTIME_PB; a table can be planted from outside (the persisted PB) and
//     read back; forgetBestLap() spares the all-time one, reset() does not
//   - a non-PB lap leaves the reference alone; forgetBestLap() drops the
//     reference but keeps sampling; clearCurrentLap() (pit exit) drops the
//     in-progress samples and keeps the reference; reset() drops everything
//   - bestLapProgressAt(): the ghost's position, interpolated, -1 without a
//     reference and clamped to the finish once the PB would be done
//   - non-finite positions are ignored (a NaN in the slot math is UB)
// Elapsed times are fed directly, so nothing here depends on the wall clock.
// ============================================================================
#include "doctest.h"
#include "core/pb_gap_tracker.h"
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

// The centre of slot i as a track position, so a float sample lands in slot i
// and not, by truncation, in i-1 (0.251f * 1000 is 250.99998).
float slotPos(int i) {
    return (static_cast<float>(i) + 0.5f) / static_cast<float>(PbGapTracker::NUM_POINTS);
}

// Drive slots [from, to) at constant pace with the lap's clock running: slot i at
// i * lapTimeMs / 1000. The first sample of a lap (slot 0 after a wrap) is fed
// with the timer re-anchored, as LapTimer reports it.
void driveLap(PbGapTracker& t, int lapTimeMs, int from = 0, int to = PbGapTracker::NUM_POINTS) {
    for (int i = from; i < to; ++i) {
        t.onTrackPosition(slotPos(i), i * lapTimeMs / PbGapTracker::NUM_POINTS, /*reanchored=*/i == 0);
    }
}

// What the game does at the first line crossing after joining mid-lap: the
// position wraps (the timer re-anchors) and the RaceLap for the partial lap
// arrives -- refused by the observed-start gate, but it closes the pending
// table so the lap now starting is the in-progress one.
void startFirstLap(PbGapTracker& t) {
    t.onTrackPosition(0.98f, 55000, false);       // approaching S/F, unobserved lap
    t.onTrackPosition(slotPos(0), 0, /*reanchored=*/true);
    t.onLapCompleted(0, false);
}

// A tracker holding a 60 s linear reference, built like a real lap: wrap, the
// lap driven, its completion committing it.
PbGapTracker withReference(int lapTimeMs = 60000) {
    PbGapTracker t;
    startFirstLap(t);
    driveLap(t, lapTimeMs, /*from=*/1);
    t.onLapCompleted(lapTimeMs, /*isPersonalBest=*/true);
    return t;
}

}  // namespace

TEST_CASE("no reference: nothing valid, nothing to draw") {
    PbGapTracker t;
    bool ok = true;
    CHECK_FALSE(t.hasBestLap());
    CHECK(t.gapAt(0.5f, 30000, &ok) == 0);
    CHECK_FALSE(ok);
    CHECK(t.bestLapProgressAt(30000) == doctest::Approx(-1.0f));
}

TEST_CASE("a PB lap with an observed start becomes the reference, and the gap has the right sign") {
    PbGapTracker t = withReference(60000);
    REQUIRE(t.hasBestLap());
    CHECK(t.bestLapTimeMs() == 60000);

    bool ok = false;
    // Halfway round, the PB was at 30000: a rider there at 31000 is 1s behind...
    CHECK(t.gapAt(0.5f, 31000, &ok) == 1000);
    CHECK(ok);
    // ...and at 29000 1s ahead.
    CHECK(t.gapAt(0.5f, 29000) == -1000);
    // Exactly on the PB's pace reads zero.
    CHECK(t.gapAt(0.25f, 15000) == 0);
}

TEST_CASE("the gap interpolates between the two enclosing samples") {
    PbGapTracker t = withReference(60000);
    // Slot 500 = 30000 ms, slot 501 = 30060 ms; 0.5005 is halfway between them
    // (within a millisecond: the position is a float and the slot math truncates).
    CHECK(std::abs(t.gapAt(0.5005f, 30030)) <= 1);
    CHECK(std::abs(t.gapAt(0.5005f, 30130) - 100) <= 1);
}

TEST_CASE("missing samples: one side is used directly, a hole walks back up to 9 slots") {
    PbGapTracker t;
    startFirstLap(t);
    // A sparse lap: only every 10th slot sampled.
    for (int i = 10; i < PbGapTracker::NUM_POINTS; i += 10) {
        t.onTrackPosition(slotPos(i), i * 60, false);   // 60 ms per slot -> 60 s lap
    }
    t.onLapCompleted(60000, true);
    REQUIRE(t.hasBestLap());

    bool ok = false;
    // 0.500 is a sampled slot (500); 0.501 is not -> lower side only.
    CHECK(t.gapAt(0.5f, 30000, &ok) == 0);
    CHECK(ok);
    CHECK(t.gapAt(0.5005f, 30000, &ok) == 0);   // lower valid (500), upper (501) not
    CHECK(ok);
    // 0.505: slots 505/506 both empty; walks back to 500 (30000).
    CHECK(t.gapAt(0.505f, 30000, &ok) == 0);
    CHECK(ok);

    // A hole wider than the walk-back: a reference with a 30-slot gap.
    PbGapTracker sparse;
    startFirstLap(sparse);
    sparse.onTrackPosition(slotPos(100), 6000, false);
    sparse.onTrackPosition(slotPos(130), 7800, false);
    sparse.onLapCompleted(60000, true);
    ok = true;
    CHECK(sparse.gapAt(0.115f, 6900, &ok) == 0);   // slots 115/116 empty, nearest earlier is 100 (15 back)
    CHECK_FALSE(ok);
}

TEST_CASE("THE LAP FENCE: wrap before completion commits the lap that wrapped, untouched") {
    PbGapTracker t = withReference(60000);
    driveLap(t, 58000);                          // a faster lap, up to slot 999
    // The position wraps before the RaceLap arrives, and the timer re-anchors on
    // that sample: it belongs to the NEW lap at elapsed 0. Before the fence this
    // overwrote the old lap's slot 20 a moment before the commit.
    t.onTrackPosition(slotPos(20), 0, /*reanchored=*/true);
    t.onTrackPosition(slotPos(21), 60, false);
    t.onLapCompleted(58000, true);
    REQUIRE(t.bestLapTimeMs() == 58000);
    CHECK(std::abs(t.gapAt(slotPos(20), 1189)) <= 1);   // slot 20 still holds the 58 s lap's 1160 ms (1189 halfway to slot 21), not 0

    // The samples taken after the wrap went to the new lap, which can itself
    // become the reference in full.
    driveLap(t, 57000, /*from=*/22, /*to=*/PbGapTracker::NUM_POINTS);
    t.onLapCompleted(57000, true);
    CHECK(t.bestLapTimeMs() == 57000);
    CHECK(t.gapAt(slotPos(20), 30) == 0);        // the post-wrap samples (slots 20/21) are part of it
    CHECK(t.gapAt(0.5f, 28500) == 0);
}

TEST_CASE("THE LAP FENCE: a wrap whose completion never comes goes stale, and the lap after it still commits") {
    PbGapTracker t;
    // Joined mid-lap; the first crossing wraps (timer re-anchors) but the game
    // sends no RaceLap for the partial lap. The full lap that follows must be the
    // reference, not the stale partial table, and not nothing.
    t.onTrackPosition(0.98f, 55000, false);
    driveLap(t, 60000);                          // slot 0 is that wrap
    t.onLapCompleted(60000, true);
    REQUIRE(t.hasBestLap());
    CHECK(t.gapAt(0.5f, 30000) == 0);
}

TEST_CASE("THE LAP FENCE: a wrap whose completion never comes stops blanking the reads once stale") {
    // A first crossing the game never reports (a pit-out lap's, in the real
    // capture that first drove this) opened the line window at the wrap and
    // nothing ever closed it: every read of the whole lap was blank. The window
    // goes stale with the pending table it guards.
    PbGapTracker t = withReference(60000);
    driveLap(t, 58000, 0, 998);
    t.onTrackPosition(slotPos(20), 0, /*reanchored=*/true);   // the wrap; no RaceLap follows
    CHECK(t.atLine());
    driveLap(t, 58000, 21, PbGapTracker::STALE_PENDING_SLOTS - 1);
    CHECK(t.atLine());                                        // still within the window
    driveLap(t, 58000, PbGapTracker::STALE_PENDING_SLOTS - 1, 100);
    CHECK_FALSE(t.atLine());                                  // stale: the lap reads again
    bool ok = false;
    t.gapAt(slotPos(80), 4640, &ok);
    CHECK(ok);
}

TEST_CASE("THE LAP FENCE: a wrap the timer did not re-anchor on, whose completion never comes, stays blank rather than a lap ahead") {
    // The staleness close above must not fire while the window still waits for
    // its clock: the elapsed fed here is the old lap's (~58 s and counting), so
    // an open read at slot 80 would be that against the reference's 4.8 s --
    // "+53 s" on the bar for the whole lap. Blank is the right answer until a
    // clock arrives, and the wrap that re-anchors brings one.
    PbGapTracker t = withReference(60000);
    driveLap(t, 58000);
    t.onTrackPosition(slotPos(20), 58100, /*reanchored=*/false);   // the wrap; no RaceLap follows
    for (int i = 21; i < 100; ++i) t.onTrackPosition(slotPos(i), 58100 + i * 58, false);
    CHECK(t.atLine());                                              // stale by distance, but no clock
    for (int i = 100; i < 1000; ++i) t.onTrackPosition(slotPos(i), 58100 + i * 58, false);
    CHECK(t.atLine());
    t.onTrackPosition(slotPos(0), 0, /*reanchored=*/true);          // the next wrap re-anchors
    t.onLapCompleted(58000, false);                                 // ...and its RaceLap lands
    CHECK_FALSE(t.atLine());
    driveLap(t, 57000, /*from=*/1);
    t.onLapCompleted(57000, true);
    REQUIRE(t.bestLapTimeMs() == 57000);                            // the lap after it commits whole
    bool ok = false;
    CHECK(t.gapAt(0.5f, 28500, &ok) == 0);
    CHECK(ok);
}

TEST_CASE("THE LAP FENCE: a wrap the timer did not re-anchor on records nothing until the completion") {
    PbGapTracker t = withReference(60000);
    driveLap(t, 58000);
    // Wrap with the timer still on the old lap's clock (anchor valid, lap number
    // unchanged): the sample reads ~58 s at slot 20 and must not be recorded.
    t.onTrackPosition(slotPos(20), 58100, /*reanchored=*/false);
    CHECK(t.atLine());                           // ...and the reader must show no gap meanwhile
    t.onTrackPosition(slotPos(21), 58160, false);
    t.onLapCompleted(58000, true);               // re-anchors: the new lap has its clock now
    CHECK_FALSE(t.atLine());
    CHECK(t.bestLapTimeMs() == 58000);
    driveLap(t, 57000, /*from=*/22);
    t.onLapCompleted(57000, true);
    REQUIRE(t.bestLapTimeMs() == 57000);
    bool ok = true;
    t.gapAt(slotPos(20), 1000, &ok);             // slots 20/21 empty, and nothing earlier: no gap, not 58 s
    CHECK_FALSE(ok);
}

TEST_CASE("THE LAP FENCE: completion before wrap discards the strays taken at 0.99x") {
    PbGapTracker t = withReference(60000);
    driveLap(t, 58000, 0, 998);                  // up to slot 997
    t.onLapCompleted(58000, true);               // RaceLap first; the timer re-anchors at 0
    REQUIRE(t.bestLapTimeMs() == 58000);
    // The position has not wrapped yet: two strays of the ended lap at the new clock,
    // and a read here would be a whole lap ahead -- the line window is open.
    CHECK(t.atLine());
    t.onTrackPosition(slotPos(998), 5, false);
    t.onTrackPosition(slotPos(999), 40, false);
    CHECK(t.atLine());
    // Now the wrap; the timer, already anchored for this lap, does not re-anchor.
    t.onTrackPosition(slotPos(0), 80, false);
    CHECK_FALSE(t.atLine());
    driveLap(t, 57000, /*from=*/1, /*to=*/998); // the next lap skips 998/999 on its way to the line
    t.onLapCompleted(57000, true);
    REQUIRE(t.bestLapTimeMs() == 57000);
    // Without the fence slot 998 would hold 5 ms and read as a 57 s spike here.
    bool ok = true;
    const int gap = t.gapAt(slotPos(998), 56886, &ok);
    CHECK(ok);                                   // walked back to slot 997 (56829)
    CHECK(std::abs(gap - 57) <= 1);
}

TEST_CASE("THE OBSERVED-START GATE: a lap sampled from mid-lap is never the reference") {
    PbGapTracker t;
    // Joined at 0.4 with the timer already running: samples from 0.4 on only,
    // no wrap seen.
    driveLap(t, 60000, /*from=*/400);
    CHECK_FALSE(t.currentLapObserved());
    t.onLapCompleted(60000, /*isPersonalBest=*/true);
    CHECK_FALSE(t.hasBestLap());          // a PB, but not one we saw start

    // The completion IS an observed start for the next lap, which can be committed.
    CHECK(t.currentLapObserved());
    driveLap(t, 59000);
    t.onLapCompleted(59000, true);
    CHECK(t.hasBestLap());
    CHECK(t.bestLapTimeMs() == 59000);
}

TEST_CASE("THE GRID-START OPENING LAP is never the reference; lap 2 is") {
    PbGapTracker t;
    t.onGridStart();                              // green flag: the clock runs from the gate
    CHECK_FALSE(t.currentLapObserved());
    // The grid run: positions just before the line, 4 s of it.
    t.onTrackPosition(slotPos(960), 1000, false);
    t.onTrackPosition(slotPos(980), 3000, false);
    // The first S/F crossing: LapTimer keeps the gate anchor (no re-anchor), so the
    // lap's samples carry the grid run -- and the wrap must not fence a lap here.
    t.onTrackPosition(slotPos(0), 4000, /*reanchored=*/false);
    for (int i = 1; i < PbGapTracker::NUM_POINTS; ++i) {
        t.onTrackPosition(slotPos(i), 4000 + i * 60, false);
    }
    t.onLapCompleted(64000, /*isPersonalBest=*/true);   // the official lap 1: gate to line
    CHECK_FALSE(t.hasBestLap());

    // Lap 2 is measured from the line and is the first usable reference.
    CHECK(t.currentLapObserved());
    driveLap(t, 60000, /*from=*/1);              // slot 0 was the completion's sample
    t.onLapCompleted(60000, true);
    REQUIRE(t.hasBestLap());
    CHECK(t.gapAt(0.5f, 30000) == 0);            // no 4 s grid-run offset
}

TEST_CASE("THE GRID-START OPENING LAP ending wrap-first: lap 2 is still fenced and committed whole") {
    // The real capture in pb_gap_golden_test does this at 184.70 s: the wrap
    // 15 ms before the RaceLap that ends the grace. LapTimer's grace holds the
    // gate anchor through that wrap too, so the samples in between are on the old
    // clock; the completion is the grid lap's whole fence -- its fill discards
    // them -- and the wrap has to leave it reading as at-the-line. It did not:
    // the staleness counter stayed at ~999, the completion read as
    // completion-first, the line window stayed open for all of lap 2, and lap
    // 2's own wrap shed that lap as strays, committing an EMPTY table.
    PbGapTracker t;
    t.onGridStart();
    t.onTrackPosition(slotPos(960), 1000, false);
    t.onTrackPosition(slotPos(980), 3000, false);
    t.onTrackPosition(slotPos(0), 4000, /*reanchored=*/false);   // the grid run reaches the line
    for (int i = 1; i < 999; ++i) t.onTrackPosition(slotPos(i), 4000 + i * 60, false);
    t.onTrackPosition(slotPos(0), 64000, /*reanchored=*/false);  // lap 1 ends: the wrap, on the old clock
    t.onLapCompleted(64000, /*isPersonalBest=*/true);          // ...then the RaceLap ends the grace
    CHECK_FALSE(t.hasBestLap());
    CHECK_FALSE(t.atLine());                                   // the completion closed the line
    CHECK(t.currentLapObserved());

    driveLap(t, 60000, /*from=*/1, /*to=*/998);                 // lap 2, on the line's clock
    t.onTrackPosition(slotPos(20), 0, /*reanchored=*/true);    // ends wrap-first as well
    t.onLapCompleted(60000, true);
    REQUIRE(t.hasBestLap());
    bool ok = false;
    CHECK(t.gapAt(0.5f, 30000, &ok) == 0);
    CHECK(ok);                                                 // a table with samples in it, not an empty one
}

TEST_CASE("a non-PB lap leaves the reference alone") {
    PbGapTracker t = withReference(60000);
    driveLap(t, 65000);
    t.onLapCompleted(65000, /*isPersonalBest=*/false);
    CHECK(t.bestLapTimeMs() == 60000);
    CHECK(t.gapAt(0.5f, 30000) == 0);     // still the 60 s lap's pace
}

TEST_CASE("forget / clear / reset drop exactly what they say") {
    SUBCASE("forgetBestLap keeps sampling the lap in progress") {
        PbGapTracker t = withReference(60000);
        driveLap(t, 58000);                // a better lap under way
        t.forgetBestLap();
        CHECK_FALSE(t.hasBestLap());
        t.onLapCompleted(58000, true);     // ...which becomes the new reference intact
        REQUIRE(t.hasBestLap());
        CHECK(t.gapAt(0.5f, 29000) == 0);
    }
    SUBCASE("clearCurrentLap (pit exit) keeps the reference, drops the dead lap's samples") {
        PbGapTracker t = withReference(60000);
        driveLap(t, 50000, 0, 500);        // half a lap that will die in the pits
        t.clearCurrentLap();
        CHECK(t.hasBestLap());
        CHECK_FALSE(t.currentLapObserved());       // the run from the pits to the line is a partial
        // Rejoins and crosses the line: a full lap at 59 s becomes the reference,
        // and the dead lap's first half is not in it.
        t.onTrackPosition(0.98f, -1, false);           // unanchored after pit exit: not recorded
        driveLap(t, 59000);
        t.onLapCompleted(59000, true);
        REQUIRE(t.bestLapTimeMs() == 59000);
        CHECK(t.gapAt(0.25f, 14750) == 0); // 59 s pace, not the dead lap's 50 s
    }
    SUBCASE("reset drops everything, including the observed start") {
        PbGapTracker t = withReference(60000);
        t.reset();
        CHECK_FALSE(t.hasBestLap());
        CHECK_FALSE(t.currentLapObserved());
        CHECK(t.trackPos() == doctest::Approx(0.0f));
        // No wrap is inferred against a position from before the reset.
        t.onTrackPosition(0.02f, 0, true);
        CHECK(t.trackPos() == doctest::Approx(0.02f));
    }
}

TEST_CASE("THREE REFERENCES: last lap always, session PB when told, all-time PB when told") {
    using Ref = PbGapTracker::Ref;
    PbGapTracker t = withReference(60000);
    // The one committed lap is the session PB and the last lap; nothing all-time yet.
    CHECK(t.hasBestLap(Ref::SESSION_PB));
    CHECK(t.hasBestLap(Ref::LAST_LAP));
    CHECK_FALSE(t.hasBestLap(Ref::ALLTIME_PB));
    CHECK(t.bestLapTimeMs(Ref::LAST_LAP) == 60000);

    // A slower lap moves LAST_LAP and leaves the session PB alone.
    driveLap(t, 62000);
    CHECK(t.onLapCompleted(62000, /*isPersonalBest=*/false));
    CHECK(t.bestLapTimeMs(Ref::SESSION_PB) == 60000);
    CHECK(t.bestLapTimeMs(Ref::LAST_LAP) == 62000);
    bool ok = false;
    CHECK(t.gapAt(0.5f, 31000, &ok, Ref::LAST_LAP) == 0);   // on the last lap's pace
    CHECK(ok);
    CHECK(t.gapAt(0.5f, 31000, &ok, Ref::SESSION_PB) == 1000);   // 1 s behind the PB's
    CHECK(t.gapAt(0.5f, 31000, &ok, Ref::ALLTIME_PB) == 0);
    CHECK_FALSE(ok);                                          // no all-time reference to read

    // A lap the caller rates as the all-time best becomes all three.
    driveLap(t, 59000);
    CHECK(t.onLapCompleted(59000, /*isPersonalBest=*/true, /*isAllTimeBest=*/true));
    CHECK(t.bestLapTimeMs(Ref::SESSION_PB) == 59000);
    CHECK(t.bestLapTimeMs(Ref::ALLTIME_PB) == 59000);
    CHECK(t.bestLapTimeMs(Ref::LAST_LAP) == 59000);
    CHECK(t.bestLapProgressAt(29500, Ref::ALLTIME_PB) == doctest::Approx(0.5f).epsilon(0.002));

    // The all-time flag without the session flag commits all-time and last only
    // (PluginData never sends that pairing, but the tracker does what it is told).
    driveLap(t, 58000);
    t.onLapCompleted(58000, false, true);
    CHECK(t.bestLapTimeMs(Ref::SESSION_PB) == 59000);
    CHECK(t.bestLapTimeMs(Ref::ALLTIME_PB) == 58000);

    SUBCASE("forgetBestLap drops the session's two, not the all-time one") {
        t.forgetBestLap();
        CHECK_FALSE(t.hasBestLap(Ref::SESSION_PB));
        CHECK_FALSE(t.hasBestLap(Ref::LAST_LAP));
        CHECK(t.hasBestLap(Ref::ALLTIME_PB));
    }
    SUBCASE("reset drops all three") {
        t.reset();
        CHECK_FALSE(t.hasBestLap(Ref::SESSION_PB));
        CHECK_FALSE(t.hasBestLap(Ref::LAST_LAP));
        CHECK_FALSE(t.hasBestLap(Ref::ALLTIME_PB));
    }
    SUBCASE("a planted table is read like a committed one, and read back intact") {
        PbGapTracker fresh;
        fresh.setReference(Ref::ALLTIME_PB, t.referenceTable(Ref::ALLTIME_PB), t.bestLapTimeMs(Ref::ALLTIME_PB));
        CHECK(fresh.hasBestLap(Ref::ALLTIME_PB));
        CHECK_FALSE(fresh.hasBestLap(Ref::SESSION_PB));
        // Reads need a clock: a lap has to be under way.
        startFirstLap(fresh);
        fresh.onTrackPosition(0.5f, 29000, false);
        CHECK(fresh.gapAt(0.5f, 29000, &ok, Ref::ALLTIME_PB) == 0);   // 58 s pace
        CHECK(ok);
        // A zero lap time plants nothing.
        fresh.setReference(Ref::ALLTIME_PB, t.referenceTable(Ref::ALLTIME_PB), 0);
        CHECK_FALSE(fresh.hasBestLap(Ref::ALLTIME_PB));
    }
    SUBCASE("a planted reference is not read during a grid-start opening lap") {
        PbGapTracker grid;
        grid.setReference(Ref::ALLTIME_PB, t.referenceTable(Ref::ALLTIME_PB), t.bestLapTimeMs(Ref::ALLTIME_PB));
        grid.onGridStart();
        grid.onTrackPosition(0.02f, 4000, false);      // the grid run reaching the line: 4 s on the gate clock
        grid.onTrackPosition(0.5f, 33000, false);      // half a lap later, still on the gate clock
        CHECK(grid.gapAt(0.5f, 33000, &ok, Ref::ALLTIME_PB) == 0);
        CHECK_FALSE(ok);                               // not "+4 s behind": no read on the gate-measured lap
        CHECK(grid.bestLapProgressAt(33000, Ref::ALLTIME_PB) == doctest::Approx(-1.0f));
        // Lap 2 runs from the line and reads normally.
        grid.onLapCompleted(64000, false);
        grid.onTrackPosition(0.5f, 29000, false);
        CHECK(grid.gapAt(0.5f, 29000, &ok, Ref::ALLTIME_PB) == 0);
        CHECK(ok);
    }
    SUBCASE("an unobserved lap commits nothing, and says so") {
        PbGapTracker mid;
        mid.onTrackPosition(0.5f, 30000, false);   // joined mid-lap: no observed start
        driveLap(mid, 60000, 501);
        CHECK_FALSE(mid.onLapCompleted(60000, true, true));
        CHECK_FALSE(mid.hasBestLap(Ref::LAST_LAP));
        CHECK_FALSE(mid.hasBestLap(Ref::ALLTIME_PB));
    }
}

TEST_CASE("bestLapProgressAt places the ghost where the PB lap was at that time") {
    PbGapTracker t = withReference(60000);
    CHECK(t.bestLapProgressAt(30000) == doctest::Approx(0.5f).epsilon(0.002));
    CHECK(t.bestLapProgressAt(30030) == doctest::Approx(0.5005f).epsilon(0.002));   // interpolated
    CHECK(t.bestLapProgressAt(0) == doctest::Approx(0.0f));
    CHECK(t.bestLapProgressAt(70000) == doctest::Approx(1.0f));   // the PB already finished: clamp
}

TEST_CASE("track position is remembered from samples, clamped, and non-finite input is ignored") {
    PbGapTracker t = withReference(60000);
    t.onTrackPosition(0.25f, 1000, false);
    CHECK(t.trackPos() == doctest::Approx(0.25f));
    t.onTrackPosition(1.7f, 2000, false);
    CHECK(t.trackPos() == doctest::Approx(1.0f));
    t.onTrackPosition(-0.2f, 3000, false);
    CHECK(t.trackPos() == doctest::Approx(0.0f));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    t.onTrackPosition(nan, 4000, false);
    t.onTrackPosition(inf, 4000, false);
    CHECK(t.trackPos() == doctest::Approx(0.0f));         // unchanged
    bool ok = true;
    CHECK(t.gapAt(nan, 4000, &ok) == 0);
    CHECK_FALSE(ok);
    CHECK(t.gapAt(0.5f, 30000) == 0);                     // the reference is intact
}

// Every way a lap can end, driven the same way: the ended lap must be the
// reference intact, and the lap after it must be measurable in full. The two
// explicit fence cases above explain the mechanism; this sweep is what stops a
// third ordering (or a fourth flag combination) from going untested.
TEST_CASE("THE LAP FENCE holds for every order of wrap and completion, re-anchored or not") {
    enum class Order { WrapFirst, CompletionFirst };
    for (Order order : { Order::WrapFirst, Order::CompletionFirst }) {
        for (bool reanchoredAtWrap : { true, false }) {
            INFO("order=" << (order == Order::WrapFirst ? "wrap-first" : "completion-first")
                          << " reanchoredAtWrap=" << reanchoredAtWrap);
            PbGapTracker t = withReference(60000);
            driveLap(t, 58000, 0, 998);              // the lap that will end, up to slot 997

            // The line. Completion-first re-anchors at the completion; a wrap that
            // follows never re-anchors again, so its flag is only meaningful wrap-first.
            if (order == Order::WrapFirst) {
                t.onTrackPosition(slotPos(20), reanchoredAtWrap ? 0 : 58100, reanchoredAtWrap);
                t.onLapCompleted(58000, true);
            } else {
                t.onLapCompleted(58000, true);
                t.onTrackPosition(slotPos(998), 5, false);       // a stray of the ended lap
                t.onTrackPosition(slotPos(20), 1200, false);     // the wrap, on the new clock
            }
            REQUIRE(t.bestLapTimeMs() == 58000);
            // The ended lap is the reference, and its start was not overwritten.
            // WITH ok: an empty table also answers 0, with ok=false.
            bool ok = false;
            CHECK(t.gapAt(0.5f, 29000, &ok) == 0);
            CHECK(ok);
            CHECK(std::abs(t.gapAt(slotPos(20), 1189)) <= 1);
            // Nothing that happened at the line put a bogus sample in it.
            ok = true;
            t.gapAt(slotPos(998), 56886, &ok);
            CHECK(ok);                               // 997 is the nearest, 998/999 were never written

            // The next lap, on the new clock, is measurable in full and commits in
            // full -- ending in the SAME order, so the second fence follows the
            // first. Two wrap-first endings in a row once wiped this lap: the
            // completion that closed the first left "strays to shed" set, and the
            // next wrap shed the whole lap, then committed the empty table.
            driveLap(t, 57000, /*from=*/21, /*to=*/998);
            if (order == Order::WrapFirst) {
                t.onTrackPosition(slotPos(20), 0, /*reanchored=*/true);
                t.onLapCompleted(57000, true);
            } else {
                t.onLapCompleted(57000, true);
                t.onTrackPosition(slotPos(20), 1200, false);
            }
            REQUIRE(t.bestLapTimeMs() == 57000);
            ok = false;
            CHECK(t.gapAt(0.5f, 28500, &ok) == 0);
            CHECK(ok);
            CHECK(t.bestLapProgressAt(28500) == doctest::Approx(0.5f).epsilon(0.01));
        }
    }
}
