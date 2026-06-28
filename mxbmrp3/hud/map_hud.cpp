// ============================================================================
// hud/map_hud.cpp
// Map HUD implementation - displays track layout and rider positions
// ============================================================================
#include "map_hud.h"
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

// Track width is calculated as a percentage of the smaller track dimension
// This ensures consistent visual appearance across different track sizes
static constexpr float TRACK_WIDTH_BASE_RATIO = 0.036f;  // 3.6% of smaller dimension

// Track outline width as a multiplier of the fill width (1.4 = 40% wider).
// Also used to size race-data marker triangles (S/F, splits, holeshot) so
// their base spans the outline edges, not the fill edges - much more visible
// against the white outline.
static constexpr float OUTLINE_WIDTH_MULTIPLIER = 1.4f;

// Advance (x, y, headingDeg) by `dist` meters along a circular arc of signed
// radius (the heading convention is move = sin/cos of heading, turn rate =
// 1/radius). This is the *exact* arc position - independent of how finely the
// curve is subdivided - so the rendered ribbon (renderTrack) and the marker
// positions that index into it (centerlinePositionAt) agree exactly. Reduces to a
// straight line as |radius| grows. Previously both used forward-Euler stepping
// with different step counts, so markers drifted off the ribbon through curves.
static void advanceAlongArc(float& x, float& y, float& headingDeg, float radius, float dist) {
    float h0 = headingDeg * DEG_TO_RAD;
    if (std::abs(radius) < 0.01f) {  // effectively straight
        x += std::sin(h0) * dist;
        y += std::cos(h0) * dist;
        return;
    }
    float theta = dist / radius;  // signed turn (radians)
    x += radius * (std::cos(h0) - std::cos(h0 + theta));
    y += radius * (std::sin(h0 + theta) - std::sin(h0));
    headingDeg += theta * RAD_TO_DEG;
}

// Default icon filename
static constexpr const char* DEFAULT_RIDER_ICON = "circle-chevron-up";

void MapHud::CachedIcons::ensureInitialized() {
    if (initialized) return;
    const AssetManager& assets = AssetManager::getInstance();
    circleExclamation = assets.getIconSpriteIndex("circle-exclamation");
    flag = assets.getIconSpriteIndex("flag");
    flagCheckered = assets.getIconSpriteIndex("flag-checkered");
    initialized = true;
}

// Helper to get shape index from filename (returns 1 if not found)
static int getShapeIndexByFilename(const char* filename) {
    const auto& assetMgr = AssetManager::getInstance();
    int spriteIndex = assetMgr.getIconSpriteIndex(filename);
    if (spriteIndex <= 0) return 1;  // Fallback to first icon
    return spriteIndex - assetMgr.getFirstIconSpriteIndex() + 1;
}

MapHud::MapHud()
    : m_fTrackWidthScale(DEFAULT_TRACK_WIDTH_SCALE),  // Reordered to match header declaration order
      m_minX(0.0f), m_maxX(0.0f), m_minY(0.0f), m_maxY(0.0f),
      m_fTrackScale(1.0f), m_fBaseMapWidth(0.0f), m_fBaseMapHeight(0.0f), m_bHasTrackData(false),
      m_bRotateToPlayer(false), m_fLastRotationAngle(0.0f),
      m_fLastPlayerX(0.0f), m_fLastPlayerZ(0.0f),
      m_bShowOutline(true),
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
    if (!isVisible()) {
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
            const CursorPosition& cursor = input.getCursorPosition();
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
    m_ribbonCacheValid = false;  // Cached ribbon quads belong to the old track

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

bool MapHud::calculateZoomBounds(float& zoomMinX, float& zoomMaxX, float& zoomMinY, float& zoomMaxY) const {
    // Find the local player position
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    float playerX = 0.0f;
    float playerZ = 0.0f;
    bool foundPlayer = false;

    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) {
            if (!pos.crashed) {
                playerX = pos.posX;
                playerZ = pos.posZ;
            } else {
                // Use cached position when crashed
                playerX = m_fLastPlayerX;
                playerZ = m_fLastPlayerZ;
            }
            foundPlayer = true;
            break;
        }
    }

    if (!foundPlayer) {
        // No player found - fall back to full track bounds
        return false;
    }

    // Center zoom bounds on the PLAYER position
    // This ensures the player arrow is always visible and centered
    // Use a square viewport based on zoom distance (100m = 50m each direction)
    float halfBounds = m_fZoomDistance * 0.5f;

    zoomMinX = playerX - halfBounds;
    zoomMaxX = playerX + halfBounds;
    zoomMinY = playerZ - halfBounds;
    zoomMaxY = playerZ + halfBounds;

    return true;
}

void MapHud::calculateTrackScreenBounds(const RotationCache& rotation, float& minX, float& maxX, float& minY, float& maxY) const {
    // Track corners in world space
    float corners[4][2] = {
        {m_minX, m_minY},
        {m_maxX, m_minY},
        {m_maxX, m_maxY},
        {m_minX, m_maxY}
    };

    minX = 1e9f; maxX = -1e9f;
    minY = 1e9f; maxY = -1e9f;

    for (int i = 0; i < 4; ++i) {
        float screenX, screenY;
        worldToScreen(corners[i][0], corners[i][1], screenX, screenY, rotation);
        minX = std::min(minX, screenX);
        maxX = std::max(maxX, screenX);
        minY = std::min(minY, screenY);
        maxY = std::max(maxY, screenY);
    }
}

float MapHud::calculateRotationAngle() {
    // Always find and cache player position (needed for both rotation and zoom modes)
    // Calculate rotation angle only if rotation mode is enabled
    float rotationAngle = 0.0f;

    if (!m_riderPositions.empty()) {
        // Find local player by race number
        const PluginData& pluginData = PluginData::getInstance();
        int displayRaceNum = pluginData.getDisplayRaceNum();

        for (const auto& pos : m_riderPositions) {
            if (pos.raceNum == displayRaceNum) {
                if (!pos.crashed) {
                    // Player is riding - always cache position (needed for zoom mode crash freeze)
                    m_fLastPlayerX = pos.posX;
                    m_fLastPlayerZ = pos.posZ;

                    // Only update rotation angle if rotation mode is enabled
                    if (m_bRotateToPlayer) {
                        rotationAngle = pos.yaw;
                        m_fLastRotationAngle = rotationAngle;
                    }
                } else {
                    // Player crashed - use cached rotation angle if rotation mode is enabled
                    // Position cache is already set from before the crash
                    if (m_bRotateToPlayer) {
                        rotationAngle = m_fLastRotationAngle;
                    }
                }
                break;
            }
        }
    }
    return rotationAngle;
}

