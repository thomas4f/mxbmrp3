// ============================================================================
// tests/unit/test_achievement_feeds.cpp
// The pure rules behind the November achievement rows: Slalom (slalom.h),
// Leapfrog (leapfrog.h), Grounded and Flat Out (lap_purity.h), Feather Touch
// (feather_touch.h), and the four read off the final classification - Rush
// Hour, Fall From Grace, Photocopy and Nemesis (race_finish_extras.h).
//
// test_plugin_utils.cpp provides the doctest impl + main; this TU only
// registers more tests.
// ============================================================================
#include "doctest.h"

#include "core/feather_touch.h"
#include "core/lap_purity.h"
#include "core/leapfrog.h"
#include "core/race_finish_extras.h"
#include "core/slalom.h"

namespace {

Unified::TrackPositionData at(int raceNum, float x, float y, float z, bool crashed = false) {
    Unified::TrackPositionData p;
    p.raceNum = raceNum;
    p.posX = x;
    p.posY = y;
    p.posZ = z;
    p.crashed = crashed ? 1 : 0;
    return p;
}

}  // namespace

TEST_CASE("Slalom counts each fallen rider once per fall") {
    Slalom::Tracker t;
    Unified::TrackPositionData field[3] = { at(1, 0, 0, 0), at(7, 4, 0, 0, true), at(9, 30, 0, 0, true) };

    // Rider 7 is down within ten metres, rider 9 too far away.
    CHECK(t.update(field, 3, 0, true) == 1);
    // The same fall, the next fifty batches: no more credit.
    for (int i = 0; i < 50; ++i) CHECK(t.update(field, 3, 0, true) == 0);

    // Rider 7 gets up, then falls again: a new fall is a new rider to pass.
    field[1].crashed = 0;
    CHECK(t.update(field, 3, 0, true) == 0);
    CHECK_FALSE(t.holding());
    field[1].crashed = 1;
    CHECK(t.update(field, 3, 0, true) == 1);
}

TEST_CASE("Slalom gives nothing while the player is not riding past") {
    Slalom::Tracker t;
    Unified::TrackPositionData field[2] = { at(1, 0, 0, 0), at(7, 3, 0, 0, true) };
    // Too slow, or the player's own crash site: the caller says not eligible.
    CHECK(t.update(field, 2, 0, false) == 0);
    CHECK_FALSE(t.holding());
    // Riding again while the rider is still down: credited then.
    CHECK(t.update(field, 2, 0, true) == 1);
}

TEST_CASE("Slalom forgets a rider who leaves, so a reused number starts clean") {
    Slalom::Tracker t;
    Unified::TrackPositionData field[2] = { at(1, 0, 0, 0), at(7, 3, 0, 0, true) };
    CHECK(t.update(field, 2, 0, true) == 1);
    CHECK(t.update(field, 1, 0, true) == 0);   // rider 7 left the batch
    CHECK(t.update(field, 2, 0, true) == 1);   // a new #7, down: counts
}

TEST_CASE("Leapfrog counts riders cleared from above, once per flight") {
    Leapfrog::Tracker t;
    Unified::TrackPositionData field[4] = {
        at(1, 0, 3, 0),          // the player, three metres up
        at(4, 1, 0, 0),          // underneath
        at(5, 1, 2.5f, 0),       // alongside in the air: not cleared
        at(6, 6, 0, 0),          // too far sideways
    };
    CHECK(t.update(field, 4, 0) == 1);
    CHECK(t.update(field, 4, 0) == 0);   // still the same flight
    t.land();
    CHECK(t.update(field, 4, 0) == 1);   // the next jump over the same rider counts

    // A rider on the floor is not jumped over in this sense.
    Leapfrog::Tracker u;
    field[1].crashed = 1;
    CHECK(u.update(field, 4, 0) == 0);
}

