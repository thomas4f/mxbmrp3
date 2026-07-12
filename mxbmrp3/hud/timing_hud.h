// ============================================================================
// hud/timing_hud.h
// Timing HUD - a centered, stacked timing panel (default position: center of screen):
//
//   |            1:23.456            |   <- current/frozen time (large, centered)
//   | Session          +0:12.526    |   <- one row per chosen comparison: name (left),
//   | Alltime           1:22.100    |      value (right) = the live +/- gap while frozen
//   | Ideal             1:21.800    |      on a split/lap, else the (progressive) target time
//
// The player picks which comparison rows to show; the big time can be toggled. The freeze,
// progressive-reference and segment-timer BEHAVIOUR is unchanged — only the layout is this
// single vertical stack (no horizontal mode, no label row, no primary/secondary split).
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/ui_config.h"  // For PBScope enum
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
// Gap type flags - which comparison rows to show (each enabled flag = one row)
// ============================================================================
enum GapTypeFlags : uint8_t {
    GAP_NONE       = 0,       // No gap comparison
    GAP_TO_PB      = 1 << 0,  // "Session PB" - gap to personal best lap this session
    GAP_TO_IDEAL   = 1 << 1,  // "Ideal" - gap to ideal lap (sum of best sectors)
    GAP_TO_OVERALL = 1 << 2,  // "Server Best" - gap to best lap by anyone in session
    GAP_TO_ALLTIME = 1 << 3,  // "All-Time PB" - gap to all-time personal best (persisted)
    GAP_TO_RECORD  = 1 << 4,  // "Record" - gap to fastest record from RecordsHud provider
    GAP_TO_LASTLAP = 1 << 5,  // "Last Lap" - gap to the previously completed lap

    // Default comparison rows: Session PB + All-Time PB.
    GAP_DEFAULT_ENABLED = GAP_TO_PB | GAP_TO_ALLTIME
};

// Gap type count (for iteration)
#if GAME_HAS_RECORDS_PROVIDER
inline constexpr int GAP_TYPE_COUNT = 6;
#else
inline constexpr int GAP_TYPE_COUNT = 5;  // No Record gap type without records provider
#endif

// Gap type names for UI
struct GapTypeInfo {
    GapTypeFlags flag;
    const char* name;       // Full name for settings UI
};

// Ordered list of gap types for cycling and display
inline constexpr GapTypeInfo GAP_TYPE_INFO[] = {
    { GAP_TO_PB,      "Session"  },
    { GAP_TO_ALLTIME, "Alltime"  },
    { GAP_TO_IDEAL,   "Ideal"    },
    { GAP_TO_OVERALL, "Overall"  },
    { GAP_TO_LASTLAP, "Last Lap" },
#if GAME_HAS_RECORDS_PROVIDER
    { GAP_TO_RECORD,  "Record"   }
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
    GapData gapToLastLap;     // "Last Lap" - gap to the previously completed lap

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
        gapToLastLap.reset();
    }
};

class TimingHud : public BaseHud {
public:
    TimingHud();
    virtual ~TimingHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-timing"; }
    void resetToDefaults();

    // The big time row (top). Toggleable; the comparison rows stand on their own.
    bool isTimeEnabled() const { return m_showTime; }
    void setTimeEnabled(bool on) { m_showTime = on; setDataDirty(); }

    // Comparison rows: each enabled gap type is one row (name + value). No primary/secondary.
    bool isComparisonEnabled(GapTypeFlags flag) const { return (m_enabledComparisons & flag) != 0; }
    void setComparisonEnabled(GapTypeFlags flag, bool enabled);

    // Passive-reference selection, exposed for headless tests/tools (the rendered chip text
    // isn't in /api/state). All segment-agnostic — segment mode has its own single reference.
    //  - currentTargetSplit(): the split boundary the rider is driving toward (0=S1, 1=S2, …;
    //    -1 = timer idle / heading to the finish => full lap).
    //  - cumulativeReferenceMs(type, split): the cumulative target time for that split.
    //  - passiveReferenceMs(type): what the chip shows now = cumulativeReferenceMs(type,
    //    currentTargetSplit()).
    int currentTargetSplit() const;
    int cumulativeReferenceMs(GapTypeFlags type, int targetSplit) const;
    int passiveReferenceMs(GapTypeFlags type) const;

