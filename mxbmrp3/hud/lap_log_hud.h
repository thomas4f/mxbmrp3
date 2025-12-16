// ============================================================================
// hud/lap_log_hud.h
// Lap Log - displays recent lap times with sector splits and personal best
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"

class LapLogHud : public BaseHud {
public:
    LapLogHud();
    virtual ~LapLogHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Column flags - each bit represents a column that can be toggled
    enum ColumnFlags : uint32_t {
        COL_LAP  = 1 << 0,  // Lap number
        COL_S1   = 1 << 1,  // Sector 1 time
        COL_S2   = 1 << 2,  // Sector 2 time
        COL_S3   = 1 << 3,  // Sector 3 time
        COL_TIME = 1 << 4,  // Total lap time

        COL_REQUIRED = 0,    // No required columns
        COL_DEFAULT  = 0x11  // Default: Lap and Time only (excludes sectors)
    };

    // Row count limits (public so static_assert can access them)
    static constexpr int MIN_DISPLAY_LAPS = 1;   // Minimum: show at least one lap
    static constexpr int MAX_DISPLAY_LAPS = PluginConstants::HudLimits::MAX_LAP_LOG_CAPACITY;  // Maximum: matches storage capacity

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    // Override for optimized layout rebuild (just update positions)
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Check if column is enabled
    bool isColumnEnabled(ColumnFlags col) const {
        return (m_enabledColumns & col) != 0;
    }

    // Calculate dynamic width based on enabled columns
    int getBackgroundWidthChars() const;

    // HUD positioning constants
    // Positioned in bottom-left area, below session best
    // NOTE: START_Y is the TOP anchor point - HUD grows DOWNWARD as laps are completed
    // Title appears at TOP of HUD, best lap first, then oldest to newest laps
    static constexpr float START_X = HudPositions::LEFT_EDGE_X;
    static constexpr float START_Y = HudPositions::BOTTOM_Y;
    static constexpr int BACKGROUND_WIDTH_CHARS = 43;  // Max: "L99 44:44.444 44:44.444 44:44.444 44:44.444"
    static constexpr int NUM_COLUMNS = 5;  // Number of columns per row (lap, s1, s2, s3, time)

    // Column width constants (in character counts)
    // Width = max_length + 1 for spacing, except last column
    static constexpr int COL_LAP_WIDTH = 4;    // Lap column ("L99" = 3 chars + 1 gap)
    static constexpr int COL_TIME_WIDTH = 10;  // Time column (M:SS.mmm = 9 chars + 1 gap)
    static constexpr int COL_LAST_TIME_WIDTH = 9;  // Last time column (M:SS.mmm = 9 chars, no gap)

    // Column positions - cached to avoid recalculation
    struct ColumnPositions {
        float lap;     // Lap number column
        float s1;      // Sector 1 time column
        float s2;      // Sector 2 time column
        float s3;      // Sector 3 time column
        float time;    // Total lap time column

        ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns);
    };

    ColumnPositions m_columns;
    uint32_t m_enabledColumns = COL_DEFAULT;  // Bitfield of enabled columns
    int m_maxDisplayLaps = 6;  // Configurable number of laps to display (default: 6, matches SessionBestHud)
    int m_cachedNumDataRows = 6;  // Cached count for rebuildLayout
};
