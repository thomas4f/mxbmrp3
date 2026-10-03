// ============================================================================
// hud/gap_bar_hud.h
// Gap Bar HUD - visualizes current lap progress vs best lap timing
// Shows a horizontal bar with current position, best lap marker, and live gap
// Can also function as a "flat map" showing opponent positions on track
//
// The gap, the ghost's position and the rider's own are PluginData's
// (core/pb_gap_tracker.h, driven from the central lap timer); this HUD reads
// them and owns only what it presents -- the freeze that holds an official
// split/lap gap on screen -- and the opponent markers of flat-map mode.
// ============================================================================
#pragma once

#include "base_hud.h"
#include "marker_label.h"
#include "official_gap_freeze.h"
#include "freeze_duration.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../game/unified_types.h"
#include <chrono>
#include <vector>
#include <string>

class GapBarHud : public BaseHud {
public:
    // Marker display mode - controls what markers are shown on the bar. Saved by
    // value, so new modes are appended. OFF is the whole marker row gone: the bar
    // is a flat map of the lap, so the rider's OWN marker slides along it too,
    // and a player who finds the sliding distracting wants none of them, not a
    // subset (the bar and the gap text stay).
    enum class MarkerMode {
        GHOST = 0,           // Self + ghost (best lap) only - original behavior
        OPPONENTS = 1,       // Self + all opponents (no ghost)
        GHOST_OPPONENTS = 2, // Self + ghost + opponents (full flat map)
        OFF = 3              // No markers at all
    };
    static constexpr int MARKER_MODE_COUNT = 4;   // the settings cycle and the loader's range

    // One split-tick slot per split the widest game has (GP Bikes' 3).
    static constexpr int SPLIT_SLOTS = 3;

    // Which lap the bar and its ghost compare against (the Lap Log has its own):
    // PluginData's tracker holds the session PB, the persisted all-time PB and
    // the last lap (core/pb_gap_tracker.h). Session PB by default -- what the
    // bar always did -- because a PB set before traces were persisted has no
    // table, and an All-time default would show nothing until it was beaten.
    using Reference = PbGapTracker::Ref;
    static constexpr int REFERENCE_COUNT = PbGapTracker::REF_COUNT;
    Reference getReference() const { return m_reference; }

    // Label display mode - controls what labels appear above markers (like MapHud)
    // Shared with MapHud/RadarHud/GapBarHud — see hud/marker_label.h
    using LabelMode = MarkerLabel::Mode;

    // Where the rider label sits relative to the icon (INI-only, no UI), exactly as
    // MapHud has shipped it -- one key, one enum, one placement function.
    using LabelAnchor = MarkerLabel::Anchor;

    void setLabelAnchor(LabelAnchor anchor) {
        if (m_labelAnchor != anchor) {
            m_labelAnchor = anchor;
            setDataDirty();
        }
    }
    LabelAnchor getLabelAnchor() const { return m_labelAnchor; }

    // Rider color mode - controls how opponent markers are colored (like MapHud/RadarHud)
    enum class RiderColorMode {
        UNIFORM = 0,        // One colour (primary) for all riders
        BRAND = 1,          // Bike brand colors
        RELATIVE_POS = 2    // Color based on position relative to player
    };

    GapBarHud();
    virtual ~GapBarHud() = default;

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-gapbar"; }
    void resetToDefaults();

    // Set bar width (keeps bar centered when adjusting)
    void setBarWidth(int percent);
    void setShowSplits(bool on) {
        if (m_showSplits != on) {
            m_showSplits = on;
            setDataDirty();
        }
    }

    // Rider positions update for flat map mode (called from HudManager)
    void updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions);

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

protected:
    void rebuildLayout() override;

