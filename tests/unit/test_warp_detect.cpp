// ============================================================================
// tests/unit/test_warp_detect.cpp
// The reset-to-marker detector (core/warp_detect.h).
//
// Regression: a hotlapper who fell and reset to a marker near the end of the
// track saw -49 s on the Delta trace and the Lap Log until the line, because
// nothing noticed the jump. These pin both halves of the rule with the shapes
// seen in real tapes: a teleport landing well ahead is a skip; the game's crash
// recovery (a teleport back to, or a little ahead of, where the rider fell) is
// not; and the centerline position leaping while the bike moves smoothly
// (riding off track, a crashed bike flying) is no teleport at all.
// ============================================================================
#include "doctest.h"
#include "core/warp_detect.h"

namespace {

using R = WarpDetector::Result;
constexpr float TRACK_M = 1669.0f;      // the crash reels' track
constexpr int64_t TICK_US = 20000;      // 50 Hz, the riding rate

struct Rider {
    WarpDetector det;
    int64_t us = 1000000;
    float x = 0.0f;
    float trackPos = 0.10f;
    // Rides `metres` along a straight at `mps`, the centerline moving with it.
    R ride(float metres, float mps = 30.0f) {
        R worst = R::NONE;
        const float step = mps * static_cast<float>(TICK_US) / 1e6f;
        for (float done = 0.0f; done < metres; done += step) {
            us += TICK_US;
            x += step;
            trackPos += step / TRACK_M;
            const R r = det.update(x, 0.0f, 0.0f, trackPos, false, us, TRACK_M);
            if (r != R::NONE) worst = r;
        }
        return worst;
    }
    // One sample: put down at world x / centerline tp, `crashed` as given.
    R at(float newX, float tp, bool crashed = false, int64_t dtUs = 7000) {
        us += dtUs;
        x = newX;
        trackPos = tp;
        return det.update(x, 0.0f, 0.0f, trackPos, crashed, us, TRACK_M);
    }
};

}  // namespace

TEST_CASE("warp: riding is never a teleport, at any sample rate") {
    Rider r;
    CHECK(r.ride(500.0f, 50.0f) == R::NONE);
    // ~10 Hz spectating at 50 m/s: 5 m per sample.
    CHECK(r.at(r.x + 5.0f, r.trackPos + 5.0f / TRACK_M, false, 100000) == R::NONE);
    // A hitch: half a second without a sample at 25 m/s is 12.5 m.
    CHECK(r.at(r.x + 12.5f, r.trackPos + 12.5f / TRACK_M, false, 500000) == R::NONE);
}

TEST_CASE("warp: a reset to a marker far ahead is a skip") {
    Rider r;
    r.ride(200.0f);
    const float fell = r.trackPos;
    CHECK(r.at(r.x + 3.0f, fell, true) == R::NONE);            // falls: crashed, barely moves
    CHECK(r.at(-300.0f, 0.93f) == R::SKIP);                    // put down near the end of the lap
    CHECK(r.ride(50.0f) == R::NONE);                            // and rides on from there
}

TEST_CASE("warp: the game's crash recovery is a teleport, not a skip") {
    Rider r;
    r.ride(200.0f);
    const float fell = r.trackPos;
    // Back to where they fell, or behind it (the reels' recoveries, 70-90 m back).
    CHECK(r.at(r.x + 4.0f, fell, true) == R::NONE);
    CHECK(r.at(r.x - 85.0f, fell - 80.0f / TRACK_M) == R::TELEPORT);
    r.ride(100.0f);
    // Or to a marker a little ahead: the reels' furthest was 157 m.
    CHECK(r.at(r.x + 4.0f, r.trackPos, true) == R::NONE);
    CHECK(r.at(r.x + 96.0f, r.trackPos + 157.0f / TRACK_M) == R::TELEPORT);
}

TEST_CASE("warp: ahead is measured from the last uncrashed spot") {
    // A tumbling bike's centerline position is meaningless: in a reel it read
    // 0.62 on a lap ridden at 0.05, and measured from there the recovery to 0.15
    // looked 800 m BEHIND. From where it was last ridden it is 157 m ahead.
    Rider r;
    r.trackPos = 0.053f;
    r.ride(10.0f);
    const float ridden = r.trackPos;
    CHECK(r.at(r.x + 6.0f, 0.62f, true) == R::NONE);           // flung: the centerline leaps
    CHECK(r.at(r.x + 96.0f, ridden + 157.0f / TRACK_M) == R::TELEPORT);
    // And the other way: the leap ahead, measured from the flung reading, would
    // be a recovery; from the ridden spot it is a skip.
    Rider s;
    s.trackPos = 0.05f;
    s.ride(10.0f);
    CHECK(s.at(s.x + 6.0f, 0.62f, true) == R::NONE);
    CHECK(s.at(s.x + 40.0f, 0.70f) == R::SKIP);
}

TEST_CASE("warp: the centerline leaping without a teleport is not a warp") {
    // Off track, or a crashed bike in the air: the nearest centerline point jumps
    // hundreds of metres while the bike itself moves a few.
    Rider r;
    r.ride(100.0f);
    CHECK(r.at(r.x + 0.6f, r.trackPos + 424.0f / TRACK_M, true, 20000) == R::NONE);
    CHECK(r.at(r.x + 0.6f, r.trackPos + 65.0f / TRACK_M, false, 20000) == R::NONE);
}

TEST_CASE("warp: across the line either way, and without a track length") {
    // The clock runs from the last line, so a reset across it breaks the gap
    // both ways: forward (0.70 -> 0.10, read against the reference's first
    // seconds) and back (0.02 -> 0.95, read against its last).
    Rider r;
    r.trackPos = 0.68f;
    r.ride(30.0f);
    CHECK(r.at(r.x + 50.0f, 0.10f) == R::SKIP);
    Rider s;
    s.trackPos = 0.01f;
    s.ride(20.0f);
    CHECK(s.at(s.x - 60.0f, 0.95f) == R::SKIP);
    // Further than half a lap ahead is still ahead, not behind (0.22 -> 0.93).
    Rider t;
    t.trackPos = 0.20f;
    t.ride(30.0f);
    CHECK(t.at(t.x - 300.0f, 0.93f) == R::SKIP);
    // Across the line and barely on (0.99 -> 0.01) is the line itself, teleport
    // or not: the wrap must still start the lap.
    Rider u;
    u.trackPos = 0.97f;
    u.ride(20.0f);
    CHECK(u.at(u.x - 1500.0f, 0.01f) == R::NONE);
    // Track length unknown (<= 0): the share of the lap decides.
    WarpDetector d;
    d.update(0.0f, 0.0f, 0.0f, 0.20f, false, 0, 0.0f);
    CHECK(d.update(100.0f, 0.0f, 0.0f, 0.30f, false, 7000, 0.0f) == R::TELEPORT);
    CHECK(d.update(300.0f, 0.0f, 0.0f, 0.45f, false, 14000, 0.0f) == R::SKIP);
}

TEST_CASE("warp: reset forgets the rider, and a NaN is ignored") {
    Rider r;
    r.ride(50.0f);
    r.det.reset();
    CHECK(r.at(r.x + 900.0f, 0.90f) == R::NONE);              // first sample is a baseline
    CHECK(r.det.update(NAN, 0.0f, 0.0f, 0.5f, false, r.us + 7000, TRACK_M) == R::NONE);
    CHECK(r.at(r.x + 1.0f, r.trackPos + 0.0006f) == R::NONE);   // the NaN left no mark
}
