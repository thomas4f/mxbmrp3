// ============================================================================
// tests/unit/test_split_crossing.cpp
// The pure halves of the crossing machinery the Timing panel, the Pitboard and
// the gap freezes share (hud/split_crossing.h, hud/hold_timer.h):
//   - timeToCrossing: a reference's time to the same crossing. Split 3 (index 2)
//     exists only in GP Bikes, which is why it is pinned here: the integration
//     build is MX Bikes (two splits) and cannot reach it, and the Pitboard used
//     to have no third split at all.
//   - HoldTimer: up for exactly its duration, the end reported once, and the
//     duration read at each check (a Freeze changed mid-hold applies to it).
// The detector's own rules need PluginData and are pinned under Wine by
// pitboard_splits_test.cpp and pb_gap_test.cpp.
// ============================================================================
#include "doctest.h"
#include "hud/split_crossing.h"
#include "hud/hold_timer.h"

TEST_CASE("timeToCrossing: accumulated sectors to a split, the lap time at the line") {
    CHECK(timeToCrossing(60000, 20000, 20000, 20000, -1) == 60000);
    CHECK(timeToCrossing(60000, 20000, 21000, 19000, 0) == 20000);
    CHECK(timeToCrossing(60000, 20000, 21000, 19000, 1) == 41000);
    CHECK(timeToCrossing(80000, 20000, 21000, 19000, 2) == 60000);  // GP Bikes' third split
}

TEST_CASE("timeToCrossing: any missing part is no time") {
    CHECK(timeToCrossing(0, 20000, 20000, 20000, -1) == -1);
    CHECK(timeToCrossing(-1, 20000, 20000, 20000, -1) == -1);
    CHECK(timeToCrossing(60000, -1, 20000, 20000, 0) == -1);
    CHECK(timeToCrossing(60000, 20000, -1, 20000, 1) == -1);
    CHECK(timeToCrossing(60000, 20000, 20000, 0, 2) == -1);
    // A later sector missing does not matter to an earlier split
    CHECK(timeToCrossing(60000, 20000, -1, -1, 0) == 20000);
}

TEST_CASE("HoldTimer: up for its duration, the end reported once") {
    using Clock = HoldTimer::Clock;
    const Clock::time_point t0 = Clock::now();
    const auto at = [&](int ms) { return t0 + std::chrono::milliseconds(ms); };

    HoldTimer hold;
    CHECK_FALSE(hold.active());
    CHECK_FALSE(hold.running(1000, at(0)));
    CHECK_FALSE(hold.expire(1000, at(0)));

    hold.start(t0);
    CHECK(hold.active());
    CHECK(hold.running(1000, at(999)));
    CHECK_FALSE(hold.running(1000, at(1000)));
    CHECK_FALSE(hold.expire(1000, at(999)));
    CHECK(hold.active());
    CHECK(hold.expire(1000, at(1000)));
    CHECK_FALSE(hold.active());
    CHECK_FALSE(hold.expire(1000, at(2000)));   // once
}

TEST_CASE("HoldTimer: the duration is read at each check, and stop ends it") {
    using Clock = HoldTimer::Clock;
    const Clock::time_point t0 = Clock::now();
    HoldTimer hold;
    hold.start(t0);
    CHECK_FALSE(hold.expire(5000, t0 + std::chrono::milliseconds(2000)));
    CHECK(hold.expire(1000, t0 + std::chrono::milliseconds(2000)));   // Freeze shortened mid-hold

    hold.start(t0);
    hold.stop();
    CHECK_FALSE(hold.active());
    CHECK_FALSE(hold.expire(0, t0));
}
