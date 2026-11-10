// ============================================================================
// core/wheelie_angle.h
// How far the bike is up on its back wheel, in degrees.
//
// ONE DEFINITION of "a wheelie" for everything that reads one: the rumble's
// wheelie effect, FMX's Wheelie / Coaster Wheelie classification and the Wheelie
// widget. They used to spell the rule out separately (rear wheel down, front
// wheel up, nose above level), and three spellings of one rule is how a gauge
// ends up saying "wheelie" while the trick detector says "no".
//
// The angle is the WORLD pitch (the plugin API's m_fPitch: negative = nose up),
// not pitch relative to the ground: only GP Bikes reports the latter. The wheel
// contact gate is what keeps a climb from reading as a wheelie -- on a hill both
// wheels are down, so the angle is 0 however steep the hill is.
// ============================================================================
#pragma once

namespace WheelieAngle {

// Degrees of nose-up while the front wheel is off the ground and the rear wheel
// on it; 0 otherwise (grounded, airborne, or on the front wheel).
inline float of(bool frontWheelContact, bool rearWheelContact, float pitch) {
    return (!frontWheelContact && rearWheelContact && pitch < 0.0f) ? -pitch : 0.0f;
}

}  // namespace WheelieAngle
