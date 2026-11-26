// ============================================================================
// core/wheel_spin.h
// How much faster the driven wheel turns than the bike moves.
//
// ONE COPY. The rumble's wheelspin effect, the Spin deck, FMX's burnout and
// the Digging a Hole achievement all read this ratio; each used to spell it out on its own,
// which is how the deck came to treat a rear wheel that left the ground for
// one sample over a bump as "no reading" while the rumble read it as 0.
//
// The ratio is the wheel's surface speed over the bike's, less one: 0.25 = the
// wheel turns a quarter faster than the bike goes. Under FLOOR_MS the bike's
// speed counts as that floor, so a burnout from a standstill reads high but
// finite. A wheel off the ground (callers pass a negative speed) spins freely
// and is not spinning against anything: 0, the same as grip.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>

namespace WheelSpin {

inline constexpr float FLOOR_MS = 1.0f;

// The overrun ratio, 0 and up; 0 while the wheel is braking, gripping or off
// the ground (rearMs < 0), and for a non-finite input.
inline float overrun(float rearMs, float groundMs) {
    if (!std::isfinite(rearMs) || !std::isfinite(groundMs) || rearMs < 0.0f) return 0.0f;
    const float ground = std::max(groundMs, 0.0f);
    return rearMs > ground ? (rearMs - ground) / std::max(ground, FLOOR_MS) : 0.0f;
}

}  // namespace WheelSpin
