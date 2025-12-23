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
// Gap type flags - which gap comparisons to show (can enable multiple)
// ============================================================================
enum GapTypeFlags : uint8_t {
    GAP_TO_PB      = 1 << 0,  // Gap to personal best lap
    GAP_TO_IDEAL   = 1 << 1,  // Gap to ideal lap (sum of best sectors)
    GAP_TO_SESSION = 1 << 2,  // Gap to session best (fastest lap by anyone)

    GAP_DEFAULT = GAP_TO_PB   // Default: only PB comparison
};

// Gap type count (for iteration)
static constexpr int GAP_TYPE_COUNT = 3;

// ============================================================================
// Gap data for a single gap type
// ============================================================================
struct GapData {
    int gap;          // Gap in ms (positive = slower)
    bool hasGap;      // Is this gap valid?
    bool isFaster;    // Faster than reference?
    bool isSlower;    // Slower than reference?

    GapData() : gap(0), hasGap(false), isFaster(false), isSlower(false) {}

    void set(int gapMs, int referenceTime) {
        gap = gapMs;
        hasGap = (referenceTime > 0);
        isFaster = (gapMs < 0);
        isSlower = (gapMs > 0);
    }

    void reset() {
        gap = 0;
        hasGap = false;
        isFaster = false;
        isSlower = false;
    }
};

// ============================================================================
// Cached official timing data - retained between timing events
// ============================================================================
struct OfficialTimingData {
    int time;                 // Official split/lap time (ms)
    int lapNum;               // Lap number this data is for
    int splitIndex;           // -1=lap complete, 0=S1, 1=S2
    bool isInvalid;           // Was this an invalid lap?

    // Gap data for each comparison type
    GapData gapToPB;          // Gap to personal best
    GapData gapToIdeal;       // Gap to ideal lap
    GapData gapToSession;     // Gap to session best

    OfficialTimingData()
        : time(0), lapNum(0), splitIndex(-1), isInvalid(false) {}

    void reset() {
        time = 0;
        lapNum = 0;
        splitIndex = -1;
        isInvalid = false;
        gapToPB.reset();
        gapToIdeal.reset();
        gapToSession.reset();
    }

    // Legacy accessors for backwards compatibility
    int gap() const { return gapToPB.gap; }
    bool hasGap() const { return gapToPB.hasGap; }
    bool isFaster() const { return gapToPB.isFaster; }
    bool isSlower() const { return gapToPB.isSlower; }
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
    uint8_t getGapTypes() const { return m_gapTypes; }
    bool isGapTypeEnabled(GapTypeFlags flag) const { return (m_gapTypes & flag) != 0; }
    void setGapType(GapTypeFlags flag, bool enabled);
    int getEnabledGapCount() const;

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

    bool needsFrequentUpdates() const override;

private:
    void rebuildRenderData() override;
    void processTimingUpdates();
    void checkFreezeExpiration();
    bool shouldShowColumn(Column col) const;
    int calculateGap(int currentTime, int referenceTime) const;
    int getVisibleColumnCount() const;
    void resetLiveTimingState();

    // Gap calculation helpers
    int getSessionBestLapTime() const;
    int getSessionBestSplit1() const;
    int getSessionBestSplit2() const;
    void calculateAllGaps(int splitTime, int splitIndex, bool isLapComplete);

    // Per-column visibility modes
    ColumnMode m_columnModes[COL_COUNT];

    // Configuration
    int m_displayDurationMs;         // How long to freeze on official times (in milliseconds)
    uint8_t m_gapTypes;              // Bitfield of enabled gap comparisons (GapTypeFlags)

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

    // Cached official data (retained between timing events)
    OfficialTimingData m_officialData;

    // Duration limits
    static constexpr int MIN_DURATION_MS = 0;      // 0 = disabled
    static constexpr int MAX_DURATION_MS = 10000;  // 10 seconds maximum
    static constexpr int DEFAULT_DURATION_MS = 3000;  // 3 seconds default
    static constexpr int DURATION_STEP_MS = 500;   // 0.5 second steps
};
