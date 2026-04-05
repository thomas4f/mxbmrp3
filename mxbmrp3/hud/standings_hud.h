// ============================================================================
// hud/standings_hud.h
// Displays race standings and lap times with position, gaps, and rider information
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/widget_constants.h"
#include <vector>
#include <unordered_map>
#include <chrono>

class StandingsHud : public BaseHud {
public:
    StandingsHud();
    virtual ~StandingsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Column flags - each bit represents a column that can be toggled
    enum ColumnFlags : uint32_t {
        COL_TRACKED     = 1 << 0,   // Status icon column (hazard/blue flag/checkered/tracked)
        COL_POS         = 1 << 1,   // Position
        COL_RACENUM     = 1 << 2,   // Race number
        COL_NAME        = 1 << 3,   // Rider name
        COL_BIKE        = 1 << 4,   // Bike name
        COL_BEST_LAP    = 1 << 5,   // Best lap time
        COL_GAP         = 1 << 6,   // Gap column (auto-selects official or live data, shows RET/DNS/DSQ for non-participants)
        COL_PENALTY     = 1 << 7,   // Penalty seconds (last column, rare event)

        COL_REQUIRED = 0,      // No required columns
        COL_DEFAULT  = 0x4F    // Default columns: Tracked, Pos, RaceNum, Name, Gap
    };

    // Who to show gap data for
    // Gap display mode (merged scope + on/off toggle)
    enum class GapMode : uint8_t {
        OFF = 0,       // Gap column hidden
        PLAYER = 1,    // Show only player's gap
        ADJACENT = 2,  // Show gap to rider directly ahead (all rows)
        ALL = 3        // Show all riders' gaps
    };

    // Gap reference point (what gaps are relative to)
    enum class GapReferenceMode : uint8_t {
        LEADER = 0,      // Gaps relative to race leader
        PLAYER = 1,      // Gaps relative to player (negative = ahead, positive = behind)
        ALTERNATING = 2   // Automatically cycles between Leader and Player
    };

    // Rider name display mode
    enum class NameMode : uint8_t {
        OFF = 0,     // No rider name column
        SHORT = 1,   // 3-character abbreviated name (default)
        LONG = 2     // Full name (width determined by longest name in list)
    };

    // Column indices (used with ColumnDef::columnIndex to identify columns)
    static constexpr uint8_t COL_IDX_TRACKED     = 0;
    static constexpr uint8_t COL_IDX_POS         = 1;
    static constexpr uint8_t COL_IDX_RACENUM     = 2;
    static constexpr uint8_t COL_IDX_NAME        = 3;
    static constexpr uint8_t COL_IDX_BIKE        = 4;
    static constexpr uint8_t COL_IDX_BEST_LAP    = 5;
    static constexpr uint8_t COL_IDX_GAP         = 6;
    static constexpr uint8_t COL_IDX_PENALTY     = 7;

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

    // Click region for rider selection
    struct RiderClickRegion {
        float x, y, width, height;
        int raceNum;
    };

    // Penalty formatting constants (milliseconds to seconds conversion with rounding)
    static constexpr int MS_TO_SEC_DIVISOR = 1000;       // Divide milliseconds by 1000 to get seconds
    static constexpr int MS_TO_SEC_ROUNDING_OFFSET = 500; // Add 500ms before dividing to round to nearest second

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Check if column is enabled
    bool isColumnEnabled(ColumnFlags col) const {
        return (m_enabledColumns & col) != 0;
    }

    // Calculate dynamic width based on enabled columns
    int getBackgroundWidthChars() const;

    struct DisplayEntry {
        int position;
        int raceNum;
        char name[32];  // Rider name (short=3 chars, long=up to 31 chars)
        char bikeShortName[16];
        unsigned long bikeBrandColor;
        int officialGap;
        int gapLaps;
        int realTimeGap;
        int penalty;
        int state;
        int pit;
        int numLaps;
        int bestLap;
        int numLapsAtLeaderFinish;

