// ============================================================================
// core/fmx_types.h
// FMX (Freestyle Motocross) trick detection types and data structures
// ============================================================================
#pragma once

#include <cstdio>
#include <chrono>
#include <algorithm>

namespace Fmx {

// ============================================================================
// Trick Type Enumeration
// ============================================================================
enum class TrickType {
    NONE = 0,

    // Ground tricks (require wheel contact)
    WHEELIE,            // Pitch back + rear wheel contact + movement
    ENDO,               // Pitch forward + front wheel contact + movement
    STOPPIE,            // Pitch forward + front wheel contact + nearly stopped
    BURNOUT,            // Rear wheel spin + stationary
    DONUT,              // Burnout + yaw rotation
    DRIFT_LEFT,         // Wheel slip + movement (sliding left)
    DRIFT_RIGHT,        // Wheel slip + movement (sliding right)
    PIVOT_LEFT,         // One-wheel yaw rotation at low speed (turning left)
    PIVOT_RIGHT,        // One-wheel yaw rotation at low speed (turning right)

    // Air tricks - basic rotation
    AIR,                // Simple airtime (no rotation)

    // Air tricks - pitch axis
    BACKFLIP,           // Backward pitch rotation in air
    FRONTFLIP,          // Forward pitch rotation in air

    // Air tricks - roll axis
    BARREL_ROLL_LEFT,   // Left roll rotation in air
    BARREL_ROLL_RIGHT,  // Right roll rotation in air
    SCRUB_LEFT,         // Partial left roll (45-90 degrees)
    SCRUB_RIGHT,        // Partial right roll (45-90 degrees)

    // Air tricks - yaw axis
    WHIP_LEFT,          // Partial left yaw (bike sideways)
    WHIP_RIGHT,         // Partial right yaw (bike sideways)
    SPIN_LEFT,          // Full left yaw rotation (360+)
    SPIN_RIGHT,         // Full right yaw rotation (360+)

    // Combination tricks (multi-axis)
    TURN_UP_LEFT,       // Nose up + yaw left
    TURN_UP_RIGHT,      // Nose up + yaw right
    TURN_DOWN_LEFT,     // Nose down + yaw left
    TURN_DOWN_RIGHT,    // Nose down + yaw right
    FLAT_360_LEFT,      // Flip while rolled left ~90°
    FLAT_360_RIGHT,     // Flip while rolled right ~90°

