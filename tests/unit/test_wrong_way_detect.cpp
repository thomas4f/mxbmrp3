// ============================================================================
// tests/unit/test_wrong_way_detect.cpp
// The per-rider wrong-way timer (core/wrong_way_detect.h).
//
// Regression: the flag used to clear on the first sample that was not backward.
// The game repeats a rider's position about once a second at 50 Hz, so riding
// the wrong way showed WRONG WAY for about a second (or never) before it
// vanished. These drive a fake clock at the real sample rates and pin that a
// repeated sample neither breaks the confirmation nor clears the flag, while
// stopping or turning round still clears it.
// ============================================================================
#include "doctest.h"
#include "core/wrong_way_detect.h"

namespace {

using Clock = WrongWayDetector::Clock;
constexpr long long CONFIRM_MS = 1500;   // hazardWrongWayDurationMs default
constexpr int TICK_MS = 20;              // 50 Hz, the riding rate in real captures
constexpr float BACK = -0.0002f;
constexpr float FWD = 0.0002f;

struct Feed {
    WrongWayDetector det;
    Clock::time_point now = Clock::time_point{} + std::chrono::hours(1);
    bool last = false;
    // Feeds `ms` worth of samples of one delta; returns the final flag.
    bool run(float delta, int ms) {
        for (int t = 0; t < ms; t += TICK_MS) {
            now += std::chrono::milliseconds(TICK_MS);
            last = det.update(delta, now, CONFIRM_MS);
        }
        return last;
    }
};

}  // namespace

TEST_CASE("wrong way: confirms only after the backward run reaches the duration") {
    Feed f;
    CHECK_FALSE(f.run(BACK, 1400));
    CHECK(f.run(BACK, 200));
}

TEST_CASE("wrong way: a repeated sample does not clear a confirmed flag") {
    Feed f;
    REQUIRE(f.run(BACK, 1600));
    // One repeat a second for ten seconds, as in the 50 Hz capture.
    for (int s = 0; s < 10; ++s) {
        CHECK(f.run(0.0f, TICK_MS));
        CHECK(f.run(BACK, 980));
    }
}

TEST_CASE("wrong way: a repeated sample does not restart the confirmation") {
    Feed f;
    CHECK_FALSE(f.run(BACK, 1000));
    CHECK_FALSE(f.run(0.0f, TICK_MS));
    CHECK(f.run(BACK, 600));   // 1.6 s since the run began, not since the repeat
}

TEST_CASE("wrong way: a single forward jitter sample does not clear it") {
    Feed f;
    REQUIRE(f.run(BACK, 1600));
    CHECK(f.run(FWD, TICK_MS));
    CHECK(f.run(BACK, 200));
}

TEST_CASE("wrong way: turning round clears it after the hold") {
    Feed f;
    REQUIRE(f.run(BACK, 1600));
    CHECK(f.run(FWD, static_cast<int>(WrongWayDetector::CLEAR_HOLD_MS)));
    CHECK_FALSE(f.run(FWD, TICK_MS));
    // And the next backward run confirms from scratch.
    CHECK_FALSE(f.run(BACK, 1400));
    CHECK(f.run(BACK, 200));
}

TEST_CASE("wrong way: stopping clears it after the hold") {
    Feed f;
    REQUIRE(f.run(BACK, 1600));
    CHECK_FALSE(f.run(0.0f, static_cast<int>(WrongWayDetector::CLEAR_HOLD_MS) + TICK_MS));
}

TEST_CASE("wrong way: a run that had not confirmed does not confirm during the hold") {
    Feed f;
    CHECK_FALSE(f.run(BACK, 1400));
    CHECK_FALSE(f.run(FWD, 400));   // past the 1.5 s mark, but no longer backward
}

TEST_CASE("wrong way: the 10 Hz spectator rate behaves the same") {
    WrongWayDetector det;
    Clock::time_point now = Clock::time_point{} + std::chrono::hours(1);
    bool flag = false;
    for (int i = 0; i < 16; ++i) { now += std::chrono::milliseconds(100); flag = det.update(BACK, now, CONFIRM_MS); }
    REQUIRE(flag);
    now += std::chrono::milliseconds(100);
    CHECK(det.update(0.0f, now, CONFIRM_MS));
    for (int i = 0; i < 5; ++i) { now += std::chrono::milliseconds(100); flag = det.update(FWD, now, CONFIRM_MS); }
    CHECK_FALSE(flag);
}

TEST_CASE("wrong way: a stopped rider's position wobble never confirms") {
    // Tiny backward steps between repeats, as a bike standing still reports.
    Feed f;
    for (int t = 0; t < 3000; t += 2 * TICK_MS) {
        CHECK_FALSE(f.run(-0.000005f, TICK_MS));
        CHECK_FALSE(f.run(0.0f, TICK_MS));
    }
}