MapHud::RotationCache MapHud::createRotationCache(float rotationAngle) const {
    RotationCache cache;
    cache.angle = rotationAngle;
    if (rotationAngle != 0.0f) {
        float angleRad = rotationAngle * DEG_TO_RAD;
        cache.cosAngle = std::cos(angleRad);
        cache.sinAngle = std::sin(angleRad);
        cache.hasRotation = true;
    }
    return cache;
}

void MapHud::worldToScreen(float worldX, float worldY, float& screenX, float& screenY, const RotationCache& rotation) const {
    // Normalize to world space using the same scale for both axes (preserves aspect ratio)
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float maxDimension = std::max(trackWidth, trackHeight);

    // Normalize to square space (0-1) using the larger dimension
    // This preserves aspect ratio and prevents rotation skewing
    float normX = (worldX - m_minX) / maxDimension;
    float normY = (worldY - m_minY) / maxDimension;

    // Center point of the normalized track
    float centerX = (trackWidth / maxDimension) * 0.5f;
    float centerY = (trackHeight / maxDimension) * 0.5f;

    // Apply rotation around the center of the track (using pre-calculated cos/sin)
    if (rotation.hasRotation) {
        // Center coordinates around track center
        float centeredX = normX - centerX;
        float centeredY = normY - centerY;

        // Rotate in square space (equal scale for X and Y)
        float rotatedX = centeredX * rotation.cosAngle - centeredY * rotation.sinAngle;
        float rotatedY = centeredX * rotation.sinAngle + centeredY * rotation.cosAngle;

        // Uncenter back
        normX = rotatedX + centerX;
        normY = rotatedY + centerY;
    }

    // Map to screen coordinates
    float scaledMapWidth = m_fBaseMapWidth * m_fScale;
    float scaledMapHeight = m_fBaseMapHeight * m_fScale;

    // Scale normalized coords to screen, maintaining aspect ratio
    float scaleX = scaledMapWidth / (trackWidth / maxDimension);
    float scaleY = scaledMapHeight / (trackHeight / maxDimension);

    screenX = normX * scaleX;
    screenY = (1.0f - normY) * scaleY;  // Flip Y axis since screen Y increases downward
}

