// ============================================================================
// core/fmx_scoring.h
// Pure FMX scoring math — trick score and chain multiplier. Extracted from
// FmxManager (fmx_manager.cpp) as free functions parameterized on FmxConfig so
// the scoring can be unit-tested headless (tests/unit/test_fmx_scoring.cpp),
// the same header-only-math pattern as core/segment_cumulative.h and
// hud/session_charts_math.h. FmxManager::calculateTrickScore/
// calculateChainMultiplier are now thin wrappers that pass m_config here; the
// bodies are unchanged apart from reading `config` instead of `m_config`.
// ============================================================================
#pragma once

#include "fmx_types.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Fmx {

// Coaster-wheelie score bonus (scaled by clutch-engagement ratio). See
// calculateTrickScore(); tune this rather than COASTER_WHEELIE's base score.
constexpr int COASTER_SCORE_BONUS = 10;

inline int calculateTrickScore(const TrickInstance& trick, const FmxConfig& config) {
    // Base score × continuous rotation on the trick's primary axis.
    // Unlike the integer multiplier (used for display names like "Double Backflip"),
    // scoring scales smoothly: 540° backflip = 1.5× base, 350° = 0.97×.
    // Tricks with no rotation axis (Air, Burnout, Drift) use multiplier=1.
    float rotationScale = 1.0f;
    Fmx::RotationAxis axis = Fmx::getPrimaryAxis(trick.type);
    if (axis != Fmx::RotationAxis::NONE) {
        float peakDegrees = 0.0f;
        switch (axis) {
            case Fmx::RotationAxis::PITCH: peakDegrees = std::abs(trick.peakPitch); break;
            case Fmx::RotationAxis::YAW:   peakDegrees = std::abs(trick.peakYaw);   break;
            case Fmx::RotationAxis::ROLL:  peakDegrees = std::abs(trick.peakRoll);  break;
            default: break;
        }
        rotationScale = std::max(1.0f, peakDegrees / 360.0f);
    }

    int score = static_cast<int>(trick.baseScore * rotationScale);

    // Coaster bonus: scaled by clutch-engagement ratio so brief taps can't farm
    // the bonus on a long wheelie. Added BEFORE the duration/distance multiplier
    // below intentionally — a sustained coaster wheelie should compound both
    // the bonus and the duration scaling.
    // Design intent: COASTER_WHEELIE's base score in getTrickBaseScore() is the
    // same as WHEELIE's (10), and this bonus is what differentiates the two —
    // a perfect coaster (ratio=1.0) scores ~2× a regular wheelie of equivalent
    // duration. If you want to change relative coaster value, tune
    // COASTER_SCORE_BONUS rather than the COASTER_WHEELIE base score.
    if (trick.type == Fmx::TrickType::COASTER_WHEELIE) {
        float ratio = (trick.duration > 0.0f)
            ? std::min(1.0f, trick.clutchHeldTime / trick.duration)
            : 0.0f;
        score += static_cast<int>(COASTER_SCORE_BONUS * ratio);
    }

    if (Fmx::isAirTrick(trick.type)) {
        // Air tricks: bonus for duration + distance
        // Duration: 1s=x1.25, 2s=x1.5, 4s=x2.0
        // Distance: 20m=+20%, 50m=+50%, 100m=+100%
        float airBonus = 1.0f + trick.duration * config.durationBonusRate
                              + trick.distance * config.distanceBonusRate;
        score = static_cast<int>(score * airBonus);
    } else {
        // Ground tricks: scale with duration + distance
        // Duration: longer trick = more points (floor at 1.0)
        // Distance: rewards covering ground (100m wheelie at speed > 100m wheelie crawling)
        // Stationary tricks (burnout, stoppie, donut) naturally get 0 distance bonus
        float fullDuration = (trick.type == Fmx::TrickType::WHEELIE ||
                              trick.type == Fmx::TrickType::COASTER_WHEELIE ||
                              trick.type == Fmx::TrickType::ENDO ||
                              trick.type == Fmx::TrickType::STOPPIE)
            ? Fmx::BALANCE_TRICK_FULL_DURATION : Fmx::GROUND_TRICK_FULL_DURATION;
        float groundBonus = std::max(1.0f, trick.duration / fullDuration)
                          + trick.distance * config.distanceBonusRate;
        score = static_cast<int>(score * groundBonus);
    }

    return score;
}

inline float calculateChainMultiplier(const std::vector<TrickInstance>& tricks,
                                           TrickType extraType, const FmxConfig& config) {
    // Each trick adds a bonus to the multiplier, but repeated trick types
    // add a diminishing bonus (THPS-style). L/R variants = same type.
    // 1st unique = +0.5, 2nd same = +0.25, 3rd same = +0.125, etc.
    float multiplier = 1.0f;
    size_t count = tricks.size();
    bool hasExtra = (extraType != Fmx::TrickType::NONE);

    // Total tricks = chain + optional active trick
    size_t totalTricks = count + (hasExtra ? 1 : 0);

    for (size_t i = 1; i < totalTricks; ++i) {
        Fmx::TrickType baseType = (i < count)
            ? Fmx::getBaseTrickType(tricks[i].type)
            : Fmx::getBaseTrickType(extraType);

        // Count how many times this base type appeared before this index
        int priorCount = 0;
        size_t searchEnd = std::min(i, count);
        for (size_t j = 0; j < searchEnd; ++j) {
            if (Fmx::getBaseTrickType(tricks[j].type) == baseType) {
                priorCount++;
            }
        }

        // Full bonus for first occurrence, diminishing for repeats
        float bonus = config.chainBonusPerTrick *
            static_cast<float>(std::pow(config.repetitionPenalty, priorCount));
        multiplier += bonus;
    }

    return multiplier;
}
}  // namespace Fmx
