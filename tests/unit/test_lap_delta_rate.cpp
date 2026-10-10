// ============================================================================
// tests/unit/test_lap_delta_rate.cpp
// The Map's lap delta colouring (LapDeltaRate in core/lap_delta_profile.h):
// the rate of gain or loss at each profile point, smoothed over a window and
// read back per track position. Pins:
//   - sign: a gap that grows (losing time) rates positive, a shrinking one
//     negative, an even pace close to zero
//   - scale: the lap's largest slope rates +/-1, so any pace uses the full range
//   - SMOOTHNESS (Thomas, 1.32): reading the rate at many positions across a
//     step change in pace moves in small increments, not one jump per profile
//     point - the Labs version took the nearest point's colour, which drew the
//     track in visible chunks
//   - no data, no colour: positions past the last sampled point read nothing
//   - THE SWEEP: ahead of the rider, past the overwrite gap, the previous lap
//     reads at PREVIOUS_STRENGTH; inside the gap nothing; with no lap live
//     the previous lap reads at full strength
//   - THE SWEEP'S EDGES (Thomas, 1.32): while a lap is live the tint fades in
//     on both sides of the gap - plain at the rider's furthest point and where
//     the previous lap starts, full EDGE_FADE_POINTS inside - instead of each
//     new point popping in at full colour
//   - mixColor() blends RGB and keeps the base colour's alpha
// ============================================================================
#include "doctest.h"
#include "core/lap_delta_profile.h"
#include "core/plugin_utils.h"
#include <cmath>

namespace {

// A profile whose gap follows f(point) for points 0..last.
template <typename F>
LapDeltaProfile makeProfile(int last, F f) {
    LapDeltaProfile d;
    for (int p = 0; p <= last; ++p) {
        d.gapMs[p] = f(p);
        d.has[p] = true;
    }
    d.last = last;
    return d;
}

float posOf(int p) { return static_cast<float>(p) / static_cast<float>(LapDeltaProfile::POINTS - 1); }

}  // namespace

TEST_CASE("lap delta rate: losing reads positive, gaining negative") {
    // Gain for the first half, lose for the second.
    const auto d = makeProfile(200, [](int p) { return p < 100 ? -20.0f * p : -2000.0f + 40.0f * (p - 100); });
    LapDeltaRate r;
    r.build(d);
    REQUIRE(r.any);
    float v = 0.0f;
    REQUIRE(r.at(posOf(40), v));
    CHECK(v < 0.0f);
    REQUIRE(r.at(posOf(160), v));
    CHECK(v > 0.0f);
    // The steeper (losing) half is the lap's largest swing.
    CHECK(v == doctest::Approx(1.0f));
}

TEST_CASE("lap delta rate: an even pace stays near the plain fill") {
    const auto d = makeProfile(200, [](int p) { return 0.2f * p; });   // 0.4 ms per 1%
    LapDeltaRate r;
    r.build(d);
    float v = 1.0f;
    REQUIRE(r.at(0.5f, v));
    CHECK(std::fabs(v) < 0.05f);
    CHECK(LapDeltaRate::weight(v) < 0.25f);
}

TEST_CASE("lap delta rate: colour moves smoothly along the track") {
    // A step in pace at point 100: even before, losing 40 ms per point after.
    const auto d = makeProfile(200, [](int p) { return p < 100 ? 0.0f : 40.0f * (p - 100); });
    LapDeltaRate r;
    r.build(d);
    // Sample 20 positions per profile point across the change: no two
    // neighbouring readings may differ by more than a small step.
    float prev = 0.0f;
    REQUIRE(r.at(posOf(90), prev));
    float maxStep = 0.0f;
    for (int i = 1; i <= 20 * 20; ++i) {
        const float pos = posOf(90) + static_cast<float>(i) / (20.0f * (LapDeltaProfile::POINTS - 1));
        float v = 0.0f;
        REQUIRE(r.at(pos, v));
        maxStep = std::max(maxStep, std::fabs(v - prev));
        prev = v;
    }
    INFO("largest step between neighbouring readings: " << maxStep);
    CHECK(maxStep < 0.02f);
    CHECK(prev == doctest::Approx(1.0f));
}

TEST_CASE("lap delta rate: nothing past the last sampled point") {
    const auto d = makeProfile(50, [](int p) { return 3.0f * p; });
    LapDeltaRate r;
    r.build(d);
    float v = 0.0f;
    CHECK(r.at(posOf(50), v));
    CHECK_FALSE(r.at(posOf(52), v));
    CHECK_FALSE(r.at(0.9f, v));

    LapDeltaRate empty;
    empty.build(LapDeltaProfile{});
    CHECK_FALSE(empty.any);
    CHECK_FALSE(empty.at(0.5f, v));
}