void MapHud::renderTrack(const RotationCache& rotation, unsigned long trackColor, float widthMultiplier,
                         float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (m_trackSegments.empty()) {
        return;
    }

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Calculate base track width from track dimensions
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float baseWidthMeters = std::min(trackWidth, trackHeight) * TRACK_WIDTH_BASE_RATIO;
    // Apply user scale and clamp to reasonable bounds (1m - 30m)
    float effectiveWidthMeters = std::clamp(baseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);

    // Track half-width in world coordinates (ribbon edge offset from centerline)
    float halfWidth = effectiveWidthMeters * 0.5f * widthMultiplier;

    // --- Spatial culling setup ---
    // Expand bounds by track width + some margin to ensure we don't clip visible track
    float cullMargin = effectiveWidthMeters * 2.0f;
    float cullMinX = m_minX - cullMargin;
    float cullMaxX = m_maxX + cullMargin;
    float cullMinY = m_minY - cullMargin;
    float cullMaxY = m_maxY + cullMargin;

    // Resolve LOD to ribbon subdivision spacing (meters per quad).
    // AUTO: adaptive — targets ~3-4 px between quads at typical 1080p viewport.
    //       m_fTrackScale converts world meters to normalized screen units
    //       and is already zoom-aware (overridden in zoom mode), so this
    //       gives correct density across zoom levels without extra fudge.
    // Fixed presets: predictable density in meters. Zoom mode reduces the
    //       configured spacing proportionally so close zoom stays smooth.
    const bool autoLod = (m_detail == Detail::AUTO);
    float lodSpacing;
    if (autoLod) {
        // Set conservatively so detail at close zoom and high-DPI displays
        // still looks clean; raise carefully if quad count becomes a problem
        // on a new track.
        constexpr float AUTO_TARGET_NORM_STEP = 0.002f;
        lodSpacing = AUTO_TARGET_NORM_STEP / std::max(m_fTrackScale, 1e-6f);
        // Clamp so degenerate scales don't produce extreme values.
        lodSpacing = std::clamp(lodSpacing, 0.5f, 32.0f);
    } else {
        // AUTO is handled above; only fixed presets reach this branch.
        lodSpacing = (m_detail == Detail::HIGH) ? 1.0f : 4.0f;
        if (m_bZoomEnabled) {
            lodSpacing = std::max(0.5f, lodSpacing * (m_fZoomDistance / MAX_ZOOM_DISTANCE));
        }
    }
    // Subdivision floor: AUTO allows tiny segments to collapse to 1 quad
    // (they're sub-pixel anyway). Fixed presets keep min=3 for consistency.
    const int curveMinSteps = autoLod ? 1 : 3;

    // Lambda to check if a point is within culling bounds
    auto isPointInBounds = [&](float x, float y) -> bool {
        return x >= cullMinX && x <= cullMaxX && y >= cullMinY && y <= cullMaxY;
    };

    // Lambda to check if a point is inside the clip region
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };

    // Lambda to check if quad centerline is inside clip region
    // Using centerline (not vertices) ensures outline and track clip at same position
    auto isQuadCenterlineInside = [&](float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) -> bool {
        // Centerline points: midpoint of each edge pair (left-right)
        // Quad vertices: 0=prevLeft, 1=currLeft, 2=currRight, 3=prevRight
        float prevCenterX = (x0 + x3) * 0.5f;
        float prevCenterY = (y0 + y3) * 0.5f;
        float currCenterX = (x1 + x2) * 0.5f;
        float currCenterY = (y1 + y2) * 0.5f;
        return isPointInClip(prevCenterX, prevCenterY) && isPointInClip(currCenterX, currCenterY);
    };

    // Start position and angle
    float currentX = m_trackSegments[0].startX;
    float currentY = m_trackSegments[0].startY;
    float currentAngle = m_trackSegments[0].angle;

    // Previous edge points for ribbon quad creation
    float prevLeftX = 0.0f, prevLeftY = 0.0f, prevRightX = 0.0f, prevRightY = 0.0f;
    bool hasPrevPoint = false;

    // Lambda to create a ribbon quad between previous and current edge points
    auto createRibbonQuad = [&](float leftX, float leftY, float rightX, float rightY) {
        if (!hasPrevPoint) {
            // First point - just store edges for next iteration
            prevLeftX = leftX;
            prevLeftY = leftY;
            prevRightX = rightX;
            prevRightY = rightY;
            hasPrevPoint = true;
            return;
        }

        // Convert all four corners to screen coordinates
        float screenPrevLeftX, screenPrevLeftY;
        worldToScreen(prevLeftX, prevLeftY, screenPrevLeftX, screenPrevLeftY, rotation);
        screenPrevLeftY += titleOffset;

        float screenPrevRightX, screenPrevRightY;
        worldToScreen(prevRightX, prevRightY, screenPrevRightX, screenPrevRightY, rotation);
        screenPrevRightY += titleOffset;

        float screenLeftX, screenLeftY;
        worldToScreen(leftX, leftY, screenLeftX, screenLeftY, rotation);
        screenLeftY += titleOffset;

        float screenRightX, screenRightY;
        worldToScreen(rightX, rightY, screenRightX, screenRightY, rotation);
        screenRightY += titleOffset;

        // Apply HUD offset
        applyOffset(screenPrevLeftX, screenPrevLeftY);
        applyOffset(screenPrevRightX, screenPrevRightY);
        applyOffset(screenLeftX, screenLeftY);
        applyOffset(screenRightX, screenRightY);

        // Skip quad if centerline not inside clip region (ensures outline and track clip together)
        if (!isQuadCenterlineInside(screenPrevLeftX, screenPrevLeftY, screenLeftX, screenLeftY,
                                    screenRightX, screenRightY, screenPrevRightX, screenPrevRightY)) {
            // Still store current edges for next iteration
            prevLeftX = leftX;
            prevLeftY = leftY;
            prevRightX = rightX;
            prevRightY = rightY;
            return;
        }

        // Create quad connecting previous edges to current edges (counter-clockwise ordering to match engine)
        SPluginQuad_t quad;
        quad.m_aafPos[0][0] = screenPrevLeftX;
        quad.m_aafPos[0][1] = screenPrevLeftY;
        quad.m_aafPos[1][0] = screenLeftX;
        quad.m_aafPos[1][1] = screenLeftY;
        quad.m_aafPos[2][0] = screenRightX;
        quad.m_aafPos[2][1] = screenRightY;
        quad.m_aafPos[3][0] = screenPrevRightX;
        quad.m_aafPos[3][1] = screenPrevRightY;

        quad.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        quad.m_ulColor = trackColor;
        m_quads.push_back(quad);

        // Store current edges for next iteration
        prevLeftX = leftX;
        prevLeftY = leftY;
        prevRightX = rightX;
        prevRightY = rightY;
    };

    // Render each segment as a continuous ribbon
    for (const auto& segment : m_trackSegments) {
        float startX = currentX;
        float startY = currentY;

        // Calculate segment end position for culling check
        float endX = startX;
        float endY = startY;

        if (segment.type == TrackSegmentType::STRAIGHT) {
            float angleRad = currentAngle * DEG_TO_RAD;
            endX = startX + std::sin(angleRad) * segment.length;
            endY = startY + std::cos(angleRad) * segment.length;
        } else {
            // For curves, approximate end position
            float segRadius = segment.radius;
            float arcLength = segment.length;
            float absRadius = std::abs(segRadius);
            float totalAngleChange = arcLength / absRadius;
            if (segRadius < 0) totalAngleChange = -totalAngleChange;
            // Rough approximation - use chord
            float angleRad = currentAngle * DEG_TO_RAD;
            endX = startX + std::sin(angleRad) * segment.length * 0.9f;
            endY = startY + std::cos(angleRad) * segment.length * 0.9f;
        }

        // Check if segment is within culling bounds (either endpoint or midpoint)
        float midX = (startX + endX) * 0.5f;
        float midY = (startY + endY) * 0.5f;
        bool segmentInBounds = isPointInBounds(startX, startY) ||
                               isPointInBounds(endX, endY) ||
                               isPointInBounds(midX, midY);

        // If segment is outside bounds, reset ribbon continuity and skip rendering
        if (!segmentInBounds) {
            hasPrevPoint = false;  // Reset ribbon for next visible segment
        }

        if (segment.type == TrackSegmentType::STRAIGHT) {
            // Straight segment
            float angleRad = currentAngle * DEG_TO_RAD;
            float dx = std::sin(angleRad) * segment.length;
            float dy = std::cos(angleRad) * segment.length;

            // Calculate perpendicular direction for ribbon edges (constant for straight segments)
            float perpAngle = currentAngle + 90.0f;
            float perpAngleRad = perpAngle * DEG_TO_RAD;
            float perpDx = std::sin(perpAngleRad) * halfWidth;
            float perpDy = std::cos(perpAngleRad) * halfWidth;

            // Only render if segment is in bounds
            if (segmentInBounds) {
                // When zoom mode is active, subdivide straights for better clipping
                // Otherwise use 1 quad (optimal for non-zoomed rendering)
                int numSteps = 1;
                if (m_bZoomEnabled) {
                    // Subdivide using LOD spacing (finer at closer zoom)
                    numSteps = std::max(1, static_cast<int>(segment.length / lodSpacing));
                }

                for (int i = 0; i <= numSteps; ++i) {
                    float t = static_cast<float>(i) / numSteps;
                    float worldX = startX + dx * t;
                    float worldY = startY + dy * t;

                    // Calculate left and right edge points perpendicular to track direction
                    float leftX = worldX + perpDx;
                    float leftY = worldY + perpDy;
                    float rightX = worldX - perpDx;
                    float rightY = worldY - perpDy;

                    createRibbonQuad(leftX, leftY, rightX, rightY);
                }
            }

            currentX += dx;
            currentY += dy;
        } else {
            // Curved segment - subdivide into quads for smoothness, but place each
            // point with the exact arc geometry (advanceAlongArc) so the ribbon and
            // the marker positions (centerlinePositionAt) agree regardless of how
            // finely either subdivides.
            float segRadius = segment.radius;  // Keep sign (positive = right turn, negative = left turn)
            float arcLength = segment.length;

            // Subdivision count is purely visual; positions are exact either way.
            // AUTO collapses sub-pixel curves to 1 quad; fixed presets keep min=3.
            int numSteps = std::max(curveMinSteps, static_cast<int>(arcLength / lodSpacing));
            float stepLength = arcLength / numSteps;

            for (int i = 0; i <= numSteps; ++i) {
                // Exact position/heading this far along the arc from the segment start.
                float tempX = startX, tempY = startY, tempAngle = currentAngle;
                advanceAlongArc(tempX, tempY, tempAngle, segRadius, stepLength * i);

                // Only render points that are in bounds
                bool pointInBounds = isPointInBounds(tempX, tempY);

                if (segmentInBounds || pointInBounds) {
                    // Calculate perpendicular direction for ribbon edges at current point
                    float perpAngleRad = (tempAngle + 90.0f) * DEG_TO_RAD;

                    // Calculate left and right edge points perpendicular to current heading
                    float leftX = tempX + std::sin(perpAngleRad) * halfWidth;
                    float leftY = tempY + std::cos(perpAngleRad) * halfWidth;
                    float rightX = tempX - std::sin(perpAngleRad) * halfWidth;
                    float rightY = tempY - std::cos(perpAngleRad) * halfWidth;

                    createRibbonQuad(leftX, leftY, rightX, rightY);
                } else if (hasPrevPoint) {
                    // Reset ribbon if we're leaving bounds
                    hasPrevPoint = false;
                }
            }

            // Advance current position/heading to the end of the curve (exact).
            advanceAlongArc(currentX, currentY, currentAngle, segRadius, arcLength);
        }
    }
}

