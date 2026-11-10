// ============================================================================
// tests/unit/test_slip_angle.cpp
// Unit tests for core/slip_angle.h — heading against ground-plane velocity, the
// one projection FMX's drift tricks, the rumble slide effect and the Slide
// widget all read.
//
// THE SIGN IS THE CONTRACT. The game's headings are compass-style (degrees from
// north, clockwise; X east, Z north), so a bike pointing north and travelling
// north-east is moving to the RIGHT of its nose: rear out to the right, a
// positive angle, the right-hand side of the Slide widget's arc. A flipped axis
// would still pass every magnitude check, so each case below pins the sign at
// a heading where sin and cos both matter.
// ============================================================================
#include "doctest.h"

#include "core/slip_angle.h"
#include <cmath>
#include <limits>

namespace {

constexpr float kDeg = 3.14159265358979323846f / 180.0f;

// World velocity (X east, Z north) for a ground speed travelling ON a compass bearing.
void velocityOn(float bearingDeg, float speed, float& vx, float& vz) {
    vx = speed * std::sin(bearingDeg * kDeg);
    vz = speed * std::cos(bearingDeg * kDeg);
}

}  // namespace

TEST_CASE("slip angle: straight along the nose reads zero at any heading") {
    for (float yaw : { -170.0f, -90.0f, 0.0f, 37.0f, 90.0f, 179.0f }) {
        float vx = 0.0f, vz = 0.0f;
        velocityOn(yaw, 20.0f, vx, vz);
        CAPTURE(yaw);
        CHECK(SlipAngle::slideDeg(vx, vz, yaw, true) == doctest::Approx(0.0f).epsilon(0.001));
    }
}

TEST_CASE("slip angle: travelling right of the nose is positive (rear out to the right)") {
    for (float yaw : { 0.0f, 60.0f, -120.0f }) {
        float vx = 0.0f, vz = 0.0f;
        velocityOn(yaw + 20.0f, 15.0f, vx, vz);   // path 20 degrees clockwise of the nose
        CAPTURE(yaw);
        CHECK(SlipAngle::slideDeg(vx, vz, yaw, true) == doctest::Approx(20.0f).epsilon(0.001));
        velocityOn(yaw - 20.0f, 15.0f, vx, vz);   // and the mirror slide
        CHECK(SlipAngle::slideDeg(vx, vz, yaw, true) == doctest::Approx(-20.0f).epsilon(0.001));
    }
}

TEST_CASE("slip angle: the heading wraps through +-180 without a jump") {
    float vx = 0.0f, vz = 0.0f;
    velocityOn(-170.0f, 15.0f, vx, vz);           // 20 degrees right of a 170 heading
    CHECK(SlipAngle::slideDeg(vx, vz, 170.0f, true) == doctest::Approx(20.0f).epsilon(0.001));
}

TEST_CASE("slip angle: rolling backwards reads how sideways, not which end leads") {
    float vx = 0.0f, vz = 0.0f;
    velocityOn(180.0f + 10.0f, 5.0f, vx, vz);     // reversing, path 10 degrees off the tail
    const float s = SlipAngle::slideDeg(vx, vz, 0.0f, true);
    CHECK(std::abs(s) == doctest::Approx(10.0f).epsilon(0.001));
    CHECK(std::abs(s) <= 90.0f);
}

TEST_CASE("slip angle: no slide in the air, at walking pace or on a bad sample") {
    float vx = 0.0f, vz = 0.0f;
    velocityOn(45.0f, 15.0f, vx, vz);
    CHECK(SlipAngle::slideDeg(vx, vz, 0.0f, true) == doctest::Approx(45.0f).epsilon(0.001));
    CHECK(SlipAngle::slideDeg(vx, vz, 0.0f, false) == 0.0f);          // airborne: a whip, not a slide
    velocityOn(45.0f, SlipAngle::MIN_SPEED_MS * 0.9f, vx, vz);
    CHECK(SlipAngle::slideDeg(vx, vz, 0.0f, true) == 0.0f);           // creeping
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(SlipAngle::slideDeg(nan, 10.0f, 0.0f, true) == 0.0f);
    CHECK(SlipAngle::slideDeg(10.0f, 10.0f, std::numeric_limits<float>::infinity(), true) == 0.0f);
}

TEST_CASE("slip angle: the unsigned magnitude agrees with the signed angle") {
    float vx = 0.0f, vz = 0.0f;
    velocityOn(-30.0f, 12.0f, vx, vz);
    const SlipAngle::Components c = SlipAngle::resolve(vx, vz, 0.0f);
    CHECK(c.lateral < 0.0f);
    CHECK(SlipAngle::magnitudeDeg(c) == doctest::Approx(std::abs(SlipAngle::signedDeg(c))));
    CHECK(SlipAngle::magnitudeDeg(c) == doctest::Approx(30.0f).epsilon(0.001));
}