TEST_CASE("Grounded and Flat Out ask for a clean lap, at any pace") {
    LapPurity::Lap lap;
    CHECK(LapPurity::flatOut(lap, true));
    CHECK(LapPurity::grounded(lap, true));
    CHECK_FALSE(LapPurity::flatOut(lap, false));    // invalid, crashed in, or unwatched
    CHECK_FALSE(LapPurity::grounded(lap, false));
    lap.braked = true;
    CHECK_FALSE(LapPurity::flatOut(lap, true));
    CHECK(LapPurity::grounded(lap, true));          // braking is Flat Out's business only
    lap.flew = true;
    CHECK_FALSE(LapPurity::grounded(lap, true));

    // Watched: the odometer saw most of the lap. A lap with no telemetry in
    // it has no brake in it either, and must not count for that.
    CHECK(LapPurity::watched(1600.0, 1600.0));
    CHECK(LapPurity::watched(1280.0, 1600.0));
    CHECK_FALSE(LapPurity::watched(1279.0, 1600.0));
    CHECK_FALSE(LapPurity::watched(0.0, 1600.0));
    CHECK_FALSE(LapPurity::watched(1600.0, 0.0));   // unknown track length
}

TEST_CASE("Feather Touch needs a 25m jump and a landing under 10G") {
    CHECK(FeatherTouch::soft(30.0f, 9.0f));
    CHECK(FeatherTouch::soft(25.0f, 9.9f));                // exactly the row's 25m
    CHECK_FALSE(FeatherTouch::soft(24.9f, 9.0f));          // not far enough
    CHECK_FALSE(FeatherTouch::soft(30.0f, FeatherTouch::MAX_G));
    CHECK_FALSE(FeatherTouch::soft(30.0f, 0.0f));          // no landing measured
}

TEST_CASE("Race finish extras read the classification") {
    using RaceFinishExtras::Rider;
    auto rider = [](int num, int state, int gapLaps, int best) {
        Rider r;
        r.raceNum = num;
        r.state = state;
        r.gapLaps = gapLaps;
        r.bestLapMs = best;
        return r;
    };
    const int RACING = RaceFinishExtras::STATE_RACING;
    const int DNS = RaceFinishExtras::STATE_DNS;
    const int RETIRED = 3;

    SUBCASE("a winner who lapped two riders, one of them twice") {
        const Rider field[4] = { rider(1, RACING, 0, 60000), rider(2, RACING, 1, 61000),
                                 rider(3, RACING, 2, 62000), rider(4, RETIRED, 3, 63000) };
        const auto f = RaceFinishExtras::of(field, 4, 0);
        CHECK(f.lapsPutOn == 3);           // the retired rider is not one you lapped
        CHECK_FALSE(f.lastOfFinishers);
        CHECK(f.aheadRaceNum == -1);
        CHECK(f.behindRaceNum == 2);
        CHECK_FALSE(f.bestLapTwin);
    }
    SUBCASE("last of the finishers, with a retirement and a DNS behind") {
        const Rider field[4] = { rider(1, RACING, 0, 60000), rider(2, RACING, 0, 61000),
                                 rider(3, RETIRED, 0, 62000), rider(4, DNS, 0, -1) };
        const auto f = RaceFinishExtras::of(field, 4, 1);
        CHECK(f.lastOfFinishers);
        CHECK(f.aheadRaceNum == 1);
        CHECK(f.behindRaceNum == -1);
    }
    SUBCASE("a best lap equal to another rider's, to the millisecond") {
        const Rider field[3] = { rider(1, RACING, 0, 60123), rider(2, RACING, 0, 61000),
                                 rider(3, RETIRED, 0, 60123) };
        CHECK(RaceFinishExtras::of(field, 3, 0).bestLapTwin);   // a retired rider's lap still happened
        const Rider none[2] = { rider(1, RACING, 0, -1), rider(2, RACING, 0, -1) };
        CHECK_FALSE(RaceFinishExtras::of(none, 2, 0).bestLapTwin);   // no lap is not a match
    }
    SUBCASE("alone in the race is not last") {
        const Rider field[1] = { rider(1, RACING, 0, 60000) };
        CHECK_FALSE(RaceFinishExtras::of(field, 1, 0).lastOfFinishers);
    }
}
