// ============================================================================
// core/fmx_manager_detection.cpp
// FMX trick detection — the telemetry-driven state machine that watches ground
// contact, rotation and inputs to recognise tricks (updateFromTelemetry,
// updateGroundContact, updateRotation, updateTrickDetection, classifyCurrentTrick,
// calculateProgress, shouldStart/EndTrick). Extracted verbatim from
// fmx_manager.cpp; the FmxManager class, members, and API are unchanged — only
// where these method bodies live moves.
// ============================================================================
#include "fmx_manager.h"
#include "fmx_manager_internal.h"
#include "plugin_data.h"
#include "plugin_constants.h"
#include "../diagnostics/logger.h"
#include <algorithm>
#include <cmath>

using PluginConstants::Math::DEG_TO_RAD;
using PluginConstants::Math::RAD_TO_DEG;

void FmxManager::updateFromTelemetry(const Unified::TelemetryData& telemetry) {
    // Skip updates while game is paused (RunStop → isPlayerRunning=false).
    // Mark m_bFirstUpdate so the resume frame uses a clean default dt
    // instead of the accumulated pause duration.
    if (!PluginData::getInstance().isPlayerRunning()) {
        m_bFirstUpdate = true;
        return;
    }

    auto now = Fmx::clockNow();

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
            m_chainEndAnimation.startTime += pauseDuration;
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

    // Update chain-end animation (auto-deactivate after duration)
    if (m_chainEndAnimation.active) {
        auto elapsed = std::chrono::duration<float>(now - m_chainEndAnimation.startTime).count();
        if (elapsed >= m_chainEndAnimation.duration) {
            m_chainEndAnimation.active = false;
            m_chainEndAnimation.chainTricks.clear();
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

    m_rotationTracker.hasPreviousFrame = true;
}

// ============================================================================
// Main Trick Detection State Machine
// ============================================================================
void FmxManager::updateTrickDetection(const Unified::TelemetryData& telemetry, float dt) {
    auto now = Fmx::clockNow();

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

            // Accumulate clutch-engagement time — drives coaster wheelie
            // promotion and the ratio-based score bonus. Once the rider has
            // committed to a coaster (clutch held past the promotion time),
            // any later release invalidates it — a Coaster Wheelie must be
            // held continuously until the wheelie ends (front wheel touches
            // down). Releases before promotion are fine; the rider hasn't
            // committed yet and can still earn the coaster on this trick.
            if (telemetry.clutch > COASTER_CLUTCH_THRESHOLD) {
                m_activeTrick.clutchHeldTime += dt;
            } else if (m_activeTrick.clutchHeldTime >= COASTER_PROMOTION_TIME) {
                m_activeTrick.coasterInvalidated = true;
            }

            // Update airborne tracking with debounce. hasBeenAirborne is
            // sticky for the rest of the trick, so it must only latch on
            // *sustained* airtime — a 1-frame rear-wheel lift from a bump
            // or terrain seam would otherwise unlock the air-trick branches
            // in classifyCurrentTrick and reclassify a wheelie to Whip/AIR.
            bool airborne = m_groundState.isAirborne();
            m_activeTrick.isCurrentlyAirborne = airborne;

            bool justConfirmedAirborne = false;
            if (airborne) {
                m_continuousAirborneTime += dt;
                if (!m_activeTrick.hasBeenAirborne &&
                    m_continuousAirborneTime >= AIRBORNE_DEBOUNCE_TIME) {
                    m_activeTrick.hasBeenAirborne = true;
                    justConfirmedAirborne = true;
                }
            } else {
                m_continuousAirborneTime = 0.0f;
            }

            // =========================================================
            // DOMAIN TRANSITION: Ground → Air
            // Once sustained airborne is confirmed, if we were doing a good
            // ground trick, bank it and start fresh for the air portion.
            // Gating on the debounced latch (instead of the raw transition
            // frame) means micro-bumps don't trigger spurious banks either.
            // =========================================================
            if (justConfirmedAirborne && m_activeTrick.type != Fmx::TrickType::NONE) {
                bool isGroundTrick = !Fmx::isAirTrick(m_activeTrick.type);
                if (isGroundTrick && m_activeTrick.progress >= Fmx::MIN_GROUND_TRICK_PROGRESS) {
                    FMX_LOG("FMX: Ground->Air bank %s prog=%.0f%%",
                        Fmx::getTrickName(m_activeTrick.type),
                        m_activeTrick.progress * 100.0f);
                    bankAndContinue(telemetry);
                    break;  // Exit this frame, continue with fresh trick next frame
                }
            }

            // =========================================================
            // DOMAIN TRANSITION: Air → Ground
            // When an active air trick lands into a committed one-wheel
            // posture (wheelie or endo pitch threshold met) and the rider
            // holds it past GROUND_DEBOUNCE_TIME, bank the air and start a
            // fresh trick that can classify as the new ground trick. Mirror
            // of the Ground→Air bank above. Brief rebounds and clumsy
            // touchdowns don't trigger the bank because the debounce gates
            // it on sustained held posture. Pitch gates ensure spurious
            // landings (nose near level) don't qualify either.
            // =========================================================
            if (Fmx::isAirTrick(m_activeTrick.type)) {
                bool inWheelieLanding = m_groundState.isWheeliePosition() &&
                                        telemetry.pitch <= -m_config.wheelieAngleThreshold;
                bool inEndoLanding = m_groundState.isEndoPosition() &&
                                     telemetry.pitch >= -m_config.endoAngleThreshold;
                if (inWheelieLanding || inEndoLanding) {
                    m_airToGroundTime += dt;
                    if (m_airToGroundTime >= GROUND_DEBOUNCE_TIME) {
                        FMX_LOG("FMX: Air->Ground bank %s prog=%.0f%%",
                            Fmx::getTrickName(m_activeTrick.type),
                            m_activeTrick.progress * 100.0f);
                        bankAndContinue(telemetry);
                        break;
                    }
                } else {
                    m_airToGroundTime = 0.0f;
                }
            } else {
                m_airToGroundTime = 0.0f;
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
            Fmx::TrickType rawType = classifyCurrentTrick(telemetry);
            Fmx::TrickType newType = rawType;
            // Honor per-trick disable flags from INI: treat a disabled trick
            // as if nothing classified this frame. The active trick keeps its
            // current type (which may be NONE) and the direction-commit logic
            // below is skipped — so e.g. disabling PivotLeft/Right means a
            // wheelie that yaws past pivotMinYaw simply stays a Wheelie
            // instead of upgrading. rawType is preserved for downstream
            // consumers (e.g. stuck detection) that need to know what would
            // have classified.
            if (newType != Fmx::TrickType::NONE &&
                !m_config.tricksEnabled[static_cast<int>(newType)]) {
                newType = Fmx::TrickType::NONE;
            }
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
                    bool firstClassify = (m_activeTrick.type == Fmx::TrickType::NONE);
                    if (!firstClassify) {
                        FMX_LOG("FMX: Reclassify %s -> %s",
                            Fmx::getTrickName(m_activeTrick.type), Fmx::getTrickName(newType));
                    }
                    m_activeTrick.type = newType;
                    m_activeTrick.baseScore = Fmx::getTrickBaseScore(newType);
                    if (firstClassify) {
                        // Backdate to the moment of classification so pre-classify
                        // hover time isn't credited to the trick. Otherwise a rider
                        // who holds the front up at sub-threshold pitch for 5
                        // seconds then briefly crosses threshold for one frame
                        // would bank a wheelie scored for the full 5s. Rotation,
                        // clutch time, and peak metrics are intentionally not
                        // reset — those reflect real measurements that may have
                        // enabled the classification (e.g. an in-air flip that
                        // classified on reaching 270° pitch).
                        m_activeTrick.duration = 0.0f;
                        m_activeTrick.distance = 0.0f;
                    }
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
            // Stoppie/Burnout/Donut are legitimately stationary, skip them.
            // Also skip if rawType (pre-gate classification) was a stationary
            // trick — otherwise disabling pivots via tricksEnabled would kill
            // any slow wheelie that crossed into pivot territory after 500ms,
            // and disabling burnout/donut/stoppie would kill those outright.
            // Speed gate is STUCK_MAX_SPEED (1.389 m/s), tighter than
            // isStationary() (2.5 m/s) — see comment on STUCK_MAX_SPEED.
            {
                auto isStationaryType = [](Fmx::TrickType t) {
                    return t == Fmx::TrickType::STOPPIE ||
                           t == Fmx::TrickType::BURNOUT ||
                           t == Fmx::TrickType::DONUT ||
                           t == Fmx::TrickType::PIVOT_LEFT ||
                           t == Fmx::TrickType::PIVOT_RIGHT;
                };
                bool isStationaryTrick = isStationaryType(m_activeTrick.type) ||
                                         isStationaryType(rawType);

                if (!isStationaryTrick && m_groundState.vehicleSpeed < STUCK_MAX_SPEED) {
                    m_stuckTime += dt;
                    if (m_stuckTime >= STUCK_THRESHOLD) {
                        FMX_LOG("FMX: Stuck detected (low-speed %.1fs) - failing trick", m_stuckTime);
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
Fmx::TrickType FmxManager::classifyCurrentTrick(const Unified::TelemetryData& telemetry) const {
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
    // Gate air-trick branches on the debounced latch only. Reading the raw
    // per-frame airborne flag here would briefly flip hasAirtime true during
    // a sub-debounce bump on a long wheelie, reclassify the wheelie to
    // AIR/Whip for a frame or two, then revert on landing — a visible title
    // flicker. Real air tricks (flips at 270°+, whip/scrub gated by
    // airCommitTime at 0.3s) take far longer than the airborne debounce
    // anyway, so they don't lose anything by waiting for the latch.
    bool hasAirtime = m_activeTrick.hasBeenAirborne;

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
    // Require the rider to be CURRENTLY airborne in addition to having latched
    // hasAirtime and reaching airCommitTime. Without the airborne gate, a
    // ground trick that briefly went airborne (e.g. a wheelie that triggered
    // the ground→air bank, or a wheelie past its bank threshold that caught
    // a real lift-off) leaves hasAirtime true for the lifetime of the trick;
    // once duration crosses airCommitTime, Whip/Scrub/AIR fires even though
    // the rider has long since landed and is clearly continuing the ground
    // trick. The airborne gate keeps the air-trick fallbacks scoped to the
    // window when the rider is actually in the air. Priority 1/2 (full
    // rotations 270°+) intentionally don't get this gate — once a real flip
    // or spin is committed, the title should stick through landing.
    if (airborne && hasAirtime && m_activeTrick.duration >= m_config.airCommitTime) {
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

        // Basic air (significant airtime, no rotation crossed any threshold).
        // hasBeenAirborne is debounced upstream, so reaching this point
        // already means the rider was genuinely airborne long enough to
        // count. Duration/distance scoring downstream handles the "how big
        // was the air" question; no separate height gate is needed here.
        return TrickType::AIR;
    }

    // =========================================================================
    // PRIORITY 4: Ground tricks
    // =========================================================================
    // Once a trick is classified as an air trick, don't allow it to demote
    // back to a ground trick. The symmetric mirror of the Priority-3 airborne
    // gate above: that gate prevents promoting ground tricks to air tricks
    // unless actually airborne; this gate prevents demoting air tricks to
    // ground tricks when the landing posture happens to match one (e.g. a
    // nose-down Air landing momentarily satisfying endoPos+pitch, or a
    // rear-first landing momentarily satisfying wheeliePos+pitch). Returning
    // NONE leaves the existing air trick type intact via the reclassify
    // guard; shouldEndTrick's "landed after airborne with both wheels down"
    // path ends the trick correctly.
    if (Fmx::isAirTrick(m_activeTrick.type)) {
        return TrickType::NONE;
    }

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

    // Balance tricks (Wheelie/Endo/Stoppie) require BOTH the right wheel-contact
    // state AND the bike pitched past the entry threshold. Wheel state alone
    // isn't enough: a momentary front-pop where the nose only lifts 10° still
    // satisfies wheeliePos and would otherwise classify as WHEELIE, then bank
    // for the trick's accumulated duration even though the rider never really
    // wheelied. The pitch gate is the entry mirror of the exit threshold used
    // in shouldEndTrick (hysteresis: enter at full angle, exit at half).
    //
    // Note: endoAngleThreshold is stored as a negative number (-15°) by
    // convention; -endoAngleThreshold gives the positive "nose-down" entry
    // angle the bike needs to reach.

    // Stoppie: endo position + stationary + nose past entry threshold
    if (endoPos && telemetry.pitch >= -m_config.endoAngleThreshold &&
        m_groundState.isStationary()) {
        return TrickType::STOPPIE;
    }

    // Endo: front wheel only + moving + nose past entry threshold
    if (endoPos && telemetry.pitch >= -m_config.endoAngleThreshold) {
        return TrickType::ENDO;
    }

    // Wheelie: rear wheel only + nose past entry threshold. Promotes to
    // Coaster Wheelie once the rider has held the clutch for 0.5s cumulative
    // during this trick. Releasing the clutch invalidates the coaster for the
    // remainder of this trick — the type downgrades back to Wheelie and the
    // coaster score bonus is dropped.
    if (wheeliePos && telemetry.pitch <= -m_config.wheelieAngleThreshold) {
        if (!m_activeTrick.coasterInvalidated &&
            m_activeTrick.clutchHeldTime >= COASTER_PROMOTION_TIME) {
            return TrickType::COASTER_WHEELIE;
        }
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
        case Fmx::TrickType::COASTER_WHEELIE:
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
    // Require both wheels grounded. Swerving the bike on its rear wheel
    // during a wheelie yaws it faster than its velocity vector rotates,
    // producing an apparent lateral slip angle that would otherwise be
    // read as a drift. A genuine drift is a corner with both wheels planted.
    return !m_groundState.isStationary() &&
           m_groundState.frontWheelContact && m_groundState.rearWheelContact &&
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

    // Unclassified tricks: end the moment conditions no longer warrant a trick.
    // The classifier returns NONE while the rider is in a position that
    // satisfies shouldStartTrick (e.g. front wheel off) but hasn't crossed any
    // classification threshold yet (e.g. pitch hasn't reached wheelie angle).
    // Without this gate, the trick would sit in ACTIVE/NONE indefinitely, and
    // a late one-frame classification (e.g. a brief tilt past threshold) would
    // bank the trick's whole accumulated duration as that trick.
    if (type == Fmx::TrickType::NONE && !shouldStartTrick()) {
        return true;
    }

    // Ground trick specific end conditions
    switch (type) {
        case Fmx::TrickType::WHEELIE:
        case Fmx::TrickType::COASTER_WHEELIE:
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
