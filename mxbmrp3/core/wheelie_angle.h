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

#include <cmath>

namespace WheelieAngle {

// When a wheelie is HELD: FMX's Wheelie trick (FmxManager::shouldEndTrick
// and its start test) and the Wheelie deck's timer and MAX, one rule. It
// STARTS when the angle below reaches fromDeg, and ENDS when the front wheel
// touches or the RAW world pitch comes back over half of fromDeg nose-up, so
// a nose bobbing around fromDeg is one wheelie, not many. The end reads the
// raw pitch, as FMX always has: past vertical the game folds the pitch back
// towards level, so a wheelie nearly flipped over (about 172 degrees up)
// ends there, for the deck as for the trick. FmxConfig takes its default
// threshold from FROM_DEG.
inline constexpr float FROM_DEG = 15.0f;

inline bool starts(float angleDeg, float fromDeg = FROM_DEG) {
    return std::isfinite(angleDeg) && angleDeg >= fromDeg;
}
// A non-finite pitch ends nothing: FMX's comparison has always let it pass
inline bool ends(bool frontWheelContact, float pitch, float fromDeg = FROM_DEG) {
    return frontWheelContact || pitch > -fromDeg * 0.5f;
}
inline bool held(bool wasHeld, float angleDeg, bool frontWheelContact, float pitch, float fromDeg = FROM_DEG) {
    return wasHeld ? !ends(frontWheelContact, pitch, fromDeg) : starts(angleDeg, fromDeg);
}

// Degrees of nose-up while the front wheel is off the ground and the rear wheel
// on it; 0 otherwise (grounded, airborne, or on the front wheel).
//
// PAST VERTICAL. pitch and roll are Euler angles, and past 90 degrees nose-up
// the game reports the same attitude as its twin: pitch folds back towards
// level and roll flips by 180 (gimbal lock; FMX counts rotation from the
// rates for the same reason). Read raw, a 100 degree wheelie came out as 80,
// or with the fold's sign as no wheelie at all, and every reader's hold reset.
// The twin's roll sits near 180, so a roll past 135 is read as the twin: the
// nose-up is then 180 minus the folded pitch. A bike just past lying on its
// side (roll 95) stays a real roll, not a wheelie of nearly 180.
inline float of(bool frontWheelContact, bool rearWheelContact, float pitch, float roll) {
    if (frontWheelContact || !rearWheelContact || !std::isfinite(pitch)) return 0.0f;
    if (std::isfinite(roll) && std::fabs(roll) > 135.0f) return 180.0f - std::fabs(pitch);
    return pitch < 0.0f ? -pitch : 0.0f;
}

}  // namespace WheelieAngle
