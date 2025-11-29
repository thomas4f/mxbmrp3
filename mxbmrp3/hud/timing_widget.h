// ============================================================================
// hud/timing_widget.h
// Timing widget - displays accumulated split and lap times as they happen
// Shows accumulated times and gaps in center of screen for 3 seconds
// Example: S1: 30.00s, S2: 60.00s (accumulated), Lap: 90.00s
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <chrono>

class TimingWidget : public BaseHud {
public:
    TimingWidget();
    virtual ~TimingWidget() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void processTimingUpdates();
    bool shouldDisplayTime() const;
    int calculateGapToBest(int currentTime, int bestTime) const;

    // Split type for label display
    enum class SplitType {
        SPLIT_1,
        SPLIT_2,
        LAP
    };

    // Cached data to detect changes (accumulated times from CurrentLapData)
    int m_cachedSplit1;      // Accumulated time to split 1
    int m_cachedSplit2;      // Accumulated time to split 2
    int m_cachedLastCompletedLapNum; // Last completed lap number (for detection)
    int m_cachedDisplayRaceNum; // Track spectate target changes

    // Display state
    int m_displayedTime;     // The time currently being displayed (in milliseconds)
    int m_bestTime;          // The best time for this split/lap (for gap calculation)
    int m_previousBestTime;  // The previous best time (used when setting new PB)
    SplitType m_splitType;   // What type of split/lap is being displayed
    int m_displayedLapNum;   // Lap number being displayed (for lap completions)
    std::chrono::time_point<std::chrono::steady_clock> m_displayStartTime;  // When we started displaying
    bool m_bIsDisplaying;     // Whether we're currently showing a time
    bool m_bIsInvalidLap;     // Whether the current displayed lap is invalid

    // Display duration (3 seconds)
    static constexpr int DISPLAY_DURATION_MS = 3000;
};