TEST_CASE("lap delta rate: the new lap sweeps over the previous one, leaving a gap") {
    // In progress: losing to point 60. Previous lap: losing everywhere, shown
    // from point 60 + 1 + OVERWRITE_GAP on.
    auto d = makeProfile(60, [](int p) { return 30.0f * p; });
    d.live = true;
    d.prevFrom = 61 + LapDeltaProfile::OVERWRITE_GAP;
    for (int p = d.prevFrom; p < LapDeltaProfile::POINTS; ++p) {
        d.prevGapMs[p] = 30.0f * p;
        d.prevHas[p] = true;
    }
    d.anyPrev = true;
    LapDeltaRate r;
    r.build(d);
    float v = 0.0f;
    REQUIRE(r.at(posOf(30), v));
    CHECK(v == doctest::Approx(1.0f));
    CHECK_FALSE(r.at(posOf(63), v));                    // the gap
    REQUIRE(r.at(posOf(150), v));
    CHECK(v == doctest::Approx(LapDeltaRate::PREVIOUS_STRENGTH));

    // In the pits nothing is live: the previous lap is the whole picture.
    LapDeltaProfile pits;
    for (int p = 0; p < LapDeltaProfile::POINTS; ++p) {
        pits.prevGapMs[p] = 30.0f * p;
        pits.prevHas[p] = true;
    }
    pits.anyPrev = true;
    r.build(pits);
    REQUIRE(r.at(posOf(30), v));
    CHECK(v == doctest::Approx(1.0f));
}

TEST_CASE("lap delta rate: the tint fades in on both sides of the gap") {
    // In progress: losing evenly to point 60, so every point rates 1 before
    // the fade. Previous lap the same, from past the gap on.
    auto d = makeProfile(60, [](int p) { return 30.0f * p; });
    d.live = true;
    d.prevFrom = 61 + LapDeltaProfile::OVERWRITE_GAP;
    for (int p = d.prevFrom; p < LapDeltaProfile::POINTS; ++p) {
        d.prevGapMs[p] = 30.0f * p;
        d.prevHas[p] = true;
    }
    d.anyPrev = true;
    LapDeltaRate r;
    r.build(d);
    constexpr int F = LapDeltaRate::EDGE_FADE_POINTS;
    float v = 0.0f;
    // Behind the rider: full well back, rising weight toward plain at the front.
    REQUIRE(r.at(posOf(60 - F - 2), v));
    CHECK(v == doctest::Approx(1.0f));
    REQUIRE(r.at(posOf(60), v));
    CHECK(v == doctest::Approx(0.0f).epsilon(0.01));
    float prevW = 2.0f;
    for (int p = 60 - F; p <= 60; ++p) {
        REQUIRE(r.at(posOf(p), v));
        const float w = LapDeltaRate::weight(v);
        CHECK(w < prevW);                               // fades toward the front
        prevW = w;
    }
    // Past the gap: plain where the previous lap starts, its full strength
    // EDGE_FADE_POINTS on.
    REQUIRE(r.at(posOf(d.prevFrom), v));
    CHECK(v == doctest::Approx(0.0f).epsilon(0.01));
    REQUIRE(r.at(posOf(d.prevFrom + F + 2), v));
    CHECK(v == doctest::Approx(LapDeltaRate::PREVIOUS_STRENGTH));
    REQUIRE(r.at(posOf(d.prevFrom + F / 2), v));
    CHECK(v > 0.0f);
    CHECK(v < LapDeltaRate::PREVIOUS_STRENGTH);

    // With no lap live (the pits) nothing fades: the previous lap reads full
    // right from the start.
    LapDeltaProfile pits;
    for (int p = 0; p < LapDeltaProfile::POINTS; ++p) {
        pits.prevGapMs[p] = 30.0f * p;
        pits.prevHas[p] = true;
    }
    pits.anyPrev = true;
    r.build(pits);
    REQUIRE(r.at(posOf(1), v));
    CHECK(v == doctest::Approx(1.0f));
}

TEST_CASE("mixColor blends RGB and keeps the base alpha") {
    const unsigned long from = PluginUtils::makeColor(0, 0, 0, 200);
    const unsigned long to = PluginUtils::makeColor(255, 100, 50, 255);
    CHECK(PluginUtils::mixColor(from, to, 0.0f) == from);
    CHECK(PluginUtils::mixColor(from, to, 1.0f) == PluginUtils::makeColor(255, 100, 50, 200));
    CHECK(PluginUtils::mixColor(from, to, 0.5f) == PluginUtils::makeColor(128, 50, 25, 200));
}
