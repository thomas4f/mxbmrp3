// ============================================================================
// hud/map_hud.cpp
// Map HUD implementation - displays track layout and rider positions
// ============================================================================
#include "map_hud.h"
#include "map_hud_internal.h"
#include "../core/plugin_data.h"
#include "../core/plugin_manager.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/ui_config.h"
#include "../core/asset_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

using namespace PluginConstants;
using namespace PluginConstants::Math;

using namespace map_hud_detail;

#if defined(MXBMRP3_TEST_BUILD)
#include <chrono>
// Per-phase profiling for the headless map perf driver (test builds only). Times
// the four rebuild phases and counts ribbon-cache hits/misses, so the map's
// per-frame cost is attributed to layout/bounds vs ribbon tessellation vs markers
// vs riders — and so a regression that defeats the ribbon cache is visible as a
// miss-rate spike. Compiled out of every shipping DLL.
namespace {
    using ProfClock = std::chrono::steady_clock;
    double g_mapBoundsUs = 0.0, g_mapRibbonUs = 0.0, g_mapMarkersUs = 0.0, g_mapRidersUs = 0.0;
    long long g_mapProfCount = 0;
    long long g_mapRibbonHits = 0, g_mapRibbonMiss = 0;
    inline double usSince(ProfClock::time_point a) {
        return std::chrono::duration<double, std::micro>(ProfClock::now() - a).count();
    }
}
void mapHudReadProfile(double& boundsUs, double& ribbonUs, double& markersUs,
                       double& ridersUs, long long& count,
                       long long& ribbonHits, long long& ribbonMiss) {
    boundsUs = g_mapBoundsUs; ribbonUs = g_mapRibbonUs;
    markersUs = g_mapMarkersUs; ridersUs = g_mapRidersUs; count = g_mapProfCount;
    ribbonHits = g_mapRibbonHits; ribbonMiss = g_mapRibbonMiss;
    g_mapBoundsUs = g_mapRibbonUs = g_mapMarkersUs = g_mapRidersUs = 0.0;
    g_mapProfCount = 0; g_mapRibbonHits = 0; g_mapRibbonMiss = 0;
}
#endif

void MapHud::CachedIcons::ensureInitialized() {
    if (initialized) return;
    const AssetManager& assets = AssetManager::getInstance();
    circleExclamation = assets.getIconSpriteIndex("circle-exclamation");
    flag = assets.getIconSpriteIndex("flag");
    flagCheckered = assets.getIconSpriteIndex("flag-checkered");
    initialized = true;
}

MapHud::MapHud()
    : m_fTrackWidthScale(DEFAULT_TRACK_WIDTH_SCALE),  // Reordered to match header declaration order
      m_minX(0.0f), m_maxX(0.0f), m_minY(0.0f), m_maxY(0.0f),
      m_fTrackScale(1.0f), m_fBaseMapWidth(0.0f), m_fBaseMapHeight(0.0f), m_bHasTrackData(false),
      m_bRotateToPlayer(false), m_fLastRotationAngle(0.0f),
      m_fLastPlayerX(0.0f), m_fLastPlayerZ(0.0f),
      m_bShowOutline(true),
      m_bShowTrackMarkers(true),
      m_riderColorMode(RiderColorMode::RELATIVE_POS),
      m_labelMode(LabelMode::RACE_NUM),
      m_labelAnchor(LabelAnchor::BELOW),
      m_riderShapeIndex(1),  // Will be set properly via settings or resetToDefaults
      m_anchorPoint(AnchorPoint::TOP_RIGHT),
      m_fAnchorX(0.0f), m_fAnchorY(0.0f),
      m_bZoomEnabled(false),
      m_fZoomDistance(DEFAULT_ZOOM_DISTANCE),
      m_fMarkerScale(DEFAULT_MARKER_SCALE),
      m_detail(Detail::AUTO) {

    // One-time setup
    DEBUG_INFO("MapHud created");
    setDraggable(true);

    // Initialize map dimensions (will be adjusted when track data loads)
    m_fBaseMapHeight = MAP_HEIGHT;
    m_fBaseMapWidth = MAP_HEIGHT / UI_ASPECT_RATIO;

    // Pre-allocate memory for track segments, quads, and rider positions
    m_trackSegments.reserve(RESERVE_TRACK_SEGMENTS);
    m_riderPositions.reserve(GameLimits::MAX_CONNECTIONS);
    m_riderClickRegions.reserve(GameLimits::MAX_CONNECTIONS);
    m_quads.reserve(RESERVE_QUADS);
    m_strings.reserve(RESERVE_STRINGS);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("map_hud");

    // Set all configurable defaults (including anchor-based position)
    resetToDefaults();
}