void MapHud::renderStartMarker(const RotationCache& rotation,
                               float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (m_trackSegments.empty()) {
        return;
    }

    // Calculate effective track width (same formula as renderTrack)
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float baseWidthMeters = std::min(trackWidth, trackHeight) * TRACK_WIDTH_BASE_RATIO;
    float effectiveWidthMeters = std::clamp(baseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);

    // Get start position. Prefer the actual S/F line from raceData (resolved on
    // track update); fall back to segment[0] (the data start) if unavailable
    // (older game build, or N/A in raceData).
    float startX, startY, startAngle;
    if (m_raceMarkers[MARKER_SF].valid) {
        startX = m_raceMarkers[MARKER_SF].worldX;
        startY = m_raceMarkers[MARKER_SF].worldY;
        startAngle = m_raceMarkers[MARKER_SF].angleDeg;
    } else {
        startX = m_trackSegments[0].startX;
        startY = m_trackSegments[0].startY;
        startAngle = m_trackSegments[0].angle;
    }

    // Cull if start marker is outside current bounds (with margin for marker size)
    float cullMargin = effectiveWidthMeters;
    if (startX < m_minX - cullMargin || startX > m_maxX + cullMargin ||
        startY < m_minY - cullMargin || startY > m_maxY + cullMargin) {
        return;
    }

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Draw triangle quad at track start pointing in direction.
    // Base spans the outline-edge width (wider than the track fill) for visibility,
    // and the tip is scaled the same way so the triangle keeps its proportions.
    float forwardAngleRad = startAngle * DEG_TO_RAD;
    float baseHalfWidth = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float pointLength   = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float pointX = startX + std::sin(forwardAngleRad) * pointLength;
    float pointY = startY + std::cos(forwardAngleRad) * pointLength;

    // Base endpoints (perpendicular to track at start)
    float perpAngle = startAngle + 90.0f;
    float perpAngleRad = perpAngle * DEG_TO_RAD;

    float baseLeftX = startX + std::sin(perpAngleRad) * baseHalfWidth;
    float baseLeftY = startY + std::cos(perpAngleRad) * baseHalfWidth;
    float baseRightX = startX - std::sin(perpAngleRad) * baseHalfWidth;
    float baseRightY = startY - std::cos(perpAngleRad) * baseHalfWidth;

    // Convert to screen coordinates
    float screenPointX, screenPointY;
    worldToScreen(pointX, pointY, screenPointX, screenPointY, rotation);
    screenPointY += titleOffset;

    float screenBaseLeftX, screenBaseLeftY;
    worldToScreen(baseLeftX, baseLeftY, screenBaseLeftX, screenBaseLeftY, rotation);
    screenBaseLeftY += titleOffset;

    float screenBaseRightX, screenBaseRightY;
    worldToScreen(baseRightX, baseRightY, screenBaseRightX, screenBaseRightY, rotation);
    screenBaseRightY += titleOffset;

    // Apply HUD offset
    applyOffset(screenPointX, screenPointY);
    applyOffset(screenBaseLeftX, screenBaseLeftY);
    applyOffset(screenBaseRightX, screenBaseRightY);

    // Skip if any vertex is outside clip bounds (clean cutoff, no distortion)
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };
    if (!isPointInClip(screenPointX, screenPointY) ||
        !isPointInClip(screenBaseLeftX, screenBaseLeftY) ||
        !isPointInClip(screenBaseRightX, screenBaseRightY)) {
        return;
    }

    // Create triangle quad (duplicate one vertex to make 4 points)
    SPluginQuad_t triangle;
    triangle.m_aafPos[0][0] = screenPointX;       // Point
    triangle.m_aafPos[0][1] = screenPointY;
    triangle.m_aafPos[1][0] = screenBaseRightX;   // Base right
    triangle.m_aafPos[1][1] = screenBaseRightY;
    triangle.m_aafPos[2][0] = screenBaseLeftX;    // Base left
    triangle.m_aafPos[2][1] = screenBaseLeftY;
    triangle.m_aafPos[3][0] = screenBaseLeftX;    // Duplicate base left to complete quad
    triangle.m_aafPos[3][1] = screenBaseLeftY;

    triangle.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    triangle.m_ulColor = this->getColor(ColorSlot::PRIMARY);  // White start/finish indicator
    m_quads.push_back(triangle);
}