    // Count for iteration
    COUNT
};

// Get display name for trick type
inline const char* getTrickName(TrickType type) {
    switch (type) {
        case TrickType::NONE:              return "None";
        case TrickType::WHEELIE:           return "Wheelie";
        case TrickType::ENDO:              return "Endo";
        case TrickType::STOPPIE:           return "Stoppie";
        case TrickType::BURNOUT:           return "Burnout";
        case TrickType::DONUT:             return "Donut";
        case TrickType::DRIFT_LEFT:        return "Drift L";
        case TrickType::DRIFT_RIGHT:       return "Drift R";
        case TrickType::PIVOT_LEFT:        return "Pivot L";
        case TrickType::PIVOT_RIGHT:       return "Pivot R";
        case TrickType::AIR:               return "Air";
        case TrickType::BACKFLIP:          return "Backflip";
        case TrickType::FRONTFLIP:         return "Frontflip";
        case TrickType::BARREL_ROLL_LEFT:  return "Barrel Roll L";
        case TrickType::BARREL_ROLL_RIGHT: return "Barrel Roll R";
        case TrickType::SCRUB_LEFT:        return "Scrub L";
        case TrickType::SCRUB_RIGHT:       return "Scrub R";
        case TrickType::WHIP_LEFT:         return "Whip L";
        case TrickType::WHIP_RIGHT:        return "Whip R";
        case TrickType::SPIN_LEFT:         return "Spin L";
        case TrickType::SPIN_RIGHT:        return "Spin R";
        case TrickType::TURN_UP_LEFT:      return "Turn Up L";
        case TrickType::TURN_UP_RIGHT:     return "Turn Up R";
        case TrickType::TURN_DOWN_LEFT:    return "Turn Down L";
        case TrickType::TURN_DOWN_RIGHT:   return "Turn Down R";
        case TrickType::FLAT_360_LEFT:     return "Flat 360 L";
        case TrickType::FLAT_360_RIGHT:    return "Flat 360 R";
        default:                           return "Unknown";
    }
}

// ============================================================================
// Rotation Classification Thresholds (degrees)
// Used for trick classification and upgrades - not configurable
// ============================================================================
constexpr float PARTIAL_ROTATION_MIN = 30.0f;   // Minimum rotation for scrub/whip
constexpr float FULL_ROTATION_MIN = 270.0f;    // 3/4 rotation — ensures commitment before classifying as full trick
constexpr float TURN_PITCH_THRESHOLD = 67.5f;   // World-space pitch angle for turn up/down classification
constexpr float TURN_YAW_THRESHOLD = 67.5f;     // Minimum yaw rotation for turn up/down classification

// ============================================================================
// Minimum Progress Threshold
// Ground tricks need a minimum duration gate (0.5s wheelie, 0.75s burnout)
// to prevent momentary blips from entering the grace/chain flow.
// Air tricks don't need this — classification thresholds (≥30° rotation)
// already guarantee meaningful progress before a trick gets a name.
// ============================================================================
constexpr float MIN_GROUND_TRICK_PROGRESS = 0.25f; // 25% of completion time

// Progress duration constants (seconds for 100% progress)
constexpr float AIR_TRICK_FULL_DURATION = 2.0f;     // Air: 2s airtime = 100%
constexpr float BALANCE_TRICK_FULL_DURATION = 2.0f;  // Wheelie/endo/stoppie: 2s = 100%
constexpr float GROUND_TRICK_FULL_DURATION = 3.0f;   // Burnout/donut/drift: 3s = 100%

// Check if trick type is an air trick (requires both wheels off ground)
inline bool isAirTrick(TrickType type) {
    switch (type) {
        case TrickType::AIR:
        case TrickType::BACKFLIP:
        case TrickType::FRONTFLIP:
        case TrickType::BARREL_ROLL_LEFT:
        case TrickType::BARREL_ROLL_RIGHT:
        case TrickType::SCRUB_LEFT:
        case TrickType::SCRUB_RIGHT:
        case TrickType::WHIP_LEFT:
        case TrickType::WHIP_RIGHT:
        case TrickType::SPIN_LEFT:
        case TrickType::SPIN_RIGHT:
        case TrickType::TURN_UP_LEFT:
        case TrickType::TURN_UP_RIGHT:
        case TrickType::TURN_DOWN_LEFT:
        case TrickType::TURN_DOWN_RIGHT:
        case TrickType::FLAT_360_LEFT:
        case TrickType::FLAT_360_RIGHT:
            return true;
        default:
            return false;
    }
}

// Primary rotation axis for a trick type — determines which peak rotation
// value to show in the stats row (e.g., pitch for backflip, yaw for spin).
enum class RotationAxis { NONE, PITCH, YAW, ROLL };

inline RotationAxis getPrimaryAxis(TrickType type) {
    switch (type) {
        // Pitch-based
        case TrickType::WHEELIE:
        case TrickType::ENDO:
        case TrickType::STOPPIE:
        case TrickType::BACKFLIP:
        case TrickType::FRONTFLIP:
        case TrickType::FLAT_360_LEFT:
        case TrickType::FLAT_360_RIGHT:
            return RotationAxis::PITCH;

        // Yaw-based
        case TrickType::DONUT:
        case TrickType::PIVOT_LEFT:
        case TrickType::PIVOT_RIGHT:
        case TrickType::WHIP_LEFT:
        case TrickType::WHIP_RIGHT:
        case TrickType::SPIN_LEFT:
        case TrickType::SPIN_RIGHT:
        case TrickType::TURN_UP_LEFT:
        case TrickType::TURN_UP_RIGHT:
        case TrickType::TURN_DOWN_LEFT:
        case TrickType::TURN_DOWN_RIGHT:
            return RotationAxis::YAW;

        // Roll-based
        case TrickType::SCRUB_LEFT:
        case TrickType::SCRUB_RIGHT:
        case TrickType::BARREL_ROLL_LEFT:
        case TrickType::BARREL_ROLL_RIGHT:
            return RotationAxis::ROLL;

        // No meaningful rotation axis
        default:
            return RotationAxis::NONE;
    }
}

// Get base trick type, stripping L/R direction variants
inline TrickType getBaseTrickType(TrickType type) {
    switch (type) {
        case TrickType::DRIFT_RIGHT:         return TrickType::DRIFT_LEFT;
        case TrickType::PIVOT_RIGHT:         return TrickType::PIVOT_LEFT;
        case TrickType::BARREL_ROLL_RIGHT:   return TrickType::BARREL_ROLL_LEFT;
        case TrickType::SCRUB_RIGHT:         return TrickType::SCRUB_LEFT;
        case TrickType::WHIP_RIGHT:          return TrickType::WHIP_LEFT;
        case TrickType::SPIN_RIGHT:          return TrickType::SPIN_LEFT;
        case TrickType::TURN_UP_RIGHT:       return TrickType::TURN_UP_LEFT;
        case TrickType::TURN_DOWN_RIGHT:     return TrickType::TURN_DOWN_LEFT;
        case TrickType::FLAT_360_RIGHT:      return TrickType::FLAT_360_LEFT;
        default:                             return type;
    }
}

// Flip a trick's L/R direction (LEFT→RIGHT, RIGHT→LEFT)
// Returns the type unchanged if it has no direction variant
inline TrickType flipTrickDirection(TrickType type) {
    switch (type) {
        case TrickType::DRIFT_LEFT:          return TrickType::DRIFT_RIGHT;
        case TrickType::DRIFT_RIGHT:         return TrickType::DRIFT_LEFT;
        case TrickType::PIVOT_LEFT:          return TrickType::PIVOT_RIGHT;
        case TrickType::PIVOT_RIGHT:         return TrickType::PIVOT_LEFT;
        case TrickType::BARREL_ROLL_LEFT:    return TrickType::BARREL_ROLL_RIGHT;
        case TrickType::BARREL_ROLL_RIGHT:   return TrickType::BARREL_ROLL_LEFT;
        case TrickType::SCRUB_LEFT:          return TrickType::SCRUB_RIGHT;
        case TrickType::SCRUB_RIGHT:         return TrickType::SCRUB_LEFT;
        case TrickType::WHIP_LEFT:           return TrickType::WHIP_RIGHT;
        case TrickType::WHIP_RIGHT:          return TrickType::WHIP_LEFT;
        case TrickType::SPIN_LEFT:           return TrickType::SPIN_RIGHT;
        case TrickType::SPIN_RIGHT:          return TrickType::SPIN_LEFT;
        case TrickType::TURN_UP_LEFT:        return TrickType::TURN_UP_RIGHT;
        case TrickType::TURN_UP_RIGHT:       return TrickType::TURN_UP_LEFT;
        case TrickType::TURN_DOWN_LEFT:      return TrickType::TURN_DOWN_RIGHT;
        case TrickType::TURN_DOWN_RIGHT:     return TrickType::TURN_DOWN_LEFT;
        case TrickType::FLAT_360_LEFT:       return TrickType::FLAT_360_RIGHT;
        case TrickType::FLAT_360_RIGHT:      return TrickType::FLAT_360_LEFT;
        default:                             return type;
    }
}

// Direction enum for committed L/R tracking
enum class TrickDirection { NONE, LEFT, RIGHT };

// Get the direction of a trick type (NONE if non-directional like Backflip, Air)
inline TrickDirection getTrickDirection(TrickType type) {
    if (flipTrickDirection(type) == type) return TrickDirection::NONE;  // No L/R variant
    return (getBaseTrickType(type) == type) ? TrickDirection::LEFT : TrickDirection::RIGHT;
}

// Apply a committed direction to a trick type.
// If the trick has no direction variant, returns it unchanged.
// If dir is NONE, returns it unchanged.
inline TrickType withDirection(TrickType type, TrickDirection dir) {
    if (dir == TrickDirection::NONE) return type;
    if (flipTrickDirection(type) == type) return type;  // Non-directional trick
    TrickType leftVariant = getBaseTrickType(type);
    return (dir == TrickDirection::LEFT) ? leftVariant : flipTrickDirection(leftVariant);
}

// Format trick name with named multiplier prefix (e.g. "Double Backflip")
// Writes into caller-provided buffer to avoid heap allocation in hot paths.
inline void formatTrickName(TrickType type, int multiplier, char* buffer, size_t bufferSize) {
    const char* name = getTrickName(type);
    if (multiplier == 2) {
        snprintf(buffer, bufferSize, "Double %s", name);
    } else if (multiplier == 3) {
        snprintf(buffer, bufferSize, "Triple %s", name);
    } else if (multiplier == 4) {
        snprintf(buffer, bufferSize, "Quad %s", name);
    } else if (multiplier > 4) {
        snprintf(buffer, bufferSize, "x%d %s", multiplier, name);
    } else {
        snprintf(buffer, bufferSize, "%s", name);
    }
}

// Get minimum progress threshold for a trick type
// Air tricks: 0 (classification thresholds are the real gate)
// Ground tricks: 25% (prevents momentary blips from scoring)
inline float getMinProgress(TrickType type) {
    return isAirTrick(type) ? 0.0f : MIN_GROUND_TRICK_PROGRESS;
}

// Get base score for trick type
inline int getTrickBaseScore(TrickType type) {
    switch (type) {
        case TrickType::WHEELIE:           return 10;
        case TrickType::ENDO:              return 15;
        case TrickType::STOPPIE:           return 20;
        case TrickType::BURNOUT:           return 5;
        case TrickType::DONUT:             return 25;
        case TrickType::DRIFT_LEFT:        return 15;
        case TrickType::DRIFT_RIGHT:       return 15;
        case TrickType::PIVOT_LEFT:        return 40;
        case TrickType::PIVOT_RIGHT:       return 40;
        case TrickType::AIR:               return 5;
        case TrickType::BACKFLIP:          return 100;
        case TrickType::FRONTFLIP:         return 150;
        case TrickType::BARREL_ROLL_LEFT:  return 80;
        case TrickType::BARREL_ROLL_RIGHT: return 80;
        case TrickType::SCRUB_LEFT:        return 30;
        case TrickType::SCRUB_RIGHT:       return 30;
        case TrickType::WHIP_LEFT:         return 25;
        case TrickType::WHIP_RIGHT:        return 25;
        case TrickType::SPIN_LEFT:         return 120;
        case TrickType::SPIN_RIGHT:        return 120;
        case TrickType::TURN_UP_LEFT:      return 60;
        case TrickType::TURN_UP_RIGHT:     return 60;
        case TrickType::TURN_DOWN_LEFT:    return 60;
        case TrickType::TURN_DOWN_RIGHT:   return 60;
        case TrickType::FLAT_360_LEFT:     return 180;
        case TrickType::FLAT_360_RIGHT:    return 180;
        default:                           return 0;
    }
}

// ============================================================================
// Trick State Enumeration (simplified)
// ============================================================================
enum class TrickState {
    IDLE,       // Waiting for trick initiation
    ACTIVE,     // Trick is happening - type dynamically classified each frame
    GRACE,      // Post-action grace period (crash detection)
    CHAIN,      // Waiting for next trick or timeout
    COMPLETED,  // Successfully landed and stayed upright (points awarded)
    FAILED      // Crashed or bailed before completion
};

inline const char* getTrickStateName(TrickState state) {
    switch (state) {
        case TrickState::IDLE:      return "Idle";
        case TrickState::ACTIVE:    return "Active";
        case TrickState::GRACE:     return "Grace";
        case TrickState::CHAIN:     return "Chain";
        case TrickState::COMPLETED: return "Completed";
        case TrickState::FAILED:    return "Failed";
        default:                    return "Unknown";
    }
}

// ============================================================================
// Trick Instance - Active or Completed Trick
// ============================================================================
struct TrickInstance {
    TrickType type = TrickType::NONE;
    TrickState state = TrickState::IDLE;

