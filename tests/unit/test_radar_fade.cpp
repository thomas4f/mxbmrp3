// ============================================================================
// tests/unit/test_radar_fade.cpp
// Unit tests for hud/radar_fade.h — the radar's auto-hide fade.
//
// This math used to live inline in RadarHud::rebuildRenderData(), so exercising
// it meant a DLL under Wine with staged track positions. The two cases worth
// staging are exactly the ones that are awkward there: a rider crossing the
// start/finish line (where the normalised track position wraps) and a rider who
// is physically close but half a lap away on the racing line.
//
// The start/finish case is the one that shows up as a real, visible bug: without
// the wrap fold the radar blinks off as a rider crosses the line, which is the
// moment a rider is most likely to be looking at it.
// ============================================================================
#include "doctest.h"

#include "hud/radar_fade.h"
#include <vector>

using namespace RadarFade;

namespace {

constexpr float kRange = 50.0f;      // metres
constexpr float kTrackLength = 1000.0f;  // metres per lap

// The fade reads the caller's own position array in place, so the tests feed it
// the same game type RadarHud holds — no parallel sample struct to keep in step.
using Sample = Unified::TrackPositionData;

constexpr int kDisplayNum = 99;  // the rider being displayed for
constexpr int kOpponentNum = 1;

Sample rider(float x, float z, float trackPos) {
    Sample s;
    s.raceNum = kOpponentNum;
    s.posX = x;
    s.posZ = z;
    s.trackPos = trackPos;
    return s;
}

float opacityOf(std::vector<Sample>& v, float px, float pz, float ptp,
                float trackLength = kTrackLength) {
    return maxRiderOpacity(v.empty() ? nullptr : v.data(),
                           static_cast<int>(v.size()), kDisplayNum,
                           px, pz, ptp, kRange, trackLength);
}

}  // namespace

