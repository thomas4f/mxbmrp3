// ============================================================================
// hud/timing_hud.h
// Timing HUD - displays accumulated split and lap times as they happen
// Shows accumulated times and gaps (default position: center of screen)
// Supports real-time elapsed timer with per-column visibility modes
// Example: S1: 30.00s, S2: 60.00s (accumulated), Lap: 90.00s
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include "../vendor/piboso/mxb_api.h"
#include <chrono>

// ============================================================================
// Column visibility modes - controls when each column is displayed
// ============================================================================
enum class ColumnMode : uint8_t {
    OFF = 0,      // Never show
    SPLITS = 1,   // Show only during freeze period (official events)
    ALWAYS = 2    // Show always (gap retains last value, time ticks)
};

// ============================================================================
// Cached official timing data - retained between timing events
// ============================================================================
struct OfficialTimingData {
    int time;                 // Official split/lap time (ms)
    int gap;                  // Gap vs best (ms, positive = slower)
    int lapNum;               // Lap number this data is for
    int splitIndex;           // -1=lap complete, 0=S1, 1=S2
    bool hasGap;              // Do we have valid gap data?
    bool isFaster;            // Gap color: faster than best
    bool isSlower;            // Gap color: slower than best
    bool isInvalid;           // Was this an invalid lap?

    OfficialTimingData()
        : time(0), gap(0), lapNum(0), splitIndex(-1)
        , hasGap(false), isFaster(false), isSlower(false), isInvalid(false) {}

    void reset() {
        time = 0;
        gap = 0;
        lapNum = 0;
        splitIndex = -1;
        hasGap = false;
        isFaster = false;
        isSlower = false;
        isInvalid = false;
    }
};

class TimingHud : public BaseHud {
public:
    TimingHud();
    virtual ~TimingHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Column indices
    enum Column : int {
        COL_LABEL = 0,
        COL_TIME = 1,
        COL_GAP = 2,
        COL_COUNT = 3
    };

    // Getters for settings UI
    ColumnMode getColumnMode(Column col) const { return m_columnModes[col]; }
    int getDisplayDuration() const { return m_displayDurationMs; }

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;
    void processTimingUpdates();
    void checkFreezeExpiration();
    bool shouldShowColumn(Column col) const;
    bool needsFrequentUpdates() const;
    int calculateGapToBest(int currentTime, int bestTime) const;
    int getVisibleColumnCount() const;
    void resetLiveTimingState();

    // Per-column visibility modes
    ColumnMode m_columnModes[COL_COUNT];

    // Configuration
    int m_displayDurationMs;         // How long to freeze on official times (in milliseconds)

    // Cached data to detect changes (accumulated times from CurrentLapData)
    int m_cachedSplit1;              // Accumulated time to split 1
    int m_cachedSplit2;              // Accumulated time to split 2
    int m_cachedLastCompletedLapNum; // Last completed lap number (for detection)
    int m_cachedDisplayRaceNum;      // Track spectate target changes
    int m_cachedSession;             // Track session changes (new event)
    int m_cachedPitState;            // Track pit entry/exit (0 = on track, 1 = in pits)

    // Display state
    bool m_isFrozen;                 // Currently showing official time (frozen)?
    std::chrono::time_point<std::chrono::steady_clock> m_frozenAt;  // When freeze started
    std::chrono::time_point<std::chrono::steady_clock> m_lastTickUpdate;  // For rate limiting ticks

    // Cached official data (retained between timing events)
    OfficialTimingData m_officialData;

    // Duration limits
    static constexpr int MIN_DURATION_MS = 0;      // 0 = disabled
    static constexpr int MAX_DURATION_MS = 10000;  // 10 seconds maximum
    static constexpr int DEFAULT_DURATION_MS = 3000;  // 3 seconds default
    static constexpr int DURATION_STEP_MS = 500;   // 0.5 second steps
    static constexpr int TICK_UPDATE_INTERVAL_MS = 6;   // Update ticking display every 6ms (~165Hz)
};