    // Timing
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point graceStartTime;  // When grace period started
    std::chrono::steady_clock::time_point endTime;
    float duration = 0.0f;      // Total seconds since start

    // Air vs ground tracking (for dynamic classification)
    bool hasBeenAirborne = false;   // True if trick involved any airtime
    bool isCurrentlyAirborne = false;  // Current frame state

    // Rotation tracking (accumulated degrees from start)
    float accumulatedPitch = 0.0f;
    float accumulatedYaw = 0.0f;
    float accumulatedRoll = 0.0f;

    // Peak rotation (furthest extent, for classification)
    float peakPitch = 0.0f;
    float peakYaw = 0.0f;
    float peakRoll = 0.0f;

    // Starting angles (for visualization)
    float startPitch = 0.0f;
    float startYaw = 0.0f;
    float startRoll = 0.0f;

    // Progress (0.0 to 1.0+ toward completion threshold)
    float progress = 0.0f;

    // Multiplier (1 = single, 2 = double flip, etc.)
    int multiplier = 1;

    // Scoring
    int baseScore = 0;
    int finalScore = 0;

    // Distance traveled (horizontal, accumulated frame-by-frame)
    float distance = 0.0f;        // meters

    TrickInstance() = default;
};

// ============================================================================
// Rotation Tracker - Handles rotation accumulation via angular velocity integration
//
// IMPORTANT — Euler angles vs accumulated rotation:
//
//   start* + accumulated*  = RELIABLE world-relative orientation estimate.
//                            Accumulated values come from angular velocity
//                            integration, which is immune to gimbal lock.
//                            Use this for ALL logic/classification decisions.
//
//   current*               = RAW Euler angles from telemetry. These suffer from
//                            gimbal lock when pitch approaches ±90° — roll and
//                            yaw values become meaningless and can jump wildly
//                            between frames. NEVER use for logic decisions.
//                            Safe only for HUD display when the bike is upright.
//
// ============================================================================
struct RotationTracker {
    bool hasPreviousFrame = false;

