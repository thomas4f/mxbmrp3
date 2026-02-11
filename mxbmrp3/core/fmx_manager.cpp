// ============================================================================
// core/fmx_manager.cpp
// FMX (Freestyle Motocross) trick detection and scoring implementation
// Refactored: Dynamic classification - trick type determined every frame
// ============================================================================
#include "fmx_manager.h"
#include "plugin_data.h"
#include "plugin_constants.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cmath>

using PluginConstants::Math::DEG_TO_RAD;
using PluginConstants::Math::RAD_TO_DEG;

// Log helper: all FMX logging gated behind m_bLoggingEnabled
#define FMX_LOG(...) do { if (m_bLoggingEnabled) DEBUG_INFO_F(__VA_ARGS__); } while(0)

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
    m_failureAnimation.active = false;
    m_failureAnimation.startProgress = 0.0f;
    m_failureAnimation.duration = 2.0f;
    m_failureAnimation.failedType = Fmx::TrickType::NONE;
    m_failureAnimation.lostChainTricks.clear();
    m_failureAnimation.lostChainScore = 0;
    m_committedDirection = Fmx::TrickDirection::NONE;
    m_chainTimerPaused = false;
    m_chainPausedElapsed = 0.0f;
    m_bFirstUpdate = true;
    m_bHasPrevPosition = false;
    m_sessionTime = 0.0f;
    m_groundPendingTime = 0.0f;
    m_stuckTime = 0.0f;
    DEBUG_INFO("FmxManager: Reset");
}

void FmxManager::resetScore() {
    m_score.reset();
}

// ============================================================================
// Main Telemetry Update
// ============================================================================
void FmxManager::updateFromTelemetry(const Unified::TelemetryData& telemetry) {
    // Skip updates while game is paused (RunStop → isPlayerRunning=false).
    // Mark m_bFirstUpdate so the resume frame uses a clean default dt
    // instead of the accumulated pause duration.
    if (!PluginData::getInstance().isPlayerRunning()) {
        m_bFirstUpdate = true;
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Compensate for pause: if the gap since last telemetry call exceeds a
    // threshold, the game was paused. MX Bikes calls RunStop on pause but
    // does NOT call RunTelemetry during the pause window, so the
    // isPlayerRunning() early-return above never fires and m_bFirstUpdate
    // is never set. Detect the pause from the telemetry gap instead.
    // m_bHasPrevPosition guards against the very first frame (no valid timestamps yet).
    if (m_bHasPrevPosition) {
        auto rawElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
            now - m_lastUpdateTime).count();
        constexpr long long PAUSE_THRESHOLD_US = 200000;  // 200ms (telemetry runs at ~10ms)
        if (rawElapsedUs > PAUSE_THRESHOLD_US) {
            auto pauseDuration = now - m_lastUpdateTime;
            m_activeTrick.startTime += pauseDuration;
            m_activeTrick.graceStartTime += pauseDuration;
            m_score.chainStartTime += pauseDuration;
            m_rotationTracker.trackingStartTime += pauseDuration;
            m_failureAnimation.startTime += pauseDuration;
            m_lastLogTime += pauseDuration;
            m_rotationTracker.hasPreviousFrame = false;  // Don't accumulate rotation across pause
        }
    }

    // Calculate delta time, clamped as a fallback against system hitches
    float dt = 0.01f;  // Default 10ms (100Hz)
    if (!m_bFirstUpdate) {
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            now - m_lastUpdateTime).count();
        dt = std::min(elapsed / 1000000.0f, 0.1f);  // Cap at 100ms (10fps minimum)
    }

    m_lastUpdateTime = now;
    m_bFirstUpdate = false;
    m_sessionTime += dt;

    // Teleport detection - prevent reset-to-track from scoring tricks in grace/chain
    if (m_bHasPrevPosition) {
        float dx = telemetry.posX - m_prevPosX;
        float dy = telemetry.posY - m_prevPosY;
        float dz = telemetry.posZ - m_prevPosZ;
        float distSq = dx * dx + dy * dy + dz * dz;
        constexpr float TELEPORT_THRESHOLD_SQ = 2.0f * 2.0f;  // 2 meters per frame
        if (distSq > TELEPORT_THRESHOLD_SQ) {
            FMX_LOG("Teleport detected (%.1fm) - failing active trick",
                std::sqrt(distSq));
            if (m_activeTrick.state == Fmx::TrickState::ACTIVE ||
                m_activeTrick.state == Fmx::TrickState::GRACE ||
                m_activeTrick.state == Fmx::TrickState::CHAIN) {
                failTrick(true);  // Kill chain on teleport
            }
        }
    }

    // Check for crash - fail any active trick or chain
    if (telemetry.crashed) {
        if (m_activeTrick.state == Fmx::TrickState::ACTIVE ||
            m_activeTrick.state == Fmx::TrickState::GRACE ||
            m_activeTrick.state == Fmx::TrickState::CHAIN) {
            failTrick(true);  // crashed = true: always kill chain
        }
    }

    // Update subsystems
    updateGroundContact(telemetry);
    updateRotation(telemetry, dt);
    updateTrickDetection(telemetry, dt);

    // Debug logging
    if (m_bLoggingEnabled) {
        logFrame(telemetry);
    }

    // Update failure animation (auto-deactivate after duration)
    if (m_failureAnimation.active) {
        auto elapsed = std::chrono::duration<float>(now - m_failureAnimation.startTime).count();
        if (elapsed >= m_failureAnimation.duration) {
            m_failureAnimation.active = false;
            m_failureAnimation.lostChainTricks.clear();
        }
    }

    // Store position for next frame (only fields needed for teleport/distance)
    m_prevPosX = telemetry.posX;
    m_prevPosY = telemetry.posY;
    m_prevPosZ = telemetry.posZ;
    m_bHasPrevPosition = true;
    m_prevGroundState = m_groundState;
}