void MapHud::update() {
    // OPTIMIZATION: Skip all processing when not visible
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();

    // Check for click in spectator/replay mode to switch to different rider
    const PluginData& pluginData = PluginData::getInstance();
    int drawState = pluginData.getDrawState();
    bool canSwitchRider = (drawState == ViewState::SPECTATE || drawState == ViewState::REPLAY);

    if (canSwitchRider) {
        InputManager& input = InputManager::getInstance();
        if (input.getLeftButton().isClicked()) {
            // Shift into build space so click-to-switch lands right when the map is
            // dragged to a different spot on the companion (no-op in-game).
            CursorPosition cursor = input.getCursorPosition();
            mapCursorToHudSpace(cursor.x, cursor.y);
            if (cursor.isValid && isPointInBounds(cursor.x, cursor.y)) {
                handleClick(cursor.x, cursor.y);
            }
        }
    }
}

bool MapHud::handleMouseInput(bool allowInput) {
    bool wasDragging = isDragging();
    bool result = BaseHud::handleMouseInput(allowInput);

    // If we just stopped dragging, update the anchor point
    if (wasDragging && !isDragging()) {
        updateAnchorFromCurrentPosition();
    }

    return result;
}

bool MapHud::handlesDataType(DataChangeType dataType) const {
    // Need to rebuild rider labels when standings/positions change
    // Also rebuild when tracked riders change (color/shape)
    return dataType == DataChangeType::Standings ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::TrackedRiders;
}

void MapHud::setTrackWidthScale(float scale) {
    // Clamp to valid range
    if (scale < MIN_TRACK_WIDTH_SCALE) scale = MIN_TRACK_WIDTH_SCALE;
    if (scale > MAX_TRACK_WIDTH_SCALE) scale = MAX_TRACK_WIDTH_SCALE;

    if (m_fTrackWidthScale != scale) {
        m_fTrackWidthScale = scale;
        setDataDirty();
    }
}

void MapHud::setZoomDistance(float meters) {
    // Clamp to valid range
    if (meters < MIN_ZOOM_DISTANCE) meters = MIN_ZOOM_DISTANCE;
    if (meters > MAX_ZOOM_DISTANCE) meters = MAX_ZOOM_DISTANCE;

    if (m_fZoomDistance != meters) {
        m_fZoomDistance = meters;
        setDataDirty();
    }
}

void MapHud::setMarkerScale(float scale) {
    // Clamp to valid range
    if (scale < MIN_MARKER_SCALE) scale = MIN_MARKER_SCALE;
    if (scale > MAX_MARKER_SCALE) scale = MAX_MARKER_SCALE;

    if (m_fMarkerScale != scale) {
        m_fMarkerScale = scale;
        setDataDirty();
    }
}

void MapHud::setRiderShape(int shapeIndex) {
    // Clamp to valid range (0=OFF, 1-N=shapes)
    int maxShape = static_cast<int>(AssetManager::getInstance().getIconCount());
    if (shapeIndex < 0) shapeIndex = 0;
    if (shapeIndex > maxShape) shapeIndex = maxShape;

    if (m_riderShapeIndex != shapeIndex) {
        m_riderShapeIndex = shapeIndex;
        setDataDirty();
    }
}

MapHud::AnchorPoint MapHud::calculateAnchorFromPosition() const {
    // Calculate center of HUD in screen space
    // Use (left + right) / 2 to handle cases where bounds are offset (e.g., negative left in rotation mode)
    float centerX = m_fOffsetX + (m_fBoundsLeft + m_fBoundsRight) / 2.0f;
    float centerY = m_fOffsetY + (m_fBoundsTop + m_fBoundsBottom) / 2.0f;

    // Determine which quadrant the HUD is in
    bool isRight = centerX > 0.5f;
    bool isBottom = centerY > 0.5f;

    if (!isBottom && isRight) {
        return AnchorPoint::TOP_RIGHT;
    } else if (!isBottom && !isRight) {
        return AnchorPoint::TOP_LEFT;
    } else if (isBottom && !isRight) {
        return AnchorPoint::BOTTOM_LEFT;
    } else {
        return AnchorPoint::BOTTOM_RIGHT;
    }
}

