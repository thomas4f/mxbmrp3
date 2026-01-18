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
// Gap type flags - which gap comparisons to show
// Primary gap is shown large, secondary gaps shown as compact chips
// ============================================================================
enum GapTypeFlags : uint8_t {
    GAP_NONE       = 0,       // No gap comparison
    GAP_TO_PB      = 1 << 0,  // "Session PB" - gap to personal best lap this session
    GAP_TO_IDEAL   = 1 << 1,  // "Ideal" - gap to ideal lap (sum of best sectors)
    GAP_TO_OVERALL = 1 << 2,  // "Server Best" - gap to best lap by anyone in session
    GAP_TO_ALLTIME = 1 << 3,  // "All-Time PB" - gap to all-time personal best (persisted)
    GAP_TO_RECORD  = 1 << 4,  // "Record" - gap to fastest record from RecordsHud provider

    GAP_DEFAULT_PRIMARY = GAP_TO_PB,           // Default primary: Session PB
    GAP_DEFAULT_SECONDARY = GAP_TO_ALLTIME | GAP_TO_IDEAL | GAP_TO_OVERALL  // Default secondaries
};

// Gap type count (for iteration)
#if GAME_HAS_RECORDS_PROVIDER
inline constexpr int GAP_TYPE_COUNT = 5;
#else
inline constexpr int GAP_TYPE_COUNT = 4;  // No Record gap type without records provider
#endif

// Gap type names and abbreviations for UI
struct GapTypeInfo {
    GapTypeFlags flag;
    const char* name;       // Full name for settings UI
    const char* abbrev;     // 2-char abbreviation for chips
};

// Ordered list of gap types for cycling and display
inline constexpr GapTypeInfo GAP_TYPE_INFO[] = {
    { GAP_TO_PB,      "Session PB",   "PB" },
    { GAP_TO_ALLTIME, "Alltime",      "AT" },
    { GAP_TO_IDEAL,   "Ideal",        "ID" },
    { GAP_TO_OVERALL, "Overall",      "OA" },
#if GAME_HAS_RECORDS_PROVIDER
    { GAP_TO_RECORD,  "Record",       "RC" }
#endif
};

// ============================================================================
// Gap data for a single gap type
// ============================================================================
struct GapData {
    int gap;           // Gap in ms (positive = slower)
    int refTime;       // Reference time in ms (for display)
    bool hasGap;       // Is this gap valid?
    bool isFaster;     // Faster than reference?
    bool isSlower;     // Slower than reference?

    GapData() : gap(0), refTime(0), hasGap(false), isFaster(false), isSlower(false) {}

    void set(int gapMs, int referenceTime) {
        gap = gapMs;
        refTime = referenceTime;
        hasGap = (referenceTime > 0);
        isFaster = (gapMs < 0);
        isSlower = (gapMs > 0);
    }

    void reset() {
        gap = 0;
        refTime = 0;
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
    int splitIndex;           // -1=lap complete, 0=S1, 1=S2, 2=S3 (4-sector games)
    bool isInvalid;           // Was this an invalid lap?

    // Gap data for each comparison type (display names in quotes)
    GapData gapToPB;          // "Session PB" - gap to personal best this session
    GapData gapToIdeal;       // "Ideal" - gap to ideal lap (sum of best sectors)
    GapData gapToOverall;     // "Overall" - gap to overall best lap by anyone in session
    GapData gapToAllTime;     // "All-Time PB" - gap to all-time personal best
    GapData gapToRecord;      // "Record" - gap to fastest record from provider

    OfficialTimingData()
        : time(0), lapNum(0), splitIndex(-1), isInvalid(false) {}

    void reset() {
        time = 0;
        lapNum = 0;
        splitIndex = -1;
        isInvalid = false;
        gapToPB.reset();
        gapToIdeal.reset();
        gapToOverall.reset();
        gapToAllTime.reset();
        gapToRecord.reset();
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
    ColumnMode getDisplayMode() const { return m_displayMode; }
    bool isColumnEnabled(Column col) const { return m_columnEnabled[col]; }
    bool isReferenceEnabled() const { return m_showReference; }
    int getDisplayDuration() const { return m_displayDurationMs; }

    // Primary gap (shown large in main row)
    GapTypeFlags getPrimaryGapType() const { return m_primaryGapType; }
    void setPrimaryGapType(GapTypeFlags type);
    void cyclePrimaryGapType(bool forward);

    // Secondary gaps (shown as compact horizontal chips below primary)
    uint8_t getSecondaryGapTypes() const { return m_secondaryGapTypes; }
    bool isSecondaryGapEnabled(GapTypeFlags flag) const { return (m_secondaryGapTypes & flag) != 0; }
    void setSecondaryGapType(GapTypeFlags flag, bool enabled);
    int getEnabledSecondaryGapCount(bool skipPrimaryType = true) const;

    // Helper to get gap type info
    static const GapTypeInfo* getGapTypeInfo(GapTypeFlags flag);
    static const char* getGapTypeName(GapTypeFlags flag);
    static const char* getGapTypeAbbrev(GapTypeFlags flag);

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
    int getOverallBestLapTime() const;
    void calculateAllGaps(int splitTime, int splitIndex, bool isLapComplete);
    void cacheAllTimePB();     // Cache current all-time PB for showing improvement

    // Display mode (Off/Splits/Always) controls when HUD content is shown
    ColumnMode m_displayMode;

    // Column visibility (simple on/off)
    bool m_columnEnabled[COL_COUNT];
    bool m_showReference;            // Show reference time in gap column

    // Configuration
    int m_displayDurationMs;         // How long to freeze on official times (in milliseconds)
    GapTypeFlags m_primaryGapType;   // Primary gap comparison (shown large)
    uint8_t m_secondaryGapTypes;     // Bitfield of secondary gap comparisons (as chips)
    bool m_layoutVertical;           // True = vertical columns, False = horizontal rows

    // Cached data to detect changes (accumulated times from CurrentLapData)
    int m_cachedSplit1;              // Accumulated time to split 1
    int m_cachedSplit2;              // Accumulated time to split 2
    int m_cachedSplit3;              // Accumulated time to split 3 (4-sector games only)
    int m_cachedLastCompletedLapNum; // Last completed lap number (for detection)
    int m_cachedDisplayRaceNum;      // Track spectate target changes
    int m_cachedSession;             // Track session changes (new event)
    int m_cachedPitState;            // Track pit entry/exit (0 = on track, 1 = in pits)

    // Cached all-time PB (for showing improvement when beating PB)
    int m_previousAllTimeLap;        // Previous all-time PB lap time
    int m_previousAllTimeSector1;    // Previous all-time PB sector 1
    int m_previousAllTimeS1PlusS2;   // Previous all-time PB sector 1+2
    int m_previousAllTimeS1PlusS2PlusS3;  // Previous all-time PB sector 1+2+3 (4-sector games)

    // Display state
    bool m_isFrozen;                 // Currently showing official time (frozen)?
    std::chrono::time_point<std::chrono::steady_clock> m_frozenAt;  // When freeze started

    // Cached official data (retained between timing events)
    OfficialTimingData m_officialData;

    // Duration limits
    static constexpr int MIN_DURATION_MS = 0;      // 0 = disabled
    static constexpr int MAX_DURATION_MS = 10000;  // 10 seconds maximum
    static constexpr int DEFAULT_DURATION_MS = 5000;  // 5 seconds default
    static constexpr int DURATION_STEP_MS = 1000;  // 1 second steps
};