// ============================================================================
// Ground Contact State Update
// ============================================================================
void FmxManager::updateGroundContact(const Unified::TelemetryData& telemetry) {
    m_groundState.frontWheelContact = telemetry.wheelMaterial[0] != 0;
    m_groundState.rearWheelContact = telemetry.wheelMaterial[1] != 0;
    m_groundState.frontWheelSpeed = telemetry.wheelSpeed[0];
    m_groundState.rearWheelSpeed = telemetry.wheelSpeed[1];
    m_groundState.vehicleSpeed = telemetry.speedometer;

    // Calculate lateral slip angle (angle between heading and velocity vector)
    float yawRad = telemetry.yaw * DEG_TO_RAD;
    float sinYaw = std::sin(yawRad);
    float cosYaw = std::cos(yawRad);

    float forwardVel = telemetry.velocityX * sinYaw + telemetry.velocityZ * cosYaw;
    float lateralVel = telemetry.velocityX * cosYaw - telemetry.velocityZ * sinYaw;

    m_groundState.lateralVelocity = lateralVel;
    if (std::abs(forwardVel) > 1.0f || std::abs(lateralVel) > 1.0f) {
        m_groundState.lateralSlipAngle = std::atan2(std::abs(lateralVel), std::abs(forwardVel)) * RAD_TO_DEG;
    } else {
        m_groundState.lateralSlipAngle = 0.0f;
    }
}

// ============================================================================
// Rotation Tracking (preserved - works well)
// ============================================================================
void FmxManager::updateRotation(const Unified::TelemetryData& telemetry, float dt) {
    // Update current Euler angles
    m_rotationTracker.currentPitch = telemetry.pitch;
    m_rotationTracker.currentYaw = telemetry.yaw;
    m_rotationTracker.currentRoll = telemetry.roll;

    // Track peak world-space pitch for Turn Up/Down classification
    // Only track once yaw has started accumulating — excludes ramp angle at launch
    if (std::abs(m_rotationTracker.accumulatedYaw) >= Fmx::TURN_YAW_THRESHOLD) {
        if (telemetry.pitch > m_rotationTracker.peakWorldPitch)
            m_rotationTracker.peakWorldPitch = telemetry.pitch;
        if (telemetry.pitch < m_rotationTracker.minWorldPitch)
            m_rotationTracker.minWorldPitch = telemetry.pitch;
    }

    // Update angular velocities
    m_rotationTracker.pitchVelocity = telemetry.pitchVel;
    m_rotationTracker.yawVelocity = telemetry.yawVel;
    m_rotationTracker.rollVelocity = telemetry.rollVel;

    // Track rotation when ANY wheel is off the ground (wheelie, endo, or airborne)
    bool shouldTrackRotation = !m_groundState.frontWheelContact || !m_groundState.rearWheelContact;

    if (!shouldTrackRotation) {
        // Still update tracking duration/height for grounded ACTIVE tricks (burnout, drift, etc.)
        if (m_activeTrick.state == Fmx::TrickState::ACTIVE) {
            m_rotationTracker.updateTracking(telemetry.posY);
        }
        m_rotationTracker.hasPreviousFrame = false;
        return;
    }

    // Use angular velocity integration for accumulation
    if (m_rotationTracker.hasPreviousFrame) {
        m_rotationTracker.accumulatedPitch += telemetry.pitchVel * dt;
        m_rotationTracker.accumulatedYaw += telemetry.yawVel * dt;
        m_rotationTracker.accumulatedRoll += telemetry.rollVel * dt;

        // Track peak rotation (furthest extent from 0)
        if (std::abs(m_rotationTracker.accumulatedPitch) > std::abs(m_rotationTracker.peakPitch)) {
            m_rotationTracker.peakPitch = m_rotationTracker.accumulatedPitch;
        }
        if (std::abs(m_rotationTracker.accumulatedYaw) > std::abs(m_rotationTracker.peakYaw)) {
            m_rotationTracker.peakYaw = m_rotationTracker.accumulatedYaw;
        }
        if (std::abs(m_rotationTracker.accumulatedRoll) > std::abs(m_rotationTracker.peakRoll)) {
            m_rotationTracker.peakRoll = m_rotationTracker.accumulatedRoll;
        }
    }

    m_rotationTracker.updateTracking(telemetry.posY);
    m_rotationTracker.hasPreviousFrame = true;
}

