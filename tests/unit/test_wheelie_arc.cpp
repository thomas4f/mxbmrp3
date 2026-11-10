// ============================================================================
// tests/unit/test_wheelie_arc.cpp
// The Wheelie widget's two pure pieces:
//   - core/wheelie_angle.h, the one "is this a wheelie, and how far up" rule the
//     rumble, the FMX trick detector and the widget share: a climb with both
//     wheels down, a jump and a stoppie all read 0, and only rear-down,
//     front-up, nose-up reads the angle.
//   - hud/wheelie_arc.h, the protractor arithmetic. The widget's ring is
//     stretched sideways to fill a box wider than it is tall, so "90 - degrees"
//     no longer lands the fill on the bike's angle; angleOnArc solves the end on
//     the ring instead. The cases check the point it returns really lies on the
//     ray drawn from the pivot at that angle, which is what a player reads, and
//     that a circle still gives the plain answer.
// ============================================================================
#include "doctest.h"

#include <cmath>

#include "core/wheelie_angle.h"
#include "hud/wheelie_arc.h"

namespace {
constexpr float kPi = 3.14159265358979f;

// The angle off level, in degrees, of the ring point at addArcSegment angle t.
float readAngle(float t, float rx, float ry) {
    const float x = std::sin(t) * rx;
    const float y = std::cos(t) * ry;
    return std::atan2(y, x) * 180.0f / kPi;
}
}  // namespace

TEST_CASE("wheelie angle: only rear down, front up and nose up is a wheelie") {
    CHECK(WheelieAngle::of(false, true, -30.0f) == doctest::Approx(30.0f));
    CHECK(WheelieAngle::of(true, true, -30.0f) == 0.0f);    // climbing a hill
    CHECK(WheelieAngle::of(false, false, -30.0f) == 0.0f);  // in the air
    CHECK(WheelieAngle::of(true, false, 20.0f) == 0.0f);    // stoppie
    CHECK(WheelieAngle::of(false, true, 5.0f) == 0.0f);     // rear only, nose down
}

TEST_CASE("wheelie arc: on a circle the fill ends at 90 minus the angle") {
    for (float deg : {0.0f, 15.0f, 45.0f, 70.0f, 90.0f}) {
        CAPTURE(deg);
        CHECK(WheelieArc::angleOnArc(deg, 1.0f, 1.0f) * 180.0f / kPi ==
              doctest::Approx(90.0f - deg).epsilon(1e-4));
    }
}

TEST_CASE("wheelie arc: on the stretched ring the fill end lies on the bike's angle") {
    // The shipped box: about 1.18 wide per 1 tall, and wider still at other fonts.
    for (float rx : {1.18f, 1.5f, 0.8f}) {
        for (float deg : {0.0f, 10.0f, 30.0f, 45.0f, 60.0f, 85.0f, 90.0f}) {
            CAPTURE(rx);
            CAPTURE(deg);
            const float t = WheelieArc::angleOnArc(deg, rx, 1.0f);
            CHECK(t >= -1e-5f);
            CHECK(t <= kPi * 0.5f + 1e-5f);
            CHECK(readAngle(t, rx, 1.0f) == doctest::Approx(deg).epsilon(1e-3));
        }
    }
}

TEST_CASE("wheelie arc: out-of-range angles clamp to the quarter") {
    CHECK(WheelieArc::angleOnArc(-20.0f, 1.2f, 1.0f) == doctest::Approx(kPi * 0.5f));
    CHECK(WheelieArc::angleOnArc(140.0f, 1.2f, 1.0f) == doctest::Approx(0.0f));
}
