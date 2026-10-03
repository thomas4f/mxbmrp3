// ============================================================================
// core/hold_repeat.h
// The hold-to-repeat curve: how long a held control waits before repeating, and
// how fast it repeats after that. One definition for the settings arrows
// (SettingsHud) and the text fields' editing keys (HotkeyManager), so holding
// anything in the menu feels the same.
//
// Pinned by tests/unit/test_hold_repeat.cpp.
// ============================================================================
#pragma once

#include <algorithm>

namespace HoldRepeat {

// Wait before the first repeat.
constexpr long long INITIAL_DELAY_MS = 400;
// The first repeats come this far apart...
constexpr long long SLOW_MS = 200;
// ...and reach the fast interval ([Advanced] holdRepeatFastMs) after this many.
constexpr int ACCEL_REPEATS = 15;

// Interval before the next repeat, given how many have fired so far: a linear
// ramp from SLOW_MS to fastMs over ACCEL_REPEATS, then fastMs.
inline long long intervalMs(int repeatsSoFar, long long fastMs) {
    const float accel = std::min(static_cast<float>(repeatsSoFar) / ACCEL_REPEATS, 1.0f);
    return static_cast<long long>(SLOW_MS + accel * (fastMs - SLOW_MS));
}

}  // namespace HoldRepeat
