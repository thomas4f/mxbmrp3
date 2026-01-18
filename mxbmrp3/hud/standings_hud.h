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

class StandingsHud : public BaseHud {
public:
    StandingsHud();
    virtual ~StandingsHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    void resetToDefaults();

    // Column flags - each bit represents a column that can be toggled
    enum ColumnFlags : uint32_t {
        COL_TRACKED     = 1 << 0,   // Tracked rider indicator (sprite)
        COL_POS         = 1 << 1,   // Position
        COL_RACENUM     = 1 << 2,   // Race number
        COL_NAME        = 1 << 3,   // Rider name
        COL_BIKE        = 1 << 4,   // Bike name
        COL_STATUS      = 1 << 5,   // Status (PIT, DNS, etc.)
        COL_PENALTY     = 1 << 6,   // Penalty seconds
        COL_BEST_LAP    = 1 << 7,   // Best lap time
        COL_OFFICIAL_GAP = 1 << 8,  // Official gap (multi-state: see GapMode)
        COL_LIVE_GAP    = 1 << 9,   // Live gap (multi-state: see GapMode)
        COL_DEBUG       = 1 << 10,  // Debug column (RTG diagnostics)

        COL_REQUIRED = 0,      // No required columns
        COL_DEFAULT  = 0x3AE   // Default columns (excludes Bike, Penalty, and Tracked)
    };

    // Gap column display modes (for COL_OFFICIAL_GAP and COL_LIVE_GAP)
    enum class GapMode : uint8_t {
        OFF = 0,     // Column hidden
        PLAYER = 1,  // Show only player's gap
        ALL = 2      // Show all riders' gaps
    };

    // Gap indicator row display modes (what data to show in gap rows)
    enum class GapIndicatorMode : uint8_t {
        OFF = 0,       // No gap indicator rows
        OFFICIAL = 1,  // Show only official gap
        LIVE = 2,      // Show only live gap
        BOTH = 3       // Show both official and live gap
    };

    // Gap reference point (what gaps are relative to)
    enum class GapReferenceMode : uint8_t {
        LEADER = 0,  // Gaps relative to race leader (default)
        PLAYER = 1   // Gaps relative to player (negative = ahead, positive = behind)
    };

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
        char name[4];
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

        bool isFinishedRace;
        bool hasBestLap;
        bool hasOfficialGap;
        bool isGapRow;  // Special row showing gap to neighbor
        bool isGapToRiderAhead;  // true = gap to rider ahead (red), false = gap to rider behind (green)
        bool isGapInverted;  // true = track position inverted vs classification (use warning color)
        bool isPlaceholder;  // Empty row to show configured HUD size

        char formattedPosition[4];
        char formattedRaceNum[12];  // Sized for "#999" (5 bytes) with margin
        char formattedStatus[10];
        char formattedOfficialGap[16];
        char formattedLiveGap[16];
        char formattedPenalty[8];
        char formattedLapTime[16];
        char formattedDebug[24];    // Debug column for RTG diagnostics

        DisplayEntry() : position(0), raceNum(-1), bikeBrandColor(0),
            officialGap(0), gapLaps(0), realTimeGap(0), penalty(0), state(0), pit(0), numLaps(0), bestLap(-1),
            isFinishedRace(false), hasBestLap(false), hasOfficialGap(false), isGapRow(false), isGapToRiderAhead(false), isGapInverted(false), isPlaceholder(false) {
            name[0] = '\0';
            bikeShortName[0] = '\0';
            formattedPosition[0] = '\0';
            formattedRaceNum[0] = '\0';
            formattedStatus[0] = '\0';
            formattedOfficialGap[0] = '\0';
            formattedLiveGap[0] = '\0';
            formattedPenalty[0] = '\0';
            formattedLapTime[0] = '\0';
            formattedDebug[0] = '\0';
        }

        static DisplayEntry fromRaceEntry(const RaceEntryData& entry, const StandingsData* standings);

