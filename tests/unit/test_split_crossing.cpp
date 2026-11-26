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
//   - SplitLearner (core/split_learner.h): where the splits are when the game
//     sends no marker data (GP Bikes), learned at each crossing; a new rider's
//     splits so far are adopted, never taken for crossings made here.
// The detector's own rules need PluginData and are pinned under Wine by
// pitboard_splits_test.cpp and pb_gap_test.cpp.
// ============================================================================
#include "doctest.h"
#include "hud/split_crossing.h"
#include "hud/hold_timer.h"
#include "core/split_learner.h"

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

TEST_CASE("SplitLearner: a split's new time is the rider standing on it") {
    SplitLearner learner;
    const std::vector<float> none;
    float out[SplitLearner::SLOTS];
    learner.observe(7, { -1, -1, -1 }, 0.10f);   // first sight: adopted
    CHECK(learner.collect(none, out) == 0);
    const uint32_t before = learner.revision();
    learner.observe(7, { 31000, -1, -1 }, 0.34f);
    learner.observe(7, { 31000, -1, -1 }, 0.40f);  // the same time: no new crossing
    learner.observe(7, { 31000, 62000, -1 }, 0.71f);
    REQUIRE(learner.collect(none, out) == 2);
    CHECK(out[0] == doctest::Approx(0.34f));
    CHECK(out[1] == doctest::Approx(0.71f));
    CHECK(learner.revision() == before + 2);
    // The line: times reset to -1, the positions stay
    learner.observe(7, { -1, -1, -1 }, 0.0f);
    CHECK(learner.collect(none, out) == 2);
}

TEST_CASE("SplitLearner: a new rider's splits are adopted, the track's kept") {
    SplitLearner learner;
    const std::vector<float> none;
    float out[SplitLearner::SLOTS];
    learner.observe(7, { -1, -1, -1 }, 0.10f);
    learner.observe(7, { 31000, -1, -1 }, 0.34f);
    // Spectating #12, already past both splits at 0.80: not crossings made here
    learner.observe(12, { 30000, 61000, -1 }, 0.80f);
    REQUIRE(learner.collect(none, out) == 1);
    CHECK(out[0] == doctest::Approx(0.34f));
    // A crossing at the edge of the lap is no position
    learner.observe(12, { 30000, 61000, 90000 }, 0.9995f);
    CHECK(learner.collect(none, out) == 1);
}

TEST_CASE("SplitLearner: the centerline's markers win, and a reset forgets") {
    SplitLearner learner;
    float out[SplitLearner::SLOTS];
    learner.observe(7, { -1, -1, -1 }, 0.10f);
    learner.observe(7, { 31000, -1, -1 }, 0.34f);
    // Entry 0 is the S/F line; the rest are taken, sorted
    const std::vector<float> official = { 0.0f, 0.66f, 0.25f };
    REQUIRE(learner.collect(official, out) == 2);
    CHECK(out[0] == doctest::Approx(0.25f));
    CHECK(out[1] == doctest::Approx(0.66f));
    // A session change: nothing learned, and the rider's splits so far adopted
    learner.reset();
    learner.observe(7, { 31000, -1, -1 }, 0.50f);
    CHECK(learner.collect({}, out) == 0);
}
