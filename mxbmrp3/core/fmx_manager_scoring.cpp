// ============================================================================
// core/fmx_manager_scoring.cpp
// FMX trick lifecycle, chaining and scoring — start/complete/fail a trick, the
// grace/chain state transitions, banking, and the score/multiplier wrappers.
// Extracted verbatim from fmx_manager.cpp; the FmxManager class, members, and
// API are unchanged. The two scoring calculations themselves now live as pure,
// config-parameterized free functions in fmx_scoring.h (unit-tested headless);
// FmxManager::calculateTrickScore/calculateChainMultiplier delegate to them.
// ============================================================================
#include "fmx_manager.h"
#include "fmx_manager_internal.h"
#include "fmx_scoring.h"
#include "plugin_data.h"
#include "plugin_constants.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cmath>

void FmxManager::initializeNewTrick(const Unified::TelemetryData& telemetry) {
    m_activeTrick = Fmx::TrickInstance();
    m_activeTrick.state = Fmx::TrickState::ACTIVE;
    m_activeTrick.startTime = Fmx::clockNow();
    m_activeTrick.startPitch = telemetry.pitch;
    m_activeTrick.startYaw = telemetry.yaw;
    m_activeTrick.startRoll = telemetry.roll;
    m_continuousAirborneTime = 0.0f;
    m_airToGroundTime = 0.0f;
    m_rotationTracker.startTracking();
    m_rotationTracker.startPitch = telemetry.pitch;
    m_rotationTracker.startYaw = telemetry.yaw;
    m_rotationTracker.startRoll = telemetry.roll;
}

void FmxManager::startTrick(const Unified::TelemetryData& telemetry) {
    bool wasChaining = (m_activeTrick.state == Fmx::TrickState::CHAIN);
    int prevChainCount = m_score.chainCount;

    m_committedDirection = Fmx::TrickDirection::NONE;
    m_stuckTime = 0.0f;
    m_chainTimerPaused = false;
    initializeNewTrick(telemetry);

    if (wasChaining) {
        FMX_LOG("FMX: ACTIVE (chain #%d)", prevChainCount + 1);
    } else {
        FMX_LOG("FMX: ACTIVE (new)");
    }
}

void FmxManager::enterGrace() {
    m_activeTrick.state = Fmx::TrickState::GRACE;
    m_activeTrick.graceStartTime = Fmx::clockNow();

    FMX_LOG("FMX: GRACE %s prog=%.0f%% score=%d",
        Fmx::getTrickName(m_activeTrick.type),
        m_activeTrick.progress * 100.0f,
        calculateTrickScore(m_activeTrick));
}

void FmxManager::addTrickToChain() {
    m_activeTrick.finalScore = calculateTrickScore(m_activeTrick);
    m_score.chainScore += m_activeTrick.finalScore;
    m_score.chainCount++;
    m_chainTricks.push_back(m_activeTrick);
}

void FmxManager::enterChainState() {
    bool firstInChain = (m_score.chainCount == 0);

    addTrickToChain();

    if (firstInChain) {
        FMX_LOG("FMX: ============= CHAIN START =============");
    }

    FMX_LOG("FMX: CHAIN %s +%d (chain: %d tricks %d pts)",
        Fmx::getTrickName(m_activeTrick.type),
        m_activeTrick.finalScore,
        m_score.chainCount,
        m_score.chainScore);

    m_activeTrick.state = Fmx::TrickState::CHAIN;
    m_score.restartChainTimer();
}

void FmxManager::completeTrick() {
    m_activeTrick.state = Fmx::TrickState::COMPLETED;
    m_activeTrick.endTime = Fmx::clockNow();

    // Bank chain score (chainCount is always >= 1 here — only called from CHAIN state)
    int totalScore = m_score.chainScore;
    int totalTricks = m_score.chainCount;

    // Apply chain multiplier: unique tricks add full bonus, repeats add diminishing bonus
    float chainMultiplier = calculateChainMultiplier(m_chainTricks);
    totalScore = static_cast<int>(totalScore * chainMultiplier);

    // Update session score
    m_score.sessionScore += totalScore;
    m_score.tricksCompleted += totalTricks;
    if (totalScore > m_score.bestComboScore) {
        m_score.bestComboScore = totalScore;
    }
    m_score.currentTrickScore = 0;

    FMX_LOG("FMX: COMPLETED %d tricks +%d pts (x%.1f chain) (session: %d)",
        totalTricks, totalScore, chainMultiplier, m_score.sessionScore);
    FMX_LOG("FMX: ============== CHAIN END ==============");

    // Note: individual trick callbacks already fired via addTrickToChain()

    // Start chain-end animation (success) — copy chain before clearing so the
    // HUD can linger the completed chain in green for the animation duration,
    // mirroring the red linger that already happens on failure.
    m_chainEndAnimation.active = true;
    m_chainEndAnimation.success = true;
    m_chainEndAnimation.startTime = Fmx::clockNow();
    m_chainEndAnimation.startProgress = m_activeTrick.progress;
    m_chainEndAnimation.duration = m_config.chainPeriod;
    m_chainEndAnimation.finalType = m_chainTricks.empty()
        ? Fmx::TrickType::NONE
        : m_chainTricks.back().type;
    m_chainEndAnimation.chainScore = m_score.chainScore;
    m_chainEndAnimation.chainTricks = std::move(m_chainTricks);

    // Reset chain and trick state (re-reserve after move left m_chainTricks
    // in unspecified state, matches failTrick's reset behavior).
    m_score.clearChain();
    m_chainTricks.clear();
    m_chainTricks.reserve(8);
    m_activeTrick = Fmx::TrickInstance();
}