void MapHud::updateAnchorFromCurrentPosition() {
    // Update anchor point based on current position
    m_anchorPoint = calculateAnchorFromPosition();

    // Calculate and store the anchor position in screen space
    float width = m_fBoundsRight - m_fBoundsLeft;
    float height = m_fBoundsBottom - m_fBoundsTop;

    switch (m_anchorPoint) {
        case AnchorPoint::TOP_LEFT:
            m_fAnchorX = m_fOffsetX + m_fBoundsLeft;
            m_fAnchorY = m_fOffsetY + m_fBoundsTop;
            break;
        case AnchorPoint::TOP_RIGHT:
            m_fAnchorX = m_fOffsetX + m_fBoundsLeft + width;
            m_fAnchorY = m_fOffsetY + m_fBoundsTop;
            break;
        case AnchorPoint::BOTTOM_LEFT:
            m_fAnchorX = m_fOffsetX + m_fBoundsLeft;
            m_fAnchorY = m_fOffsetY + m_fBoundsTop + height;
            break;
        case AnchorPoint::BOTTOM_RIGHT:
            m_fAnchorX = m_fOffsetX + m_fBoundsLeft + width;
            m_fAnchorY = m_fOffsetY + m_fBoundsTop + height;
            break;
        default:
            DEBUG_WARN_F("Invalid anchor point: %d, defaulting to TOP_LEFT", static_cast<int>(m_anchorPoint));
            m_fAnchorX = m_fOffsetX + m_fBoundsLeft;
            m_fAnchorY = m_fOffsetY + m_fBoundsTop;
            break;
    }

    DEBUG_INFO_F("MapHud anchor updated: point=%d, position=(%.3f, %.3f)",
                 static_cast<int>(m_anchorPoint), m_fAnchorX, m_fAnchorY);
}

void MapHud::updatePositionFromAnchor() {
    // Calculate new offset to maintain anchor position with current dimensions
    float width = m_fBoundsRight - m_fBoundsLeft;
    float height = m_fBoundsBottom - m_fBoundsTop;

    float newOffsetX = m_fOffsetX;
    float newOffsetY = m_fOffsetY;

    switch (m_anchorPoint) {
        case AnchorPoint::TOP_LEFT:
            // Anchor at top-left - grows right and down
            newOffsetX = m_fAnchorX - m_fBoundsLeft;
            newOffsetY = m_fAnchorY - m_fBoundsTop;
            break;
        case AnchorPoint::TOP_RIGHT:
            // Anchor at top-right - grows left and down
            newOffsetX = m_fAnchorX - m_fBoundsLeft - width;
            newOffsetY = m_fAnchorY - m_fBoundsTop;
            break;
        case AnchorPoint::BOTTOM_LEFT:
            // Anchor at bottom-left - grows right and up
            newOffsetX = m_fAnchorX - m_fBoundsLeft;
            newOffsetY = m_fAnchorY - m_fBoundsTop - height;
            break;
        case AnchorPoint::BOTTOM_RIGHT:
            // Anchor at bottom-right - grows left and up
            newOffsetX = m_fAnchorX - m_fBoundsLeft - width;
            newOffsetY = m_fAnchorY - m_fBoundsTop - height;
            break;
        default:
            DEBUG_WARN_F("Invalid anchor point: %d, defaulting to TOP_LEFT", static_cast<int>(m_anchorPoint));
            newOffsetX = m_fAnchorX - m_fBoundsLeft;
            newOffsetY = m_fAnchorY - m_fBoundsTop;
            break;
    }

    // Update position if changed
    if (newOffsetX != m_fOffsetX || newOffsetY != m_fOffsetY) {
        setPosition(newOffsetX, newOffsetY);
        DEBUG_INFO_F("MapHud position updated from anchor: (%.3f, %.3f)", newOffsetX, newOffsetY);
    }
}

void MapHud::updateTrackData(int numSegments, const Unified::TrackSegment* segments, const float* raceData) {
    if (numSegments <= 0 || segments == nullptr) {
        DEBUG_WARN("MapHud: Invalid track data");
        return;
    }

    DEBUG_INFO_F("MapHud: Received %d track segments", numSegments);

    // Copy track segments
    m_trackSegments.clear();
    m_trackSegments.reserve(numSegments);
    for (int i = 0; i < numSegments; ++i) {
        m_trackSegments.push_back(segments[i]);
    }

    m_bHasTrackData = true;
    m_ribbonCacheValid = false;   // Cached ribbon quads belong to the old track
    m_worldRibbonValid = false;   // Cached world centerline belongs to the old track

    // Calculate track bounds and scale
    calculateTrackBounds();

    // Resolve race markers (start/finish, splits, holeshot) to world coordinates.
    // Each entry is meters along the centerline; <=0 means "not present".
    for (auto& marker : m_raceMarkers) {
        marker.valid = false;
    }
    m_sfMeters = (raceData && raceData[0] > 0.0f) ? raceData[0] : -1.0f;
    if (raceData) {
        // The TrackCenterline API documents raceData as a float array but does
        // NOT specify its length. We trust the active game to deliver at least
        // RACE_MARKER_COUNT entries (verified empirically for current game
        // builds); older builds emitting a shorter array would be UB on the
        // unchecked indices below. Forwarding nullptr from the per-game export
        // is how to opt out (e.g. gpb_api.cpp does this until PiBoSo fixes
        // GP Bikes' raceData).
        for (int i = 0; i < RACE_MARKER_COUNT; ++i) {
            float meters = raceData[i];
            if (meters > 0.0f && centerlinePositionAt(meters,
                                                     m_raceMarkers[i].worldX,
                                                     m_raceMarkers[i].worldY,
                                                     m_raceMarkers[i].angleDeg)) {
                m_raceMarkers[i].valid = true;
            }
        }
        // Log only the markers we actually validated, so we don't print
        // uninitialized memory if the upstream array is shorter than expected.
        DEBUG_INFO_F("MapHud: Race markers - S/F=%s split1=%s split2=%s holeshot=%s",
                     m_raceMarkers[MARKER_SF].valid       ? "set" : "n/a",
                     m_raceMarkers[MARKER_SPLIT_1].valid  ? "set" : "n/a",
                     m_raceMarkers[MARKER_SPLIT_2].valid  ? "set" : "n/a",
                     m_raceMarkers[MARKER_HOLESHOT].valid ? "set" : "n/a");
    }

    // Trigger rebuild
    setDataDirty();
}

