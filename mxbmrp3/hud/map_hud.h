// ============================================================================
// hud/map_hud.h
// Map HUD that displays track layout and rider positions
// ============================================================================
#pragma once

#include "base_hud.h"
#include "marker_label.h"
#include "rider_flag_icons.h"
#include "../core/lap_delta_profile.h"
#include "hud_defaults.h"
#include "../game/unified_types.h"
#include <algorithm>
#include <array>
#include <vector>

class MapHud : public BaseHud {
public:
    // Pre-calculated rotation values to avoid redundant trig in tight loops
    struct RotationCache {
        float angle = 0.0f;      // Original angle in degrees (needed for yaw adjustment)
        float cosAngle = 1.0f;   // cos(angle) - 1.0 means no rotation (identity)
        float sinAngle = 0.0f;   // sin(angle) - 0.0 means no rotation (identity)
        bool hasRotation = false;
    };

    MapHud();

    void update() override;
    bool handlesDataType(DataChangeType dataType) const override;
    const char* getIconName() const override { return "hud-map"; }
    void resetToDefaults();

    // Override mouse input to update anchor when dragging ends
    bool handleMouseInput(bool allowInput = true) override;

    // Update track centerline data
    // raceData: float array [S/F, split1, split2, holeshot] in meters along centerline,
    // or nullptr if unavailable.
    void updateTrackData(int numSegments, const Unified::TrackSegment* segments, const float* raceData);

