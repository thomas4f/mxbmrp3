// ============================================================================
// core/fmx_manager.cpp
// FMX (Freestyle Motocross) trick detection and scoring implementation
// Refactored: Dynamic classification - trick type determined every frame
// ============================================================================
#include "fmx_manager.h"
#include "fmx_manager_internal.h"
#include "plugin_data.h"
#include "plugin_constants.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cmath>

// Singleton instance
FmxManager& FmxManager::getInstance() {
    static FmxManager instance;
    return instance;
}

FmxManager::FmxManager() {
    m_chainTricks.reserve(8);
    reset();
}

void FmxManager::reset() {
    m_score.reset();
    m_activeTrick = Fmx::TrickInstance();
    m_rotationTracker.reset();
    m_groundState = Fmx::GroundContactState();
    m_prevGroundState = Fmx::GroundContactState();
    m_chainTricks.clear();
    m_chainEndAnimation.active = false;
    m_chainEndAnimation.success = false;
    m_chainEndAnimation.startProgress = 0.0f;
    m_chainEndAnimation.duration = 0.0f;
    m_chainEndAnimation.finalType = Fmx::TrickType::NONE;
    m_chainEndAnimation.chainTricks.clear();
    m_chainEndAnimation.chainScore = 0;
    m_committedDirection = Fmx::TrickDirection::NONE;
    m_chainTimerPaused = false;
    m_chainPausedElapsed = 0.0f;
    m_bFirstUpdate = true;
    m_bHasPrevPosition = false;
    m_sessionTime = 0.0f;
    m_groundPendingTime = 0.0f;
    m_airToGroundTime = 0.0f;
    m_continuousAirborneTime = 0.0f;
    m_stuckTime = 0.0f;
    DEBUG_INFO("FmxManager: Reset");
}

void FmxManager::resetScore() {
    m_score.reset();
}

// ============================================================================
// Debug Logging
// ============================================================================
void FmxManager::logFrame(const Unified::TelemetryData& telemetry) {
    if (m_activeTrick.state == Fmx::TrickState::IDLE) {
        return;
    }

    // Rate limit to 10fps
    auto now = Fmx::clockNow();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastLogTime).count();
    if (elapsed < 100) {
        return;
    }
    m_lastLogTime = now;

    const char* state = Fmx::getTrickStateName(m_activeTrick.state);
    const char* trick = (m_activeTrick.type == Fmx::TrickType::NONE)
        ? "---" : Fmx::getTrickName(m_activeTrick.type);

    // Line 1: State + trick info + wheels + speed + crash
    if (m_activeTrick.state == Fmx::TrickState::CHAIN) {
        DEBUG_INFO_F("FMX: [%s] %s chain=%d pts=%d remain=%.1fs F%dR%d spd=%.1f crash=%d",
            state, trick,
            m_score.chainCount, m_score.chainScore,
            std::max(0.0f, m_config.chainPeriod - m_score.chainElapsed),
            m_groundState.frontWheelContact ? 1 : 0,
            m_groundState.rearWheelContact ? 1 : 0,
            telemetry.speedometer, telemetry.crashed);
    } else {
        DEBUG_INFO_F("FMX: [%s] %s prog=%.0f%% x%d dur=%.2fs F%dR%d spd=%.1f crash=%d",
            state, trick,
            m_activeTrick.progress * 100.0f,
            m_activeTrick.multiplier,
            m_activeTrick.duration,
            m_groundState.frontWheelContact ? 1 : 0,
            m_groundState.rearWheelContact ? 1 : 0,
            telemetry.speedometer, telemetry.crashed);
    }

    // Line 2: Rotation arcs (start/accumulated/peak per axis) + current world-space angles
    DEBUG_INFO_F("FMX:   P[%+.0f %+.0f ^%.0f] Y[%+.0f %+.0f ^%.0f] R[%+.0f %+.0f ^%.0f] world[%+.0f %+.0f %+.0f] wp[%+.0f %+.0f]",
        m_rotationTracker.startPitch, m_rotationTracker.accumulatedPitch, m_rotationTracker.peakPitch,
        m_rotationTracker.startYaw, m_rotationTracker.accumulatedYaw, m_rotationTracker.peakYaw,
        m_rotationTracker.startRoll, m_rotationTracker.accumulatedRoll, m_rotationTracker.peakRoll,
        m_rotationTracker.currentPitch, m_rotationTracker.currentYaw, m_rotationTracker.currentRoll,
        m_rotationTracker.minWorldPitch, m_rotationTracker.peakWorldPitch);
}
