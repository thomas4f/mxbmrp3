// ============================================================================
// tests/unit/test_ghost_ride.cpp
// Unit tests for core/ghost_ride.h - when a bike counts as riding on without
// its rider, and how long a run of it lasts.
//
// The frames below are read off the tape the rule was cut from (10 Nov 2026,
// 47 crashes on purpose): the bike-on-its-side case is what every ordinary
// crash looks like, and it reports both tyres in contact - so an upright test
// is what stops "both wheels down" from firing on every crash. The one real
// ghost ride in that tape rolled upright for about 1.5 s, under the row's
// three seconds, which the last case pins.
// ============================================================================
#include "doctest.h"

#include "core/ghost_ride.h"

#include <algorithm>

using namespace GhostRide;

TEST_CASE("an upright bike rolling on both wheels after the rider came off") {
    // Crash 47, 485.8 s: roll -17.6, pitch -3.5, 5.5 m/s, both tyres down.
    CHECK(rolling(true, true, true, -17.6f, -3.5f, 5.5f));
}

TEST_CASE("the rider on the bike is not a ghost ride") {
    CHECK_FALSE(rolling(false, true, true, 0.0f, 0.0f, 20.0f));
}

TEST_CASE("a bike sliding on its side reads both tyres down and is still not one") {
    // Crash 47, 488.06 s: roll 75.8, both in contact. And the ordinary crash.
    CHECK_FALSE(rolling(true, true, true, 75.8f, 4.0f, 3.5f));
    CHECK_FALSE(rolling(true, true, true, -96.0f, 0.0f, 6.6f));
}

TEST_CASE("one wheel down, mid loop-out, or settling to a stop") {
    CHECK_FALSE(rolling(true, true, false, 0.0f, 0.0f, 8.0f));   // landing on the front
    CHECK_FALSE(rolling(true, false, true, 0.0f, 0.0f, 8.0f));   // landing on the rear
    CHECK_FALSE(rolling(true, true, true, 5.0f, -59.4f, 7.1f));  // nose high, over the back
    CHECK_FALSE(rolling(true, true, true, 5.0f, 0.0f, 2.5f));    // coming to rest upright
}

TEST_CASE("a run adds up the time spent rolling") {
    Run r;
    for (int i = 0; i < 100; ++i) r.step(true, true, 0.01);
    CHECK(r.seconds() == doctest::Approx(1.0));
}

TEST_CASE("a skipped wheel inside the grace holds the run, a longer break ends it") {
    Run r;
    for (int i = 0; i < 100; ++i) r.step(true, true, 0.01);
    for (int i = 0; i < 20; ++i) r.step(true, false, 0.01);    // 0.2 s off a bump
    CHECK(r.seconds() == doctest::Approx(1.0));                // held, not counted
    for (int i = 0; i < 100; ++i) r.step(true, true, 0.01);
    CHECK(r.seconds() == doctest::Approx(2.0));
    for (int i = 0; i < 30; ++i) r.step(true, false, 0.01);    // 0.3 s: fell over
    CHECK(r.seconds() == doctest::Approx(0.0));
}

TEST_CASE("the rider being put back ends the run at once") {
    Run r;
    for (int i = 0; i < 100; ++i) r.step(true, true, 0.01);
    CHECK(r.step(false, false, 0.01) == doctest::Approx(0.0));
}

TEST_CASE("the recorded ghost ride falls short of Endo's three seconds") {
    // Crash 47 as the rule saw it: ~1.6 s rolling upright, then on its side.
    Run r;
    double best = 0.0;
    for (int i = 0; i < 160; ++i) best = std::max(best, r.step(true, true, 0.01));
    for (int i = 0; i < 50; ++i) best = std::max(best, r.step(true, false, 0.01));
    CHECK(best == doctest::Approx(1.6));
    CHECK(best < 3.0);
}