    // Accumulated rotation since tracking started (angular velocity integration — reliable)
    float accumulatedPitch = 0.0f;
    float accumulatedYaw = 0.0f;
    float accumulatedRoll = 0.0f;

    // Peak accumulated rotation (furthest extent reached, for marker position)
    float peakPitch = 0.0f;
    float peakYaw = 0.0f;
    float peakRoll = 0.0f;

    // Starting angles (captured once at trick start — reliable snapshot)
    float startPitch = 0.0f;
    float startYaw = 0.0f;
    float startRoll = 0.0f;

    // Raw Euler angles from telemetry (world-space)
    float currentPitch = 0.0f;
    float currentYaw = 0.0f;
    float currentRoll = 0.0f;

    // Peak world-space pitch (tracks extremes for Turn Up/Down classification)
    float peakWorldPitch = 0.0f;    // Most positive (most nose-down)
    float minWorldPitch = 0.0f;     // Most negative (most nose-up)

    // Angular velocities (deg/sec)
    float pitchVelocity = 0.0f;
    float yawVelocity = 0.0f;
    float rollVelocity = 0.0f;

    // Tracking state (for TRACKING → COMMITTED transition)
    std::chrono::steady_clock::time_point trackingStartTime;
    float trackingStartHeight = 0.0f;   // Y position when tracking started
    float trackingMaxHeight = 0.0f;     // Max height reached during tracking
    float trackingMinHeight = 0.0f;     // Min height reached during tracking (for downhill jumps)
    float trackingDuration = 0.0f;      // Seconds since tracking started

