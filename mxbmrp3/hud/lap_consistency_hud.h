// ============================================================================
// hud/lap_consistency_hud.h
// Lap Consistency HUD - visualizes lap time consistency with charts and statistics
// Shows variance from reference lap, trend analysis, and consistency metrics
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../game/game_config.h"
#include <vector>

class LapConsistencyHud : public BaseHud {
public:
    // Display mode determines the visualization style (matches PerformanceHud/TelemetryHud)
    enum DisplayMode : uint8_t {
        DISPLAY_GRAPHS = 0,  // Show only graphs (bars + trend line)
        DISPLAY_VALUES = 1,  // Show only numeric statistics
        DISPLAY_BOTH = 2,    // Show graphs on left, stats on right
        DISPLAY_DEFAULT = DISPLAY_BOTH
    };

    // Reference mode determines what lap times are compared against
    // Names and order match TimingHud for consistency
    enum class ReferenceMode : uint8_t {
        SESSION_PB = 0,     // Compare against personal best lap this session
        ALLTIME = 1,        // Compare against all-time personal best (persisted)
        IDEAL = 2,          // Compare against ideal lap (sum of best sectors)
        OVERALL = 3,        // Compare against best lap by anyone in session
#if GAME_HAS_RECORDS_PROVIDER
        RECORD = 4,         // Compare against fastest record from RecordsHud provider
        AVERAGE = 5,        // Compare against average of displayed laps (unique to this HUD)
        REFERENCE_COUNT = 6
#else
        AVERAGE = 4,        // Compare against average of displayed laps (unique to this HUD)
        REFERENCE_COUNT = 5
#endif
    };

    // Trend line mode determines what overlay is drawn on the chart
    enum class TrendMode : uint8_t {
        OFF = 0,        // No trend line overlay
        LINE = 1,       // Connected dots showing each lap (current behavior)
        AVERAGE = 2,    // Moving average (smoothed line)
        LINEAR = 3,     // Linear regression best-fit line
        TREND_COUNT = 4
    };

    // Stat row flags - which statistics to show in the legend
    enum StatFlags : uint32_t {
        STAT_REF     = 1 << 0,  // Reference time (baseline for comparison)
        STAT_BEST    = 1 << 1,  // Best lap in sample
        STAT_AVG     = 1 << 2,  // Average lap time
        STAT_WORST   = 1 << 3,  // Worst lap in sample
        STAT_LAST    = 1 << 4,  // Most recent lap
        STAT_STDDEV  = 1 << 5,  // Standard deviation (+/-)
        STAT_TREND   = 1 << 6,  // Trend indicator (Faster/Stable/Slower)
        STAT_CONS    = 1 << 7,  // Consistency score (%)

        STAT_DEFAULT = STAT_REF | STAT_BEST | STAT_AVG | STAT_WORST | STAT_LAST |
                       STAT_STDDEV | STAT_CONS,  // Trend disabled by default
        STAT_COUNT   = 8        // Total number of stat rows
    };

    // Lap count options
    static constexpr int MIN_LAP_COUNT = 5;
    static constexpr int MAX_LAP_COUNT = PluginConstants::HudLimits::MAX_LAP_LOG_CAPACITY;  // 30

    LapConsistencyHud();
    virtual ~LapConsistencyHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Statistics calculated from lap data
    struct LapStats {
        int averageMs = 0;          // Average lap time in ms
        int bestMs = 0;             // Best lap time in ms
        int worstMs = 0;            // Worst lap time in ms
        int lastMs = 0;             // Most recent lap time in ms
        int bestLapNum = 0;         // Lap number of best lap
        float stdDevMs = 0.0f;      // Standard deviation in ms
        float consistencyScore = 0.0f;  // 0-100% (higher = more consistent)
        int trendDirection = 0;     // -1 = declining, 0 = stable, +1 = improving
        int validLapCount = 0;      // Number of valid laps in sample
    };

    // Per-lap data for rendering
    struct LapBarData {
        int lapNum;         // Lap number (1-based)
        int lapTimeMs;      // Actual lap time in ms
        int deltaMs;        // Delta from reference (negative = faster)
        bool isValid;       // Whether lap was valid
        bool isBest;        // Whether this is the best lap
    };


    // Calculate statistics from lap log data
    void calculateStatistics();

    // Get reference time based on current mode
    int getReferenceTime() const;

    // Render methods for each display mode
    void renderBars(float x, float y, float width, float height);
    void renderTrendLine(float x, float y, float width, float height);
    void renderStatistics(float x, float y, float width);

    // Helper to add a consistency bar (delta visualization)
    // Note: Caller must skip invalid laps - this only renders valid bars
    void addConsistencyBar(float x, float y, float barWidth, float maxBarHeight,
                           int deltaMs, int maxDeltaMs, bool isBest);

    // Format time for display (e.g., "1:42.583")
    static void formatLapTime(char* buffer, size_t bufferSize, int timeMs);

    // HUD positioning constants
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Layout dimensions (in character units, matches PerformanceHud/TelemetryHud pattern)
    static constexpr int GRAPH_WIDTH_CHARS = 27;   // Width of graph area
    static constexpr int LEGEND_WIDTH_CHARS = 14;  // Width of stats/legend area
    static constexpr float GRAPH_HEIGHT_LINES = 6.0f;  // Fixed graph height (matches TelemetryHud/RumbleHud)

    // Helper to count enabled stat rows
    int getEnabledStatCount() const {
        int count = 0;
        for (int i = 0; i < STAT_COUNT; ++i) {
            if (m_enabledStats & (1 << i)) ++count;
        }
        return count;
    }

    // Helper to calculate background width based on display mode
    int getBackgroundWidthChars() const {
        switch (m_displayMode) {
            case DISPLAY_GRAPHS: return GRAPH_WIDTH_CHARS;
            case DISPLAY_VALUES: return LEGEND_WIDTH_CHARS;
            case DISPLAY_BOTH:
            default: return GRAPH_WIDTH_CHARS + 1 + LEGEND_WIDTH_CHARS;  // +1 for gap
        }
    }

    // Configuration options (saved to INI)
    uint8_t m_displayMode = DISPLAY_DEFAULT;
    ReferenceMode m_referenceMode = ReferenceMode::AVERAGE;
    TrendMode m_trendMode = TrendMode::LINE;  // Default to line graph (current behavior)
    uint32_t m_enabledStats = STAT_DEFAULT;  // Bitfield of enabled stat rows
    int m_lapCount = 15;                     // Number of laps to display (5-30)

    // Advanced tuning parameters (INI-only, not exposed in UI)
    // Consistency score: 100% at CV=0, reaches 0% at CV = 1/scaleFactor
    // Default 20.0 means CV of 5% = 0% consistency (aggressive)
    // Lower values (e.g., 10.0) are more lenient: CV of 10% = 0% consistency
    float m_consistencyScaleFactor = 20.0f;

    // Trend detection: minimum percentage difference between halves to register a trend
    // Default 0.5% - adapts to track length (300ms for 60s laps, 600ms for 120s laps)
    float m_trendThresholdPercent = 0.5f;

    // Calculated data (rebuilt when dirty)
    LapStats m_stats;
    std::vector<LapBarData> m_lapBars;
    int m_cachedMaxDeltaMs = 1000;          // Max delta for scaling (minimum 1 second)
    mutable bool m_referenceAvailable = false;  // True if selected reference mode has valid data (not fallback)
};