bool MapHud::centerlinePositionAt(float meters, float& outX, float& outY, float& outAngleDeg) const {
    if (m_trackSegments.empty() || meters < 0.0f) {
        return false;
    }

    float accumulated = 0.0f;
    float currentX = m_trackSegments[0].startX;
    float currentY = m_trackSegments[0].startY;
    float currentAngle = m_trackSegments[0].angle;  // degrees, 0 = north

    for (const auto& segment : m_trackSegments) {
        float segLen = segment.length;
        if (accumulated + segLen >= meters) {
            // Target lies within this segment
            float offset = meters - accumulated;

            // Exact position/heading at the target offset (straight or arc).
            outX = currentX;
            outY = currentY;
            outAngleDeg = currentAngle;
            float radius = (segment.type == TrackSegmentType::STRAIGHT) ? 0.0f : segment.radius;
            advanceAlongArc(outX, outY, outAngleDeg, radius, offset);
            return true;
        }

        // Advance to end of this segment (exact, straight or arc)
        float radius = (segment.type == TrackSegmentType::STRAIGHT) ? 0.0f : segment.radius;
        advanceAlongArc(currentX, currentY, currentAngle, radius, segLen);
        accumulated += segLen;
    }

    // meters past end of track
    return false;
}

void MapHud::updateRiderPositions(int numVehicles, const Unified::TrackPositionData* positions) {
    if (numVehicles <= 0 || positions == nullptr) {
        m_riderPositions.clear();
        return;
    }

    // Copy rider positions (fast operation - runs at high frequency)
    // Use assign() for better performance - single allocation instead of multiple push_back calls
    m_riderPositions.assign(positions, positions + numVehicles);

    // Mark data as dirty to trigger render update
    setDataDirty();
}