    // Whether the time cell currently renders "INVALID" - frozen on a lap flagged invalid,
    // outside segment mode. Single source of truth for the render path and the test hook
    // (the rendered text isn't in /api/state). A lap interrupted by a pit stop is NOT flagged
    // invalid (see processTimingUpdates), so this is only true for a genuinely timed lap.
    bool showingInvalid() const;

    // Whether the panel is currently holding a frozen official split/lap time (the display
    // "freeze" that follows a split/lap event for displayDuration). Test-only accessor: the
    // freeze state isn't in /api/state, and it's the signal for the "a completed lap must
    // freeze" regression (see MXBMRP3_Test_TimingFrozen).
    bool isFrozen() const { return m_isFrozen; }

    // Test-only: the rendered panel HEIGHT and the scaled dimensions the grid-band geometry
    // test asserts against. The panel is a stack of grid-aligned bands — one lineHeightLarge
    // band for the time row plus one lineHeightNormal band per comparison row, no outer padding
    // (height == (showTime ? lineHeightLarge : 0) + rows*lineHeightNormal) — see
    // rebuildRenderData() and tests/integration/tests/timing_reference_test.cpp.
    struct TestGeometry { float height, paddingV, fontLarge, fontNormal, lineLarge, lineNormal; };
    TestGeometry testGeometry() const {
        ScaledDimensions d = getScaledDimensions();
        return { m_fBoundsBottom - m_fBoundsTop, d.paddingV, d.fontSizeLarge,
                 d.fontSize, d.lineHeightLarge, d.lineHeightNormal };
    }

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
    // Whether the panel's content is showing now: Always -> yes; At-Splits -> only during the
    // freeze after a split/lap (or continuously in segment mode); Off -> never.
    bool contentVisible() const;
    int calculateGap(int currentTime, int referenceTime) const;
    void resetLiveTimingState();

    // Gap calculation helpers
    int getOverallBestLapTime() const;
    void calculateAllGaps(int splitTime, int splitIndex, bool isLapComplete);
    void cacheAllTimePB();     // Cache current all-time PB for showing improvement

    // Whole-lap target for a gap type (segment-agnostic); helper for cumulativeReferenceMs.
    int fullLapReferenceMs(GapTypeFlags type) const;

    // Display mode (Off/Splits/Always) controls when HUD content is shown
    ColumnMode m_displayMode;

    // Configuration
    int m_displayDurationMs;         // How long to freeze on official times (in milliseconds)
    bool m_showTime;                 // Show the big centered time row at the top
    uint8_t m_enabledComparisons;    // Bitfield of comparison rows to show (GapTypeFlags)

    // Cached data to detect changes (accumulated times from CurrentLapData)
    int m_cachedSplit1;              // Accumulated time to split 1
    int m_cachedSplit2;              // Accumulated time to split 2
    int m_cachedSplit3;              // Accumulated time to split 3 (4-sector games only)
    int m_cachedLastCompletedLapNum; // Last completed lap number (for detection)
    int m_cachedDisplayRaceNum;      // Track spectate target changes
    int m_cachedSessionGeneration;   // Track session changes (monotonic counter from PluginData)
    PBScope m_cachedPBScope;         // Track PB scope changes (re-cache all-time PB)
    int m_cachedPitState;            // Track pit entry/exit (0 = on track, 1 = in pits)
    bool m_lapInterruptedByPit = false;  // The in-progress lap passed through the pits -> its
                                         // completion isn't a genuine timed lap (suppress INVALID)
    long long m_cachedSegmentSig = -1;  // Track segment-timer state changes (segment mode line)
    // Segment split-style freeze: hold a completed segment's time on screen briefly.
    unsigned int m_segCachedCompletion = 0;  // last segment completionCounter seen
    bool m_segFrozen = false;                // currently holding a completed segment
    std::chrono::time_point<std::chrono::steady_clock> m_segFrozenAt;  // when the hold started

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