        bool isFinishedRace;
        bool sessionFinished;   // Crossed start/finish line after non-race session time expired
        bool hasBestLap;
        bool isPlaceholder;  // Empty row to show configured HUD size

        enum class GapStyle : uint8_t {
            OFFICIAL,   // Primary color (default)
            LIVE,       // Secondary color (fresh live gap)
            LABEL       // Tertiary color (text labels like "Leader"/"Player")
        };
        GapStyle gapStyle;
        unsigned long gapColorOverride;  // Non-zero = override gap column color (e.g., adjacent coloring)

        char formattedPosition[4];
        char formattedRaceNum[12];  // Sized for "999" (4 bytes) with margin
        char formattedGap[16];      // Gap column (official or live, auto-selected; shows RET/DNS/DSQ for non-participants)
        char formattedPenalty[8];
        char formattedLapTime[16];

        DisplayEntry() : position(0), raceNum(-1), bikeBrandColor(0),
            officialGap(0), gapLaps(0), realTimeGap(0), penalty(0), state(0), pit(0), numLaps(0), bestLap(-1), numLapsAtLeaderFinish(-1),
            isFinishedRace(false), sessionFinished(false), hasBestLap(false), isPlaceholder(false), gapStyle(GapStyle::OFFICIAL), gapColorOverride(0) {
            name[0] = '\0';
            bikeShortName[0] = '\0';
            formattedPosition[0] = '\0';
            formattedRaceNum[0] = '\0';
            formattedGap[0] = '\0';
            formattedPenalty[0] = '\0';
            formattedLapTime[0] = '\0';
        }

        static DisplayEntry fromRaceEntry(const RaceEntryData& entry, const StandingsData* standings);

        void updateFormattedStrings();
    };

    // Rendering helpers (declared after DisplayEntry)
    void renderRiderRow(const DisplayEntry& entry, bool isPlaceholder, float currentY, const ScaledDimensions& dim, int rowIndex);

    // Add riders from classification[startIdx..endIdx] to m_displayEntries
    // Updates m_cachedPlayerIndex when player found; positionBase is display position (e.g., 1 for P1)
    void addDisplayEntries(int startIdx, int endIdx, int positionBase,
                          const std::vector<int>& classificationOrder, const PluginData& pluginData);

    // Returns true if gap should display for this row (accounts for PLAYER/ALL scope)
    bool shouldShowGapForScope(bool isPlayerRow) const;

    // Click handling for rider selection
    void handleClick(float mouseX, float mouseY);