// ============================================================================
// Main Trick Detection State Machine
// ============================================================================
void FmxManager::updateTrickDetection(const Unified::TelemetryData& telemetry, float dt) {
    auto now = std::chrono::steady_clock::now();

    switch (m_activeTrick.state) {
        case Fmx::TrickState::IDLE:
            // Check if any trick initiation condition is met
            if (shouldStartTrick()) {
                if (m_groundState.isAirborne()) {
                    // Air trick — instant start, no debounce
                    m_groundPendingTime = 0.0f;
                    startTrick(telemetry);
                } else {
                    // Ground trick — debounce to filter micro-lifts from bumps
                    m_groundPendingTime += dt;
                    if (m_groundPendingTime >= GROUND_DEBOUNCE_TIME) {
                        float pendingTime = m_groundPendingTime;
                        m_groundPendingTime = 0.0f;
                        startTrick(telemetry);
                        m_activeTrick.duration = pendingTime;  // Backdate: no time lost
                    }
                }
            } else {
                m_groundPendingTime = 0.0f;
            }
            break;

        case Fmx::TrickState::ACTIVE: {
            // Update timing
            m_activeTrick.duration += dt;

            // Update airborne tracking
            bool airborne = m_groundState.isAirborne();
            bool wasAirborne = m_prevGroundState.isAirborne();
            m_activeTrick.isCurrentlyAirborne = airborne;
            if (airborne) {
                m_activeTrick.hasBeenAirborne = true;
            }

            // =========================================================
            // DOMAIN TRANSITION: Ground → Air
            // If we were doing a good ground trick and just went airborne,
            // bank the ground trick and start fresh for the air portion
            // =========================================================
            bool groundToAir = !wasAirborne && airborne;
            if (groundToAir && m_activeTrick.type != Fmx::TrickType::NONE) {
                bool isGroundTrick = !Fmx::isAirTrick(m_activeTrick.type);
                if (isGroundTrick && m_activeTrick.progress >= Fmx::MIN_GROUND_TRICK_PROGRESS) {
                    // Ground trick was good enough - bank it and start fresh
                    FMX_LOG("FMX: Ground->Air bank %s prog=%.0f%%",
                        Fmx::getTrickName(m_activeTrick.type),
                        m_activeTrick.progress * 100.0f);
                    bankAndContinue(telemetry);
                    break;  // Exit this frame, continue with fresh trick next frame
                }
            }

            // Update rotation in active trick
            m_activeTrick.accumulatedPitch = m_rotationTracker.accumulatedPitch;
            m_activeTrick.accumulatedYaw = m_rotationTracker.accumulatedYaw;
            m_activeTrick.accumulatedRoll = m_rotationTracker.accumulatedRoll;
            m_activeTrick.peakPitch = m_rotationTracker.peakPitch;
            m_activeTrick.peakYaw = m_rotationTracker.peakYaw;
            m_activeTrick.peakRoll = m_rotationTracker.peakRoll;

            // Accumulate horizontal distance traveled
            if (m_bHasPrevPosition) {
                float dx = telemetry.posX - m_prevPosX;
                float dz = telemetry.posZ - m_prevPosZ;
                float horizDist = std::sqrt(dx * dx + dz * dz);
                if (horizDist < 2.0f) {  // Skip teleports (same threshold as rotation)
                    m_activeTrick.distance += horizDist;
                }
            }

            // DYNAMIC CLASSIFICATION: Determine trick type based on current state
            Fmx::TrickType newType = classifyCurrentTrick();
            if (newType != m_activeTrick.type && newType != Fmx::TrickType::NONE) {
                // Apply committed L/R direction. Once the player commits to a direction
                // (e.g., Left on Scrub L), ALL subsequent reclassifications keep it —
                // even through non-directional intermediaries (Scrub L → Backflip → Flat 360 L).
                // Different tricks use different axes for L/R, but the player committed
                // to one rotational direction and should see it consistently.
                if (m_committedDirection != Fmx::TrickDirection::NONE) {
                    newType = Fmx::withDirection(newType, m_committedDirection);
                } else {
                    Fmx::TrickDirection dir = Fmx::getTrickDirection(newType);
                    if (dir != Fmx::TrickDirection::NONE) {
                        m_committedDirection = dir;
                    }
                }

                if (newType != m_activeTrick.type) {
                    if (m_activeTrick.type != Fmx::TrickType::NONE) {
                        FMX_LOG("FMX: Reclassify %s -> %s",
                            Fmx::getTrickName(m_activeTrick.type), Fmx::getTrickName(newType));
                    }
                    m_activeTrick.type = newType;
                    m_activeTrick.baseScore = Fmx::getTrickBaseScore(newType);
                }
            }

            // Calculate progress based on current type
            m_activeTrick.progress = calculateProgress(m_activeTrick.type);

            // Calculate multiplier for rotation tricks (use peaks, same as classification)
            float absPitch = std::abs(m_activeTrick.peakPitch);
            float absRoll = std::abs(m_activeTrick.peakRoll);
            float absYaw = std::abs(m_activeTrick.peakYaw);

            // Multiplier: x1 = partial/single, x2 = two complete rotations, etc.
            // Pitch-based multiplier (flips and flip combos)
            if (m_activeTrick.type == Fmx::TrickType::BACKFLIP ||
                m_activeTrick.type == Fmx::TrickType::FRONTFLIP ||
                m_activeTrick.type == Fmx::TrickType::FLAT_360_LEFT ||
                m_activeTrick.type == Fmx::TrickType::FLAT_360_RIGHT) {
                m_activeTrick.multiplier = std::max(1, static_cast<int>(absPitch / 360.0f));
            // Roll-based multiplier (barrel rolls)
            } else if (m_activeTrick.type == Fmx::TrickType::BARREL_ROLL_LEFT ||
                       m_activeTrick.type == Fmx::TrickType::BARREL_ROLL_RIGHT) {
                m_activeTrick.multiplier = std::max(1, static_cast<int>(absRoll / 360.0f));
            // Yaw-based multiplier (spins, pivots)
            } else if (m_activeTrick.type == Fmx::TrickType::SPIN_LEFT ||
                       m_activeTrick.type == Fmx::TrickType::SPIN_RIGHT ||
                       m_activeTrick.type == Fmx::TrickType::PIVOT_LEFT ||
                       m_activeTrick.type == Fmx::TrickType::PIVOT_RIGHT) {
                m_activeTrick.multiplier = std::max(1, static_cast<int>(absYaw / 360.0f));
            }

            // Calculate current score
            m_activeTrick.finalScore = calculateTrickScore(m_activeTrick);
            m_score.currentTrickScore = m_activeTrick.finalScore;

            // Chain timer: keep running during ACTIVE within a chain, pause when committed.
            // This lets the chain countdown continue while tracking a new trick, but pauses
            // once committed so the chain window isn't consumed while performing the trick.
            if (m_score.chainCount > 0 && !m_chainTimerPaused) {
                m_score.updateChainElapsed();

                bool isCommitted = m_activeTrick.progress >= Fmx::getMinProgress(m_activeTrick.type)
                                   && m_activeTrick.type != Fmx::TrickType::NONE;
                if (isCommitted) {
                    m_chainPausedElapsed = m_score.chainElapsed;
                    m_chainTimerPaused = true;
                }
            }

            // Stuck detection — fail trick if stationary too long
            // Runs AFTER reclassification so Endo→Stoppie transition happens first
            // Stoppie/Burnout/Donut are legitimately stationary, skip them
            {
                bool isStationaryTrick =
                    m_activeTrick.type == Fmx::TrickType::STOPPIE ||
                    m_activeTrick.type == Fmx::TrickType::BURNOUT ||
                    m_activeTrick.type == Fmx::TrickType::DONUT ||
                    m_activeTrick.type == Fmx::TrickType::PIVOT_LEFT ||
                    m_activeTrick.type == Fmx::TrickType::PIVOT_RIGHT;

                if (!isStationaryTrick && m_groundState.isStationary()) {
                    m_stuckTime += dt;
                    if (m_stuckTime >= STUCK_THRESHOLD) {
                        FMX_LOG("FMX: Stuck detected (stationary %.1fs) - failing trick", m_stuckTime);
                        failTrick(true);
                        break;
                    }
                } else {
                    m_stuckTime = 0.0f;
                }
            }

            // Check if trick should end
            if (shouldEndTrick(telemetry)) {
                // Must be classified and past minimum progress to enter grace
                if (m_activeTrick.type != Fmx::TrickType::NONE &&
                    m_activeTrick.progress >= Fmx::getMinProgress(m_activeTrick.type)) {
                    enterGrace();
                } else {
                    // Unclassified or too short - fail it
                    failTrick();
                }
            }
            break;
        }

        case Fmx::TrickState::GRACE: {
            // Wait for grace period (crash detection)
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_activeTrick.graceStartTime).count();
            float graceSeconds = elapsed / 1000.0f;

            if (graceSeconds >= m_config.landingGracePeriod) {
                // Grace period passed without crash - enter chain
                enterChainState();
            }
            break;
        }

        case Fmx::TrickState::CHAIN: {
            // Update chain timer
            m_score.updateChainElapsed();

            // Check if new trick is starting (chain continues)
            if (shouldStartTrick()) {
                if (m_groundState.isAirborne()) {
                    m_groundPendingTime = 0.0f;
                    startTrick(telemetry);
                    break;
                }
                // Ground trick debounce during chain
                m_groundPendingTime += dt;
                if (m_groundPendingTime >= GROUND_DEBOUNCE_TIME) {
                    float pendingTime = m_groundPendingTime;
                    m_groundPendingTime = 0.0f;
                    startTrick(telemetry);
                    m_activeTrick.duration = pendingTime;
                    break;
                }
            } else {
                m_groundPendingTime = 0.0f;
            }

            // Check if chain timer expired
            if (m_score.chainElapsed >= m_config.chainPeriod) {
                completeTrick();
            }
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// Dynamic Trick Classification
// Uses peak accumulated rotation so tricks don't downgrade when the rider
// recovers for landing. Once you've rotated enough, the trick sticks.
// Direction (L/R) still uses current accumulated values (sign).
// ============================================================================
Fmx::TrickType FmxManager::classifyCurrentTrick() const {
    float absPitch = std::abs(m_rotationTracker.peakPitch);
    float absYaw = std::abs(m_rotationTracker.peakYaw);
    float absRoll = std::abs(m_rotationTracker.peakRoll);

    // Takeoff angles (for scrub detection)
    float absStartRoll = std::abs(m_rotationTracker.startRoll);

    using namespace Fmx;

    // Ground state checks
    bool airborne = m_groundState.isAirborne();
    bool wheeliePos = m_groundState.isWheeliePosition();
    bool endoPos = m_groundState.isEndoPosition();
    bool hasAirtime = m_activeTrick.hasBeenAirborne || airborne;

    // =========================================================================
    // PRIORITY 1: Multi-axis rotation tricks (highest skill)
    // =========================================================================
    if (hasAirtime) {
        // FLAT 360: flip + rolled sideways
        // Uses start + accumulated roll so launch lean contributes (40° lean + 40° added = 80°)
        // Can't use raw telemetry.roll (currentRoll) here — Euler angles are unreliable
        // during flips due to gimbal lock when pitch passes through ±90°
        {
            float effectiveRoll = m_rotationTracker.startRoll + m_rotationTracker.accumulatedRoll;
            if (absPitch >= FULL_ROTATION_MIN && std::abs(effectiveRoll) >= m_config.flat360MinRoll) {
                return (effectiveRoll > 0)
                    ? TrickType::FLAT_360_LEFT : TrickType::FLAT_360_RIGHT;
            }
        }
    }

    // =========================================================================
    // PRIORITY 2: Full rotation tricks (270°+ ensures commitment to rotation)
    // =========================================================================
    if (hasAirtime) {
        // Flip: pitch rotation past FULL_ROTATION_MIN (270°)
        if (absPitch >= FULL_ROTATION_MIN) {
            return (m_rotationTracker.accumulatedPitch < 0)
                ? TrickType::BACKFLIP : TrickType::FRONTFLIP;
        }

        // Barrel roll: roll rotation past FULL_ROTATION_MIN (270°)
        if (absRoll >= FULL_ROTATION_MIN) {
            return (m_rotationTracker.accumulatedRoll > 0)
                ? TrickType::BARREL_ROLL_LEFT : TrickType::BARREL_ROLL_RIGHT;
        }

        // Spin: yaw rotation past FULL_ROTATION_MIN (270°)
        if (absYaw >= FULL_ROTATION_MIN) {
            return (m_rotationTracker.accumulatedYaw > 0)
                ? TrickType::SPIN_RIGHT : TrickType::SPIN_LEFT;
        }
    }

    // =========================================================================
    // PRIORITY 3: Partial rotation tricks (30-179°)
    // =========================================================================
    if (hasAirtime) {
        // Turn Up/Down: significant yaw rotation with nose pointing up/down in world space
        // Uses peak world-space pitch — once the nose pointed up/down, the trick sticks
        if (absYaw >= TURN_YAW_THRESHOLD) {
            if (m_rotationTracker.minWorldPitch <= -TURN_PITCH_THRESHOLD) {
                return (m_rotationTracker.accumulatedYaw > 0)
                    ? TrickType::TURN_UP_RIGHT : TrickType::TURN_UP_LEFT;
            } else if (m_rotationTracker.peakWorldPitch >= TURN_PITCH_THRESHOLD) {
                return (m_rotationTracker.accumulatedYaw > 0)
                    ? TrickType::TURN_DOWN_RIGHT : TrickType::TURN_DOWN_LEFT;
            }
        }

        // Whip: yaw rotation with nose roughly level
        if (absYaw >= PARTIAL_ROTATION_MIN) {
            return (m_rotationTracker.accumulatedYaw > 0)
                ? TrickType::WHIP_RIGHT : TrickType::WHIP_LEFT;
        }

        // Scrub: roll rotation or takeoff lean
        if (absStartRoll >= PARTIAL_ROTATION_MIN || absRoll >= PARTIAL_ROTATION_MIN) {
            float rollDir = (absRoll >= PARTIAL_ROTATION_MIN)
                ? m_rotationTracker.accumulatedRoll : m_rotationTracker.startRoll;
            return (rollDir > 0)
                ? TrickType::SCRUB_LEFT : TrickType::SCRUB_RIGHT;
        }

        // Basic air (significant airtime but minimal rotation)
        // Use getHeightChange() to detect both uphill and downhill jumps
        if (m_activeTrick.duration >= m_config.airCommitTime &&
            m_rotationTracker.getHeightChange() >= m_config.airCommitHeight) {
            return TrickType::AIR;
        }
    }

    // =========================================================================
    // PRIORITY 4: Ground tricks
    // =========================================================================

    // Donut: burnout + yaw rotation
    if (isBurnoutActive() && absYaw >= m_config.donutYawThreshold) {
        return TrickType::DONUT;
    }

    // Burnout: stationary + rear wheel slip
    if (isBurnoutActive()) {
        return TrickType::BURNOUT;
    }

    // Drift: moving + lateral slip
    // lateralVelocity > 0 = rear slides right = bike turning left
    if (isDriftActive()) {
        return (m_groundState.lateralVelocity > 0)
            ? TrickType::DRIFT_LEFT : TrickType::DRIFT_RIGHT;
    }

    // Pivot: one-wheel yaw rotation at low speed (upgrades wheelie/endo/stoppie)
    if ((wheeliePos || endoPos) && absYaw >= m_config.pivotMinYaw &&
        m_groundState.vehicleSpeed < m_config.pivotMaxSpeed) {
        return (m_rotationTracker.accumulatedYaw > 0)
            ? TrickType::PIVOT_RIGHT : TrickType::PIVOT_LEFT;
    }

    // Stoppie: endo position + stationary (nearly stopped)
    if (endoPos && m_groundState.isStationary()) {
        return TrickType::STOPPIE;
    }

    // Endo: front wheel only + moving
    if (endoPos) {
        return TrickType::ENDO;
    }

    // Wheelie: rear wheel only
    if (wheeliePos) {
        return TrickType::WHEELIE;
    }

    return TrickType::NONE;
}

// ============================================================================
// Calculate Progress for Given Trick Type
// ============================================================================
float FmxManager::calculateProgress(Fmx::TrickType type) const {
    float absPitch = std::abs(m_rotationTracker.peakPitch);
    float absYaw = std::abs(m_rotationTracker.peakYaw);
    float absRoll = std::abs(m_rotationTracker.peakRoll);

    float progress = 0.0f;

    switch (type) {
        case Fmx::TrickType::BACKFLIP:
        case Fmx::TrickType::FRONTFLIP:
            progress = absPitch / m_config.flipCompletionAngle;
            break;

        case Fmx::TrickType::BARREL_ROLL_LEFT:
        case Fmx::TrickType::BARREL_ROLL_RIGHT:
            progress = absRoll / m_config.barrelRollCompletionAngle;
            break;

        case Fmx::TrickType::SPIN_LEFT:
        case Fmx::TrickType::SPIN_RIGHT:
            progress = absYaw / m_config.spinCompletionAngle;
            break;

        case Fmx::TrickType::SCRUB_LEFT:
        case Fmx::TrickType::SCRUB_RIGHT:
            progress = absRoll / m_config.scrubMaxAngle;
            break;

        case Fmx::TrickType::WHIP_LEFT:
        case Fmx::TrickType::WHIP_RIGHT:
        case Fmx::TrickType::TURN_UP_LEFT:
        case Fmx::TrickType::TURN_UP_RIGHT:
        case Fmx::TrickType::TURN_DOWN_LEFT:
        case Fmx::TrickType::TURN_DOWN_RIGHT:
            progress = absYaw / m_config.whipMaxAngle;
            break;

        case Fmx::TrickType::AIR:
            progress = m_activeTrick.duration / Fmx::AIR_TRICK_FULL_DURATION;
            break;

        case Fmx::TrickType::WHEELIE:
        case Fmx::TrickType::ENDO:
        case Fmx::TrickType::STOPPIE:
            progress = m_activeTrick.duration / Fmx::BALANCE_TRICK_FULL_DURATION;
            break;

        case Fmx::TrickType::PIVOT_LEFT:
        case Fmx::TrickType::PIVOT_RIGHT:
            progress = absYaw / m_config.pivotCompletionAngle;
            break;

        case Fmx::TrickType::BURNOUT:
        case Fmx::TrickType::DONUT:
        case Fmx::TrickType::DRIFT_LEFT:
        case Fmx::TrickType::DRIFT_RIGHT:
            progress = m_activeTrick.duration / Fmx::GROUND_TRICK_FULL_DURATION;
            break;

        case Fmx::TrickType::FLAT_360_LEFT:
        case Fmx::TrickType::FLAT_360_RIGHT:
            // Combination - use max of relevant axes
            progress = std::max(
                absPitch / m_config.flipCompletionAngle,
                absRoll / m_config.barrelRollCompletionAngle
            );
            break;

        default:
            progress = m_activeTrick.duration / 1.0f;
            break;
    }

    return progress;  // Don't clamp - allow > 1.0 for multipliers
}

// ============================================================================
// Ground Trick Condition Helpers
// ============================================================================
bool FmxManager::isBurnoutActive() const {
    return m_groundState.isStationary() && m_groundState.rearWheelContact &&
           m_groundState.getRearWheelSlip() > m_config.burnoutSlipThreshold;
}

bool FmxManager::isDriftActive() const {
    return !m_groundState.isStationary() && m_groundState.rearWheelContact &&
           m_groundState.lateralSlipAngle > m_config.driftSlipAngleThreshold;
}

// ============================================================================
// Check if Trick Should Start
// ============================================================================
bool FmxManager::shouldStartTrick() const {
    // Any wheel off ground — but require movement to prevent stuck-on-fence
    // restart loops (stuck at 0 m/s with wheel off → fail → restart → fail → ...)
    if (!m_groundState.frontWheelContact || !m_groundState.rearWheelContact) {
        return !m_groundState.isStationary();
    }

    return isBurnoutActive() || isDriftActive();
}

// ============================================================================
// Check if Active Trick Should End
// ============================================================================
bool FmxManager::shouldEndTrick(const Unified::TelemetryData& telemetry) const {
    Fmx::TrickType type = m_activeTrick.type;

    // If currently airborne with significant rotation, don't end yet
    float absPitch = std::abs(m_rotationTracker.accumulatedPitch);
    if (m_groundState.isAirborne() && absPitch >= Fmx::PARTIAL_ROTATION_MIN) {
        return false;  // Potential flip in progress
    }

    // Air tricks end when landing (both wheels down after being airborne)
    if (m_activeTrick.hasBeenAirborne && !m_activeTrick.isCurrentlyAirborne) {
        if (m_groundState.frontWheelContact && m_groundState.rearWheelContact) {
            return true;  // Landed
        }
    }

    // Ground trick specific end conditions
    switch (type) {
        case Fmx::TrickType::WHEELIE:
            // End when front wheel touches or nose drops
            // Pitch is negative during wheelie (nose up), so end when it rises above half the entry threshold
            if (m_groundState.frontWheelContact) return true;
            if (telemetry.pitch > -m_config.wheelieAngleThreshold * 0.5f) return true;
            break;

        case Fmx::TrickType::ENDO:
        case Fmx::TrickType::STOPPIE:
            // End when rear wheel touches or nose comes up.
            // endoAngleThreshold is -15° (negative = forward pitch). End at half
            // the absolute entry angle — once pitch drops below 7.5°, nose is nearly level.
            if (m_groundState.rearWheelContact) return true;
            if (telemetry.pitch < -m_config.endoAngleThreshold * 0.5f) return true;
            break;

        case Fmx::TrickType::BURNOUT:
        case Fmx::TrickType::DONUT:
            // End when slip stops or bike moves
            if (m_groundState.getRearWheelSlip() < m_config.burnoutSlipThreshold * 0.5f) return true;
            if (!m_groundState.isStationary()) return true;
            break;

        case Fmx::TrickType::DRIFT_LEFT:
        case Fmx::TrickType::DRIFT_RIGHT:
            // End when slip angle reduces
            if (m_groundState.lateralSlipAngle < m_config.driftSlipAngleThreshold * 0.5f) return true;
            break;

        case Fmx::TrickType::PIVOT_LEFT:
        case Fmx::TrickType::PIVOT_RIGHT:
            // End when both wheels down or speed exceeds pivot threshold
            if (m_groundState.frontWheelContact && m_groundState.rearWheelContact) return true;
            if (m_groundState.vehicleSpeed > m_config.pivotMaxSpeed) return true;
            break;

        default:
            break;
    }

    return false;
}

// ============================================================================
// State Machine Transitions
// ============================================================================
void FmxManager::initializeNewTrick(const Unified::TelemetryData& telemetry) {
    m_activeTrick = Fmx::TrickInstance();
    m_activeTrick.state = Fmx::TrickState::ACTIVE;
    m_activeTrick.startTime = std::chrono::steady_clock::now();
    m_activeTrick.startPitch = telemetry.pitch;
    m_activeTrick.startYaw = telemetry.yaw;
    m_activeTrick.startRoll = telemetry.roll;
    m_rotationTracker.startTracking(telemetry.posY);
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
    m_activeTrick.graceStartTime = std::chrono::steady_clock::now();

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
    addTrickToChain();

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
    m_activeTrick.endTime = std::chrono::steady_clock::now();

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

    // Note: individual trick callbacks already fired via addTrickToChain()

    // Reset chain and trick state
    m_score.clearChain();
    m_chainTricks.clear();
    m_activeTrick = Fmx::TrickInstance();
}

void FmxManager::failTrick(bool crashed) {
    // Save previous state before overwriting — needed to detect if trick was already in chain
    bool wasInChain = (m_activeTrick.state == Fmx::TrickState::CHAIN);

    m_activeTrick.state = Fmx::TrickState::FAILED;
    m_activeTrick.endTime = std::chrono::steady_clock::now();
    m_score.currentTrickScore = 0;

    // Check if trick reached minimum threshold (was "committed")
    bool wasCommitted = m_activeTrick.progress >= Fmx::getMinProgress(m_activeTrick.type);

    // A crash always kills the chain, even if the current trick wasn't committed
    if (wasCommitted || (crashed && m_score.chainCount > 0)) {
        // Trick was committed - lose the chain
        m_score.tricksFailed++;

        // Start failure animation - copy chain before clearing
        m_failureAnimation.active = true;
        m_failureAnimation.startTime = std::chrono::steady_clock::now();
        m_failureAnimation.startProgress = m_activeTrick.progress;
        m_failureAnimation.duration = m_config.chainPeriod;  // Match chain cooldown
        m_failureAnimation.failedType = m_activeTrick.type;
        m_failureAnimation.lostChainScore = m_score.chainScore;

        // Move chain tricks (m_chainTricks is about to be cleared anyway)
        m_failureAnimation.lostChainTricks = std::move(m_chainTricks);
        // Only add active trick if it's not already in the chain
        // (CHAIN state = trick was already added by addTrickToChain, avoid double-counting)
        if (m_activeTrick.type != Fmx::TrickType::NONE && !wasInChain) {
            m_failureAnimation.lostChainTricks.push_back(m_activeTrick);
        }

        FMX_LOG("FMX: FAILED %s (lost chain: %d tricks %d pts)",
            Fmx::getTrickName(m_activeTrick.type),
            m_score.chainCount,
            m_score.chainScore);

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
            auto now = std::chrono::steady_clock::now();
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
    addTrickToChain();

    FMX_LOG("FMX: Bank %s +%d (chain: %d tricks %d pts)",
        Fmx::getTrickName(m_activeTrick.type),
        m_activeTrick.finalScore,
        m_score.chainCount,
        m_score.chainScore);

    // Start fresh trick immediately (stay in ACTIVE)
    initializeNewTrick(telemetry);

    // Set initial airborne state for the new trick
    bool airborne = m_groundState.isAirborne();
    m_activeTrick.isCurrentlyAirborne = airborne;
    m_activeTrick.hasBeenAirborne = airborne;

    FMX_LOG("FMX: ACTIVE (banked, fresh start)");
}

// ============================================================================
// Score Calculation
// ============================================================================
int FmxManager::calculateTrickScore(const Fmx::TrickInstance& trick) const {
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

    if (Fmx::isAirTrick(trick.type)) {
        // Air tricks: bonus for duration + distance
        // Duration: 1s=x1.25, 2s=x1.5, 4s=x2.0
        // Distance: 20m=+20%, 50m=+50%, 100m=+100%
        float airBonus = 1.0f + trick.duration * m_config.durationBonusRate
                              + trick.distance * m_config.distanceBonusRate;
        score = static_cast<int>(score * airBonus);
    } else {
        // Ground tricks: scale with duration + distance
        // Duration: longer trick = more points (floor at 1.0)
        // Distance: rewards covering ground (100m wheelie at speed > 100m wheelie crawling)
        // Stationary tricks (burnout, stoppie, donut) naturally get 0 distance bonus
        float fullDuration = (trick.type == Fmx::TrickType::WHEELIE ||
                              trick.type == Fmx::TrickType::ENDO ||
                              trick.type == Fmx::TrickType::STOPPIE)
            ? Fmx::BALANCE_TRICK_FULL_DURATION : Fmx::GROUND_TRICK_FULL_DURATION;
        float groundBonus = std::max(1.0f, trick.duration / fullDuration)
                          + trick.distance * m_config.distanceBonusRate;
        score = static_cast<int>(score * groundBonus);
    }

    return score;
}

float FmxManager::calculateChainMultiplier(const std::vector<Fmx::TrickInstance>& tricks,
                                           Fmx::TrickType extraType) const {
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
        float bonus = m_config.chainBonusPerTrick *
            static_cast<float>(std::pow(m_config.repetitionPenalty, priorCount));
        multiplier += bonus;
    }

    return multiplier;
}

// ============================================================================
// Debug Logging
// ============================================================================
void FmxManager::logFrame(const Unified::TelemetryData& telemetry) {
    if (m_activeTrick.state == Fmx::TrickState::IDLE) {
        return;
    }

    // Rate limit to 10fps
    auto now = std::chrono::steady_clock::now();
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
