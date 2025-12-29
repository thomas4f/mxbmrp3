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

// Default icon filename
static constexpr const char* DEFAULT_RIDER_ICON = "circle-chevron-up";

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
      m_labelMode(LabelMode::POSITION),
      m_riderShapeIndex(1),  // Will be set properly via settings or resetToDefaults
      m_anchorPoint(AnchorPoint::TOP_RIGHT),
      m_fAnchorX(0.0f), m_fAnchorY(0.0f),
      m_bZoomEnabled(false),
      m_fZoomDistance(DEFAULT_ZOOM_DISTANCE),
      m_fMarkerScale(DEFAULT_MARKER_SCALE),
      m_fPixelSpacing(DEFAULT_PIXEL_SPACING) {

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
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();
    } else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }

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

void MapHud::setPixelSpacing(float spacing) {
    // Clamp to valid range
    if (spacing < MIN_PIXEL_SPACING) spacing = MIN_PIXEL_SPACING;
    if (spacing > MAX_PIXEL_SPACING) spacing = MAX_PIXEL_SPACING;

    if (m_fPixelSpacing != spacing) {
        m_fPixelSpacing = spacing;
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

void MapHud::updateTrackData(int numSegments, const SPluginsTrackSegment_t* segments) {
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

    // Calculate track bounds and scale
    calculateTrackBounds();

    // Trigger rebuild
    setDataDirty();
}

void MapHud::updateRiderPositions(int numVehicles, const SPluginsRaceTrackPosition_t* positions) {
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
    m_minX = m_maxX = m_trackSegments[0].m_afStart[0];
    m_minY = m_maxY = m_trackSegments[0].m_afStart[1];

    // Calculate bounds by traversing all segments
    float currentX = m_trackSegments[0].m_afStart[0];
    float currentY = m_trackSegments[0].m_afStart[1];
    float currentAngle = m_trackSegments[0].m_fAngle;

    for (const auto& segment : m_trackSegments) {
        // Update bounds with current position
        m_minX = std::min(m_minX, currentX);
        m_maxX = std::max(m_maxX, currentX);
        m_minY = std::min(m_minY, currentY);
        m_maxY = std::max(m_maxY, currentY);

        // Calculate end position based on segment type
        if (segment.m_iType == TrackSegmentType::STRAIGHT) {
            // Straight segment
            float angleRad = currentAngle * DEG_TO_RAD;
            float dx = std::sin(angleRad) * segment.m_fLength;
            float dy = std::cos(angleRad) * segment.m_fLength;
            currentX += dx;
            currentY += dy;
        } else {
            // Curved segment - simple stepping approach
            float radius = segment.m_fRadius;
            float arcLength = segment.m_fLength;
            float absRadius = std::abs(radius);

            // Safety: Skip curved segments with invalid radius to avoid division by zero
            if (absRadius < 0.01f) {
                DEBUG_WARN_F("MapHud: Curved segment with invalid radius %.3f, skipping", radius);
                continue;
            }

            // Total angle change through the curve
            float totalAngleChange = arcLength / absRadius;
            if (radius < 0) {
                totalAngleChange = -totalAngleChange;
            }

            // Sample points along the curve for accurate bounds
            int numSamples = std::max(3, static_cast<int>(arcLength / m_fPixelSpacing));
            float stepLength = arcLength / numSamples;
            float stepAngle = totalAngleChange / numSamples;

            float tempX = currentX;
            float tempY = currentY;
            float tempAngle = currentAngle;

            for (int i = 1; i <= numSamples; ++i) {
                float tempAngleRad = tempAngle * DEG_TO_RAD;
                tempX += std::sin(tempAngleRad) * stepLength;
                tempY += std::cos(tempAngleRad) * stepLength;
                tempAngle += stepAngle * RAD_TO_DEG;

                m_minX = std::min(m_minX, tempX);
                m_maxX = std::max(m_maxX, tempX);
                m_minY = std::min(m_minY, tempY);
                m_maxY = std::max(m_maxY, tempY);
            }

            // Update current position and angle
            currentX = tempX;
            currentY = tempY;
            currentAngle = tempAngle;
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
        if (pos.m_iRaceNum == displayRaceNum) {
            if (!pos.m_iCrashed) {
                playerX = pos.m_fPosX;
                playerZ = pos.m_fPosZ;
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

void MapHud::calculateTrackScreenBounds(float rotationAngle, float& minX, float& maxX, float& minY, float& maxY) const {
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
        worldToScreen(corners[i][0], corners[i][1], screenX, screenY, rotationAngle);
        minX = std::min(minX, screenX);
        maxX = std::max(maxX, screenX);
        minY = std::min(minY, screenY);
        maxY = std::max(maxY, screenY);
    }
}

float MapHud::calculateRotationAngle() {
    // Calculate rotation angle if rotation mode is enabled
    float rotationAngle = 0.0f;
    if (m_bRotateToPlayer && !m_riderPositions.empty()) {
        // Find local player by race number
        const PluginData& pluginData = PluginData::getInstance();
        int displayRaceNum = pluginData.getDisplayRaceNum();

        for (const auto& pos : m_riderPositions) {
            if (pos.m_iRaceNum == displayRaceNum) {
                if (!pos.m_iCrashed) {
                    // Player is riding - update and cache rotation angle and position
                    rotationAngle = pos.m_fYaw;
                    m_fLastRotationAngle = rotationAngle;
                    m_fLastPlayerX = pos.m_fPosX;
                    m_fLastPlayerZ = pos.m_fPosZ;
                } else {
                    // Player crashed - keep using last rotation angle
                    // This keeps the map at the same orientation it was before the crash
                    rotationAngle = m_fLastRotationAngle;
                }
                break;
            }
        }
    }
    return rotationAngle;
}

void MapHud::worldToScreen(float worldX, float worldY, float& screenX, float& screenY, float rotationAngle) const {
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

    // Apply rotation around the center of the track
    if (rotationAngle != 0.0f) {
        float angleRad = rotationAngle * DEG_TO_RAD;
        float cosAngle = std::cos(angleRad);
        float sinAngle = std::sin(angleRad);

        // Center coordinates around track center
        float centeredX = normX - centerX;
        float centeredY = normY - centerY;

        // Rotate in square space (equal scale for X and Y)
        float rotatedX = centeredX * cosAngle - centeredY * sinAngle;
        float rotatedY = centeredX * sinAngle + centeredY * cosAngle;

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

void MapHud::renderTrack(float rotationAngle, unsigned long trackColor, float widthMultiplier,
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

    // Adaptive spacing for zoom mode - finer detail at closer zoom
    // At 50m: MIN_PIXEL_SPACING, at 500m: use configured m_fPixelSpacing
    float adaptiveSpacing = m_fPixelSpacing;  // Use configured spacing
    if (m_bZoomEnabled) {
        adaptiveSpacing = std::max(MIN_PIXEL_SPACING, m_fPixelSpacing * (m_fZoomDistance / MAX_ZOOM_DISTANCE));
    }

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
    float currentX = m_trackSegments[0].m_afStart[0];
    float currentY = m_trackSegments[0].m_afStart[1];
    float currentAngle = m_trackSegments[0].m_fAngle;

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
        worldToScreen(prevLeftX, prevLeftY, screenPrevLeftX, screenPrevLeftY, rotationAngle);
        screenPrevLeftY += titleOffset;

        float screenPrevRightX, screenPrevRightY;
        worldToScreen(prevRightX, prevRightY, screenPrevRightX, screenPrevRightY, rotationAngle);
        screenPrevRightY += titleOffset;

        float screenLeftX, screenLeftY;
        worldToScreen(leftX, leftY, screenLeftX, screenLeftY, rotationAngle);
        screenLeftY += titleOffset;

        float screenRightX, screenRightY;
        worldToScreen(rightX, rightY, screenRightX, screenRightY, rotationAngle);
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

        if (segment.m_iType == TrackSegmentType::STRAIGHT) {
            float angleRad = currentAngle * DEG_TO_RAD;
            endX = startX + std::sin(angleRad) * segment.m_fLength;
            endY = startY + std::cos(angleRad) * segment.m_fLength;
        } else {
            // For curves, approximate end position
            float radius = segment.m_fRadius;
            float arcLength = segment.m_fLength;
            float absRadius = std::abs(radius);
            float totalAngleChange = arcLength / absRadius;
            if (radius < 0) totalAngleChange = -totalAngleChange;
            // Rough approximation - use chord
            float angleRad = currentAngle * DEG_TO_RAD;
            endX = startX + std::sin(angleRad) * segment.m_fLength * 0.9f;
            endY = startY + std::cos(angleRad) * segment.m_fLength * 0.9f;
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

        if (segment.m_iType == TrackSegmentType::STRAIGHT) {
            // Straight segment
            float angleRad = currentAngle * DEG_TO_RAD;
            float dx = std::sin(angleRad) * segment.m_fLength;
            float dy = std::cos(angleRad) * segment.m_fLength;

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
                    // Subdivide using adaptive spacing (finer at closer zoom)
                    numSteps = std::max(1, static_cast<int>(segment.m_fLength / adaptiveSpacing));
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
            // Curved segment - stepping approach with changing perpendicular direction
            float radius = segment.m_fRadius;  // Keep sign (positive = right turn, negative = left turn)
            float arcLength = segment.m_fLength;
            float absRadius = std::abs(radius);

            // Total angle change through the curve (in radians)
            float totalAngleChange = arcLength / absRadius;
            if (radius < 0) {
                totalAngleChange = -totalAngleChange;  // Left turn = negative angle change
            }

            // Calculate number of steps needed based on arc length
            // Use adaptive spacing when zoom enabled (finer at closer zoom)
            float curveSpacing = m_bZoomEnabled ? adaptiveSpacing : m_fPixelSpacing;
            int numSteps = std::max(3, static_cast<int>(arcLength / curveSpacing));
            float stepLength = arcLength / numSteps;
            float stepAngle = totalAngleChange / numSteps;

            // Track position and angle as we step through curve
            float tempX = startX;
            float tempY = startY;
            float tempAngle = currentAngle;

            for (int i = 0; i <= numSteps; ++i) {
                // Only render points that are in bounds
                bool pointInBounds = isPointInBounds(tempX, tempY);

                if (segmentInBounds || pointInBounds) {
                    // Calculate perpendicular direction for ribbon edges at current point
                    float perpAngle = tempAngle + 90.0f;
                    float perpAngleRad = perpAngle * DEG_TO_RAD;

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

                // Step forward (except after last point)
                if (i < numSteps) {
                    float tempAngleRad = tempAngle * DEG_TO_RAD;
                    tempX += std::sin(tempAngleRad) * stepLength;
                    tempY += std::cos(tempAngleRad) * stepLength;
                    tempAngle += stepAngle * RAD_TO_DEG;
                }
            }

            // Update current position to end of curve (tempX/tempY/tempAngle already at final position)
            currentX = tempX;
            currentY = tempY;
            currentAngle = tempAngle;
        }
    }
}

void MapHud::renderStartMarker(float rotationAngle,
                               float clipLeft, float clipTop, float clipRight, float clipBottom) {
    if (m_trackSegments.empty()) {
        return;
    }

    // Calculate effective track width (same formula as renderTrack)
    float trackWidth = m_maxX - m_minX;
    float trackHeight = m_maxY - m_minY;
    float baseWidthMeters = std::min(trackWidth, trackHeight) * TRACK_WIDTH_BASE_RATIO;
    float effectiveWidthMeters = std::clamp(baseWidthMeters * m_fTrackWidthScale, 1.0f, 30.0f);

    // Get start position
    float startX = m_trackSegments[0].m_afStart[0];
    float startY = m_trackSegments[0].m_afStart[1];

    // Cull if start marker is outside current bounds (with margin for marker size)
    float cullMargin = effectiveWidthMeters;
    if (startX < m_minX - cullMargin || startX > m_maxX + cullMargin ||
        startY < m_minY - cullMargin || startY > m_maxY + cullMargin) {
        return;
    }

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Draw white triangle quad at track start pointing in direction
    float startAngle = m_trackSegments[0].m_fAngle;

    // Triangle dimensions: width = track width, length = 0.5× track width
    // Triangle point is along track direction
    float forwardAngleRad = startAngle * DEG_TO_RAD;
    float pointX = startX + std::sin(forwardAngleRad) * (effectiveWidthMeters * 0.5f);
    float pointY = startY + std::cos(forwardAngleRad) * (effectiveWidthMeters * 0.5f);

    // Base endpoints (perpendicular to track at start, total width = track width)
    float perpAngle = startAngle + 90.0f;
    float perpAngleRad = perpAngle * DEG_TO_RAD;
    float baseHalfWidth = effectiveWidthMeters * 0.5f;

    float baseLeftX = startX + std::sin(perpAngleRad) * baseHalfWidth;
    float baseLeftY = startY + std::cos(perpAngleRad) * baseHalfWidth;
    float baseRightX = startX - std::sin(perpAngleRad) * baseHalfWidth;
    float baseRightY = startY - std::cos(perpAngleRad) * baseHalfWidth;

    // Convert to screen coordinates
    float screenPointX, screenPointY;
    worldToScreen(pointX, pointY, screenPointX, screenPointY, rotationAngle);
    screenPointY += titleOffset;

    float screenBaseLeftX, screenBaseLeftY;
    worldToScreen(baseLeftX, baseLeftY, screenBaseLeftX, screenBaseLeftY, rotationAngle);
    screenBaseLeftY += titleOffset;

    float screenBaseRightX, screenBaseRightY;
    worldToScreen(baseRightX, baseRightY, screenBaseRightX, screenBaseRightY, rotationAngle);
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
    triangle.m_ulColor = ColorConfig::getInstance().getPrimary();  // White start/finish indicator
    m_quads.push_back(triangle);
}

void MapHud::renderRiders(float rotationAngle,
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
    auto renderRider = [&](const SPluginsRaceTrackPosition_t& pos, bool isLocalPlayer) {
        // Get rider entry data
        const RaceEntryData* entry = pluginData.getRaceEntry(pos.m_iRaceNum);
        if (!entry) {
            return;  // Skip if we don't have race entry data
        }

        // For the active player with rotation mode enabled, use cached position if crashed
        // to keep screen position stable. But use actual yaw so arrow can spin in place.
        float renderX = pos.m_fPosX;
        float renderZ = pos.m_fPosZ;
        if (isLocalPlayer && pos.m_iCrashed && m_bRotateToPlayer) {
            renderX = m_fLastPlayerX;
            renderZ = m_fLastPlayerZ;
        }

        float renderYaw = pos.m_fYaw;  // Always use current yaw for arrow direction

        // Convert world coordinates to screen coordinates
        // Use X and Z for ground plane (Y is altitude, not used for top-down map)
        float screenX, screenY;
        worldToScreen(renderX, renderZ, screenX, screenY, rotationAngle);
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
            const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
            const StandingsData* riderStanding = pluginData.getStanding(pos.m_iRaceNum);
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
        } else if (isLocalPlayer) {
            // Player always shows green in relative position mode, otherwise bike brand color
            if (m_riderColorMode == RiderColorMode::RELATIVE_POS) {
                riderColor = ColorConfig::getInstance().getPositive();  // Green
            } else {
                riderColor = entry->bikeBrandColor;
            }
        } else if (m_riderColorMode == RiderColorMode::RELATIVE_POS) {
            // Relative position coloring: color based on position/lap relative to player
            const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
            const StandingsData* riderStanding = pluginData.getStanding(pos.m_iRaceNum);
            int playerPosition = pluginData.getPositionForRaceNum(displayRaceNum);
            int riderPosition = pluginData.getPositionForRaceNum(pos.m_iRaceNum);
            int playerLaps = playerStanding ? playerStanding->numLaps : 0;
            int riderLaps = riderStanding ? riderStanding->numLaps : 0;

            riderColor = PluginUtils::getRelativePositionColor(
                playerPosition, riderPosition, playerLaps, riderLaps,
                ColorConfig::getInstance().getNeutral(),
                ColorConfig::getInstance().getWarning(),
                ColorConfig::getInstance().getTertiary());
        } else if (m_riderColorMode == RiderColorMode::BRAND) {
            // Brand colors at full opacity
            riderColor = entry->bikeBrandColor;
        } else {
            // Uniform: Others in uniform tertiary color
            riderColor = ColorConfig::getInstance().getTertiary();
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

        // Calculate rotation only for directional icons
        float cosYaw = 1.0f;
        float sinYaw = 0.0f;
        if (TrackedRidersManager::shouldRotate(shapeIndex)) {
            float adjustedYaw = renderYaw - rotationAngle;
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
        clickRegion.raceNum = pos.m_iRaceNum;
        m_riderClickRegions.push_back(clickRegion);

        // Render label centered on arrow based on label mode
        if (m_labelMode != LabelMode::NONE) {
            // Scale font size by marker scale
            float labelFontSize = dim.fontSizeSmall * m_fMarkerScale;

            // Position label just below the icon:
            // - spriteHalfSize is the icon's half-height (already includes m_fMarkerScale)
            // - Add a small gap proportional to icon size (20% of icon half-size)
            float labelGap = spriteHalfSize * 0.2f;
            float offsetY = screenY + spriteHalfSize + labelGap;

            char labelStr[20];  // Sized for "P100" (5) + "#999" (5) = "P100#999" (9 + null)
            int position = pluginData.getPositionForRaceNum(pos.m_iRaceNum);

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
                    snprintf(labelStr, sizeof(labelStr), "%d", pos.m_iRaceNum);
                    break;

                case LabelMode::BOTH:
                    // Show both position and race number (P1 #5)
                    if (position > 0) {
                        snprintf(labelStr, sizeof(labelStr), "P%d #%d", position, pos.m_iRaceNum);
                    } else {
                        snprintf(labelStr, sizeof(labelStr), "#%d", pos.m_iRaceNum);
                    }
                    break;

                default:
                    labelStr[0] = '\0';
                    break;
            }

            if (labelStr[0] != '\0') {
                // Use podium colors for position labels (P1/P2/P3)
                unsigned long labelColor = ColorConfig::getInstance().getPrimary();
                if (m_labelMode == LabelMode::POSITION || m_labelMode == LabelMode::BOTH) {
                    if (position == Position::FIRST) {
                        labelColor = PodiumColors::GOLD;
                    } else if (position == Position::SECOND) {
                        labelColor = PodiumColors::SILVER;
                    } else if (position == Position::THIRD) {
                        labelColor = PodiumColors::BRONZE;
                    }
                }

                // Create text outline by rendering dark text at offsets first
                float outlineOffset = labelFontSize * 0.05f;  // Small offset for outline
                unsigned long outlineColor = 0xFF000000;  // Black with full opacity

                // Render outline at 4 cardinal directions
                addString(labelStr, screenX - outlineOffset, offsetY, Justify::CENTER,
                         Fonts::getSmall(), outlineColor, labelFontSize);
                addString(labelStr, screenX + outlineOffset, offsetY, Justify::CENTER,
                         Fonts::getSmall(), outlineColor, labelFontSize);
                addString(labelStr, screenX, offsetY - outlineOffset, Justify::CENTER,
                         Fonts::getSmall(), outlineColor, labelFontSize);
                addString(labelStr, screenX, offsetY + outlineOffset, Justify::CENTER,
                         Fonts::getSmall(), outlineColor, labelFontSize);

                // Render main text on top
                addString(labelStr, screenX, offsetY, Justify::CENTER,
                         Fonts::getSmall(), labelColor, labelFontSize);
            }
        }
    };

    // First pass: render all other riders (not local player)
    for (const auto& pos : m_riderPositions) {
        if (pos.m_iRaceNum == displayRaceNum) continue;  // Skip player, render last

        // Skip non-tracked riders if global shape is OFF (0)
        // Tracked riders always render with their own shape
        if (m_riderShapeIndex == 0) {
            const RaceEntryData* entry = pluginData.getRaceEntry(pos.m_iRaceNum);
            if (!entry || !TrackedRidersManager::getInstance().isTracked(entry->name)) {
                continue;
            }
        }

        renderRider(pos, false);
    }

    // Second pass: render local player LAST (always on top)
    for (const auto& pos : m_riderPositions) {
        if (pos.m_iRaceNum == displayRaceNum) {
            renderRider(pos, true);
            break;  // Found and rendered player, done
        }
    }
}

void MapHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();
    m_riderClickRegions.clear();

    // Don't render until we have track data
    // TrackCenterline callback fires during track load, before first render
    if (!m_bHasTrackData) {
        return;
    }

    // Calculate scaled dimensions
    auto dim = getScaledDimensions();

    // Calculate actual rotation angle for rendering
    float rotationAngle = calculateRotationAngle();

    // Calculate container size FIRST using original track bounds (before any zoom override)
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float width, height, x, y;

    // Calculate maximum bounds across all rotation angles to ensure container fits track at any angle
    float maxScreenWidth = 0.0f;
    float maxScreenHeight = 0.0f;

    float testAngles[] = {0.0f, 45.0f, 90.0f, 135.0f};
    for (float angle : testAngles) {
        float minX, maxX, minY, maxY;
        calculateTrackScreenBounds(angle, minX, maxX, minY, maxY);
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
    calculateTrackScreenBounds(rotationAngle, currMinX, currMaxX, currMinY, currMaxY);

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
            m_fBaseMapWidth = squareWidth;
            m_fBaseMapHeight = squareHeight;

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
                  Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);

    // Calculate clip bounds for track rendering (absolute screen coords)
    // Clip to the map area below the title
    // Inset by half outline width since we clip on centerline but edges extend beyond
    constexpr float OUTLINE_WIDTH_MULTIPLIER = 1.4f;
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
    if (m_bShowOutline) {
        renderTrack(rotationAngle, ColorConfig::getInstance().getPrimary(), OUTLINE_WIDTH_MULTIPLIER,
                    clipLeft, clipTop, clipRight, clipBottom);  // White outline
    }
    renderTrack(rotationAngle, ColorConfig::getInstance().getBackground(), 1.0f,
                clipLeft, clipTop, clipRight, clipBottom);  // Black fill
    size_t trackQuads = m_quads.size() - quadsBeforeTrack;

    // Render start marker on top of track
    renderStartMarker(rotationAngle, clipLeft, clipTop, clipRight, clipBottom);

    // Render rider positions on top of track
    renderRiders(rotationAngle, clipLeft, clipTop, clipRight, clipBottom);

    // Log quad count once for performance analysis
    static bool quadCountLogged = false;
    if (!quadCountLogged) {
        // Count segment types
        size_t straightCount = 0, curveCount = 0;
        for (const auto& seg : m_trackSegments) {
            if (seg.m_iType == 0) straightCount++;
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
    m_fBackgroundOpacity = 0.1f;  // 10% opacity
    m_fScale = 1.0f;
    m_anchorPoint = AnchorPoint::TOP_RIGHT;
    m_fAnchorX = 0.994125f;
    m_fAnchorY = 0.0113039f;
    m_bRotateToPlayer = false;
    m_bShowOutline = true;  // Enable outline by default
    m_riderColorMode = RiderColorMode::RELATIVE_POS;  // Default to relative position coloring
    m_labelMode = LabelMode::POSITION;
    m_riderShapeIndex = getShapeIndexByFilename(DEFAULT_RIDER_ICON);
    m_fTrackWidthScale = DEFAULT_TRACK_WIDTH_SCALE;
    m_bZoomEnabled = false;
    m_fZoomDistance = DEFAULT_ZOOM_DISTANCE;
    m_fMarkerScale = DEFAULT_MARKER_SCALE;
    m_fPixelSpacing = DEFAULT_PIXEL_SPACING;
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