void FmxManager::failTrick(bool crashed) {
    // Save previous state before overwriting — needed to detect if trick was already in chain
    bool wasInChain = (m_activeTrick.state == Fmx::TrickState::CHAIN);

    m_activeTrick.state = Fmx::TrickState::FAILED;
    m_activeTrick.endTime = Fmx::clockNow();
    m_score.currentTrickScore = 0;

    // Check if trick reached minimum threshold (was "committed"). A trick that
    // never classified (type=NONE) is never committed regardless of its
    // accumulated progress — the default progress formula for NONE is
    // (duration / 1.0s) so an unclassified trick alive > 250ms would otherwise
    // be falsely treated as committed and kill the chain on failure.
    bool wasCommitted = m_activeTrick.type != Fmx::TrickType::NONE &&
                        m_activeTrick.progress >= Fmx::getMinProgress(m_activeTrick.type);

    // A crash always kills the chain, even if the current trick wasn't committed
    if (wasCommitted || (crashed && m_score.chainCount > 0)) {
        // Trick was committed - lose the chain
        m_score.tricksFailed++;

        // Start chain-end animation (failure) — copy chain before clearing
        m_chainEndAnimation.active = true;
        m_chainEndAnimation.success = false;
        m_chainEndAnimation.startTime = Fmx::clockNow();
        m_chainEndAnimation.startProgress = m_activeTrick.progress;
        m_chainEndAnimation.duration = m_config.chainPeriod;  // Match chain cooldown
        m_chainEndAnimation.finalType = m_activeTrick.type;
        m_chainEndAnimation.chainScore = m_score.chainScore;

        // Move chain tricks (m_chainTricks is about to be cleared anyway)
        m_chainEndAnimation.chainTricks = std::move(m_chainTricks);
        // Only add active trick if it's not already in the chain
        // (CHAIN state = trick was already added by addTrickToChain, avoid double-counting)
        if (m_activeTrick.type != Fmx::TrickType::NONE && !wasInChain) {
            m_chainEndAnimation.chainTricks.push_back(m_activeTrick);
        }

        FMX_LOG("FMX: FAILED %s (lost chain: %d tricks %d pts)",
            Fmx::getTrickName(m_activeTrick.type),
            m_score.chainCount,
            m_score.chainScore);
        if (m_score.chainCount > 0) {
            FMX_LOG("FMX: ============= CHAIN BROKEN ============");
        }

        // Reset chain (re-reserve after move left m_chainTricks in unspecified state)
        m_score.clearChain();
        m_chainTricks.clear();
        m_chainTricks.reserve(8);
        m_activeTrick = Fmx::TrickInstance();
    } else {
        // Trick wasn't committed - just discard this attempt, preserve chain
        FMX_LOG("FMX: Discard %s prog=%.0f%% (chain preserved)",
            Fmx::getTrickName(m_activeTrick.type),
            m_activeTrick.progress * 100.0f);

        // Return to chain state if we have banked tricks
        if (m_score.chainCount > 0) {
            m_activeTrick.state = Fmx::TrickState::CHAIN;

            // Resume chain timer from where it was (paused at commit, or last updated value)
            float resumeElapsed = m_chainTimerPaused ? m_chainPausedElapsed : m_score.chainElapsed;
            auto now = Fmx::clockNow();
            auto resumeDuration = std::chrono::milliseconds(static_cast<long long>(resumeElapsed * 1000));
            m_score.chainStartTime = now - resumeDuration;
            m_score.chainElapsed = resumeElapsed;
            m_chainTimerPaused = false;
        } else {
            m_activeTrick = Fmx::TrickInstance();
        }
    }
}

void FmxManager::bankAndContinue(const Unified::TelemetryData& telemetry) {
    // Bank the current trick and immediately start a new one
    // Used when transitioning domains (ground → air) with a good trick
    bool firstInChain = (m_score.chainCount == 0);

    addTrickToChain();

    if (firstInChain) {
        FMX_LOG("FMX: ============= CHAIN START =============");
    }

    FMX_LOG("FMX: Bank %s +%d (chain: %d tricks %d pts)",
        Fmx::getTrickName(m_activeTrick.type),
        m_activeTrick.finalScore,
        m_score.chainCount,
        m_score.chainScore);

    // Start fresh trick immediately (stay in ACTIVE)
    initializeNewTrick(telemetry);

    // Set initial airborne state for the new trick. hasBeenAirborne is set
    // directly (bypassing the AIRBORNE_DEBOUNCE_TIME accumulator) because the
    // caller has already confirmed sustained airborne — bankAndContinue is
    // only invoked from updateTrickDetection right after justConfirmedAirborne
    // fires, which itself only fires after the debounce timer crossed its
    // threshold. So by the time we get here, the rider has already been
    // airborne for the full debounce duration and the new trick is a
    // continuation of confirmed flight, not a fresh airborne event that needs
    // re-debouncing. Don't add a new caller path without re-evaluating this
    // invariant.
    bool airborne = m_groundState.isAirborne();
    m_activeTrick.isCurrentlyAirborne = airborne;
    m_activeTrick.hasBeenAirborne = airborne;

    FMX_LOG("FMX: ACTIVE (banked, fresh start)");
}

// ============================================================================
// Score Calculation
// ============================================================================

int FmxManager::calculateTrickScore(const Fmx::TrickInstance& trick) const {
    return Fmx::calculateTrickScore(trick, m_config);
}

float FmxManager::calculateChainMultiplier(const std::vector<Fmx::TrickInstance>& tricks,
                                           Fmx::TrickType extraType) const {
    return Fmx::calculateChainMultiplier(tricks, extraType, m_config);
}