void MapHud::drawDirectionMarker(const RaceMarker& marker, unsigned long color,
                                 const RotationCache& rotation,
                                 float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (!marker.valid) return;

    // Effective track width (matches renderTrack and renderStartMarker)
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float baseWidthMeters = std::min(trackWidth, trackHeight) * TRACK_WIDTH_BASE_RATIO;
    float effectiveWidthMeters = std::clamp(baseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);

    // Triangle dimensions (matches renderStartMarker). Base sits at the marker
    // position; the point extends forward in travel direction.
    float baseHalfWidth = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float pointLength   = effectiveWidthMeters * 0.5f * OUTLINE_WIDTH_MULTIPLIER;
    float cullMargin = effectiveWidthMeters;

    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Cull if marker is outside current bounds
    if (marker.worldX < m_minX - cullMargin || marker.worldX > m_maxX + cullMargin ||
        marker.worldY < m_minY - cullMargin || marker.worldY > m_maxY + cullMargin) {
        return;
    }

    // Triangle vertices: base sits at the marker position, point extends forward.
    float fwdRad = marker.angleDeg * DEG_TO_RAD;
    float perpRad = (marker.angleDeg + 90.0f) * DEG_TO_RAD;

    float pointX = marker.worldX + std::sin(fwdRad) * pointLength;
    float pointY = marker.worldY + std::cos(fwdRad) * pointLength;
    float baseLeftX  = marker.worldX + std::sin(perpRad) * baseHalfWidth;
    float baseLeftY  = marker.worldY + std::cos(perpRad) * baseHalfWidth;
    float baseRightX = marker.worldX - std::sin(perpRad) * baseHalfWidth;
    float baseRightY = marker.worldY - std::cos(perpRad) * baseHalfWidth;

    // Convert to screen coordinates
    float sPointX, sPointY, sLeftX, sLeftY, sRightX, sRightY;
    worldToScreen(pointX, pointY, sPointX, sPointY, rotation);
    worldToScreen(baseLeftX, baseLeftY, sLeftX, sLeftY, rotation);
    worldToScreen(baseRightX, baseRightY, sRightX, sRightY, rotation);
    sPointY += titleOffset;
    sLeftY  += titleOffset;
    sRightY += titleOffset;
    applyOffset(sPointX, sPointY);
    applyOffset(sLeftX,  sLeftY);
    applyOffset(sRightX, sRightY);

    // Skip if any vertex outside clip bounds (clean cutoff, no distortion)
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };
    if (!isPointInClip(sPointX, sPointY) ||
        !isPointInClip(sLeftX, sLeftY) ||
        !isPointInClip(sRightX, sRightY)) {
        return;
    }

    // Triangle as a quad with one duplicated vertex (matches renderStartMarker)
    SPluginQuad_t triangle;
    triangle.m_aafPos[0][0] = sPointX;   triangle.m_aafPos[0][1] = sPointY;
    triangle.m_aafPos[1][0] = sRightX;   triangle.m_aafPos[1][1] = sRightY;
    triangle.m_aafPos[2][0] = sLeftX;    triangle.m_aafPos[2][1] = sLeftY;
    triangle.m_aafPos[3][0] = sLeftX;    triangle.m_aafPos[3][1] = sLeftY;
    triangle.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
    triangle.m_ulColor = color;
    m_quads.push_back(triangle);
}