    // Reset accumulated rotation (start tracking new trick)
    void resetAccumulation() {
        accumulatedPitch = 0.0f;
        accumulatedYaw = 0.0f;
        accumulatedRoll = 0.0f;
        peakPitch = 0.0f;
        peakYaw = 0.0f;
        peakRoll = 0.0f;
        peakWorldPitch = 0.0f;
        minWorldPitch = 0.0f;
    }

    // Start tracking (wheel lifted)
    void startTracking(float height) {
        hasPreviousFrame = false;
        resetAccumulation();
        trackingStartTime = std::chrono::steady_clock::now();
        trackingStartHeight = height;
        trackingMaxHeight = height;
        trackingMinHeight = height;
        trackingDuration = 0.0f;
    }

    // Update tracking duration and height range
    void updateTracking(float currentHeight) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - trackingStartTime).count();
        trackingDuration = elapsed / 1000.0f;
        if (currentHeight > trackingMaxHeight) {
            trackingMaxHeight = currentHeight;
        }
        if (currentHeight < trackingMinHeight) {
            trackingMinHeight = currentHeight;
        }
    }

    // Get height gained since tracking started (upward only)
    float getHeightGained() const {
        return trackingMaxHeight - trackingStartHeight;
    }

    // Get maximum height change in either direction (for downhill jumps)
    float getHeightChange() const {
        float upward = trackingMaxHeight - trackingStartHeight;
        float downward = trackingStartHeight - trackingMinHeight;
        return std::max(upward, downward);
    }

    // Reset everything (new session)
    void reset() {
        hasPreviousFrame = false;
        resetAccumulation();
        startPitch = startYaw = startRoll = 0.0f;
        currentPitch = currentYaw = currentRoll = 0.0f;
        pitchVelocity = yawVelocity = rollVelocity = 0.0f;
        trackingStartTime = {};
        trackingStartHeight = 0.0f;
        trackingMaxHeight = 0.0f;
        trackingMinHeight = 0.0f;
        trackingDuration = 0.0f;
    }
};

