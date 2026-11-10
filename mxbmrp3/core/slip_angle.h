// ============================================================================
// core/slip_angle.h
// How far the bike is travelling sideways: the angle between where the chassis
// points (yaw) and where its centre of gravity is actually going (the world
// velocity), in the ground plane.
//
// ONE COPY. FmxManager (drift tricks), the rumble slide effect and the Slide
// widget all read this angle; the first two used to carry their own spelling of
// the projection, which is how a sign or an axis fix lands in one and not the
// other.
//
// CONVENTION. The game's headings are compass-style: degrees from north,
// clockwise, with X east and Z north, so forward = (sin, cos) in the X/Z plane
// and the rider's right = (cos, -sin) (the same convention the spotter's
// lateral offset uses). A POSITIVE lateral velocity is travel to the right of
// the nose -- the bike's nose points left of its path, i.e. the rear has
// stepped out to the right, which is what a left-hand power slide looks like.
// ============================================================================
#pragma once

#include "plugin_constants.h"

#include <cmath>

namespace SlipAngle {

struct Components {
    float forward = 0.0f;  // m/s along the nose (negative = rolling backwards)
    float lateral = 0.0f;  // m/s to the rider's right (positive = rear out to the right)
};

// The ground-plane velocity resolved into the chassis' forward and lateral axes.
inline Components resolve(float velocityX, float velocityZ, float yawDeg) {
    const float yawRad = yawDeg * PluginConstants::Math::DEG_TO_RAD;
    const float sinYaw = std::sin(yawRad);
    const float cosYaw = std::cos(yawRad);
    Components c;
    c.forward = velocityX * sinYaw + velocityZ * cosYaw;
    c.lateral = velocityX * cosYaw - velocityZ * sinYaw;
    return c;
}

// The unsigned slip angle in degrees, 0..90. Rolling backwards counts the same as
// forwards: the question is how sideways the bike is, not which end leads.
inline float magnitudeDeg(const Components& c) {
    return std::atan2(std::abs(c.lateral), std::abs(c.forward)) * PluginConstants::Math::RAD_TO_DEG;
}

// The signed slip angle in degrees, -90..90: positive = rear out to the right.
inline float signedDeg(const Components& c) {
    return std::atan2(c.lateral, std::abs(c.forward)) * PluginConstants::Math::RAD_TO_DEG;
}

// Below this ground speed the angle is noise: at walking pace a few cm/s of
// sideways creep reads as a huge angle, and at a standstill it is undefined.
constexpr float MIN_SPEED_MS = 3.0f;

// What the Slide widget shows: the signed slip angle while the bike is on the
// ground and moving, else 0. Airborne, the same angle is a whip or a scrub, not
// a slide, so it is not reported as one.
inline float slideDeg(float velocityX, float velocityZ, float yawDeg, bool anyWheelOnGround) {
    if (!anyWheelOnGround) return 0.0f;
    if (!std::isfinite(velocityX) || !std::isfinite(velocityZ) || !std::isfinite(yawDeg)) return 0.0f;
    if (velocityX * velocityX + velocityZ * velocityZ < MIN_SPEED_MS * MIN_SPEED_MS) return 0.0f;
    return signedDeg(resolve(velocityX, velocityZ, yawDeg));
}

}  // namespace SlipAngle