    // Update rider positions (called frequently - must be fast)
    void updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions);

    // Rotation mode - rotate map so local player always points up
    void setRotateToPlayer(bool rotate) {
        if (m_bRotateToPlayer != rotate) {
            m_bRotateToPlayer = rotate;
            setDataDirty();
        }
    }
    bool getRotateToPlayer() const { return m_bRotateToPlayer; }

    // Track outline toggle - show white outline around black track
    void setShowOutline(bool show) {
        if (m_bShowOutline != show) {
            m_bShowOutline = show;
            setDataDirty();
        }
    }
    bool getShowOutline() const { return m_bShowOutline; }

    // Outline width scale — scales the visible RIM (the part of the outline pass
    // that sticks out past the fill), not the whole outline ribbon: 100% is the
    // classic look (rim = 0.4x track width), 50% a thin edge, 300% a fat border.
    // Shares one settings control with the on/off ("Off" sits below the minimum,
    // like the zoom Range control).
    void setOutlineWidthScale(float scale) {
        scale = (scale < MIN_OUTLINE_WIDTH_SCALE) ? MIN_OUTLINE_WIDTH_SCALE
              : (scale > MAX_OUTLINE_WIDTH_SCALE) ? MAX_OUTLINE_WIDTH_SCALE : scale;
        if (m_fOutlineWidthScale != scale) {
            m_fOutlineWidthScale = scale;
            setDataDirty();
        }
    }
    float getOutlineWidthScale() const { return m_fOutlineWidthScale; }

    // 100% is the classic rim; the default ships at a slimmer 50%.
    static constexpr float DEFAULT_OUTLINE_WIDTH_SCALE = 0.5f;
    static constexpr float MIN_OUTLINE_WIDTH_SCALE = 0.25f;
    static constexpr float MAX_OUTLINE_WIDTH_SCALE = 3.0f;

    // Track markers toggle - show S/F, sector/split markers and segment-timer lines.
    // Off leaves just the track ribbon and rider markers. Not part of the ribbon cache
    // (these draw after renderTrack), so it does not belong in TrackRibbonKey.
    void setShowTrackMarkers(bool show) {
        if (m_bShowTrackMarkers != show) {
            m_bShowTrackMarkers = show;
            setDataDirty();
        }
    }
    bool getShowTrackMarkers() const { return m_bShowTrackMarkers; }

    // Rider color mode - how to color other riders on the map
    enum class RiderColorMode {
        UNIFORM = 0,        // Accent color for all riders
        BRAND = 1,          // Bike brand colors
        RELATIVE_POS = 2    // Color based on position relative to player
    };

    void setRiderColorMode(RiderColorMode mode) {
        if (m_riderColorMode != mode) {
            m_riderColorMode = mode;
            setDataDirty();
        }
    }
    RiderColorMode getRiderColorMode() const { return m_riderColorMode; }

    // Lap delta: colours the track green where the display rider is gaining on
    // the reference lap and red where they are losing, fading with how fast.
    // Ahead of the rider the previous lap stays on, fainter, until the new lap
    // draws over it (LapDeltaProfile).
    // OFF, or the lap it compares against (the Gap Bar's references, in its order),
    // or DEFAULT: General's reference (hud_defaults.h). DEFAULT is last so the
    // stored values of the others never moved.
    enum class LapDelta : uint8_t {
        OFF = 0,
        SESSION_PB = 1,
        ALLTIME_PB = 2,
        LAST_LAP = 3,
        DEFAULT = 4
    };
    static constexpr int LAP_DELTA_COUNT = 5;
    // The reference lap delta compares against (meaningless while OFF).
    PbGapTracker::Ref lapDeltaReference() const {
        return HudDefaults::reference(m_lapDelta == LapDelta::DEFAULT,
            static_cast<PbGapTracker::Ref>(static_cast<int>(m_lapDelta) - 1));
    }
    void setLapDelta(LapDelta mode) {
        if (m_lapDelta != mode) {
            m_lapDelta = mode;
            setDataDirty();
        }
    }
    LapDelta getLapDelta() const { return m_lapDelta; }

    // Tilt: how far the zoomed map is laid on the ground, in degrees from
    // straight down. 0 is the flat map; more lays it down further, seen from
    // above and behind, farther track smaller, the rider below the centre with
    // the track ahead running into the distance, like a racing game's minimap.
    // Zoomed only: with Mode Overview the map is flat whatever this says. See
    // map_hud_view.cpp.
    static constexpr int MAX_TILT_DEG = 50;
    static constexpr int TILT_STEP_DEG = 5;
    void setTilt(int degrees) {
        // Snapped to the step, so a hand-edited INI value still steps 5, 10, ...
        degrees = std::clamp(degrees, 0, MAX_TILT_DEG);
        degrees = (degrees + TILT_STEP_DEG / 2) / TILT_STEP_DEG * TILT_STEP_DEG;
        if (m_tiltDeg != degrees) {
            m_tiltDeg = degrees;
            setDataDirty();
        }
    }
    int getTilt() const { return m_tiltDeg; }

    // Track line width scale (percentage multiplier, 0.5-2.0)
    void setTrackWidthScale(float scale);
    float getTrackWidthScale() const { return m_fTrackWidthScale; }

    // Rider label display mode
    // Shared with MapHud/RadarHud/GapBarHud — see hud/marker_label.h
    using LabelMode = MarkerLabel::Mode;

    void setLabelMode(LabelMode mode) {
        if (m_labelMode != mode) {
            m_labelMode = mode;
            setDataDirty();
        }
    }
    LabelMode getLabelMode() const { return m_labelMode; }

    // Where the rider label sits relative to the icon (INI-only, no UI).
    // An alias of the shared enum, exactly as LabelMode is: the placement it names
    // is carried out by MarkerLabel::place() for all three marker HUDs, and existing
    // call sites (MapHud::LabelAnchor::LEFT) and the serde keep working unchanged.
    using LabelAnchor = MarkerLabel::Anchor;

    void setLabelAnchor(LabelAnchor anchor) {
        if (m_labelAnchor != anchor) {
            m_labelAnchor = anchor;
            setDataDirty();
        }
    }
    LabelAnchor getLabelAnchor() const { return m_labelAnchor; }

    // Rider shape index (0=OFF, 1-N=icons from AssetManager)
    void setRiderShape(int shapeIndex);
    int getRiderShape() const { return m_riderShapeIndex; }

    // Anchor point for positioning (determines how map grows when dimensions change)
    enum class AnchorPoint {
        TOP_LEFT = 0,
        TOP_RIGHT = 1,
        BOTTOM_LEFT = 2,
        BOTTOM_RIGHT = 3
    };

    void setAnchorPoint(AnchorPoint anchor) { m_anchorPoint = anchor; }
    AnchorPoint getAnchorPoint() const { return m_anchorPoint; }

    // Update position based on anchor point (call after dimension changes)
    void updatePositionFromAnchor();

    // Public constants for settings UI
    static constexpr float DEFAULT_TRACK_WIDTH_SCALE = 1.5f;  // Default 150%
    static constexpr float MIN_TRACK_WIDTH_SCALE = 0.5f;      // Min 50%
    static constexpr float MAX_TRACK_WIDTH_SCALE = 3.0f;      // Max 300%

    // Zoom mode constants (Range setting)
    static constexpr float DEFAULT_ZOOM_DISTANCE = 100.0f;   // Default when zoom first enabled
    static constexpr float MIN_ZOOM_DISTANCE = 50.0f;        // Min 50 meters
    static constexpr float MAX_ZOOM_DISTANCE = 500.0f;       // Max 500 meters

    // Marker scale constants (independent icon/label scaling)
    static constexpr float DEFAULT_MARKER_SCALE = 1.0f;      // Default 100%
    static constexpr float MIN_MARKER_SCALE = 0.5f;          // Min 50%
    static constexpr float MAX_MARKER_SCALE = 3.0f;          // Max 300%

    // Track detail — controls ribbon subdivision density (quad count).
    //
    // Two independent knobs:
    //  * Detail scale 20-200% — quad DENSITY scales linearly with the
    //    percentage (200% emits ~10x the ribbon quads of 20%). This is the
    //    user's CPU/GPU budget dial: the game re-renders every emitted quad on
    //    every frame, so at very high frame rates (300-400 fps) quad count is
    //    the fps-proportional cost, and a percentage gives real granularity
    //    (and matches the other % knobs).
    //  * Adaptive (default ON) — density is normalized in SCREEN space (a
    //    target on-screen step between quads), so a long/windy track gets the
    //    same visual density — and roughly the same quad count — as a short
    //    one at the same scale. OFF = fixed meters-per-quad, predictable in
    //    world units regardless of how big the track draws.
    //
    // The mapping from scale to density is anchored by DETAIL_BASELINE (an
    // INI-only multiplier, default 1.0): 100% at baseline 1.0 is the density a
    // legacy `detail=AUTO` INI migrates to; fixed-mode 200% is 1.0m (legacy HIGH).
    static constexpr float MIN_DETAIL_SCALE = 0.2f;
    static constexpr float MAX_DETAIL_SCALE = 2.0f;
    // Default is deliberately LEANER than the 100% a legacy AUTO maps to: 50%
    // halves the default quad budget with little visible difference at the
    // default map size. Legacy `detail=AUTO` INIs migrate to 100%, not this
    // default, so upgraders keep their exact look.
    static constexpr float DEFAULT_DETAIL_SCALE = 0.5f;
    static constexpr float MIN_DETAIL_BASELINE = 0.25f;
    static constexpr float MAX_DETAIL_BASELINE = 4.0f;
    static constexpr float DEFAULT_DETAIL_BASELINE = 1.0f;

    void setDetailScale(float scale) {
        scale = (scale < MIN_DETAIL_SCALE) ? MIN_DETAIL_SCALE
              : (scale > MAX_DETAIL_SCALE) ? MAX_DETAIL_SCALE : scale;
        if (m_fDetailScale != scale) {
            m_fDetailScale = scale;
            setDataDirty();
        }
    }
    float getDetailScale() const { return m_fDetailScale; }

    void setAdaptiveDetail(bool adaptive) {
        if (m_bAdaptiveDetail != adaptive) {
            m_bAdaptiveDetail = adaptive;
            setDataDirty();
        }
    }
    bool getAdaptiveDetail() const { return m_bAdaptiveDetail; }

    void setDetailBaseline(float baseline) {
        baseline = (baseline < MIN_DETAIL_BASELINE) ? MIN_DETAIL_BASELINE
                 : (baseline > MAX_DETAIL_BASELINE) ? MAX_DETAIL_BASELINE : baseline;
        if (m_fDetailBaseline != baseline) {
            m_fDetailBaseline = baseline;
            setDataDirty();
        }
    }
    float getDetailBaseline() const { return m_fDetailBaseline; }

    // Zoom mode - follow player showing limited track distance
    void setZoomEnabled(bool enabled) {
        if (m_bZoomEnabled != enabled) {
            m_bZoomEnabled = enabled;
            setDataDirty();
        }
    }
    bool getZoomEnabled() const { return m_bZoomEnabled; }

    void setZoomDistance(float meters);
    float getZoomDistance() const { return m_fZoomDistance; }

    // Adaptive range: Follow shows more ground the faster the rider goes - Range
    // up to ADAPTIVE_RANGE_SLOW, rising to ADAPTIVE_RANGE_MAX x Range (never
    // past MAX_ZOOM_DISTANCE) by
    // ADAPTIVE_RANGE_FAST, eased over ADAPTIVE_RANGE_EASE_US so it breathes
    // rather than twitches. See updateRangeNow().
    static constexpr float ADAPTIVE_RANGE_MAX = 2.0f;
    static constexpr float ADAPTIVE_RANGE_SLOW = 30.0f / 3.6f;    // m/s
    static constexpr float ADAPTIVE_RANGE_FAST = 120.0f / 3.6f;   // m/s
    static constexpr long long ADAPTIVE_RANGE_EASE_US = 600000;
    void setAdaptiveRange(bool on) {
        if (m_bAdaptiveRange != on) {
            m_bAdaptiveRange = on;
            setDataDirty();
        }
    }
    bool getAdaptiveRange() const { return m_bAdaptiveRange; }

    // Marker scale - independently scale rider icons and labels
    void setMarkerScale(float scale);
    float getMarkerScale() const { return m_fMarkerScale; }

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;
    // Test-only: counts the lap delta's tinted ribbon quads, which only the ribbon
    // cache tells apart from rider markers in the same colours. See
    // core/test_hooks.cpp, which is excluded from every shipping target.
    friend int MXBMRP3_Test_MapLapDeltaQuadsImpl(int*, int*, int*);
    // Test-only: reads the ribbon cache and the clip rect (test_hooks_map_view.cpp).
    friend int MXBMRP3_Test_MapViewQuadsImpl(float*, int*, int*, float*);