// ============================================================================
// Ground Contact State
// ============================================================================
struct GroundContactState {
    bool frontWheelContact = true;
    bool rearWheelContact = true;
    float frontWheelSpeed = 0.0f;
    float rearWheelSpeed = 0.0f;
    float vehicleSpeed = 0.0f;

    // Lateral slip angle (degrees) - angle between heading and velocity vector
    // Used for drift detection: 0° = going straight, 90° = full sideways
    float lateralSlipAngle = 0.0f;

    // Signed lateral velocity (m/s) - positive = sliding right, negative = sliding left
    // Used for drift L/R direction detection
    float lateralVelocity = 0.0f;

    // Derived states
    bool isAirborne() const { return !frontWheelContact && !rearWheelContact; }
    bool isWheeliePosition() const { return !frontWheelContact && rearWheelContact; }
    bool isEndoPosition() const { return frontWheelContact && !rearWheelContact; }
    bool isGrounded() const { return frontWheelContact || rearWheelContact; }

    // Wheel slip detection
    float getRearWheelSlip() const {
        // Slip ratio: how much faster rear wheel spins vs vehicle speed
        // max(1.0, vehicleSpeed) prevents division by zero at low speeds
        // while still detecting burnouts (rearWheelSpeed >> 0 when stationary)
        return (rearWheelSpeed - vehicleSpeed) / std::max(1.0f, vehicleSpeed);
    }

    bool isStationary() const { return vehicleSpeed < 2.5f; }  // < 2.5 m/s (~5.5 mph) = stationary
};

