// ============================================================================
// core/fmx_trick_stats.h
// A trick's measurements as the FMX displays show them: how long it took,
// how far it went, how high it flew and how far it turned on its own axis.
// ONE rule for every display that shows them (the FMX HUD's stats row and the
// Combo deck's), so the two never disagree on what a trick measured or on how
// long the numbers stay up.
//
// While a trick turns (past its threshold, or in its landing grace) the
// numbers follow it; after that they HOLD on that trick until the rider is
// truly idle and back up: no trick, no chain, no chain-end showing, not down.
// A crash holds them too - a trick's measurements are not what was lost.
// ============================================================================
#pragma once

#include "fmx_manager.h"

#include <cmath>

namespace Fmx {

// The trick is turning: past its threshold, or in its landing grace. Before
// that it is only a lean or a lift that may come to nothing.
inline bool isTurning(const TrickInstance& trick) {
    return trick.type != TrickType::NONE &&
        ((trick.state == TrickState::ACTIVE && trick.progress >= getMinProgress(trick.type)) ||
         trick.state == TrickState::GRACE);
}

struct TrickStats {
    float duration = 0.0f;   // seconds
    float distance = 0.0f;   // metres
    float height = 0.0f;     // peak metres above take-off (0 for a ground trick)
    float rotation = 0.0f;   // peak degrees on the trick's primary axis
    bool hasData = false;

    // Once per frame (or per rebuild): follow the trick turning, hold the
    // last, clear when idle.
    void update(const FmxManager& fmx, bool crashed) {
        const TrickInstance& trick = fmx.getActiveTrick();
        if (isTurning(trick)) {
            const RotationTracker& tracker = fmx.getRotationTracker();
            duration = trick.duration;
            distance = trick.distance;
            height = trick.peakHeight;
            switch (getPrimaryAxis(trick.type)) {
                case RotationAxis::PITCH: rotation = std::abs(tracker.peakPitch); break;
                case RotationAxis::YAW:   rotation = std::abs(tracker.peakYaw);   break;
                case RotationAxis::ROLL:  rotation = std::abs(tracker.peakRoll);  break;
                default:                  rotation = 0.0f;                        break;
            }
            hasData = true;
        } else if (trick.state == TrickState::IDLE && fmx.getScore().chainCount == 0 &&
                   !fmx.getChainEndAnimation().active && !crashed) {
            *this = TrickStats();
        }
    }
    // Shown only for a trick that flew / turned: a wheelie has neither
    bool hasHeight() const { return height >= 1.0f; }
    bool hasRotation() const { return rotation >= 1.0f; }
};

// THE ENDED CHAIN HOLDS THROUGH A CRASH. FmxManager's ChainEndAnimation
// lingers a finished chain for chainPeriod and then clears it; the rotation
// arcs hold their snapshot and their red for the whole crashed state, past
// that timer, until recovery. The ended chain follows: while the animation
// runs its numbers are copied here, and while the rider is still down after
// it ends the copy stays up. ONE rule for the FMX HUD's combo block and the
// Combo deck. POD only: the newest MAX_TRICKS tricks are kept by type.
struct ChainHold {
    static constexpr int MAX_TRICKS = 8;
    bool held = false;
    bool success = false;
    float multiplier = 1.0f;
    int chainScore = 0;       // its points before the multiplier
    int bankedScore = 0;      // what it added to the session (0 when lost)
    int lastTrickScore = 0;   // the last trick's own score
    int trickCount = 0;       // the chain's tricks, all of them
    TrickType tricks[MAX_TRICKS] = {};   // the newest min(trickCount, MAX_TRICKS), oldest first

    int keptTricks() const { return trickCount < MAX_TRICKS ? trickCount : MAX_TRICKS; }

    // Once per frame (or per rebuild): copy the chain while it animates, keep
    // it while the rider is down, let it go once up.
    void update(const FmxManager& fmx, bool crashed) {
        const FmxManager::ChainEndAnimation& end = fmx.getChainEndAnimation();
        if (end.active) {
            held = true;
            success = end.success;
            multiplier = fmx.calculateChainMultiplier(end.chainTricks);
            chainScore = end.chainScore;
            bankedScore = end.bankedScore;
            trickCount = static_cast<int>(end.chainTricks.size());
            lastTrickScore = trickCount > 0 ? end.chainTricks.back().finalScore : 0;
            const int first = trickCount - keptTricks();
            for (int i = 0; i < keptTricks(); ++i) tricks[i] = end.chainTricks[static_cast<size_t>(first + i)].type;
        } else if (!crashed) {
            held = false;
        }
    }
};

}  // namespace Fmx
