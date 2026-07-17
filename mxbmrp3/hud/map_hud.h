// ============================================================================
// hud/map_hud.h
// Map HUD that displays track layout and rider positions
// ============================================================================
#pragma once

#include "base_hud.h"
#include "../game/unified_types.h"
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

    // 100% is the classic pre-1.27.6 rim; the default ships at a slimmer 50%.
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

    // Track line width scale (percentage multiplier, 0.5-2.0)
    void setTrackWidthScale(float scale);
    float getTrackWidthScale() const { return m_fTrackWidthScale; }

    // Rider label display mode
    enum class LabelMode {
        NONE = 0,       // No labels
        POSITION = 1,   // Show position (P1, P2, etc.)
        RACE_NUM = 2,   // Show race number
        BOTH = 3        // Show both (P1 #5)
    };

    void setLabelMode(LabelMode mode) {
        if (m_labelMode != mode) {
            m_labelMode = mode;
            setDataDirty();
        }
    }
    LabelMode getLabelMode() const { return m_labelMode; }

    // Where the rider label sits relative to the icon (INI-only, no UI)
    enum class LabelAnchor {
        BELOW = 0,   // Centered under the icon (default)
        ABOVE = 1,   // Centered over the icon
        LEFT = 2,    // Left of the icon, right-aligned
        RIGHT = 3    // Right of the icon, left-aligned
    };

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
    static constexpr float DEFAULT_TRACK_WIDTH_SCALE = 1.0f;  // Default 100%
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
    // Two independent knobs (replacing the old Auto/High/Low preset):
    //  * Detail scale 20-200% — quad DENSITY scales linearly with the
    //    percentage (200% emits ~10x the ribbon quads of 20%). This is the
    //    user's CPU/GPU budget dial: the game re-renders every emitted quad on
    //    every frame, so at very high frame rates (300-400 fps) quad count is
    //    the fps-proportional cost, and a percentage gives real granularity
    //    where the old three presets didn't (and matches the other % knobs).
    //  * Adaptive (default ON) — density is normalized in SCREEN space (a
    //    target on-screen step between quads), so a long/windy track gets the
    //    same visual density — and roughly the same quad count — as a short
    //    one at the same scale. OFF = fixed meters-per-quad, predictable in
    //    world units regardless of how big the track draws.
    //
    // The mapping from scale to density is anchored by DETAIL_BASELINE (an
    // INI-only multiplier, default 1.0): 100% at baseline 1.0 reproduces the
    // old AUTO exactly; fixed-mode 200% is the old HIGH (1.0m).
    static constexpr float MIN_DETAIL_SCALE = 0.2f;
    static constexpr float MAX_DETAIL_SCALE = 2.0f;
    // Default is deliberately LEANER than the old AUTO (which sits at 100%):
    // 50% halves the default quad budget with little visible difference at the
    // default map size. Legacy `detail=AUTO` INIs migrate to 100%, not this
    // default, so upgraders keep their exact old look.
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

    // Marker scale - independently scale rider icons and labels
    void setMarkerScale(float scale);
    float getMarkerScale() const { return m_fMarkerScale; }

    // Allow SettingsHud and SettingsManager to access private members
    friend class SettingsHud;
    friend class SettingsManager;

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
        float scale = 0.0f;                                  // m_fScale (also drives title offset height)
        float offsetX = 0.0f, offsetY = 0.0f;                // Effective HUD offset (zoom-adjusted)
        float clipLeft = 0.0f, clipTop = 0.0f, clipRight = 0.0f, clipBottom = 0.0f;
        float trackWidthScale = 0.0f;
        float outlineWidthScale = 0.0f;
        float zoomDistance = 0.0f;
        float detailScale = 0.0f;
        bool adaptiveDetail = false;
        float detailBaseline = 0.0f;
        bool zoomEnabled = false;
        bool showOutline = false;
        bool showTitle = false;
        unsigned long outlineColor = 0;
        unsigned long fillColor = 0;

        bool operator==(const TrackRibbonKey& o) const {
            return angle == o.angle
                && minX == o.minX && maxX == o.maxX && minY == o.minY && maxY == o.maxY
                && trackScale == o.trackScale
                && baseMapWidth == o.baseMapWidth && baseMapHeight == o.baseMapHeight
                && scale == o.scale
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
                && fillColor == o.fillColor;
        }
    };
    TrackRibbonKey m_ribbonKey;
    std::vector<SPluginQuad_t> m_ribbonQuads;
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
    struct WorldRibbonPoint { float cx, cy, upx, upy; };  // center + UNIT perpendicular
    // Key fields: detail scale/baseline are FOLDED into the resolved lodSpacing
    // (they act only through it), so they don't appear separately; adaptiveDetail
    // also drives curveMinSteps, so it must be keyed in its own right.
    struct WorldRibbonKey {
        bool adaptiveDetail = false;
        bool zoomEnabled = false;
        float lodSpacing = 0.0f;
        bool operator==(const WorldRibbonKey& o) const {
            return adaptiveDetail == o.adaptiveDetail && zoomEnabled == o.zoomEnabled
                && lodSpacing == o.lodSpacing;
        }
    };
    std::vector<WorldRibbonPoint> m_worldRibbon;
    WorldRibbonKey m_worldRibbonKey;
    bool m_worldRibbonValid = false;
    // (Re)build m_worldRibbon for the current track/LOD if its key changed. Called
    // from renderTrack (twice per rebuild, for the outline+fill passes — the second
    // call is a cheap key-check hit).
    void ensureWorldRibbon(float lodSpacing, int curveMinSteps);

    // Calculate track bounds from segments
    void calculateTrackBounds();

    // Calculate zoom bounds centered on player position
    // Returns true if player found, false otherwise (falls back to full track)
    bool calculateZoomBounds(float& zoomMinX, float& zoomMaxX, float& zoomMinY, float& zoomMaxY) const;

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

    // Render the track as quads (takes pre-calculated rotation cache, color, and width multiplier)
    void renderTrack(const RotationCache& rotation, unsigned long trackColor, float widthMultiplier,
                     float clipLeft, float clipTop, float clipRight, float clipBottom);

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

    // Handle click on rider marker to switch spectator target
    void handleClick(float mouseX, float mouseY);

    // Cached icon sprite indices (avoid string-based map lookups per rider per frame)
    struct CachedIcons {
        int circleExclamation = 0;
        int flag = 0;
        int flagCheckered = 0;
        bool initialized = false;

        void ensureInitialized();
    };
    CachedIcons m_iconCache;
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
