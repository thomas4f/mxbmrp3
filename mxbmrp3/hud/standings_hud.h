// ============================================================================
// hud/standings_hud.h
// Displays race standings and lap times with position, gaps, and rider information
// ============================================================================
#pragma once

#include "base_hud.h"
#include "plate_geometry.h"
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
    const char* getIconName() const override { return "hud-standings"; }
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
        COL_POSGAIN     = 1 << 8,   // Positions gained/lost since race start (caret + count, races only)
        COL_LAST_LAP    = 1 << 9,   // Last lap time (each rider's most recent lap, cuts included)

        COL_REQUIRED = 0,      // No required columns
        COL_DEFAULT  = 0x4F    // Default columns: status icons, Pos, RaceNum, Name, Gap (POSGAIN + LAST_LAP off by default)
    };

    // Who to show gap data for
    // Gap display mode (merged scope + on/off toggle)
    enum class GapMode : uint8_t {
        OFF = 0,       // Gap column hidden
        PLAYER = 1,    // Show only player's gap
        ADJACENT = 2,  // Show gap to rider directly ahead (all rows)
        ALL = 3,       // Show all riders' gaps
        COUNT          // sentinel: cardinality, mirrored in StandingsGap::Scope
    };

    // Gap reference point (what gaps are relative to)
    enum class GapReferenceMode : uint8_t {
        LEADER = 0,      // Gaps relative to race leader
        PLAYER = 1,      // Gaps relative to player (negative = ahead, positive = behind)
        ALTERNATING = 2, // Automatically cycles between Leader and Player
        COUNT            // sentinel: cardinality, mirrored in StandingsGap::Reference
    };

    // Rider name display mode
    enum class NameMode : uint8_t {
        OFF = 0,     // No rider name column
        SHORT = 1,   // 3-character abbreviated name (default)
        LONG = 2     // Full name (width determined by longest name in list)
    };

    // Position-change animation mode
    enum class AnimationMode : uint8_t {
        OFF = 0,      // No animation, rows snap into place
        BASIC = 1,    // Slide rows when their race position changes
        COLORED = 2   // Slide + tint rows positive/negative during the animation
    };

    // Positions gained/lost reference (what the +/- delta is measured against).
    // Mirrors GapMode/NameMode: a single multi-state control with OFF folded in.
    enum class PosGainMode : uint8_t {
        OFF = 0,         // Column hidden
        RACE_START = 1,  // Delta vs grid position at race start (falls back to LAST_SF on mid-race join)
        LAST_SF = 2,     // Delta vs position at the rider's last start/finish crossing (resets each lap)
        LAST_SPLIT = 3   // Delta vs position at the rider's last split crossing (resets each split)
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
    static constexpr uint8_t COL_IDX_POSGAIN     = 8;
    static constexpr uint8_t COL_IDX_LAST_LAP    = 9;

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
        unsigned long trackedColor;  // Tracked-rider color (0 = not tracked); tints the number plate
        int officialGap;
        int gapLaps;
        int realTimeGap;
        int penalty;
        int state;
        int pit;
        int numLaps;
        int bestLap;
        int lastLap;         // Most recent completed lap time in ms (-1 if none); cuts included
        int numLapsAtLeaderFinish;
        int posDelta;        // Positions gained (+) / lost (-) since race start; valid only when hasPosDelta
        bool hasPosDelta;    // True when a race-start snapshot exists for this rider

        bool isFinishedRace;
        bool sessionFinished;   // Crossed start/finish line after non-race session time expired
        bool hasBestLap;
        bool hasLastLap;
        bool isPlaceholder;  // Empty row to show configured HUD size
        unsigned long lastLapColorOverride;  // Non-zero = override last-lap column color (hidden INI faster/slower coding)

        enum class GapStyle : uint8_t {
            OFFICIAL,   // Primary color (default)
            LIVE,       // Secondary color (fresh live gap)
            LABEL,      // Tertiary color (text labels like "Leader"/"Player")
            COUNT       // sentinel: cardinality, mirrored in StandingsGap::Style
        };
        GapStyle gapStyle;
        unsigned long gapColorOverride;  // Non-zero = override gap column color (e.g., adjacent coloring)

        char formattedPosition[4];
        char formattedRaceNum[12];  // Sized for "999" (4 bytes) with margin
        char formattedGap[16];      // Gap column (official or live, auto-selected; shows RET/DNS/DSQ for non-participants)
        char formattedPenalty[8];
        char formattedLapTime[16];
        char formattedLastLap[16];
        char formattedPosDelta[8];  // Positions gained/lost: abs count (caret shows direction); empty string when held or no reference

        DisplayEntry() : position(0), raceNum(-1), bikeBrandColor(0), trackedColor(0),
            officialGap(0), gapLaps(0), realTimeGap(0), penalty(0), state(0), pit(0), numLaps(0), bestLap(-1), lastLap(-1), numLapsAtLeaderFinish(-1),
            posDelta(0), hasPosDelta(false),
            isFinishedRace(false), sessionFinished(false), hasBestLap(false), hasLastLap(false), isPlaceholder(false), lastLapColorOverride(0), gapStyle(GapStyle::OFFICIAL), gapColorOverride(0) {
            name[0] = '\0';
            bikeShortName[0] = '\0';
            formattedPosition[0] = '\0';
            formattedRaceNum[0] = '\0';
            formattedGap[0] = '\0';
            formattedPenalty[0] = '\0';
            formattedLapTime[0] = '\0';
            formattedLastLap[0] = '\0';
            formattedPosDelta[0] = '\0';
        }

        static DisplayEntry fromRaceEntry(const RaceEntryData& entry, const StandingsData* standings);

        void updateFormattedStrings();
    };

    // Rendering helpers (declared after DisplayEntry)
    void renderRiderRow(const DisplayEntry& entry, bool isPlaceholder, float currentY, const ScaledDimensions& dim, int rowIndex);

    // X anchor for a column's text, accounting for position/race-number alignment
    // (centering/right-align differs by layout) and right-aligned numeric gaps.
    // Shared by renderRiderRow and the drag fast path in rebuildRenderData so the
    // two never disagree and make columns jump when the HUD is moved.
    float getColumnTextX(uint8_t columnIndex, float columnPosition, float fontSize, bool isPlaceholder, bool gapRightAlign = false) const;

    // There is deliberately no getColumnTextY() beside getColumnTextX(). It existed to
    // drop the race number onto its plate, and both row-placing paths had to call it or
    // the number jumped while the HUD was dragged. addString/positionString centre EVERY
    // glyph in its row now, so both paths just use the row's own y -- and the plate is
    // itself centred in the row, so the number lands on it with nothing to special-case.

