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
    // Captures the state of a chain at the moment it ended, either by
    // successful completion (chain timer expired) or by failure (crash / hard
    // fail mid-chain). The HUD uses this to linger the chain display for the
    // animation duration after the chain ends, in green or red depending on
    // the `success` flag.
    struct ChainEndAnimation {
        bool active = false;
        bool success = false;  // true = completed cleanly, false = failed
        std::chrono::steady_clock::time_point startTime;
        float startProgress = 0.0f;
        float duration = 0.0f;  // Animation duration — set from chainPeriod at populate time
        Fmx::TrickType finalType = Fmx::TrickType::NONE;
        std::vector<Fmx::TrickInstance> chainTricks;  // Snapshot of chain at end
        int chainScore = 0;
    };
    const ChainEndAnimation& getChainEndAnimation() const { return m_chainEndAnimation; }

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
    Fmx::TrickType classifyCurrentTrick(const Unified::TelemetryData& telemetry) const;

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

    // Ground trick debounce — gates two transitions:
    //   1. IDLE→ACTIVE: rider must hold a ground-trick posture for this long
    //      before a fresh trick is accepted (filters micro-lifts from bumps).
    //   2. Air→Ground bank: rider must hold a one-wheel landing posture for
    //      this long after an air trick before banking the air and starting
    //      a fresh ground trick.
    // Set to 500ms for symmetry with AIRBORNE_DEBOUNCE_TIME — either domain
    // requires 500ms of held state to count as real. Trick duration is
    // backdated to include the debounce window so scoring isn't lost.
    float m_groundPendingTime = 0.0f;
    float m_airToGroundTime = 0.0f;
    static constexpr float GROUND_DEBOUNCE_TIME = 0.5f;  // 500ms

    // Airborne debounce — only latch hasBeenAirborne after sustained airtime.
    // A wheelie is one frame away from "airborne" at all times (front already
    // off the ground); without this, a single-frame rear-wheel lift from a
    // bump or terrain seam unlocks the air-trick branches in classification
    // and the wheelie reclassifies to Whip/Scrub/AIR for the rest of the trick.
    // Set to 500ms — filters typical curb-hops and bumpy-terrain lift-offs
    // (those are usually well under 300ms), while still allowing fast flips
    // and spins on smaller jumps to register since they only need 500ms of
    // airtime to satisfy the latch.
    float m_continuousAirborneTime = 0.0f;
    static constexpr float AIRBORNE_DEBOUNCE_TIME = 0.5f;  // 500ms

    // Coaster wheelie tuning. Promotion is one-way (accumulator only grows) and
    // scoring is ratio-based to prevent tap-farming the bonus.
    static constexpr float COASTER_CLUTCH_THRESHOLD = 0.85f;
    static constexpr float COASTER_PROMOTION_TIME = 0.5f;
    // COASTER_SCORE_BONUS moved to core/fmx_scoring.h (Fmx::COASTER_SCORE_BONUS)
    // alongside the pure scoring math it belongs to.

    // Stuck detection — fail tricks if stationary too long (anti-fence exploit)
    float m_stuckTime = 0.0f;
    static constexpr float STUCK_THRESHOLD = 0.5f;  // 500ms below STUCK_MAX_SPEED = stuck
    // Deliberately tighter than GroundContactState::isStationary()'s 2.5 m/s
    // cutoff. The 1.389–2.5 m/s band is a deadband for wheelies: too fast to
    // upgrade to a pivot, but isStationary() still returns true — without the
    // tighter gate, a slow wheelie at ~2 m/s would false-fail after 500ms.
    // Endos escape via the STOPPIE classification, and Stoppie/Burnout/Donut/
    // Pivot are on the stuck-detection allowlist anyway, so Wheelie/Coaster/
    // Endo without a stationary cousin are the only types this protects.
    static constexpr float STUCK_MAX_SPEED = 1.389f;  // ~5 km/h, matches pivotMaxSpeed

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

    // Chain-end animation state (success and failure share this slot)
    ChainEndAnimation m_chainEndAnimation;
};
