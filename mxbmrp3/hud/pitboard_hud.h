// ============================================================================
// hud/pitboard_hud.h
// Displays pitboard-style information: rider ID, session, position, time, lap,
// split/lap times, gap to leader
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <chrono>

class PitboardHud : public BaseHud {
public:
    PitboardHud();
    virtual ~PitboardHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Row flags - each bit represents a row that can be toggled
    enum RowFlags : uint32_t {
        ROW_RIDER_ID    = 1 << 0,  // Race number and name (e.g., "#131 Tho")
        ROW_SESSION     = 1 << 1,  // Session name (e.g., "Practice")
        ROW_POSITION    = 1 << 2,  // Position (e.g., "P2")
        ROW_TIME        = 1 << 3,  // Session time (e.g., "5m")
        ROW_LAP         = 1 << 4,  // Current lap (e.g., "L2")
        ROW_LAST_LAP    = 1 << 5,  // Last lap time (e.g., "1:21.1")
        ROW_GAP         = 1 << 6,  // Gap to leader (e.g., "+13.3")

        ROW_REQUIRED = 0,          // No required rows
        ROW_DEFAULT  = 0x7F        // All 7 rows enabled (binary: 1111111)
    };

    // Display mode - when to show the pitboard
    enum DisplayMode : uint8_t {
        MODE_ALWAYS = 0,  // Always visible
        MODE_PIT    = 1,  // Show when passing pit area (80% track position)
        MODE_SPLITS = 2,  // Show for 10 seconds when passing splits or s/f
        MODE_COUNT  = 3   // Number of display modes
    };

    // Split type for timing display
    enum SplitType : uint8_t {
        SPLIT_1 = 0,  // Split 1 accumulated time
        SPLIT_2 = 1,  // Split 2 accumulated time
        LAP     = 2   // Full lap time
    };

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    // Override for optimized layout rebuild (just update positions)
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Check if row is enabled
    bool isRowEnabled(RowFlags row) const {
        return (m_enabledRows & row) != 0;
    }

    // Count enabled rows (for height calculation)
    int getEnabledRowCount() const;

    // Override to use large line height
    float calculateBackgroundHeight(int rowCount) const;

    // Check if pitboard should be visible based on display mode
    bool shouldBeVisible() const;

    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;
    static constexpr int BACKGROUND_WIDTH_CHARS = 14;  // Optimized for max content width
    static constexpr int MAX_ROW_COUNT = 5;            // Fixed row count for consistent background size
    static constexpr float LEFT_ALIGN_OFFSET = 0.175f;  // Position column (adjusted for 1920x1080)
    static constexpr float RIGHT_ALIGN_OFFSET = 0.825f; // Lap column (adjusted for 1920x1080)
    static constexpr float TEXTURE_ASPECT_RATIO = 1920.0f / 1080.0f;  // pitboard_hud.tga dimensions

    // Display timing constants
    static constexpr int DISPLAY_DURATION_MS = 10000;  // Show for 10 seconds in Splits mode
    static constexpr float PIT_TRACK_START = 0.75f;    // Start showing at 75% track position
    static constexpr float PIT_TRACK_END = 0.95f;      // Stop showing at 95% track position

    uint32_t m_enabledRows = ROW_DEFAULT;  // Bitfield of enabled rows
    uint8_t m_displayMode = MODE_ALWAYS;   // Display mode setting

    // Tracking for split-triggered display
    int m_cachedSplit1 = -1;
    int m_cachedSplit2 = -1;
    int m_cachedLastLapTime = -1;
    int m_cachedDisplayRaceNum = -1;
    std::chrono::steady_clock::time_point m_displayStartTime;
    bool m_bIsDisplayingTimed = false;  // True when showing timed display (splits mode)
    bool m_bWasVisibleLastFrame = false;  // Track visibility changes for PIT mode

    // Current timing display (split or lap time)
    int m_displayedTime = -1;      // The time to display (accumulated split or lap)
    SplitType m_splitType = LAP;   // Type of time being displayed

    // Cached session time for real-time updates (like TimeWidget)
    int m_cachedRenderedTime = -1;  // Last rendered session time (in ms)
};
