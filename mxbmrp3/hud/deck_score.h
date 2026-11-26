// ============================================================================
// hud/deck_score.h
// The later decks' game rules (deck_hud.h): the Speed deck's reaction
// grade (the REACT stamp) and the Spin deck's wheelspin. Header-only and pure
// so a unit test can pin them, as deck_coach.h does for the riding decks.
//
// DISPLAY ONLY, like DeckCoach: nothing here feeds the FMX score, stats or
// achievements, and every number is lost with the session.
//
// The thresholds are first values; each is a named constant so tuning one is
// one line. Pinned by tests/unit/test_deck_score.cpp.
// ============================================================================
#pragma once

#include "../core/wheel_spin.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace DeckScore {

// ---- Reaction ----------------------------------------------------------------
//
// The reaction is the time from the gate dropping to the bike moving
// (REACT_SPEED).
inline constexpr float REACT_SPEED = 1.0f;       // m/s: the bike is away
inline constexpr float GREAT_BELOW = 0.25f;      // seconds
inline constexpr float GOOD_BELOW = 0.5f;
// The most the drop is back-dated by (the classification that reports it can
// lag it; a larger lag is a stall, not a server rate)
inline constexpr int MAX_DROP_LAG_MS = 1000;

enum class Reaction : uint8_t { NONE = 0, GREAT, GOOD, LATE };

inline Reaction reactionGrade(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0f) return Reaction::NONE;
    if (seconds < GREAT_BELOW) return Reaction::GREAT;
    if (seconds < GOOD_BELOW) return Reaction::GOOD;
    return Reaction::LATE;
}

// ---- Spin --------------------------------------------------------------------
//
// The Spin deck's share is the wheelspin ratio (core/wheel_spin.h), shown as 1
// past 1. A rear wheel off the ground reads 0, as it does for the rumble. The
// bands are first values, to tune in game: the right amount of spin depends
// on the dirt. HOOK_FROM to HOOK_TO is hooked up, on to SPIN_FROM too much,
// SPIN_FROM and over spinning (the riding gauges' one colour rule: green,
// amber, red); the meter spans 0..SPIN_SCALE and the timer runs from HOOK_FROM.
inline constexpr float HOOK_FROM = 0.05f;
inline constexpr float HOOK_TO = 0.20f;
inline constexpr float SPIN_FROM = 0.25f;
inline constexpr float SPIN_SCALE = 0.5f;
// The shown share follows the raw one with this time constant: the raw value
// jitters sample to sample at 100 Hz, which reads as noise and rebuilds the
// deck every sample.
inline constexpr float SPIN_SMOOTH_S = 0.1f;

inline float spinShare(float rearMs, float groundMs) {
    return std::min(WheelSpin::overrun(rearMs, groundMs), 1.0f);
}

// The shown share, eased toward the raw one over dt. A crash (share < 0) is no
// reading at once; out of one it starts from the raw value.
inline float easeSpin(float shown, float share, float dt) {
    if (share < 0.0f) return -1.0f;
    if (shown < 0.0f || dt <= 0.0f) return shown < 0.0f ? share : shown;
    return shown + (share - shown) * std::min(dt / SPIN_SMOOTH_S, 1.0f);
}

}  // namespace DeckScore