TEST_CASE("radar fade: trackSeparation folds across the start/finish line") {
    CHECK(trackSeparation(0.10f, 0.20f) == doctest::Approx(0.10f));
    CHECK(trackSeparation(0.20f, 0.10f) == doctest::Approx(0.10f));
    // The case the fold exists for: either side of the line.
    CHECK(trackSeparation(0.99f, 0.01f) == doctest::Approx(0.02f));
    CHECK(trackSeparation(0.01f, 0.99f) == doctest::Approx(0.02f));
    // Never exceeds half a lap.
    CHECK(trackSeparation(0.0f, 0.5f) == doctest::Approx(0.5f));
    CHECK(trackSeparation(0.0f, 0.75f) == doctest::Approx(0.25f));
    CHECK(trackSeparation(0.5f, 0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("radar fade: nobody around means fully faded out") {
    std::vector<Sample> v;
    CHECK(opacityOf(v, 0.0f, 0.0f, 0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("radar fade: null array is safe") {
    CHECK(maxRiderOpacity(nullptr, 0, kDisplayNum, 0, 0, 0.5f, kRange, kTrackLength)
          == doctest::Approx(0.0f));
    CHECK(maxRiderOpacity(nullptr, 7, kDisplayNum, 0, 0, 0.5f, kRange, kTrackLength)
          == doctest::Approx(0.0f));
}

TEST_CASE("radar fade: the display rider never contributes to their own fade") {
    std::vector<Sample> v{rider(0.0f, 0.0f, 0.5f)};
    v[0].raceNum = kDisplayNum;
    CHECK(opacityOf(v, 0.0f, 0.0f, 0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("radar fade: a rider on top of the player is full opacity") {
    std::vector<Sample> v{rider(0.0f, 0.0f, 0.5f)};
    CHECK(opacityOf(v, 0.0f, 0.0f, 0.5f) == doctest::Approx(1.0f));
}

TEST_CASE("radar fade: opacity falls off linearly with track distance") {
    // 10m along the track = 0.01 of a 1000m lap; range 50m -> 1 - 10/50 = 0.8
    std::vector<Sample> v{rider(5.0f, 0.0f, 0.51f)};
    CHECK(opacityOf(v, 0.0f, 0.0f, 0.50f) == doctest::Approx(0.8f));

    // 25m along -> 0.5
    std::vector<Sample> v2{rider(5.0f, 0.0f, 0.525f)};
    CHECK(opacityOf(v2, 0.0f, 0.0f, 0.50f) == doctest::Approx(0.5f));
}

// Gate 1: straight-line distance. Uses a strict >, so a rider exactly at the
// range boundary still counts.
TEST_CASE("radar fade: riders beyond straight-line range are ignored") {
    std::vector<Sample> far{rider(kRange + 0.1f, 0.0f, 0.5f)};
    CHECK(opacityOf(far, 0.0f, 0.0f, 0.5f) == doctest::Approx(0.0f));

    std::vector<Sample> edge{rider(kRange, 0.0f, 0.5f)};
    CHECK(opacityOf(edge, 0.0f, 0.0f, 0.5f) > 0.0f);
}

// Gate 2: the one that matters on tight infields — physically adjacent, but not
// racing each other.
TEST_CASE("radar fade: a physically close rider half a lap away is ignored") {
    // 5 metres away in world space, but on the opposite side of the lap.
    std::vector<Sample> v{rider(5.0f, 0.0f, 0.0f)};
    CHECK(opacityOf(v, 0.0f, 0.0f, 0.5f) == doctest::Approx(0.0f));
}

// The bug the wrap fold prevents: without it, these two read as 0.98 of a lap
// apart and the radar blinks off at the line.
TEST_CASE("radar fade: a rider crossing the start/finish line does not blink off") {
    std::vector<Sample> v{rider(2.0f, 0.0f, 0.01f)};
    const float o = opacityOf(v, 0.0f, 0.0f, 0.99f);
    CHECK(o > 0.5f);  // 0.02 lap = 20m of 1000m, range 50 -> 1 - 20/50 = 0.6
    CHECK(o == doctest::Approx(0.6f));
}

TEST_CASE("radar fade: the maximum across riders wins") {
    std::vector<Sample> v{
        rider(5.0f, 0.0f, 0.54f),   // 40m along -> 0.2
        rider(5.0f, 0.0f, 0.51f),   // 10m along -> 0.8
        rider(5.0f, 0.0f, 0.53f),   // 30m along -> 0.4
    };
    CHECK(opacityOf(v, 0.0f, 0.0f, 0.50f) == doctest::Approx(0.8f));
}

// Before session data arrives trackLength is 0 and metres are unavailable; the
// fade falls back to a fixed fraction of a lap.
TEST_CASE("radar fade: unknown track length falls back to a fixed lap fraction") {
    SUBCASE("inside the fallback window") {
        // 0.025 of a lap, window is 0.05 -> 1 - 0.025/0.05 = 0.5
        std::vector<Sample> v{rider(5.0f, 0.0f, 0.525f)};
        CHECK(opacityOf(v, 0.0f, 0.0f, 0.50f, 0.0f) == doctest::Approx(0.5f));
    }
    SUBCASE("outside the fallback window") {
        std::vector<Sample> v{rider(5.0f, 0.0f, 0.56f)};  // 0.06 > 0.05
        CHECK(opacityOf(v, 0.0f, 0.0f, 0.50f, 0.0f) == doctest::Approx(0.0f));
    }
    SUBCASE("negative track length takes the same fallback") {
        std::vector<Sample> v{rider(5.0f, 0.0f, 0.50f)};
        CHECK(opacityOf(v, 0.0f, 0.0f, 0.50f, -1.0f) == doctest::Approx(1.0f));
    }
}

// The output feeds an opacity multiplier, so anything outside [0,1] would
// brighten the radar past its configured opacity or invert it.
TEST_CASE("radar fade: opacity always lands in [0,1]") {
    for (int tp = 0; tp <= 100; ++tp) {
        for (int dist = 0; dist <= 60; dist += 3) {
            std::vector<Sample> v{
                rider(static_cast<float>(dist), 0.0f, tp / 100.0f)};
            for (float len : {0.0f, 100.0f, 1000.0f, 8000.0f}) {
                const float o = opacityOf(v, 0.0f, 0.0f, 0.5f, len);
                CAPTURE(tp);
                CAPTURE(dist);
                CAPTURE(len);
                CHECK(o >= 0.0f);
                CHECK(o <= 1.0f);
            }
        }
    }
}