void MapHud::renderRaceMarkers(const RotationCache& rotation,
                               float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (!m_bHasTrackData || m_trackSegments.empty()) {
        return;
    }

    unsigned long splitColor = this->getColor(ColorSlot::POSITIVE);   // green for splits
    unsigned long holeshotColor = this->getColor(ColorSlot::NEUTRAL);  // yellow for holeshot
                                                                       // (accent is reserved for
                                                                       // the player's own segments)

    drawDirectionMarker(m_raceMarkers[MARKER_SPLIT_1], splitColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
    drawDirectionMarker(m_raceMarkers[MARKER_SPLIT_2], splitColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
    drawDirectionMarker(m_raceMarkers[MARKER_HOLESHOT], holeshotColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
}

void MapHud::renderSegmentMarkers(const RotationCache& rotation,
                                  float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (!m_bHasTrackData || m_trackSegments.empty()) {
        return;
    }

    // Custom segment-timer boundary points (training tool). Unlike the fixed race
    // markers, these are placed live via hotkey, so resolve them at render time.
    // Each point's trackPos (0-1) is S/F-relative (0 at start/finish), but
    // centerlinePositionAt measures meters from the centerline's data start. Map
    // back by adding the S/F offset (raceData[0]) and wrapping: meters =
    // (pos*totalLength + sfMeters) mod totalLength.
    const PluginData::SegmentTimerData& seg = PluginData::getInstance().getSegmentTimer();
    if (seg.points.empty()) {
        return;
    }

    float totalLength = 0.0f;
    for (const auto& s : m_trackSegments) totalLength += s.length;
    if (totalLength <= 0.0f) return;

    float sfOffset = (m_sfMeters > 0.0f) ? m_sfMeters : 0.0f;
    unsigned long segColor = this->getColor(ColorSlot::ACCENT);  // accent = the player's own segments
    for (float pos : seg.points) {
        float meters = std::fmod(pos * totalLength + sfOffset, totalLength);
        if (meters < 0.0f) meters += totalLength;
        RaceMarker m;
        if (centerlinePositionAt(meters, m.worldX, m.worldY, m.angleDeg)) {
            m.valid = true;
            drawDirectionMarker(m, segColor, rotation, clipLeft, clipTop, clipRight, clipBottom);
        }
    }
}

void MapHud::renderRiders(const RotationCache& rotation,
                          float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (m_riderPositions.empty() || !m_bHasTrackData) {
        return;
    }

    // Helper to check if a point is inside the clip region
    auto isPointInClip = [&](float x, float y) -> bool {
        return x >= clipLeft && x <= clipRight && y >= clipTop && y <= clipBottom;
    };

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Scale cone size by HUD scale factor
    constexpr float baseConeSize = 0.006f;
    float scaledConeSize = baseConeSize * m_fScale * m_fMarkerScale;

    // Calculate geometric centroid offset to center arrow on player position
    // Centroid = (tip_forward + left_forward + back_forward + right_forward) / 4
    // Where: tip=1.0, left/right≈0.0704 (=0.45*cos(81°)), back=-0.2
    // Centroid ≈ 0.235 forward from screenX,screenY
    constexpr float CENTROID_OFFSET = 0.235f;  // Offset to center arrow shape

    // Get plugin data to access rider names/numbers
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Helper lambda to render a single rider (used for both passes)
    auto renderRider = [&](const Unified::TrackPositionData& pos, bool isLocalPlayer) {
        // Get rider entry data
        const RaceEntryData* entry = pluginData.getRaceEntry(pos.raceNum);
        if (!entry) {
            return;  // Skip if we don't have race entry data
        }

        // For the active player with rotation mode enabled, use cached position if crashed
        // to keep screen position stable. But use actual yaw so arrow can spin in place.
        float renderX = pos.posX;
        float renderZ = pos.posZ;
        if (isLocalPlayer && pos.crashed && m_bRotateToPlayer) {
            renderX = m_fLastPlayerX;
            renderZ = m_fLastPlayerZ;
        }

        float renderYaw = pos.yaw;  // Always use current yaw for arrow direction

        // Convert world coordinates to screen coordinates
        // Use X and Z for ground plane (Y is altitude, not used for top-down map)
        float screenX, screenY;
        worldToScreen(renderX, renderZ, screenX, screenY, rotation);
        screenY += titleOffset;

        unsigned long riderColor;

        // Check if rider is tracked - tracked riders use their configured color with position modulation
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
        const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);

        // Tracked riders use their own icon index, non-tracked use global shape
        // trackedSpriteIndex: -1 = use global shape, otherwise direct sprite index
        int trackedSpriteIndex = -1;
        if (trackedConfig) {
            // Convert shapeIndex to sprite index (dynamically assigned)
            trackedSpriteIndex = AssetManager::getInstance().getFirstIconSpriteIndex() + trackedConfig->shapeIndex - 1;
        }

        if (trackedConfig) {
            // Tracked rider - use their configured color with position-based modulation
            unsigned long baseColor = trackedConfig->color;

            // Apply position-based color modulation (lighten if ahead by laps, darken if behind by laps)
            // Only in race sessions where lap position matters
            if (pluginData.isRaceSession()) {
                const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
                const StandingsData* riderStanding = pluginData.getStanding(pos.raceNum);
                int playerLaps = playerStanding ? playerStanding->numLaps : 0;
                int riderLaps = riderStanding ? riderStanding->numLaps : 0;
                int lapDiff = riderLaps - playerLaps;

                if (lapDiff >= 1) {
                    // Rider is ahead by laps - lighten color
                    riderColor = PluginUtils::lightenColor(baseColor, 0.4f);
                } else if (lapDiff <= -1) {
                    // Rider is behind by laps - darken color
                    riderColor = PluginUtils::darkenColor(baseColor, 0.6f);
                } else {
                    // Same lap - use base color
                    riderColor = baseColor;
                }
            } else {
                // Non-race session - use base color without modulation
                riderColor = baseColor;
            }
        } else if (isLocalPlayer) {
            // Accent is reserved for the player in every mode except Brand, where all
            // riders (player included) show their bike brand color.
            if (m_riderColorMode == RiderColorMode::BRAND) {
                riderColor = entry->bikeBrandColor;
            } else {
                riderColor = this->getColor(ColorSlot::ACCENT);
            }
        } else if (m_riderColorMode == RiderColorMode::RELATIVE_POS) {
            // Relative position coloring - only meaningful in race sessions
            if (pluginData.isRaceSession()) {
                const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
                const StandingsData* riderStanding = pluginData.getStanding(pos.raceNum);
                int playerPosition = pluginData.getDisplayPositionForRaceNum(displayRaceNum);
                int riderPosition = pluginData.getDisplayPositionForRaceNum(pos.raceNum);
                int playerLaps = playerStanding ? playerStanding->numLaps : 0;
                int riderLaps = riderStanding ? riderStanding->numLaps : 0;

                riderColor = PluginUtils::getRelativePositionColor(
                    playerPosition, riderPosition, playerLaps, riderLaps,
                    this->getColor(ColorSlot::NEUTRAL),
                    this->getColor(ColorSlot::WARNING),
                    this->getColor(ColorSlot::TERTIARY));
            } else {
                // Non-race: positions are meaningless, use uniform color
                riderColor = this->getColor(ColorSlot::NEUTRAL);
            }
        } else if (m_riderColorMode == RiderColorMode::BRAND) {
            // Brand colors at full opacity
            riderColor = entry->bikeBrandColor;
        } else {
            // Uniform: other riders use the primary color (matching their name color in
            // the standings); accent is reserved for the player.
            riderColor = this->getColor(ColorSlot::PRIMARY);
        }

        // Render sprite quad centered on rider position, rotated to match heading
        float spriteHalfSize = scaledConeSize;

        // All icons use uniform baseline scale

        // Skip if center is outside clip bounds
        float centerXClip = screenX, centerYClip = screenY;
        applyOffset(centerXClip, centerYClip);
        if (!isPointInClip(centerXClip, centerYClip)) {
            return;
        }

        // Determine sprite index and shape index for rotation check
        int spriteIndex;
        int shapeIndex;
        if (trackedSpriteIndex >= 0) {
            // Tracked rider - use their assigned icon
            spriteIndex = trackedSpriteIndex;
            shapeIndex = spriteIndex - AssetManager::getInstance().getFirstIconSpriteIndex() + 1;
        } else {
            // Non-tracked rider - use global shape (0=OFF defaults to circle for local player)
            shapeIndex = (m_riderShapeIndex > 0) ? m_riderShapeIndex : getShapeIndexByFilename(DEFAULT_RIDER_ICON);
            spriteIndex = AssetManager::getInstance().getFirstIconSpriteIndex() + shapeIndex - 1;
        }

        // Hazard/flag icon overrides — skip for the local player so their
        // own icon and color always stay consistent on the map.
        if (!isLocalPlayer) {
            HazardType hazardType = pluginData.getRiderHazardType(pos.raceNum);
            if (hazardType != HazardType::None) {
                m_iconCache.ensureInitialized();
                if (hazardType == HazardType::WrongWay) {
                    if (m_iconCache.circleExclamation > 0) {
                        spriteIndex = m_iconCache.circleExclamation;
                        riderColor = ColorPalette::RED;
                    }
                } else {
                    if (m_iconCache.flag > 0) {
                        spriteIndex = m_iconCache.flag;
                        riderColor = ColorPalette::YELLOW;
                    }
                }
                shapeIndex = spriteIndex - AssetManager::getInstance().getFirstIconSpriteIndex() + 1;
            }

            if (hazardType == HazardType::None && pluginData.isRiderBlueFlagged(pos.raceNum)) {
                m_iconCache.ensureInitialized();
                if (m_iconCache.flag > 0) {
                    spriteIndex = m_iconCache.flag;
                    shapeIndex = spriteIndex - AssetManager::getInstance().getFirstIconSpriteIndex() + 1;
                    riderColor = ColorPalette::BLUE;
                }
            } else if (hazardType == HazardType::None) {
                const StandingsData* standing = pluginData.getStanding(pos.raceNum);
                if (standing && pluginData.getSessionData().isRiderFinished(standing->numLaps, standing->numLapsAtLeaderFinish)) {
                    m_iconCache.ensureInitialized();
                    if (m_iconCache.flagCheckered > 0) {
                        spriteIndex = m_iconCache.flagCheckered;
                        shapeIndex = spriteIndex - AssetManager::getInstance().getFirstIconSpriteIndex() + 1;
                        riderColor = ColorPalette::WHITE;
                    }
                }
            }
        }

        // Calculate rotation only for directional icons
        float cosYaw = 1.0f;
        float sinYaw = 0.0f;
        if (TrackedRidersManager::shouldRotate(shapeIndex)) {
            float adjustedYaw = renderYaw - rotation.angle;
            float yawRad = adjustedYaw * DEG_TO_RAD;
            cosYaw = std::cos(yawRad);
            sinYaw = std::sin(yawRad);
        }

        // Helper lambda to create rotated sprite quad
        auto createRotatedSprite = [&](float halfSize, unsigned long spriteColor) {
            // Define corner offsets in uniform (square) space for proper rotation
            // TL, BL, BR, TR in local space
            float corners[4][2] = {
                {-halfSize, -halfSize},  // Top-left
                {-halfSize,  halfSize},  // Bottom-left
                { halfSize,  halfSize},  // Bottom-right
                { halfSize, -halfSize}   // Top-right
            };

            // Rotate corners in uniform space, then apply aspect ratio to X
            float rotatedCorners[4][2];
            for (int i = 0; i < 4; i++) {
                float dx = corners[i][0];
                float dy = corners[i][1];
                // Rotate in uniform space
                float rotX = dx * cosYaw - dy * sinYaw;
                float rotY = dx * sinYaw + dy * cosYaw;
                // Apply aspect ratio to X after rotation
                rotatedCorners[i][0] = screenX + rotX / UI_ASPECT_RATIO;
                rotatedCorners[i][1] = screenY + rotY;
                applyOffset(rotatedCorners[i][0], rotatedCorners[i][1]);
            }

            // Create rotated sprite quad
            SPluginQuad_t sprite;
            sprite.m_aafPos[0][0] = rotatedCorners[0][0];  // Top-left
            sprite.m_aafPos[0][1] = rotatedCorners[0][1];
            sprite.m_aafPos[1][0] = rotatedCorners[1][0];  // Bottom-left
            sprite.m_aafPos[1][1] = rotatedCorners[1][1];
            sprite.m_aafPos[2][0] = rotatedCorners[2][0];  // Bottom-right
            sprite.m_aafPos[2][1] = rotatedCorners[2][1];
            sprite.m_aafPos[3][0] = rotatedCorners[3][0];  // Top-right
            sprite.m_aafPos[3][1] = rotatedCorners[3][1];
            sprite.m_iSprite = spriteIndex;
            sprite.m_ulColor = spriteColor;
            m_quads.push_back(sprite);
        };

        // Render rider sprite (outline baked into sprite asset)
        createRotatedSprite(spriteHalfSize, riderColor);

        // Add click region for this rider (for spectator switching)
        // Click region is a rectangle centered on the sprite, with offset applied
        RiderClickRegion clickRegion;
        float clickWidth = spriteHalfSize * 2.0f / UI_ASPECT_RATIO;  // Account for aspect ratio
        float clickHeight = spriteHalfSize * 2.0f;
        clickRegion.x = screenX - spriteHalfSize / UI_ASPECT_RATIO + m_fOffsetX;
        clickRegion.y = screenY - spriteHalfSize + m_fOffsetY;
        clickRegion.width = clickWidth;
        clickRegion.height = clickHeight;
        clickRegion.raceNum = pos.raceNum;
        m_riderClickRegions.push_back(clickRegion);

        // Render label centered on arrow based on label mode
        if (m_labelMode != LabelMode::NONE) {
            // Scale font size by marker scale
            float labelFontSize = dim.fontSizeSmall * m_fMarkerScale;

            // Position label relative to the icon based on the configured anchor.
            // - spriteHalfSize is the icon's half-height (already includes m_fMarkerScale)
            // - spriteHalfWidth converts that to screen X (icons are aspect-corrected)
            // - labelGap is a small spacing proportional to icon size (20% of half-size)
            // String position is the text's top edge; Justify controls the X anchor.
            float labelGap = spriteHalfSize * 0.2f;
            float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
            float labelX = screenX;
            float labelY;
            int labelJustify = Justify::CENTER;
            // Side anchors center vertically on the icon. The string Y is the line-box
            // top, and the glyph sits below it by the font's leading, so 0.625 (not 0.5)
            // of the em size lands the visible text on the centerline (empirical).
            switch (m_labelAnchor) {
                case LabelAnchor::ABOVE:
                    labelY = screenY - spriteHalfSize - labelGap - labelFontSize;
                    break;
                case LabelAnchor::LEFT:
                    labelX = screenX - spriteHalfWidth - labelGap;
                    labelY = screenY - labelFontSize * 0.625f;
                    labelJustify = Justify::RIGHT;
                    break;
                case LabelAnchor::RIGHT:
                    labelX = screenX + spriteHalfWidth + labelGap;
                    labelY = screenY - labelFontSize * 0.625f;
                    labelJustify = Justify::LEFT;
                    break;
                case LabelAnchor::BELOW:
                default:
                    labelY = screenY + spriteHalfSize + labelGap;
                    break;
            }

            char labelStr[20];  // Sized for "P100" (5) + "#999" (5) = "P100#999" (9 + null)
            int position = pluginData.getDisplayPositionForRaceNum(pos.raceNum);

            switch (m_labelMode) {
                case LabelMode::POSITION:
                    // Show position only (P1, P2, etc.)
                    if (position > 0) {
                        snprintf(labelStr, sizeof(labelStr), "P%d", position);
                    } else {
                        labelStr[0] = '\0';  // No position data available
                    }
                    break;

                case LabelMode::RACE_NUM:
                    // Show race number only
                    snprintf(labelStr, sizeof(labelStr), "%d", pos.raceNum);
                    break;

                case LabelMode::BOTH:
                    // Show both position and race number (P1 #5)
                    if (position > 0) {
                        snprintf(labelStr, sizeof(labelStr), "P%d #%d", position, pos.raceNum);
                    } else {
                        snprintf(labelStr, sizeof(labelStr), "#%d", pos.raceNum);
                    }
                    break;

                default:
                    labelStr[0] = '\0';
                    break;
            }

            if (labelStr[0] != '\0') {
                // Use podium colors for position labels (P1/P2/P3)
                unsigned long labelColor = this->getColor(ColorSlot::PRIMARY);
                if (m_labelMode == LabelMode::POSITION || m_labelMode == LabelMode::BOTH) {
                    if (position == Position::FIRST) {
                        labelColor = PodiumColors::GOLD;
                    } else if (position == Position::SECOND) {
                        labelColor = PodiumColors::SILVER;
                    } else if (position == Position::THIRD) {
                        labelColor = PodiumColors::BRONZE;
                    }
                }

                // Text outline (our readability effect for labels over the map). It
                // serves the same purpose as the global drop shadow, so honor that
                // toggle: render the outline only when drop shadow is enabled.
                if (UiConfig::getInstance().getDropShadow()) {
                    float outlineOffset = labelFontSize * 0.05f;  // Small offset for outline
                    unsigned long outlineColor = 0xFF000000;  // Black with full opacity

                    // Render outline at 4 cardinal directions (skip drop shadow - this IS the outline)
                    addString(labelStr, labelX - outlineOffset, labelY, labelJustify,
                             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);
                    addString(labelStr, labelX + outlineOffset, labelY, labelJustify,
                             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);
                    addString(labelStr, labelX, labelY - outlineOffset, labelJustify,
                             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);
                    addString(labelStr, labelX, labelY + outlineOffset, labelJustify,
                             this->getFont(FontCategory::SMALL), outlineColor, labelFontSize, true);
                }

                // Render main text on top
                addString(labelStr, labelX, labelY, labelJustify,
                         this->getFont(FontCategory::SMALL), labelColor, labelFontSize, true);
            }
        }
    };

    // First pass: render all other riders (not local player)
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) continue;  // Skip player, render last

        // Skip non-tracked riders if global shape is OFF (0)
        // Tracked riders always render with their own shape
        if (m_riderShapeIndex == 0) {
            const RaceEntryData* entry = pluginData.getRaceEntry(pos.raceNum);
            if (!entry || !TrackedRidersManager::getInstance().isTracked(entry->name)) {
                continue;
            }
        }

        renderRider(pos, false);
    }

    // Second pass: render local player LAST (always on top)
    for (const auto& pos : m_riderPositions) {
        if (pos.raceNum == displayRaceNum) {
            renderRider(pos, true);
            break;  // Found and rendered player, done
        }
    }
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
    // OUTLINE_WIDTH_MULTIPLIER is defined at file scope (also reused by marker triangles).
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

    if (m_ribbonCacheValid && ribbonKey == m_ribbonKey) {
        // Same view, same style: only the rider dots changed - reuse the
        // tessellated ribbon instead of re-sampling the whole centerline
        m_quads.insert(m_quads.end(), m_ribbonQuads.begin(), m_ribbonQuads.end());
    } else {
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

    // Render split/holeshot direction-arrow triangles on top of track (below the
    // S/F triangle so the white arrow still draws on top if it visually overlaps).
    renderRaceMarkers(rotation, clipLeft, clipTop, clipRight, clipBottom);

    // Render start marker on top of track
    renderStartMarker(rotation, clipLeft, clipTop, clipRight, clipBottom);

    // Render the custom segment-timer lines above the track and race markers but below
    // riders, so rider icons remain the most visible elements on the map.
    renderSegmentMarkers(rotation, clipLeft, clipTop, clipRight, clipBottom);

    // Render rider positions last, on top of the track and all markers; within
    // renderRiders the local player is drawn last of all so the player's own icon is
    // the most visible element.
    renderRiders(rotation, clipLeft, clipTop, clipRight, clipBottom);

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
