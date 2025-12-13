// ============================================================================
// hud/gap_bar_hud.h
// Gap Bar HUD - visualizes current lap progress vs best lap timing
// Shows a horizontal bar with current position, best lap marker, and live gap
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include <chrono>
#include <array>

// ============================================================================
// Timing point for best lap comparison
// Stores when player reached each track position on their best lap
// ============================================================================
struct BestLapTimingPoint {
    int elapsedTime;      // Milliseconds from lap start when this position was reached
    bool valid;           // Is this timing point populated?

    BestLapTimingPoint() : elapsedTime(0), valid(false) {}
    BestLapTimingPoint(int time) : elapsedTime(time), valid(true) {}
};

// ============================================================================
// Live timing anchor state - tracks when current lap started
// Stores accumulated time and resyncs at splits for accuracy
// ============================================================================
struct GapBarAnchor {
    std::chrono::steady_clock::time_point wallClockTime;  // Real time when anchor was set
    int accumulatedTime;      // Known accumulated lap time at anchor (ms)
    bool valid;               // Do we have a usable anchor?

    GapBarAnchor() : accumulatedTime(0), valid(false) {}

    void reset() {
        accumulatedTime = 0;
        valid = false;
    }

    void set(int accumTime = 0) {
        wallClockTime = std::chrono::steady_clock::now();
        accumulatedTime = accumTime;
        valid = true;
    }

    int getElapsedMs() const {
        if (!valid) return 0;
        auto now = std::chrono::steady_clock::now();
        int wallClockDelta = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            now - wallClockTime).count());
        return accumulatedTime + wallClockDelta;
    }
};

// ============================================================================
// Track position monitoring for S/F line detection
// ============================================================================
struct GapBarTrackMonitor {
    float lastTrackPos;       // Previous track position (0.0-1.0)
    int lastLapNum;           // Previous lap number
    bool initialized;         // Have we received first position?

    static constexpr float WRAP_THRESHOLD = 0.5f;  // Position jump > 0.5 = S/F crossing

    GapBarTrackMonitor() : lastTrackPos(0.0f), lastLapNum(0), initialized(false) {}

    void reset() {
        lastTrackPos = 0.0f;
        lastLapNum = 0;
        initialized = false;
    }
};

class GapBarHud : public BaseHud {
public:
    GapBarHud();
    virtual ~GapBarHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Override setScale to grow from center instead of top-left
    void setScale(float scale);

    // Set bar width (keeps bar centered when adjusting)
    void setBarWidth(int percent);

    // Track position update for lap timing (called from HudManager)
    void updateTrackPosition(int raceNum, float trackPos, int lapNum);

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void checkAndSavePreviousLap();
    void updateCurrentLapTiming();
    void processSplitUpdates();
    void checkFreezeExpiration();
    int calculateCurrentGap() const;
    float calculateBestLapProgress() const;
    void resetTimingState();

    // Number of timing points to track (0.1% resolution)
    static constexpr int NUM_TIMING_POINTS = 1000;

    // Bar dimensions
    // Width: STANDARD_WIDTH (12 chars) in fontSizeLarge + paddingH on each side
    // Height: paddingV + fontSizeLarge (matches notices widget)
    static constexpr float MARKER_WIDTH_CHARS = 0.5f;  // Thin vertical marker
    static constexpr float BAR_PADDING_V_SCALE = 0.25f;  // Inner vertical padding for markers

    // Freeze duration limits (matches TimingHud)
    static constexpr int MIN_FREEZE_MS = 0;         // 0 = disabled
    static constexpr int MAX_FREEZE_MS = 10000;     // 10 seconds maximum
    static constexpr int DEFAULT_FREEZE_MS = 3000;  // 3 seconds default
    static constexpr int FREEZE_STEP_MS = 500;      // 0.5 second steps

    // Gap bar time range limits (how much time fits from center to edge)
    static constexpr int MIN_RANGE_MS = 500;        // 0.5 second minimum
    static constexpr int MAX_RANGE_MS = 5000;       // 5 seconds maximum
    static constexpr int DEFAULT_RANGE_MS = 2000;   // 2 seconds default
    static constexpr int RANGE_STEP_MS = 500;       // 0.5 second steps

    // Bar width limits (percentage of base width which matches notices widget)
    static constexpr int MIN_WIDTH_PERCENT = 50;    // 50% minimum
    static constexpr int MAX_WIDTH_PERCENT = 400;   // 400% maximum
    static constexpr int DEFAULT_WIDTH_PERCENT = 100;  // 100% default (matches notices)
    static constexpr int WIDTH_STEP_PERCENT = 10;   // 10% steps

    // Best lap timing data
    std::array<BestLapTimingPoint, NUM_TIMING_POINTS> m_bestLapTimingPoints;
    int m_bestLapTime;                // Best lap time in ms (for reference)
    bool m_hasBestLap;                // Do we have valid best lap data?

    // Current lap timing data
    std::array<BestLapTimingPoint, NUM_TIMING_POINTS> m_currentLapTimingPoints;

    GapBarAnchor m_anchor;            // Anchor for current lap timing
    GapBarTrackMonitor m_trackMonitor;  // For S/F line detection
    float m_currentTrackPos;          // Current position on track (0.0-1.0)
    int m_currentLapNum;              // Current lap number
    bool m_observedLapStart;          // Did we see this lap start at S/F?

    // Cached state for change detection
    int m_cachedDisplayRaceNum;       // Track spectate target changes
    int m_cachedSession;              // Track session changes
    int m_cachedPitState;             // Track pit entry/exit
    int m_cachedLastCompletedLapNum;  // Track lap completions
    int m_cachedSplit1;               // Track split 1 changes
    int m_cachedSplit2;               // Track split 2 changes

    // Player bike brand color (for best lap marker)
    unsigned long m_bikeBrandColor;

    // Freeze state for official split/lap times
    bool m_isFrozen;                  // Currently showing official time (frozen)?
    std::chrono::steady_clock::time_point m_frozenAt;  // When freeze started
    int m_frozenGap;                  // Gap to display during freeze
    int m_frozenSplitIndex;           // Which split triggered freeze (-1=lap, 0=S1, 1=S2)

    // Update rate limiting
    std::chrono::steady_clock::time_point m_lastUpdate;
    static constexpr int UPDATE_INTERVAL_MS = 16;  // ~60Hz update rate

    // === Configurable settings ===
    int m_freezeDurationMs;           // How long to freeze on official times
    bool m_showMarkers;               // Show vertical position markers (current + best lap)
    int m_gapRangeMs;                 // Time range for gap bar (full bar at ±range)
    int m_barWidthPercent;            // Bar width as percentage of default (50-400%)
};