// ============================================================================
// FMX Session Score
// ============================================================================
struct FmxScore {
    int currentTrickScore = 0;      // Score from active trick
    int sessionScore = 0;           // Total session score
    int bestComboScore = 0;         // Highest single trick/combo score
    int tricksCompleted = 0;        // Number of tricks landed this session
    int tricksFailed = 0;           // Number of tricks failed/crashed

    // Chain tracking
    int chainCount = 0;             // Number of tricks in current chain
    int chainScore = 0;             // Accumulated score in current chain (not yet banked)
    std::chrono::steady_clock::time_point chainStartTime;  // When chain period started
    float chainElapsed = 0.0f;      // Seconds elapsed in chain period

    void reset() {
        currentTrickScore = 0;
        sessionScore = 0;
        bestComboScore = 0;
        tricksCompleted = 0;
        tricksFailed = 0;
        chainCount = 0;
        chainScore = 0;
        chainStartTime = {};
        chainElapsed = 0.0f;
    }

    // Clear chain state completely (count, score, timer) — used after banking or failing
    void clearChain() {
        chainCount = 0;
        chainScore = 0;
        chainElapsed = 0.0f;
        chainStartTime = std::chrono::steady_clock::now();
    }

    // Reset only the chain timer (preserves count/score) — used when entering chain state
    void restartChainTimer() {
        chainStartTime = std::chrono::steady_clock::now();
        chainElapsed = 0.0f;
    }

    void updateChainElapsed() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - chainStartTime).count();
        chainElapsed = elapsed / 1000.0f;
    }
};

// ============================================================================
// FMX Configuration (adjustable thresholds)
// ============================================================================
struct FmxConfig {
    // Progress completion angles (degrees for 100% progress bar)
    float flipCompletionAngle = 360.0f;         // degrees for full flip
    float barrelRollCompletionAngle = 360.0f;   // degrees for full barrel roll
    float spinCompletionAngle = 360.0f;         // degrees for full spin
    float scrubMaxAngle = 90.0f;                // max degrees before it becomes barrel roll
    float whipMaxAngle = 90.0f;                 // max degrees before it becomes spin

    // Ground trick thresholds
    float wheelieAngleThreshold = 25.0f;        // pitch degrees for wheelie
    float endoAngleThreshold = -15.0f;          // pitch degrees for endo (negative = forward)
    float burnoutSlipThreshold = 5.0f;          // rear wheel speed difference (m/s)
    float driftSlipAngleThreshold = 30.0f;      // degrees of lateral slip angle for drift (0=straight, 90=sideways)
    float donutYawThreshold = 45.0f;            // degrees of yaw rotation to upgrade burnout to donut
    float flat360MinRoll = 80.0f;               // degrees of roll to upgrade flip to flat 360 (80-180 window)
    float pivotMinYaw = 67.5f;                  // degrees of yaw to upgrade wheelie/endo to pivot
    float pivotMaxSpeed = 3.0f;                 // m/s max vehicle speed for pivot (prevents false positives on curved wheelies)
    float pivotCompletionAngle = 180.0f;        // degrees of yaw for 100% pivot progress

    // Air trick commit thresholds (both must be met for AIR trick classification)
    float airCommitTime = 0.3f;                 // seconds of trick duration before committing to AIR
    float airCommitHeight = 0.5f;               // meters height gained before committing to air trick

    // Grace periods
    float landingGracePeriod = 0.75f;           // seconds after landing before confirming trick (check for crash)
    float chainPeriod = 2.0f;                   // seconds after grace period to chain into next trick

    // Scoring
    float durationBonusRate = 0.25f;            // air tricks: +25% score per second of duration
    float distanceBonusRate = 0.01f;            // +1% score per meter of horizontal distance
    float chainBonusPerTrick = 0.5f;            // chain: +50% per additional trick (2=x1.5, 3=x2.0, 5=x3.0)
    float repetitionPenalty = 0.5f;              // repeated trick: halve score per prior same-type in chain (THPS-style)
};

} // namespace Fmx