    struct ColumnPositions {
        float tracked;
        float pos;
        float raceNum;
        float name;
        float bike;
        float bestLap;
        float gap;
        float penalty;

        ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns, int nameWidth = COL_NAME_WIDTH_SHORT);
    };

    // Column descriptor for table-driven rendering
    struct ColumnDescriptor {
        uint8_t columnIndex;  // 0-8 for the 9 columns
        float position;
        uint8_t justify;
        bool useEmptyForPlaceholder;  // Some columns show "" for placeholder instead of "---"
    };

    void buildColumnTable();  // Build m_columnTable based on m_enabledColumns

    // Helper struct for shared dimension calculations
    struct HudDimensions {
        float backgroundWidth;
        float backgroundHeight;
        float titleHeight;
        float contentStartX;
        float contentStartY;
    };

    HudDimensions calculateHudDimensions(const ScaledDimensions& dim, int rowCount = -1) const;

    std::vector<DisplayEntry> m_displayEntries;  // Rider entries (m_displayRowCount)
    std::vector<RiderClickRegion> m_riderClickRegions;  // Click regions for rider selection
    ColumnPositions m_columns;
    uint32_t m_enabledColumns = COL_DEFAULT;  // Bitfield of enabled columns (managed by profile system)

    // Gap display settings
    GapMode m_gapMode = GapMode::ALL;
    GapReferenceMode m_gapReferenceMode = GapReferenceMode::PLAYER;

    // Alternating mode state (only used when m_gapReferenceMode == ALTERNATING)
    static constexpr int DEFAULT_ALTERNATING_INTERVAL_MS = 5000;
    int m_alternatingIntervalMs = DEFAULT_ALTERNATING_INTERVAL_MS;
    GapReferenceMode m_alternatingCurrent = GapReferenceMode::LEADER;  // Which mode is currently showing
    std::chrono::steady_clock::time_point m_lastGapRefToggle = std::chrono::steady_clock::now();

    // Returns the effective reference mode (resolves ALTERNATING to LEADER or PLAYER)
    GapReferenceMode getEffectiveGapReferenceMode() const {
        return (m_gapReferenceMode == GapReferenceMode::ALTERNATING)
            ? m_alternatingCurrent : m_gapReferenceMode;
    }
    std::vector<ColumnDescriptor> m_columnTable;  // Cached table of enabled columns (only includes enabled ones)
    int m_cachedBackgroundWidth = -1;  // Cached width in chars
    int m_cachedPlayerIndex = -1;  // Cached index of player in m_displayEntries (-1 if not found or beyond m_displayRowCount)
    int m_cachedHighlightQuadIndex = -1;  // Cached index of highlight quad in m_quads (-1 if no highlight)
    int m_hoveredRowIndex = -1;  // Row index currently hovered by cursor (-1 if none)

    // Tracking for icon quads (so we can update positions in rebuildLayout)
    struct TrackedIconQuad {
        size_t quadIndex;  // Index in m_quads
        int rowIndex;      // Which row it belongs to
    };
    std::vector<TrackedIconQuad> m_trackedIconQuads;

    // Cached icon state for displayed riders (detect icon changes without DataChangeType)
    // Each entry encodes: raceNum -> (hazardType | blueFlagged | inPit | lastLap)
    std::unordered_map<int, uint8_t> m_cachedIconStates;

    // Cached icon sprite indices (avoid string-based map lookups per rider per frame)
    struct CachedIcons {
        int circleExclamation = 0;
        int flag = 0;
        int flagCheckered = 0;
        int wrench = 0;
        bool initialized = false;

        void ensureInitialized();
    };
    CachedIcons m_iconCache;

    // Computed plate dimensions (shared between rebuildRenderData and rebuildLayout)
    struct PlateGeometry {
        float charWidth;
        float plateWidth;
        float brandStripWidth;
        float stripGap;
        float plateHeight;
        float platePadY;

        PlateGeometry(float fontSize, float lineHeightNormal)
            : charWidth(PluginUtils::calculateMonospaceTextWidth(1, fontSize))
            , plateWidth(charWidth * 4.0f)
            , brandStripWidth(charWidth * 0.5f)
            , stripGap(charWidth * 0.5f)
            , plateHeight(lineHeightNormal * 0.8f)
            , platePadY((lineHeightNormal - plateHeight) * 0.5f)
        {}
    };

    // Tracking for per-row race number plate quads (bg + brand color strip)
    struct RaceNumPlateQuad {
        size_t numberQuadIndex;   // White background quad behind race number
        size_t brandQuadIndex;    // Brand color strip quad to the right
        int rowIndex;
    };
    std::vector<RaceNumPlateQuad> m_raceNumPlateQuads;
    int m_displayRowCount = 10;  // Number of rows to display (configurable 6-50, increment 2)
    int m_topPositionsCount = DEFAULT_TOP_POSITIONS;  // Always show top N positions (global setting, 0-10)
    bool m_bUseAccentForHighlight = false;  // Advanced: use accent color instead of bike brand color for player highlight
    bool m_bClassicLayout = false;  // Classic layout: no number plates, no brand strip, primary-colored race numbers
    NameMode m_nameMode = NameMode::SHORT;  // Rider name display mode (Off/Short/Long)
    int m_longNameWidth = 4;  // Cached width for long name mode (determined by longest name)

    // ========================================================================
    // Position Animation
    // ========================================================================
    // Tracks previous row slot indices by raceNum so we can animate Y transitions
    // when riders change positions in the standings.
    bool m_bAnimatePositions = true;  // Enable/disable position animation

    // Per-rider animation state: maps raceNum -> animation data
    struct RowAnimation {
        int fromSlot;       // Previous row slot index
        int toSlot;         // Target row slot index
        std::chrono::steady_clock::time_point startTime;
    };
    std::unordered_map<int, RowAnimation> m_activeAnimations;  // raceNum -> active animation
    std::unordered_map<int, int> m_previousPositions;           // raceNum -> last known race position
    std::unordered_map<int, int> m_previousSlots;               // raceNum -> last known display slot (visibility check)
    std::chrono::steady_clock::time_point m_frameTime = std::chrono::steady_clock::now();

    float m_animationDurationMs = 250.0f;  // Duration of position slide animation (configurable 50-1000)

    // Ease-out cubic: fast start, smooth deceleration
    static float easeOutCubic(float t) {
        float inv = 1.0f - t;
        return 1.0f - (inv * inv * inv);
    }

    // Returns the animated Y offset for a given row (0.0 if no animation active)
    // The offset is in units of lineHeightNormal (e.g., -2.0 means 2 rows up)
    float getAnimatedRowOffset(int raceNum, float lineHeight) const;

    // Start animations for any riders whose position changed, update m_previousPositions
    void updateAnimationState();

    // Returns true if any animations are still in progress
    bool hasActiveAnimations() const;

    static constexpr int MIN_ROW_COUNT = 6;         // Minimum row count
    static constexpr int MAX_ROW_COUNT = 50;
    static constexpr int DEFAULT_ROW_COUNT = 10;  // Shows top 3 + player with 2 before/after symmetrically
    static constexpr int DEFAULT_TOP_POSITIONS = 3;  // Default: always show top 3
    static constexpr int MAX_TOP_POSITIONS = 10;     // Maximum top positions to always show
    static constexpr int NUM_COLUMNS = 9;
    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Column widths: max_length + 1 for spacing, except last column
    static constexpr int COL_TRACKED_WIDTH = 3;   // Sprite indicator (icon + padding)
    static constexpr int COL_POS_WIDTH = 3;       // "50" (2 chars + 1 spacing, no "P" prefix)
    static constexpr int COL_RACENUM_WIDTH = 6;  // plate (4) + gap (0.5) + strip (0.5) + padding
    static constexpr int COL_NAME_WIDTH_SHORT = 4;  // 3 chars + 1 spacing
    static constexpr int MAX_LONG_NAME_CHARS = 24;  // Max visible chars in LONG name mode
    int getNameColumnWidth() const {
        if (m_nameMode == NameMode::OFF) return 0;
        if (m_nameMode == NameMode::LONG) return m_longNameWidth;
        return COL_NAME_WIDTH_SHORT;
    }
    static constexpr int COL_BIKE_WIDTH = 10;      // Supports longest bike names (9 chars + 1 spacing)
    static constexpr int COL_PENALTY_WIDTH = 5;        // Supports +99s format (4 chars + 1 spacing)
    static constexpr int COL_BEST_LAP_WIDTH = 10;      // Supports M:SS.mmm format (9 chars + 1 spacing)
    static constexpr int COL_GAP_WIDTH = 11;           // Supports +M:SS.mmm official or +M:SS.s live (10 chars + 1 spacing)

    static constexpr int BACKGROUND_WIDTH_CHARS = COL_TRACKED_WIDTH + COL_POS_WIDTH + COL_RACENUM_WIDTH +
        COL_NAME_WIDTH_SHORT + COL_BIKE_WIDTH + COL_PENALTY_WIDTH +
        COL_BEST_LAP_WIDTH + COL_GAP_WIDTH;
};