void MapHud::calculateTrackBounds() {
    if (m_trackSegments.empty()) {
        return;
    }

    using namespace PluginConstants::Math;

    // Initialize bounds with first segment start position
    m_minX = m_maxX = m_trackSegments[0].startX;
    m_minY = m_maxY = m_trackSegments[0].startY;

    // Calculate bounds by traversing all segments
    float currentX = m_trackSegments[0].startX;
    float currentY = m_trackSegments[0].startY;
    float currentAngle = m_trackSegments[0].angle;

    for (const auto& segment : m_trackSegments) {
        // Update bounds with current position
        m_minX = std::min(m_minX, currentX);
        m_maxX = std::max(m_maxX, currentX);
        m_minY = std::min(m_minY, currentY);
        m_maxY = std::max(m_maxY, currentY);

        // Calculate end position based on segment type
        if (segment.type == TrackSegmentType::STRAIGHT) {
            // Straight segment
            float angleRad = currentAngle * DEG_TO_RAD;
            float dx = std::sin(angleRad) * segment.length;
            float dy = std::cos(angleRad) * segment.length;
            currentX += dx;
            currentY += dy;
        } else {
            // Curved segment - simple stepping approach
            float segRadius = segment.radius;
            float arcLength = segment.length;
            float absRadius = std::abs(segRadius);

            // Safety: Skip curved segments with invalid radius to avoid division by zero
            if (absRadius < 0.01f) {
                DEBUG_WARN_F("MapHud: Curved segment with invalid radius %.3f, skipping", segRadius);
                continue;
            }

            // Sample points along the curve for accurate bounds, using the exact arc
            // geometry (same as the ribbon/markers). Fixed 2m sampling step - bounds
            // don't need to track LOD (computed once at track-load and must not
            // depend on a setting that can change later).
            constexpr float BOUNDS_SAMPLE_STEP_METERS = 2.0f;
            int numSamples = std::max(3, static_cast<int>(arcLength / BOUNDS_SAMPLE_STEP_METERS));
            float stepLength = arcLength / numSamples;

            for (int i = 1; i <= numSamples; ++i) {
                float tempX = currentX, tempY = currentY, tempAngle = currentAngle;
                advanceAlongArc(tempX, tempY, tempAngle, segRadius, stepLength * i);

                m_minX = std::min(m_minX, tempX);
                m_maxX = std::max(m_maxX, tempX);
                m_minY = std::min(m_minY, tempY);
                m_maxY = std::max(m_maxY, tempY);
            }

            // Advance current position and angle to the end of the curve (exact).
            advanceAlongArc(currentX, currentY, currentAngle, segRadius, arcLength);
        }

        // Update bounds with end position
        m_minX = std::min(m_minX, currentX);
        m_maxX = std::max(m_maxX, currentX);
        m_minY = std::min(m_minY, currentY);
        m_maxY = std::max(m_maxY, currentY);
    }

    // Add padding (5% of track size)
    float paddingX = (m_maxX - m_minX) * 0.05f;
    float paddingY = (m_maxY - m_minY) * 0.05f;
    m_minX -= paddingX;
    m_maxX += paddingX;
    m_minY -= paddingY;
    m_maxY += paddingY;

    // Calculate scale to fit track in map area and adjust map dimensions to track proportions
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;

    // Safety: Validate track dimensions to avoid division by zero
    // Use minimum threshold of 0.1 meters (tracks should be much larger)
    if (trackWidth < 0.1f || trackHeight < 0.1f) {
        DEBUG_WARN_F("MapHud: Invalid track dimensions (%.2f x %.2f), using defaults", trackWidth, trackHeight);
        m_fBaseMapWidth = MAP_HEIGHT / UI_ASPECT_RATIO;
        m_fBaseMapHeight = MAP_HEIGHT;
        m_fTrackScale = 1.0f;
        return;
    }

    float trackAspectRatio = trackWidth / trackHeight;

    // Start with maximum height and calculate width based on track proportions
    // Account for UI_ASPECT_RATIO to maintain square pixels
    float maxMapHeight = MAP_HEIGHT;
    float maxMapWidth = MAP_HEIGHT / UI_ASPECT_RATIO;

    // Determine if track is limited by width or height
    if (trackAspectRatio > 1.0f) {
        // Track is wider than tall - may need to use full width and reduce height
        m_fBaseMapWidth = maxMapWidth;
        // Calculate height needed to maintain track proportions with square pixels
        // Account for UI_ASPECT_RATIO: screen coords are stretched, so we need to adjust
        float screenHeightForTrack = (m_fBaseMapWidth * UI_ASPECT_RATIO) / trackAspectRatio;
        m_fBaseMapHeight = std::min(screenHeightForTrack, maxMapHeight);

        // Calculate scale based on the limiting dimension
        float scaleX = m_fBaseMapWidth / trackWidth;
        float scaleY = m_fBaseMapHeight / trackHeight;
        m_fTrackScale = std::min(scaleX, scaleY);
    } else {
        // Track is taller than wide (or square) - use full height and adjust width
        m_fBaseMapHeight = maxMapHeight;
        // Calculate width needed to maintain track proportions with square pixels
        // Account for UI_ASPECT_RATIO: screen coords are stretched, so we need to adjust
        m_fBaseMapWidth = (m_fBaseMapHeight * trackAspectRatio) / UI_ASPECT_RATIO;
        m_fBaseMapWidth = std::min(m_fBaseMapWidth, maxMapWidth);

        // Calculate scale based on the limiting dimension
        float scaleX = m_fBaseMapWidth / trackWidth;
        float scaleY = m_fBaseMapHeight / trackHeight;
        m_fTrackScale = std::min(scaleX, scaleY);
    }

    DEBUG_INFO_F("MapHud: Track bounds: X[%.1f, %.1f], Y[%.1f, %.1f], aspect: %.2f, baseMapWidth: %.3f, scale: %.6f",
                 m_minX, m_maxX, m_minY, m_maxY, trackAspectRatio, m_fBaseMapWidth, m_fTrackScale);
}