protected:
    void rebuildRenderData() override;
    // Note: rebuildLayout() uses base class default (full rebuild)
    // This is appropriate for HUDs with many dynamically-generated quads

private:
    // Click region for rider selection (spectator switching)
    struct RiderClickRegion {
        float x, y, width, height;
        int raceNum;
    };

    // Track segment storage
    // The centreline arrives once per session via updateTrackData.
    // raw-cache: input to the ribbon caches; PluginData never stores track geometry.
    std::vector<Unified::TrackSegment> m_trackSegments;

    // Race marker layout (fixed size for the supported games)
    static constexpr int RACE_MARKER_COUNT = 4;
    enum RaceMarkerSlot { MARKER_SF = 0, MARKER_SPLIT_1 = 1, MARKER_SPLIT_2 = 2, MARKER_HOLESHOT = 3 };

    // Resolved world position for a single race marker, computed once on track update
    struct RaceMarker {
        bool valid = false;     // false if input was missing or out of range
        float worldX = 0.0f;
        float worldY = 0.0f;
        float angleDeg = 0.0f;  // tangent angle at this point (heading along track)
    };
    std::array<RaceMarker, RACE_MARKER_COUNT> m_raceMarkers{};

    // Start/finish position in centerline meters (raceData[0]); -1 if not provided.
    // Used to map a segment boundary's S/F-relative trackPos back to centerline
    // meters for rendering (the player's trackPos is 0 at S/F, not at meters 0).
    float m_sfMeters = -1.0f;

    // Rider position storage (updated frequently)
    // Pushed whole by HudManager::updateRiderPositions; assign() replaces it each batch.
    // raw-cache: the map plots riders in world space, and handleRaceTrackPosition drops
    // posX/posY/posZ/yaw before PluginData sees them — there is no other source.
    std::vector<Unified::TrackPositionData> m_riderPositions;

    // Click regions for rider selection (populated during renderRiders)
    std::vector<RiderClickRegion> m_riderClickRegions;

    // Map rendering configuration
    static constexpr float MAP_HEIGHT = 0.33f;  // Map height as fraction of screen (0.33 = 33%)
    static constexpr float MAP_PADDING = 0.01f;  // Padding from screen edge

    // Configurable track line width scale (percentage multiplier)
    float m_fTrackWidthScale;

    // Memory reservation sizes (optimize initial allocation)
    static constexpr size_t RESERVE_TRACK_SEGMENTS = 200;  // Typical track has 100-200 segments
    static constexpr size_t RESERVE_QUADS = 1000;          // 1 background + ~400-1000 track pixels (2x density)
    static constexpr size_t RESERVE_STRINGS = 60;          // Title + optional message + rider labels

    // Map bounds (calculated from track data)
    float m_minX, m_maxX, m_minY, m_maxY;
    float m_fTrackScale;     // Scale factor to fit track in map area
    float m_fBaseMapWidth;   // Base (unscaled) map width
    float m_fBaseMapHeight;  // Base (unscaled) map height
    bool m_bHasTrackData;

    // WHERE THE MAP AREA SITS INSIDE THE PANEL, from the engine's plan: the frame's
    // clearance, the caption row and the card's border, on both axes. Set once in
    // rebuildRenderData() and read by every site that converts a world point to a
    // screen point (map_hud_track.cpp, map_hud_riders.cpp).
    //
    // One owner, both axes: a per-site spelling of "how far down does the title push
    // us" covers the Y axis only, and content that runs to the panel's own edges has
    // a themed frame drawn over the track.
    float m_fContentDX = 0.0f;
    float m_fContentDY = 0.0f;

    // Rotation mode
    bool m_bRotateToPlayer;  // Rotate map so local player always points up
    float m_fLastRotationAngle;  // Last rotation angle before crash (keeps map orientation stable)
    float m_fLastPlayerX;  // Last player X position before crash (keeps screen position stable)
    float m_fLastPlayerZ;  // Last player Z position before crash (keeps screen position stable)

    // Track outline
    bool m_bShowOutline;  // Show white outline around black track for visual clarity
    float m_fOutlineWidthScale;  // Rim thickness scale (see setOutlineWidthScale)
    bool m_bShowTrackMarkers;  // Show S/F, sector markers and segment-timer lines

    // Rider colorization
    RiderColorMode m_riderColorMode;  // How to color other riders on the map

    // Lap delta colouring of the track fill (see LapDelta). m_deltaRate holds the
    // rate per profile point; it is rebuilt only when the rider reaches the next
    // point, the gap's validity flips, the reference changes or a lap is
    // committed, and the stamp moves with it so the ribbon cache re-renders
    // exactly then.
    LapDelta m_lapDelta = LapDelta::DEFAULT;
    LapDeltaProfile m_deltaProfile;
    LapDeltaRate m_deltaRate;
    int m_deltaPoint = -1;
    int m_deltaRef = -1;
    bool m_deltaLive = false;
    unsigned m_deltaLastLap = 0;   // PbGapTracker::lastLapStamp() at the last refresh
    int m_deltaStamp = 0;
    // Refresh m_deltaRate if stale; returns whether any point has a rate.
    bool refreshDeltaRate();

    // The tilted view (map_hud_view.cpp). m_tiltDeg is the setting; m_tiltMode
    // is what worldToScreen applies while rebuildRenderData draws - the tilt in
    // degrees while a tilted map is zoomed, else 0. It is 0 during layout, so
    // the panel's size and the zoom fit never see the tilt. m_tilt holds the
    // projection for m_tiltMode, recomputed only when the angle changes.
    int m_tiltDeg = 30;
    int m_tiltMode = 0;
    struct TiltView {
        int deg = -1;                 // the angle these were computed for
        float sin = 0.0f, cos = 1.0f;
        float drop = 0.0f;            // the rider sits this many half-heights below centre
        float gMax = 0.0f;            // nearest ground offset before the horizon flip
        float ahead = 0.0f, behind = 0.0f, across = 0.0f;   // the ground shown, zoom half-spans
    };
    TiltView m_tilt;
    void setTiltMode(int degrees);   // m_tiltMode, and m_tilt when the angle changed
    void tiltPoint(float& screenX, float& screenY) const;
    // A directional icon's heading (cos/sin as addRotatedSpriteQuad takes them)
    // turned to follow the tilt at flat screen point (flatX, flatY).
    void tiltHeading(float flatX, float flatY, float& cosYaw, float& sinYaw) const;
    // The world rect the map can show, grown by margin: the zoom bounds, or
    // tilted, the farther and wider ground the far half of the map shows.
    // Every world-space cull (track, markers) uses it, so they agree.
    void viewCullRect(const RotationCache& rotation, float margin,
                      float& minX, float& minY, float& maxX, float& maxY) const;

    // Zoomed, the track runs off the map: the ribbon is cut exactly at the clip
    // rect and track, markers and riders fade out over the last stretch inside
    // it instead of popping. m_fadeEdges is set while a zoomed map draws, with
    // the clip rect it fades toward; edgeFade is 1 everywhere otherwise.
    bool m_fadeEdges = false;
    float m_fadeClip[4] = {};   // left, top, right, bottom
    float edgeFade(float x, float y) const;

    // Rider label display mode
    LabelMode m_labelMode;

    // Where the rider label sits relative to the icon
    LabelAnchor m_labelAnchor;

    // Rider shape index (0=OFF, 1-N=icons from AssetManager)
    int m_riderShapeIndex;

    // Anchor point for positioning
    AnchorPoint m_anchorPoint;
    float m_fAnchorX;  // Desired anchor position in screen space
    float m_fAnchorY;

    // Zoom mode configuration
    bool m_bZoomEnabled;         // Follow player with limited view distance
    float m_fZoomDistance;       // Total view distance in meters (Range setting)
    // The view distance this rebuild draws: m_fZoomDistance, or with Adaptive
    // range on, eased toward the speed's share of it (updateRangeNow). The LOD
    // spacing keeps the setting, so a changing range never rebuilds the world
    // ribbon (WorldRibbonKey); only the screen ribbon follows it.
    bool m_bAdaptiveRange = false;
    float m_fRangeNow = 0.0f;
    long long m_rangeStampUs = -1;
    void updateRangeNow();

    // Marker scale (independent of HUD scale)
    float m_fMarkerScale;        // Scale factor for rider icons and labels

    // Track detail (LOD) — see the public setters for semantics
    float m_fDetailScale;
    bool m_bAdaptiveDetail;
    float m_fDetailBaseline;   // INI-only multiplier the 20-200% scale anchors to

    // Cached track-ribbon quads (outline + fill passes of renderTrack).
    // The ribbon re-tessellates the whole centerline with per-sample trig on
    // every rebuild, but its inputs only change when the VIEW changes - with
    // rotation/zoom off they are identical across the rider-position rebuilds
    // that dominate (every RaceTrackPosition callback), so the previous quads
    // are reused. In rotate-to-player / zoom mode the key changes every
    // rebuild and this degrades to a pass-through (no benefit, no harm).
    // The key must cover EVERY input that affects the emitted quads: the
    // view transform (worldToScreen + applyOffset + title offset), the clip
    // rect, ribbon width/LOD, and the two colors. Track data changes
    // invalidate explicitly via m_ribbonCacheValid in updateTrackData().
    struct TrackRibbonKey {
        float angle = 0.0f;                                  // Rotation
        float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f;  // Render bounds (zoom-overridden)
        float trackScale = 0.0f, baseMapWidth = 0.0f, baseMapHeight = 0.0f;
        float scale = 0.0f;                                  // m_fScale
        // The map area's origin inside the panel (m_fContentDX/DY). An INPUT to
        // renderTrack's output, so it belongs here: it moves with the theme's frame,
        // card and title terms, none of which the other fields can stand in for.
        float contentDX = 0.0f, contentDY = 0.0f;
        float offsetX = 0.0f, offsetY = 0.0f;                // Effective HUD offset (zoom-adjusted)
        float clipLeft = 0.0f, clipTop = 0.0f, clipRight = 0.0f, clipBottom = 0.0f;
        float trackWidthScale = 0.0f;
        float outlineWidthScale = 0.0f;
        float zoomDistance = 0.0f;   // m_fRangeNow at the rebuild
        float detailScale = 0.0f;
        bool adaptiveDetail = false;
        float detailBaseline = 0.0f;
        bool zoomEnabled = false;
        bool showOutline = false;
        bool showTitle = false;
        unsigned long outlineColor = 0;
        unsigned long fillColor = 0;
        // Lap delta: the fill pass is tinted from m_deltaRate (whose stamp moves
        // whenever it is rebuilt), in these two colours.
        bool lapDelta = false;
        int deltaStamp = 0;
        unsigned long gainColor = 0;
        unsigned long lossColor = 0;
        int tilt = 0;   // m_tiltMode (degrees) at the rebuild

        bool operator==(const TrackRibbonKey& o) const {
            return angle == o.angle
                && minX == o.minX && maxX == o.maxX && minY == o.minY && maxY == o.maxY
                && trackScale == o.trackScale
                && baseMapWidth == o.baseMapWidth && baseMapHeight == o.baseMapHeight
                && scale == o.scale
                && contentDX == o.contentDX && contentDY == o.contentDY
                && offsetX == o.offsetX && offsetY == o.offsetY
                && clipLeft == o.clipLeft && clipTop == o.clipTop
                && clipRight == o.clipRight && clipBottom == o.clipBottom
                && trackWidthScale == o.trackWidthScale
                && outlineWidthScale == o.outlineWidthScale
                && zoomDistance == o.zoomDistance
                && detailScale == o.detailScale
                && adaptiveDetail == o.adaptiveDetail
                && detailBaseline == o.detailBaseline
                && zoomEnabled == o.zoomEnabled
                && showOutline == o.showOutline
                && showTitle == o.showTitle
                && outlineColor == o.outlineColor
                && fillColor == o.fillColor
                && lapDelta == o.lapDelta
                && deltaStamp == o.deltaStamp
                && gainColor == o.gainColor
                && lossColor == o.lossColor
                && tilt == o.tilt;
        }
    };
    TrackRibbonKey m_ribbonKey;
    std::vector<SPluginQuad_t> m_ribbonQuads;
    size_t m_ribbonOutlineQuads = 0;   // how many of m_ribbonQuads are the outline pass (first)
    bool m_ribbonCacheValid = false;

    // World-space ribbon centerline cache: one sample point per ribbon vertex along
    // the WHOLE track (center position + unit perpendicular), in world meters. This
    // is the output of the expensive part of renderTrack() — the per-sample arc
    // walk (advanceAlongArc) and perpendicular trig — and it is INDEPENDENT of the
    // view transform (rotation angle, zoom pan, HUD offset, track colors), which are
    // applied per-frame in worldToScreen()/applyOffset(). It is also independent of
    // the track WIDTH scale (half-width is applied per-frame to the unit perp) and
    // of the render bounds (zoom pans m_minX..m_maxX every frame, but the world
    // geometry is unchanged). So it survives the per-frame rebuilds that
    // rotate-to-player and zoom trigger — exactly the case the screen-quad cache
    // (TrackRibbonKey) can't help, because there the view transform changes every
    // frame. renderTrack() transforms these cached points to screen instead of
    // re-tessellating. The cache rebuilds only when the GENERATING inputs change:
    // the LOD subdivision (m_detail + resolved lodSpacing) and whether straights are
    // subdivided (m_bZoomEnabled). Track data changes invalidate it explicitly via
    // m_worldRibbonValid in updateTrackData(). NOTE (maintenance invariant): if you
    // make the emitted world points depend on a NEW input, add it to WorldRibbonKey
    // — miss it and the ribbon serves stale geometry in rotate/zoom.
    // center + UNIT perpendicular, and the sample's track position (0..1 from the
    // S/F line, as the game's trackPos counts; from the data start when the track
    // gave no S/F line) - what the lap delta colours by. The S/F offset is set by
    // updateTrackData(), which also invalidates this cache, so it needs no key field.
    struct WorldRibbonPoint { float cx, cy, upx, upy, pos; };
    // Key fields: detail scale/baseline are FOLDED into the resolved lodSpacing
    // (they act only through it), so they don't appear separately; adaptiveDetail
    // also drives curveMinSteps, so it must be keyed in its own right.
    struct WorldRibbonKey {
        bool adaptiveDetail = false;
        bool zoomEnabled = false;
        bool lapDelta = false;   // subdivides straights, like zoom (only with a reference to tint by)
        float lodSpacing = 0.0f;
        bool operator==(const WorldRibbonKey& o) const {
            return adaptiveDetail == o.adaptiveDetail && zoomEnabled == o.zoomEnabled
                && lapDelta == o.lapDelta && lodSpacing == o.lodSpacing;
        }
    };
    std::vector<WorldRibbonPoint> m_worldRibbon;
    WorldRibbonKey m_worldRibbonKey;
    bool m_worldRibbonValid = false;
    // (Re)build m_worldRibbon for the current track/LOD if its key changed. Called
    // from renderTrack (twice per rebuild, for the outline+fill passes — the second
    // call is a cheap key-check hit).
    void ensureWorldRibbon(float lodSpacing, int curveMinSteps, bool lapDelta);

    // Calculate track bounds from segments
    void calculateTrackBounds();

    // Calculate zoom bounds centered on player position
    // Returns true if player found, false otherwise (falls back to full track)
    bool calculateZoomBounds(float& zoomMinX, float& zoomMaxX, float& zoomMinY, float& zoomMaxY);

    // Calculate which corner to anchor to based on current position
    AnchorPoint calculateAnchorFromPosition() const;

    // Update anchor position from current HUD position
    void updateAnchorFromCurrentPosition();

    // Helper: Calculate track screen bounds at given rotation
    void calculateTrackScreenBounds(const RotationCache& rotation, float& minX, float& maxX, float& minY, float& maxY) const;

    // Calculate rotation angle for map rotation mode (caches player position when active)
    // Returns both the angle and a pre-calculated RotationCache for efficient rendering
    float calculateRotationAngle();
    RotationCache createRotationCache(float rotationAngle) const;

    // Convert world coordinates to map screen coordinates
    // Uses pre-calculated rotation cache to avoid redundant trig in loops
    void worldToScreen(float worldX, float worldY, float& screenX, float& screenY, const RotationCache& rotation) const;
    // worldToScreen before the tilt (the same point when m_tiltMode is 0).
    void worldToScreenFlat(float worldX, float worldY, float& screenX, float& screenY, const RotationCache& rotation) const;

    // Render the track as quads (takes pre-calculated rotation cache, color, and width multiplier)
    // lapDelta: a reference resolved, which subdivides the world ribbon. BOTH
    // passes pass the same value, or they ask ensureWorldRibbon() for different
    // keys and re-tessellate the track twice per rebuild.
    // tint: blend each quad from m_deltaRate (the fill pass only).
    void renderTrack(const RotationCache& rotation, unsigned long trackColor, float widthMultiplier,
                     float clipLeft, float clipTop, float clipRight, float clipBottom,
                     bool lapDelta = false, bool tint = false);

    // Render start marker (takes pre-calculated rotation cache and clip bounds)
    void renderStartMarker(const RotationCache& rotation,
                          float clipLeft, float clipTop, float clipRight, float clipBottom);

    // Render split + holeshot direction-arrow triangles (takes pre-calculated rotation cache and clip bounds)
    void renderRaceMarkers(const RotationCache& rotation,
                           float clipLeft, float clipTop, float clipRight, float clipBottom);

    // Render the custom segment-timer start/end lines (resolved live from trackPos).
    // Drawn last so these dynamic markers sit on top of the fixed markers and riders.
    void renderSegmentMarkers(const RotationCache& rotation,
                              float clipLeft, float clipTop, float clipRight, float clipBottom);

    // Draw a single direction-arrow triangle marker at a resolved world position.
    // Shared by renderRaceMarkers and renderSegmentMarkers.
    void drawDirectionMarker(const RaceMarker& marker, unsigned long color,
                             const RotationCache& rotation,
                             float clipLeft, float clipTop, float clipRight, float clipBottom);

    // Walk segments to compute world XY + tangent angle at the given distance along
    // the centerline. Returns false if distance is out of range or track empty.
    bool centerlinePositionAt(float meters, float& outX, float& outY, float& outAngleDeg) const;

    // Render rider positions as strings (takes pre-calculated rotation cache and clip bounds)
    void renderRiders(const RotationCache& rotation,
                     float clipLeft, float clipTop, float clipRight, float clipBottom);

    // PIN A MARKER THAT FELL OUTSIDE THE MAP TO THE NEAREST EDGE, per axis, and
    // report whether it moved. `halfSize` is the marker's UNROTATED half-size and
    // cos/sinYaw its rotation, because the inset owed is the rotated square's
    // half-extent -- h * (|cos| + |sin|), up to 1.41h on the diagonal.
    //
    // Per-axis is what makes a pinned marker informative rather than decorative:
    // off the left of the map, x pins to the left edge while y still tracks the
    // subject up and down it, so the icon sits beside where the thing actually is.
    //
    // x/y are in POST-applyOffset (clip) space, which is the space the clip bounds
    // are in; callers holding pre-offset draw coordinates convert with the offset
    // delta they already measured for the clip test.
    //
    // One owner because there are two callers with the same geometry to get right --
    // the off-map player in renderRiders and the off-view track pointer below.
    bool clampMarkerToClip(float& x, float& y, float halfSize,
                           float cosYaw, float sinYaw,
                           float clipLeft, float clipTop,
                           float clipRight, float clipBottom) const;

    // "THE TRACK IS THAT WAY": an arrow pinned to the map's edge, pointing at the
    // nearest point of the centerline, drawn only when that point is off-view.
    //
    // ZOOM MODE ONLY, and that is the whole reason it exists. Zoom centres the view
    // on the player (calculateZoomBounds), so the player's own icon can never leave
    // the panel -- what leaves is the TRACK. Ride far enough off and the map is an
    // empty box with your arrow in the middle of it, which is precisely when you
    // most want it. The full-track view has the opposite problem and its own answer
    // (the player clamp in renderRiders); neither covers the other's case.
    void renderOffTrackPointer(const RotationCache& rotation,
                               float clipLeft, float clipTop,
                               float clipRight, float clipBottom);

    // The off-track pointer's LAST chosen ribbon sample, and whether it is usable.
    // Hysteresis state: the coarse probe below can flip between two lobes of similar
    // distance from one frame to the next, and an arrow that snaps between two
    // directions reads as broken even when both answers are defensible. Reset
    // whenever the ribbon is rebuilt, because an index means nothing across a
    // re-tessellation. mt-plain: game thread only (rebuildRenderData).
    size_t m_pointerLastSample = 0;
    bool m_pointerLastValid = false;

    // Handle click on rider marker to switch spectator target
    void handleClick(float mouseX, float mouseY);

    // Cached icon sprite indices (avoid string-based map lookups per rider per frame)
    struct CachedIcons {
        int angleUp = 0;          // the off-view track pointer (renderOffTrackPointer)
        bool initialized = false;

        void ensureInitialized();
    };
    CachedIcons m_iconCache;
    RiderFlagIcons m_flagIcons;  // wrong way, hazard, blue, finished
};

#if defined(MXBMRP3_TEST_BUILD)
// Perf profiling (test builds only): read + reset the accumulated per-phase
// MapHud::rebuildRenderData() time (microseconds), rebuild count, and ribbon-cache
// hit/miss counts since the last read. Lets the headless map perf driver attribute
// the map's per-frame cost to layout/bounds vs ribbon vs markers vs riders, and
// see when the ribbon cache is defeated (miss-rate spike).
void mapHudReadProfile(double& boundsUs, double& ribbonUs, double& markersUs,
                       double& ridersUs, long long& count,
                       long long& ribbonHits, long long& ribbonMiss);
#endif