private:
    void rebuildRenderData() override;

    // Bar dimensions
    // Width: base (100%) is 2x the Notices/Timing box width, scaled by m_barWidthPercent, so
    //        50% (the default) matches those panels and higher values run wider
    // Height: lineHeightLarge (the large-font title band = 4 snap-grid cells) - shared with
    //         the Notices and Timing rows so all three line up on the grid
    static constexpr float BAR_PADDING_V_SCALE = 0.25f;  // Inner vertical padding for markers

    // Gap bar time range limits (how much time fits from center to edge)
    static constexpr int MIN_RANGE_MS = 1000;       // 1 second minimum
    static constexpr int MAX_RANGE_MS = 5000;       // 5 seconds maximum
    static constexpr int DEFAULT_RANGE_MS = 2000;   // 2 seconds default
    static constexpr int RANGE_STEP_MS = 250;       // 0.25 second steps (accelerated stepper;
                                                    // old 1000ms-multiple saves stay valid)

    // Bar width as a percentage of the base width (base = 2x the Notices/Timing box width)
    static constexpr int MIN_WIDTH_PERCENT = 50;      // 50% = same width as Notices/Timing
    static constexpr int MAX_WIDTH_PERCENT = 400;     // 400% maximum
    static constexpr int DEFAULT_WIDTH_PERCENT = 50;  // default: the PANEL matches Notices/Timing
    static constexpr int WIDTH_STEP_PERCENT = 1;      // 1% steps

    // Cached state for change detection
    int m_cachedDisplayRaceNum;       // Track spectate target changes
    int m_cachedSessionGeneration;    // Track session changes (monotonic counter)

    // WHERE THE SPLITS ARE, learned from the display rider's track position at each
    // crossing -- the fallback for a game that sends no marker data with its
    // centerline (GP Bikes, whose 3 splits then still get markers after one lap).
    // Per track, so kept across spectate changes and dropped on a session change.
    float m_learnedSplitPos[SPLIT_SLOTS];
    int m_learnSplitCache[SPLIT_SLOTS];

    // Holds the official split/lap gap against m_reference after each crossing
    OfficialGapFreeze m_freeze;

    // Update rate limiting
    std::chrono::steady_clock::time_point m_lastUpdate;
    static constexpr int UPDATE_INTERVAL_MS = 16;  // ~60Hz update rate

    // === Configurable settings ===
    int m_freezeDurationMs;           // How long to freeze on official times
    MarkerMode m_markerMode;          // What markers to show (ghost/opponents/both)
    Reference m_reference = Reference::SESSION_PB;   // Which lap the gap is measured against
    LabelMode m_labelMode;            // What labels to show on markers (like MapHud)
    LabelAnchor m_labelAnchor = LabelAnchor::BELOW;  // ...and where they sit
    RiderColorMode m_riderColorMode;  // How to color opponent markers (like MapHud/RadarHud)
    int m_riderIconIndex;             // Icon shape index (0=OFF/default, 1-N from AssetManager)
    bool m_showGapText;               // Show gap timer text (can hide for pure flat map mode)
    bool m_showGapBar;                // Show green/red gap visualization bars
    int m_gapRangeMs;                 // Time range for gap bar (full bar at ±range)
    int m_barWidthPercent;            // Bar width as percentage of default (50-400%)
    float m_fMarkerScale;             // Marker scale multiplier (0.5-3.0, like MapHud)
    bool m_showSplits = true;         // Ticks where the track's splits are

    // Rider position storage for flat map mode (updated from HudManager)
    // Pushed whole by HudManager::updateRiderPositions; assign() replaces it each batch.
    // raw-cache: WEAKEST of the three exemptions, and the honest label matters — only
    // raceNum + trackPos are read here, both of which PluginData does store. This is a
    // convenience copy of the current batch, not data unavailable elsewhere; it is safe
    // only because it is replaced wholesale. A fair candidate to drop in favour of
    // PluginData + hasActiveTrackPos() for freshness — but not a free one, so nobody
    // has to rediscover the blockers: (1) PluginData exposes only
    // getPlayerTrackPosition(), so this needs a NEW public per-rider accessor for
    // RiderTrackState; (2) it trades batch order for unordered_map order, changing the
    // draw order of overlapping opponent markers. No headless test covers this bar's
    // markers, so (2) wants an in-game look before anyone commits to it.
    std::vector<Unified::TrackPositionData> m_riderPositions;

    // Marker scale constants (matches MapHud pattern)
    static constexpr float DEFAULT_MARKER_BASE_SIZE = 0.012f;  // Base full size (halfSize = 0.006, matches MapHud/StandingsHud)
    static constexpr float DEFAULT_MARKER_SCALE = 1.0f;        // Default 100%
    static constexpr float MIN_MARKER_SCALE = 0.5f;            // Min 50%
    static constexpr float MAX_MARKER_SCALE = 3.0f;            // Max 300%

    // Split positions (0-1 along the lap, S/F excluded, ascending) into out[];
    // returns how many. The centerline's when the game sent them, else learned.
    int collectSplitPositions(float (&out)[SPLIT_SLOTS]) const;
    void learnSplitPositions();
    // Short ticks at the bar's top and bottom edges at each split, the middle --
    // gap text and rider markers -- left clear.
    void renderSplitTicks(float boxY, float boxH, float innerX, float innerW);

    // Helper methods for flat map rendering
    void renderRiderMarkers(float innerX, float innerY, float innerWidth, float innerHeight,
                           const ScaledDimensions& dim);
    unsigned long calculateRiderColor(int riderRaceNum, int displayRaceNum) const;
    void renderMarkerIcon(float centerX, float centerY, float size, int spriteIndex,
                         unsigned long color, int shapeIndex);
    // playerBoost: 1.0 for the pack, MarkerLabel::PLAYER_BOOST for the local player's
    // own marker, which draws larger -- the label sizes and offsets with it.
    void renderMarkerLabel(float centerX, float centerY, float iconHalfSize,
                          int raceNum, int position, const ScaledDimensions& dim,
                          float playerBoost = 1.0f);
};