void MapHud::rebuildRenderData() {
    m_quads.clear();
    clearStrings();
    m_riderClickRegions.clear();

    // Don't render until we have track data
    // TrackCenterline callback fires during track load, before first render
    if (!m_bHasTrackData) {
        return;
    }

#if defined(MXBMRP3_TEST_BUILD)
    auto profBoundsStart = ProfClock::now();
#endif

    // Calculate scaled dimensions
    auto dim = getScaledDimensions();

    // Calculate actual rotation angle for rendering and create cache for efficient rendering
    float rotationAngle = calculateRotationAngle();
    RotationCache rotation = createRotationCache(rotationAngle);

    // Calculate container size FIRST using original track bounds (before any zoom override)
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float width, height, x, y;

    // Calculate maximum bounds across all rotation angles to ensure container fits track at any angle
    float maxScreenWidth = 0.0f;
    float maxScreenHeight = 0.0f;

    float testAngles[] = {0.0f, 45.0f, 90.0f, 135.0f};
    for (float angle : testAngles) {
        float minX, maxX, minY, maxY;
        RotationCache testRotation = createRotationCache(angle);
        calculateTrackScreenBounds(testRotation, minX, maxX, minY, maxY);
        maxScreenWidth = std::max(maxScreenWidth, maxX - minX);
        maxScreenHeight = std::max(maxScreenHeight, maxY - minY);
    }

    // SQUARE CONTAINER: Account for UI_ASPECT_RATIO to make visually square
    // Convert screen coords to visual space, find max, convert back
    float visualWidth = maxScreenWidth * UI_ASPECT_RATIO;  // Expand X to visual space
    float visualHeight = maxScreenHeight;                   // Y is already in visual space
    float visualSquareSize = std::max(visualWidth, visualHeight);

    // Convert back to screen coordinates
    float squareWidth = visualSquareSize / UI_ASPECT_RATIO;  // Screen X coord
    float squareHeight = visualSquareSize;                    // Screen Y coord

    // Calculate current track bounds at actual rotation angle for positioning
    float currMinX, currMaxX, currMinY, currMaxY;
    calculateTrackScreenBounds(rotation, currMinX, currMaxX, currMinY, currMaxY);

    float currWidth = currMaxX - currMinX;
    float currHeight = currMaxY - currMinY;

    // Container dimensions (visually square)
    width = squareWidth;
    height = squareHeight + titleHeight;

    // Center the track in the square container
    x = currMinX - (squareWidth - currWidth) / 2.0f;
    y = currMinY - (squareHeight - currHeight) / 2.0f;

    // --- ZOOM MODE: Override bounds for rendering AFTER container size is calculated ---
    float savedMinX = m_minX, savedMaxX = m_maxX;
    float savedMinY = m_minY, savedMaxY = m_maxY;
    float savedBaseMapWidth = m_fBaseMapWidth;
    float savedBaseMapHeight = m_fBaseMapHeight;
    float savedTrackScale = m_fTrackScale;
    bool usingZoom = false;

    if (m_bZoomEnabled) {
        float zoomMinX, zoomMaxX, zoomMinY, zoomMaxY;
        if (calculateZoomBounds(zoomMinX, zoomMaxX, zoomMinY, zoomMaxY)) {
            usingZoom = true;

            // Override world bounds with zoom bounds for rendering
            m_minX = zoomMinX;
            m_maxX = zoomMaxX;
            m_minY = zoomMinY;
            m_maxY = zoomMaxY;

            // Override base map dimensions to match the square container
            // This ensures worldToScreen produces correct aspect ratio for zoom
            // Divide by m_fScale because squareWidth/squareHeight already include scale
            // (they came from calculateTrackScreenBounds which uses worldToScreen)
            m_fBaseMapWidth = squareWidth / m_fScale;
            m_fBaseMapHeight = squareHeight / m_fScale;

            // Recalculate scale to fit zoom bounds into the container
            float zoomWidth = zoomMaxX - zoomMinX;
            float zoomHeight = zoomMaxY - zoomMinY;
            float scaleX = m_fBaseMapWidth / zoomWidth;
            float scaleY = m_fBaseMapHeight / zoomHeight;
            m_fTrackScale = std::min(scaleX, scaleY);

            // Keep same x/y as non-zoom mode so container doesn't jump
            // The zoom content will be centered because zoom bounds are square
            // and centered on the player
        }
    }

    // Store previous bounds to detect dimension changes
    float prevWidth = m_fBoundsRight - m_fBoundsLeft;
    float prevHeight = m_fBoundsBottom - m_fBoundsTop;

    // Check if dimensions will change
    // Use epsilon tolerance to avoid spurious updates from floating-point precision issues
    constexpr float DIMENSION_CHANGE_EPSILON = 0.001f;  // ~1 pixel tolerance
    bool widthChanged = std::abs(prevWidth - width) > DIMENSION_CHANGE_EPSILON;
    bool heightChanged = std::abs(prevHeight - height) > DIMENSION_CHANGE_EPSILON;
    bool isFirstRebuild = (prevWidth == 0.0f && prevHeight == 0.0f);

    // If dimensions will change, update anchor from current position BEFORE changing bounds
    // This ensures anchor is calculated based on current position with OLD bounds
    // Skip on first rebuild - use saved anchor values instead
    if ((widthChanged || heightChanged) && !isFirstRebuild) {
        updateAnchorFromCurrentPosition();
    }

    // Set bounds for dragging
    setBounds(x, y, x + width, y + height);

    // If dimensions changed, update position to maintain anchor with new bounds
    // On first rebuild (prevWidth/prevHeight = 0), this applies the position from saved anchor
    if (widthChanged || heightChanged) {
        updatePositionFromAnchor();
    }

    // Add background
    addBackgroundQuad(x, y, width, height);

    // Add title
    float titleX = x + dim.paddingH;
    float titleY = y + dim.paddingV;
    addTitleString("Map", titleX, titleY, Justify::LEFT,
                  this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);

    // Calculate clip bounds for track rendering (absolute screen coords)
    // Clip to the map area below the title
    // Inset by half outline width since we clip on centerline but edges extend beyond
    // OUTLINE_WIDTH_MULTIPLIER lives in map_hud_internal.h (also reused by marker triangles).
    // Calculate effective track width for clipping (same ratio as renderTrack)
    float clipTrackWidth = m_maxX - m_minX;
    float clipTrackHeight = m_maxY - m_minY;
    float clipBaseWidthMeters = std::min(clipTrackWidth, clipTrackHeight) * TRACK_WIDTH_BASE_RATIO;
    float clipEffectiveWidthMeters = std::clamp(clipBaseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);
    float outlineHalfWidth = clipEffectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER * m_fTrackScale;
    float clipLeft = x + m_fOffsetX + outlineHalfWidth;
    float clipTop = y + titleHeight + m_fOffsetY + outlineHalfWidth;
    float clipRight = x + width + m_fOffsetX - outlineHalfWidth;
    float clipBottom = y + height + m_fOffsetY - outlineHalfWidth;

    // For zoom mode, temporarily adjust offset so content aligns with container
    // worldToScreen outputs coords at (0,0) for zoom, but container is at (x,y)
    float savedOffsetX = m_fOffsetX;
    float savedOffsetY = m_fOffsetY;
    if (usingZoom) {
        m_fOffsetX += x;
        m_fOffsetY += y;
    }

    // Render track with optional outline effect (two passes for visual clarity)
    // Both use same clip bounds - outline clips first (wider), track extends to edge
    // This gives natural "outline on sides only" effect at boundaries
    size_t quadsBeforeTrack = m_quads.size();
    unsigned long outlineColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long fillColor = this->getColor(ColorSlot::BACKGROUND);

    // Build the ribbon-cache key from the values renderTrack actually sees
    // (captured here, AFTER the zoom overrides and offset adjustment above).
    // See TrackRibbonKey in the header for the caching rationale.
    TrackRibbonKey ribbonKey;
    ribbonKey.angle = rotation.angle;
    ribbonKey.minX = m_minX;
    ribbonKey.maxX = m_maxX;
    ribbonKey.minY = m_minY;
    ribbonKey.maxY = m_maxY;
    ribbonKey.trackScale = m_fTrackScale;
    ribbonKey.baseMapWidth = m_fBaseMapWidth;
    ribbonKey.baseMapHeight = m_fBaseMapHeight;
    ribbonKey.scale = m_fScale;
    ribbonKey.offsetX = m_fOffsetX;
    ribbonKey.offsetY = m_fOffsetY;
    ribbonKey.clipLeft = clipLeft;
    ribbonKey.clipTop = clipTop;
    ribbonKey.clipRight = clipRight;
    ribbonKey.clipBottom = clipBottom;
    ribbonKey.trackWidthScale = m_fTrackWidthScale;
    ribbonKey.zoomDistance = m_fZoomDistance;
    ribbonKey.detail = static_cast<int>(m_detail);
    ribbonKey.zoomEnabled = m_bZoomEnabled;
    ribbonKey.showOutline = m_bShowOutline;
    ribbonKey.showTitle = m_bShowTitle;
    ribbonKey.outlineColor = outlineColor;
    ribbonKey.fillColor = fillColor;

#if defined(MXBMRP3_TEST_BUILD)
    g_mapBoundsUs += usSince(profBoundsStart);
    auto profRibbonStart = ProfClock::now();
#endif

    if (m_ribbonCacheValid && ribbonKey == m_ribbonKey) {
        // Same view, same style: only the rider dots changed - reuse the
        // tessellated ribbon instead of re-sampling the whole centerline
#if defined(MXBMRP3_TEST_BUILD)
        ++g_mapRibbonHits;
#endif
        m_quads.insert(m_quads.end(), m_ribbonQuads.begin(), m_ribbonQuads.end());
    } else {
#if defined(MXBMRP3_TEST_BUILD)
        ++g_mapRibbonMiss;
#endif
        if (m_bShowOutline) {
            renderTrack(rotation, outlineColor, OUTLINE_WIDTH_MULTIPLIER,
                        clipLeft, clipTop, clipRight, clipBottom);  // White outline
        }
        renderTrack(rotation, fillColor, 1.0f,
                    clipLeft, clipTop, clipRight, clipBottom);  // Black fill
        m_ribbonQuads.assign(m_quads.begin() + quadsBeforeTrack, m_quads.end());
        m_ribbonKey = ribbonKey;
        m_ribbonCacheValid = true;
    }
    size_t trackQuads = m_quads.size() - quadsBeforeTrack;

#if defined(MXBMRP3_TEST_BUILD)
    g_mapRibbonUs += usSince(profRibbonStart);
    auto profMarkersStart = ProfClock::now();
#endif

    // Track markers (S/F, sector/split arrows, segment-timer lines) are all gated by
    // one toggle so the map can show just the track ribbon + riders when off.
    if (m_bShowTrackMarkers) {
        // Render split/holeshot direction-arrow triangles on top of track (below the
        // S/F triangle so the white arrow still draws on top if it visually overlaps).
        renderRaceMarkers(rotation, clipLeft, clipTop, clipRight, clipBottom);

        // Render start marker on top of track
        renderStartMarker(rotation, clipLeft, clipTop, clipRight, clipBottom);

        // Render the custom segment-timer lines above the track and race markers but below
        // riders, so rider icons remain the most visible elements on the map.
        renderSegmentMarkers(rotation, clipLeft, clipTop, clipRight, clipBottom);
    }

#if defined(MXBMRP3_TEST_BUILD)
    g_mapMarkersUs += usSince(profMarkersStart);
    auto profRidersStart = ProfClock::now();
#endif

    // Render rider positions last, on top of the track and all markers; within
    // renderRiders the local player is drawn last of all so the player's own icon is
    // the most visible element.
    renderRiders(rotation, clipLeft, clipTop, clipRight, clipBottom);

#if defined(MXBMRP3_TEST_BUILD)
    g_mapRidersUs += usSince(profRidersStart);
    ++g_mapProfCount;
#endif

    // Log quad count once for performance analysis
    static bool quadCountLogged = false;
    if (!quadCountLogged) {
        // Count segment types
        size_t straightCount = 0, curveCount = 0;
        for (const auto& seg : m_trackSegments) {
            if (seg.type == 0) straightCount++;
            else curveCount++;
        }
        DEBUG_INFO_F("MapHud: Rendering %zu segments (%zu straights, %zu curves)",
                     m_trackSegments.size(), straightCount, curveCount);
        DEBUG_INFO_F("MapHud: Track rendered with %zu quads (optimized straights: 1 quad per segment)",
                     trackQuads);
        DEBUG_INFO_F("MapHud: Total quads=%zu (track=%zu, background+markers+riders=%zu)",
                     m_quads.size(), trackQuads, m_quads.size() - trackQuads);
        quadCountLogged = true;
    }

    // --- ZOOM MODE: Restore original values ---
    if (usingZoom) {
        m_minX = savedMinX;
        m_maxX = savedMaxX;
        m_minY = savedMinY;
        m_maxY = savedMaxY;
        m_fBaseMapWidth = savedBaseMapWidth;
        m_fBaseMapHeight = savedBaseMapHeight;
        m_fTrackScale = savedTrackScale;
        m_fOffsetX = savedOffsetX;
        m_fOffsetY = savedOffsetY;
    }
}

void MapHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.0f;  // Transparent by default
    m_fScale = 1.0f;
    m_anchorPoint = AnchorPoint::TOP_RIGHT;
    m_fAnchorX = 0.994125f;
    m_fAnchorY = 0.0113039f;
    m_bRotateToPlayer = false;
    m_bShowOutline = true;  // Enable outline by default
    m_bShowTrackMarkers = true;  // Show S/F, sector markers and segment lines by default
    m_riderColorMode = RiderColorMode::RELATIVE_POS;  // Default to relative position coloring
    m_labelMode = LabelMode::RACE_NUM;
    m_labelAnchor = LabelAnchor::BELOW;
    m_riderShapeIndex = getShapeIndexByFilename(DEFAULT_RIDER_ICON);
    m_fTrackWidthScale = DEFAULT_TRACK_WIDTH_SCALE;
    m_bZoomEnabled = false;
    m_fZoomDistance = DEFAULT_ZOOM_DISTANCE;
    m_fMarkerScale = DEFAULT_MARKER_SCALE;
    m_detail = Detail::AUTO;
    // Reset bounds to trigger "first rebuild" behavior in rebuildRenderData
    // This ensures position is recalculated from anchor values
    setBounds(0.0f, 0.0f, 0.0f, 0.0f);
    setDataDirty();
}

void MapHud::handleClick(float mouseX, float mouseY) {
    // Check if click is within any rider marker region
    for (const auto& region : m_riderClickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            DEBUG_INFO_F("MapHud: Switching to rider #%d", region.raceNum);
            PluginManager::getInstance().requestSpectateRider(region.raceNum);
            return;  // Only process one click
        }
    }
}
