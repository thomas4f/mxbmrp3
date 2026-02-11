// ============================================================================
// core/fmx_manager.h
// FMX (Freestyle Motocross) trick detection and scoring manager
// ============================================================================
#pragma once

#include "fmx_types.h"
#include "../game/unified_types.h"
#include <vector>
#include <cmath>

class FmxManager {
public:
    static FmxManager& getInstance();

    // ========================================================================
    // Telemetry Update (called from RunTelemetryHandler at ~100Hz)
    // ========================================================================
    void updateFromTelemetry(const Unified::TelemetryData& telemetry);

    // ========================================================================
    // State Accessors (for HUD)
    // ========================================================================
    const Fmx::FmxScore& getScore() const { return m_score; }
    const Fmx::TrickInstance& getActiveTrick() const { return m_activeTrick; }
    const Fmx::RotationTracker& getRotationTracker() const { return m_rotationTracker; }
    const Fmx::GroundContactState& getGroundContactState() const { return m_groundState; }
    const Fmx::FmxConfig& getConfig() const { return m_config; }

    // Tricks in current chain (for stack display)
    const std::vector<Fmx::TrickInstance>& getChainTricks() const { return m_chainTricks; }

    // ========================================================================
    // Configuration (detection/scoring thresholds)
    // ========================================================================
    void setConfig(const Fmx::FmxConfig& config) { m_config = config; }
    void resetConfig() { m_config = Fmx::FmxConfig(); }

    // ========================================================================
    // Session Control
    // ========================================================================
    void reset();                    // Reset all state (new session)
    void resetScore();               // Reset score only (keep detection state)

    // ========================================================================
    // Debug Logging
    // ========================================================================
    void setLoggingEnabled(bool enabled) { m_bLoggingEnabled = enabled; }
    bool isLoggingEnabled() const { return m_bLoggingEnabled; }

    // ========================================================================
    // Failure Animation (for HUD display)
    // ========================================================================
    struct FailureAnimation {
        bool active = false;
        std::chrono::steady_clock::time_point startTime;
        float startProgress = 0.0f;
        float duration = 2.0f;  // Animation duration (matches chain period)
        Fmx::TrickType failedType = Fmx::TrickType::NONE;
        std::vector<Fmx::TrickInstance> lostChainTricks;  // Copy of chain that was lost
        int lostChainScore = 0;
    };
    const FailureAnimation& getFailureAnimation() const { return m_failureAnimation; }

    // Calculate chain multiplier accounting for trick variety
    // Unique tricks add full bonus, repeated types add diminishing bonus
    // Optional extraType includes an uncommitted active trick in the calculation
    float calculateChainMultiplier(const std::vector<Fmx::TrickInstance>& tricks,
                                   Fmx::TrickType extraType = Fmx::TrickType::NONE) const;

private:
    FmxManager();
    ~FmxManager() = default;
    FmxManager(const FmxManager&) = delete;
    FmxManager& operator=(const FmxManager&) = delete;

    // ========================================================================
    // Internal Detection Methods
    // ========================================================================

    // Update rotation tracking from telemetry
    void updateRotation(const Unified::TelemetryData& telemetry, float dt);

    // Update ground contact state
    void updateGroundContact(const Unified::TelemetryData& telemetry);

    // Main update loop for trick detection
    void updateTrickDetection(const Unified::TelemetryData& telemetry, float dt);

    // Classify trick type based on current state (called every frame during ACTIVE)
    // Returns the most appropriate trick type given accumulated metrics
    Fmx::TrickType classifyCurrentTrick() const;

    // Calculate progress for the given trick type
    float calculateProgress(Fmx::TrickType type) const;

    // Ground trick condition helpers
    bool isBurnoutActive() const;
    bool isDriftActive() const;

    // Check if trick should start (any initiation condition met)
    bool shouldStartTrick() const;

    // Check if active trick should end (action stopped)
    bool shouldEndTrick(const Unified::TelemetryData& telemetry) const;

    // Initialize a fresh trick instance and rotation tracker
    void initializeNewTrick(const Unified::TelemetryData& telemetry);

    // State machine transitions
    void startTrick(const Unified::TelemetryData& telemetry);  // IDLE → ACTIVE
    void enterGrace();                      // ACTIVE → GRACE
    void enterChainState();                 // GRACE → CHAIN
    void completeTrick();                   // CHAIN timeout → COMPLETED (bank score)
    void failTrick(bool crashed = false);   // Fail trick; crashed=true always kills chain

    // Add completed trick to chain (shared by enterChainState and bankAndContinue)
    void addTrickToChain();

    // Domain transition handling (ground ↔ air)
    void bankAndContinue(const Unified::TelemetryData& telemetry);  // Bank current trick, start fresh

    // Calculate final score for completed trick
    int calculateTrickScore(const Fmx::TrickInstance& trick) const;

    // Debug logging
    void logFrame(const Unified::TelemetryData& telemetry);

    // ========================================================================
    // State
    // ========================================================================
    Fmx::FmxConfig m_config;
    Fmx::FmxScore m_score;
    Fmx::TrickInstance m_activeTrick;
    Fmx::RotationTracker m_rotationTracker;
    Fmx::GroundContactState m_groundState;
    Fmx::GroundContactState m_prevGroundState;

    // Tricks in current chain (for stack display, oldest first)
    std::vector<Fmx::TrickInstance> m_chainTricks;

    // Timing
    std::chrono::steady_clock::time_point m_lastUpdateTime;
    bool m_bFirstUpdate = true;
    float m_sessionTime = 0.0f;

    // Committed L/R direction — set on first directional trick, preserved across
    // all reclassifications (including through non-directional intermediaries like Backflip)
    Fmx::TrickDirection m_committedDirection = Fmx::TrickDirection::NONE;

    // Ground trick debounce (filters micro-lifts from bumps)
    float m_groundPendingTime = 0.0f;
    static constexpr float GROUND_DEBOUNCE_TIME = 0.1f;  // 100ms

    // Stuck detection — fail tricks if stationary too long (anti-fence exploit)
    float m_stuckTime = 0.0f;
    static constexpr float STUCK_THRESHOLD = 0.5f;  // 500ms stationary = stuck

    // Chain timer pause — pauses chain countdown when a new trick is committed mid-chain,
    // so the chain window isn't consumed while performing the next trick.
    // On discard: timer resumes from paused value. On bank: timer resets normally.
    float m_chainPausedElapsed = 0.0f;   // chainElapsed value at moment of pause
    bool m_chainTimerPaused = false;     // true when committed mid-chain

    // Previous frame position for teleport detection and distance calculation
    float m_prevPosX = 0.0f;
    float m_prevPosY = 0.0f;
    float m_prevPosZ = 0.0f;
    bool m_bHasPrevPosition = false;

    // Debug logging (logs to normal log file at 10fps during tricks)
    bool m_bLoggingEnabled = false;
    std::chrono::steady_clock::time_point m_lastLogTime;

    // Failure animation state
    FailureAnimation m_failureAnimation;
};