#if defined(MXBMRP3_TEST_BUILD)
public:
    // How far a row's race number sits from its plate's centre, as a signed fraction
    // of plate height. ZERO IS CENTRED. See the definition for why it is measured from
    // the centre rather than from the plate's top edge, and for the -1000 "no plate on
    // this row" sentinel (the value is signed, so a -1 sentinel is ambiguous).
    //
    // Exists because neither thing it pins survives a screenshot: the number is off by
    // a few pixels when wrong, and the drag/layout fast path once placed it differently
    // from the rebuild path, so it only jumped while the HUD was being moved.
    float testPlateNumberInsetY(int row) const;
private:
#endif

    // Header label and its X anchor for a column. Mirrors the non-placeholder
    // alignment used by renderRiderRow so the header sits over its column. When
    // outJustify is non-null it receives the justify used at string creation
    // (rebuildLayout passes null since it only repositions existing strings).
    static const char* getColumnHeaderLabel(uint8_t columnIndex);
    float getColumnHeaderTextX(uint8_t columnIndex, float columnPosition, float fontSize, int* outJustify) const;

    // Add riders from classification[startIdx..endIdx] to m_displayEntries
    // Updates m_cachedPlayerIndex when player found; positionBase is display position (e.g., 1 for P1)
    void addDisplayEntries(int startIdx, int endIdx, int positionBase,
                          const std::vector<int>& classificationOrder, const PluginData& pluginData);

    // Click handling for rider selection
    void handleClick(float mouseX, float mouseY);

    struct ColumnPositions {
        float tracked;
        float pos;
        float posGain;
        float raceNum;
        float name;
        float bike;
        float bestLap;
        float lastLap;
        float gap;
        float penalty;

        ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns, int nameWidth = COL_NAME_WIDTH_SHORT, int raceNumWidth = COL_RACENUM_WIDTH);
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
    // BOX-MODEL: the plan owns the panel geometry; the named floats are the
    // handful of derived offsets the row/quad sites read. titleHeight no longer
    // contains the caption band — the plan does — only the optional session-info
    // row folded in so every "titleHeight + headerHeight" offset downstream
    // keeps working unchanged.
    struct HudDimensions {
        BaseHud::PanelPlan plan;
        float backgroundWidth;
        float backgroundHeight;
        float titleHeight;        // session-info row only (the band is the plan's)
        float headerHeight;       // Height of the optional column-header row between title and rows (0 if disabled)
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
    PosGainMode m_posGainMode = PosGainMode::OFF;  // Positions-gained column mode + reference (off by default)

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
    // ROW ICON HALF-HEIGHTS as a fraction of the row font -- shared, because BOTH the
    // full rebuild and the layout fast path draw these and they must not disagree. They
    // were file-local to standings_hud_render.cpp while the fast path kept the absolute
    // constants they replaced, so every flag and caret resized itself mid-drag at any
    // uiFontSize other than the default. See the definition comment there.
    static constexpr float STATUS_ICON_HALF_RATIO = 0.30f;
    static constexpr float POSGAIN_ICON_HALF_RATIO = STATUS_ICON_HALF_RATIO * 0.75f;

#if defined(MXBMRP3_TEST_BUILD)
public:
    // The player-row band's span, exactly as handed to addRowHighlight. Width 0 when
    // no band was emitted this rebuild.
    //
    // Recorded rather than re-derived: the whole bug this pins is that the band's span
    // WAS a second derivation (frame border + card border, which stopped being the
    // row's inset when [panel] padding started acting on a plan panel's card), so a
    // test that computed the expected span itself would agree with whichever
    // derivation it copied. See standings_row_band_test.
    void testRowBandX(float& x, float& w) const { x = m_testRowBandX; w = m_testRowBandW; }
private:
    float m_testRowBandX = 0.0f, m_testRowBandW = 0.0f;
#endif
    int m_cachedHighlightQuadIndex = -1;  // Cached index of player row highlight quad in m_quads (-1 if no highlight; only valid when m_bPlayerRowHighlight is on)
    int m_hoveredRowIndex = -1;  // Row index currently hovered by cursor (-1 if none)

    // Tracking for icon quads (so we can update positions in rebuildLayout)
    struct TrackedIconQuad {
        size_t quadIndex;  // Index in m_quads
        int rowIndex;      // Which row it belongs to
    };
    std::vector<TrackedIconQuad> m_trackedIconQuads;

    // Tracking for slide-highlight quads (COLORED animation mode).
    // Cached so rebuildLayout can update position + fade alpha per frame
    // without forcing a full data rebuild.
    struct SlideHighlightQuad {
        size_t quadIndex;       // Index in m_quads
        int rowIndex;           // Row in m_displayEntries
        int raceNum;            // Rider this quad belongs to
        bool promoted;          // true = positive tint, false = negative tint
    };
    std::vector<SlideHighlightQuad> m_slideHighlightQuads;

    // Cached icon state for displayed riders (detect icon changes without DataChangeType)
    // Each entry encodes: raceNum -> (hazardType | blueFlagged | inPit | lastLap | directorLock)
    std::unordered_map<int, uint8_t> m_cachedIconStates;

    // Cached icon sprite indices (avoid string-based map lookups per rider per frame)
    struct CachedIcons {
        int circleExclamation = 0;
        int flag = 0;
        int flagCheckered = 0;
        int wrench = 0;
        int caretUp = 0;          // Positions-gained/lost indicator (rotated 180° for losses)
        int lock = 0;             // Director hold/lock indicator (rider pinned by the director)
        bool initialized = false;

        void ensureInitialized();
    };
    CachedIcons m_iconCache;

    // Tracking for positions-gained/lost caret quads (so rebuildLayout can reposition
    // them on drag/scale without a full data rebuild). 'down' records orientation
    // (caret-up sprite rotated 180° to point down for lost positions).
    struct PosGainIconQuad {
        size_t quadIndex;  // Index in m_quads
        int rowIndex;      // Which row it belongs to
        bool down;         // true = flipped (lost positions), false = upright (gained)
    };
    std::vector<PosGainIconQuad> m_posGainIconQuads;

    // Computed plate dimensions (shared between rebuildRenderData and rebuildLayout)
    struct PlateGeometry {
        float charWidth;
        float plateWidth;
        float brandStripWidth;
        float stripGap;
        float plateHeight;
        float platePadY;
        // The arrow is shorter than the plate so it reads as an arrowhead rather
        // than a full-height spike. arrowInsetY is measured from the PLATE's top,
        // not the row's: callers already hold the plate origin with applyOffset()
        // applied, and re-deriving from rowY would drop that offset.
        float arrowHeight;
        float arrowInsetY;

        PlateGeometry(float fontSize, float lineHeightNormal)
            : charWidth(PluginUtils::calculateMonospaceTextWidth(1, fontSize))
            , plateWidth(charWidth * 4.0f)
            // 0.85 chars, not the 0.5 this was while it was a BAR. A triangle in the
            // same box has half the ink of the rectangle it replaced, so at 0.5 chars
            // against a ~0.96-em plate height it drew a 1:7 needle. 1.25 was tried
            // first and read as too heavy next to the plate; 0.85 with the 0.7 height
            // below gives roughly a 1:2 arrowhead. COL_RACENUM_WIDTH is 6 = plate 4 +
            // gap + strip + padding, so anything past ~1.25 needs the column widened
            // and the whole tower reflowed.
            , brandStripWidth(charWidth * 0.85f)
            // 0.3 chars, tightened from 0.5: close enough to read as part of the
            // plate group rather than a stray mark, still a clear gap so the arrow
            // does not hug the plate edge. Budget: 4 + 0.3 + 0.85 = 5.15 of the
            // 6-char COL_RACENUM_WIDTH, leaving 0.85 trailing padding.
            , stripGap(charWidth * 0.3f)
            , plateHeight(PlateLayout::plateHeight(lineHeightNormal))
            , platePadY(PlateLayout::platePadY(lineHeightNormal))
            // FONT-scaled, not row-scaled: the mark is an icon like the status
            // flags, so it holds its size against the glyphs when uiLineHeight
            // changes (see PlateLayout::kBrandMarkHeightRatio). The PLATE stays
            // row-scaled — it is a row-filling background, like the highlight.
            , arrowHeight(fontSize * PlateLayout::kBrandMarkHeightRatio)
            , arrowInsetY((PlateLayout::plateHeight(lineHeightNormal)
                           - fontSize * PlateLayout::kBrandMarkHeightRatio) * 0.5f)
        {}
    };

    // The brand-coloured mark right of the number plate: the caret-up icon turned to
    // point right, in the same rect the hand-built triangle used, so the proportions
    // PlateGeometry's comments tune (0.85 char wide, 0.7 plate tall) still apply.
    //
    // Falls back to the solid triangle when the icon set has no caret-up, and lives
    // here rather than being written out twice because the drag fast path rebuilds
    // this quad too -- the two copies of the OLD expression are exactly what would
    // have had to be kept in step by hand.
    void setBrandMarkQuad(SPluginQuad_t& quad, float x, float y, const PlateGeometry& pg);

    // Tracking for per-row race number plate quads (bg + brand color strip)
    struct RaceNumPlateQuad {
        size_t numberQuadIndex;   // White background quad behind race number
        size_t brandQuadIndex;    // Brand color strip quad to the right
        int rowIndex;
    };
    std::vector<RaceNumPlateQuad> m_raceNumPlateQuads;
    int m_displayRowCount = 10;  // Number of rows to display (configurable 6-50, increment 2)
    int m_topPositionsCount = DEFAULT_TOP_POSITIONS;  // Always show top N positions (global setting, 0-10)
    bool m_bPlayerRowHighlight = true;        // INI-only: full-row color background on the player/spectated rider's row (set 0 to disable and fall back to the accent-colored name marker)
    bool m_bPlayerRowHighlightBrand = false;  // INI-only: when m_bPlayerRowHighlight is on, use the bike brand color instead of the default accent color
    bool m_bLastLapColorCode = false;         // INI-only: color the Last Lap column vs the player's last lap (green = slower than you, red = faster). Off by default to keep the HUD uncluttered.
    bool m_bClassicLayout = false;  // Classic layout: no number plates, no brand strip, primary-colored race numbers
    bool m_bShowHeaders = false;     // Show a column-header row labeling each enabled column above the rider rows
    bool m_bShowSessionInfo = true;  // Show a session-info row ("<session>: <clock / leader lap / overtime>") below the title
    bool m_bLiveGaps = false;        // Show real-time estimated gaps during races (per-profile; was a global toggle)
    NameMode m_nameMode = NameMode::SHORT;  // Rider name display mode (Off/Short/Long)
    int m_shortNameChars = DEFAULT_SHORT_NAME_CHARS;  // INI-only: visible chars in SHORT name mode (1-31, default 3)
    int m_longNameChars = DEFAULT_LONG_NAME_CHARS;  // INI-only: static visible chars in LONG name mode (4-24, default 16)

    // ========================================================================
    // Position Animation
    // ========================================================================
    // Tracks previous row slot indices by raceNum so we can animate Y transitions
    // when riders change positions in the standings.
    AnimationMode m_animationMode = AnimationMode::BASIC;  // Off / Basic / Colored

    // Per-rider animation state: maps raceNum -> animation data
    struct RowAnimation {
        int fromSlot;       // Previous row slot index
        int toSlot;         // Target row slot index
        std::chrono::steady_clock::time_point startTime;
    };
    std::unordered_map<int, RowAnimation> m_activeAnimations;  // raceNum -> active animation
    std::unordered_map<int, int> m_previousPositions;           // raceNum -> last known race position
    std::unordered_map<int, int> m_previousSlots;               // raceNum -> last known display slot (visibility check)
    std::unordered_map<int, int> m_scratchPositions;            // reused scratch for updateAnimationState (avoids per-rebuild map alloc)
    std::unordered_map<int, int> m_scratchSlots;                //   "  (clear()+swap() keeps bucket capacity across rebuilds)
    std::chrono::steady_clock::time_point m_frameTime = std::chrono::steady_clock::now();

    float m_animationDurationMs = 500.0f;  // Duration of position slide animation (configurable 50-1000)

    // Ease-out cubic: fast start, smooth deceleration
    static float easeOutCubic(float t) {
        float inv = 1.0f - t;
        return 1.0f - (inv * inv * inv);
    }

    // Returns the animated Y offset for a given row (0.0 if no animation active)
    // The offset is in units of lineHeightNormal (e.g., -2.0 means 2 rows up)
    float getAnimatedRowOffset(int raceNum, float lineHeight) const;

    // Returns the linear slide-tint fade [1.0 .. 0.0] for the given rider, or 0.0 if
    // no slide is in progress. Slide tint uses the value directly; the player/hover
    // highlight cross-fades against it (1.0 - fade) so the row stays visually solid.
    float getSlideFade(int raceNum) const;

    // Start animations for any riders whose position changed, update m_previousPositions
    void updateAnimationState();

    // Returns true if any animations are still in progress
    bool hasActiveAnimations() const;

    static constexpr int MIN_ROW_COUNT = 6;         // Minimum row count
    static constexpr int MAX_ROW_COUNT = 50;
    static constexpr int DEFAULT_ROW_COUNT = 10;  // Shows top 3 + player with 2 before/after symmetrically
    static constexpr int DEFAULT_TOP_POSITIONS = 3;  // Default: always show top 3
    // The slide tint is a row band at selection strength, faded over time, so it
    // takes the shared alpha rather than a second copy of the same number. The hover
    // constant that sat beside this is gone: BaseHud::ROW_HOVER_ALPHA is the one
    // place both it and the four other HUDs' copies now come from.
    static constexpr float ROW_HIGHLIGHT_OPACITY = ROW_SELECT_ALPHA;
    static constexpr int MAX_TOP_POSITIONS = 10;     // Maximum top positions to always show
    static constexpr int NUM_COLUMNS = 9;
    // Base position (0,0) - actual position comes from m_fOffsetX/m_fOffsetY
    static constexpr float START_X = 0.0f;
    static constexpr float START_Y = 0.0f;

    // Column widths: max_length + 1 for spacing, except last column
    static constexpr int COL_TRACKED_WIDTH = 3;   // Sprite indicator (icon + padding)
    static constexpr int COL_POS_WIDTH = 3;       // "50" (2 chars + 1 spacing, no "P" prefix)
    static constexpr int COL_POSGAIN_WIDTH = 4;   // caret + up to 2-digit count + spacing
    static constexpr int COL_RACENUM_WIDTH = 6;  // plate (4) + gap (0.5) + strip (0.5) + padding
    static constexpr int COL_RACENUM_WIDTH_CLASSIC = 4;  // 3-digit race number + 1 spacing (no plate/strip/#)
    int getRaceNumColumnWidth() const {
        return m_bClassicLayout ? COL_RACENUM_WIDTH_CLASSIC : COL_RACENUM_WIDTH;
    }
    static constexpr int DEFAULT_SHORT_NAME_CHARS = 3;  // Default visible chars in SHORT name mode
    static constexpr int MIN_SHORT_NAME_CHARS = 1;
    static constexpr int MAX_SHORT_NAME_CHARS = 31;  // Capped by name[32] buffer
    static constexpr int COL_NAME_WIDTH_SHORT = DEFAULT_SHORT_NAME_CHARS + 1;  // default chars + 1 spacing
    static constexpr int DEFAULT_LONG_NAME_CHARS = 16;  // Default visible chars in LONG name mode
    static constexpr int MIN_LONG_NAME_CHARS = 4;
    static constexpr int MAX_LONG_NAME_CHARS = 24;  // Max visible chars in LONG name mode (capped by layout)
    int getNameColumnWidth() const {
        if (m_nameMode == NameMode::OFF) return 0;
        if (m_nameMode == NameMode::LONG) return m_longNameChars + 1;  // static width + 1 spacing
        return m_shortNameChars + 1;  // chars + 1 spacing
    }
    static constexpr int COL_BIKE_WIDTH = 10;      // Supports longest bike names (9 chars + 1 spacing)
    static constexpr int COL_PENALTY_WIDTH = 5;        // Supports +99s format (4 chars + 1 spacing)
    static constexpr int COL_BEST_LAP_WIDTH = 10;      // Supports M:SS.mmm format (9 chars + 1 spacing)
    static constexpr int COL_LAST_LAP_WIDTH = 10;      // Same format as best lap (M:SS.mmm + spacing)
    static constexpr int COL_GAP_WIDTH = 11;           // Supports +M:SS.mmm official or +M:SS.s live (10 chars + 1 spacing)
};

#if defined(MXBMRP3_TEST_BUILD)
// Perf profiling (test builds only): read + reset the accumulated per-phase
// StandingsHud::rebuildRenderData() time (microseconds) and rebuild count.
void standingsReadProfile(double& setupUs, double& formatUs, double& nameAnimUs,
                          double& layoutUs, double& renderUs, long long& count);
// Sub-phase of `render`: microseconds spent resolving the TRACKED-column status
// icon per rider (hazard / director-lock / blue-flag / pit / finished / last-lap /
// tracked lookups). Accumulated in standings_hud_render.cpp; read + reset here.
extern double g_standingsTrackedUs;
double standingsReadTrackedUs();
#endif