        void updateFormattedStrings();
    };

    // Rendering helpers (declared after DisplayEntry)
    void renderRiderRow(const DisplayEntry& entry, bool isPlaceholder, float currentY, const ScaledDimensions& dim, int rowIndex);

    // Formatting helpers (declared after DisplayEntry)
    void formatStatus(DisplayEntry& entry, const SessionData& sessionData) const;

    // Build gap indicator row for adjacent rider (ahead=red, behind=green)
    // Calculates relative gaps and applies m_gapIndicatorMode filtering
    DisplayEntry buildGapRow(int displayRaceNum, int neighborRaceNum, bool isGapToRiderAhead,
                             const PluginData& pluginData);

    // Add riders from classification[startIdx..endIdx] to m_displayEntries
    // Updates m_cachedPlayerIndex when player found; positionBase is display position (e.g., 1 for P1)
    void addDisplayEntries(int startIdx, int endIdx, int positionBase,
                          const std::vector<int>& classificationOrder, const PluginData& pluginData);

    // Returns true if gap should display (accounts for OFF/ME/ALL mode)
    bool shouldShowGapForMode(GapMode mode, bool isPlayerRow) const;

    // Click handling for rider selection
    void handleClick(float mouseX, float mouseY);

    struct ColumnPositions {
        float tracked;
        float pos;
        float raceNum;
        float name;
        float bike;
        float status;
        float penalty;
        float bestLap;
        float officialGap;
        float liveGap;
        float debug;

        ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns);
    };

    // Column descriptor for table-driven rendering
    struct ColumnDescriptor {
        uint8_t columnIndex;  // 0-9 for the 10 columns
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

    std::vector<DisplayEntry> m_displayEntries;  // Rider entries (m_displayRowCount) + gap rows
    std::vector<RiderClickRegion> m_riderClickRegions;  // Click regions for rider selection
    ColumnPositions m_columns;
    uint32_t m_enabledColumns = COL_DEFAULT;  // Bitfield of enabled columns (managed by profile system)

    // Gap column modes (separate from bitfield to support 3 states: Off/Player/All)
    // These are now single values - profile system handles per-event differentiation
    GapMode m_officialGapMode = GapMode::ALL;
    GapMode m_liveGapMode = GapMode::PLAYER;
    GapIndicatorMode m_gapIndicatorMode = GapIndicatorMode::BOTH;
    GapReferenceMode m_gapReferenceMode = GapReferenceMode::LEADER;
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
    int m_displayRowCount = 10;  // Number of rows to display (configurable 8-30, increment 2)
    int m_topPositionsCount = DEFAULT_TOP_POSITIONS;  // Always show top N positions (global setting, 0-10)
    bool m_bUseAccentForHighlight = false;  // Advanced: use accent color instead of bike brand color for player highlight

    static constexpr int MIN_ROW_COUNT = 8;         // Minimum for useful context (top 3 + player with 1 before/after + 2 gap rows)
    static constexpr int MAX_ROW_COUNT = 30;
    static constexpr int DEFAULT_ROW_COUNT = 10;  // Shows top 3 + player with 2 before/after symmetrically
    static constexpr int DEFAULT_TOP_POSITIONS = 3;  // Default: always show top 3
    static constexpr int MAX_TOP_POSITIONS = 10;     // Maximum top positions to always show
    static constexpr int NUM_COLUMNS = 11;
    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Column widths: max_length + 1 for spacing, except last column
    static constexpr int COL_TRACKED_WIDTH = 3;   // Sprite indicator (3 chars width for sprite + padding)
    static constexpr int COL_POS_WIDTH = 4;
    static constexpr int COL_RACENUM_WIDTH = 5;
    static constexpr int COL_NAME_WIDTH = 4;
    static constexpr int COL_BIKE_WIDTH = 10;      // Supports longest bike names (9 chars + 1 spacing)
    static constexpr int COL_STATUS_WIDTH = 4;
    static constexpr int COL_PENALTY_WIDTH = 5;        // Supports +99s format (4 chars + 1 spacing)
    static constexpr int COL_BEST_LAP_WIDTH = 10;      // Supports M:SS.mmm format (9 chars + 1 spacing)
    static constexpr int COL_OFFICIAL_GAP_WIDTH = 11;  // Supports +M:SS.mmm format (10 chars + 1 spacing)
    static constexpr int COL_LIVE_GAP_WIDTH = 8;       // Supports +M:SS.s format (8 chars)
    static constexpr int COL_DEBUG_WIDTH = 19;         // Debug column for RTG diagnostics (D+M:SS.s:A+M:SS.s format)

    static constexpr int BACKGROUND_WIDTH_CHARS = COL_TRACKED_WIDTH + COL_POS_WIDTH + COL_RACENUM_WIDTH +
        COL_NAME_WIDTH + COL_BIKE_WIDTH + COL_STATUS_WIDTH + COL_PENALTY_WIDTH +
        COL_BEST_LAP_WIDTH + COL_OFFICIAL_GAP_WIDTH + COL_LIVE_GAP_WIDTH + COL_DEBUG_WIDTH;
};
