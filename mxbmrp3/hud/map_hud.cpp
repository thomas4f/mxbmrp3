// ============================================================================
// hud/map_hud.cpp
// Map HUD implementation - displays track layout and rider positions
// ============================================================================
#include "map_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

// Undefine Windows min/max macros to avoid conflicts with std::min/std::max
#undef min
#undef max

using namespace PluginConstants;
using namespace PluginConstants::Math;

MapHud::MapHud()
    : m_fTrackLineWidthMeters(DEFAULT_TRACK_LINE_WIDTH),  // Reordered to match header declaration order
      m_minX(0.0f), m_maxX(0.0f), m_minY(0.0f), m_maxY(0.0f),
      m_fTrackScale(1.0f), m_fBaseMapWidth(0.0f), m_fBaseMapHeight(0.0f), m_bHasTrackData(false),
      m_bRotateToPlayer(false), m_fLastRotationAngle(0.0f),
      m_fLastPlayerX(0.0f), m_fLastPlayerZ(0.0f),
      m_bShowOutline(true),  // Enable outline by default for visual clarity
      m_bColorizeRiders(false),  // Disable rider colorization by default for cleaner look
      m_labelMode(LabelMode::POSITION),
      m_anchorPoint(AnchorPoint::TOP_RIGHT),
      m_fAnchorX(0.0f), m_fAnchorY(0.0f) {

    using namespace PluginConstants;

    // Initialize with square dimensions (will be adjusted when track data loads)
    m_fBaseMapHeight = MAP_HEIGHT;
    m_fBaseMapWidth = MAP_HEIGHT / UI_ASPECT_RATIO;

    setDraggable(true);

    // Set defaults to match user configuration
    m_bShowTitle = false;
    m_fBackgroundOpacity = 0.0f;

    // Set initial position and anchor (top-right corner)
    setPosition(0.8085f, -0.1665f);
    m_anchorPoint = AnchorPoint::TOP_RIGHT;
    m_fAnchorX = 0.994125f;
    m_fAnchorY = 0.0113039f;

    // Pre-allocate memory for track segments, quads, and rider positions
    m_trackSegments.reserve(RESERVE_TRACK_SEGMENTS);
    m_riderPositions.reserve(RESERVE_MAX_RIDERS);
    m_quads.reserve(RESERVE_QUADS);
    m_strings.reserve(RESERVE_STRINGS);

    DEBUG_INFO("MapHud initialized");
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
    return dataType == DataChangeType::Standings ||
           dataType == DataChangeType::SpectateTarget;
}

void MapHud::setTrackLineWidthMeters(float widthMeters) {
    // Clamp to valid range
    if (widthMeters < MIN_TRACK_LINE_WIDTH) widthMeters = MIN_TRACK_LINE_WIDTH;
    if (widthMeters > MAX_TRACK_LINE_WIDTH) widthMeters = MAX_TRACK_LINE_WIDTH;

    if (m_fTrackLineWidthMeters != widthMeters) {
        m_fTrackLineWidthMeters = widthMeters;
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
            int numSamples = std::max(3, static_cast<int>(arcLength / PIXEL_SPACING));
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

void MapHud::renderTrack(float rotationAngle, unsigned long trackColor, float widthMultiplier) {
    if (m_trackSegments.empty()) {
        return;
    }

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Track half-width in world coordinates (ribbon edge offset from centerline)
    float halfWidth = m_fTrackLineWidthMeters * 0.5f * widthMultiplier;

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

        if (segment.m_iType == TrackSegmentType::STRAIGHT) {
            // Straight segment - optimized to create only 1 quad (2 points: start and end)
            // Subdivision is unnecessary since affine transforms preserve straight lines
            float angleRad = currentAngle * DEG_TO_RAD;
            float dx = std::sin(angleRad) * segment.m_fLength;
            float dy = std::cos(angleRad) * segment.m_fLength;

            // Calculate perpendicular direction for ribbon edges (constant for straight segments)
            float perpAngle = currentAngle + 90.0f;
            float perpAngleRad = perpAngle * DEG_TO_RAD;
            float perpDx = std::sin(perpAngleRad) * halfWidth;
            float perpDy = std::cos(perpAngleRad) * halfWidth;

            // Only need start and end points for straight segments (1 quad)
            int numSteps = 1;

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
            int numSteps = std::max(3, static_cast<int>(arcLength / PIXEL_SPACING));
            float stepLength = arcLength / numSteps;
            float stepAngle = totalAngleChange / numSteps;

            // Track position and angle as we step through curve
            float tempX = startX;
            float tempY = startY;
            float tempAngle = currentAngle;

            for (int i = 0; i <= numSteps; ++i) {
                // Calculate perpendicular direction for ribbon edges at current point
                float perpAngle = tempAngle + 90.0f;
                float perpAngleRad = perpAngle * DEG_TO_RAD;

                // Calculate left and right edge points perpendicular to current heading
                float leftX = tempX + std::sin(perpAngleRad) * halfWidth;
                float leftY = tempY + std::cos(perpAngleRad) * halfWidth;
                float rightX = tempX - std::sin(perpAngleRad) * halfWidth;
                float rightY = tempY - std::cos(perpAngleRad) * halfWidth;

                createRibbonQuad(leftX, leftY, rightX, rightY);

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

void MapHud::renderStartMarker(float rotationAngle) {
    if (m_trackSegments.empty()) {
        return;
    }

    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Draw white triangle quad at track start pointing in direction
    float startX = m_trackSegments[0].m_afStart[0];
    float startY = m_trackSegments[0].m_afStart[1];
    float startAngle = m_trackSegments[0].m_fAngle;

    // Triangle dimensions: width = track width, length = 0.5× track width
    // Triangle point is along track direction
    float forwardAngleRad = startAngle * DEG_TO_RAD;
    float pointX = startX + std::sin(forwardAngleRad) * (m_fTrackLineWidthMeters * 0.5f);
    float pointY = startY + std::cos(forwardAngleRad) * (m_fTrackLineWidthMeters * 0.5f);

    // Base endpoints (perpendicular to track at start, total width = track width)
    float perpAngle = startAngle + 90.0f;
    float perpAngleRad = perpAngle * DEG_TO_RAD;
    float baseHalfWidth = m_fTrackLineWidthMeters * 0.5f;

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
    triangle.m_ulColor = TextColors::PRIMARY;  // White start/finish indicator
    m_quads.push_back(triangle);
}

void MapHud::renderRiders(float rotationAngle) {
    if (m_riderPositions.empty() || !m_bHasTrackData) {
        return;
    }


    // Get dimensions for title offset
    auto dim = getScaledDimensions();
    float titleOffset = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Scale cone size by HUD scale factor (25% larger than previous 0.012f)
    constexpr float baseConeSize = 0.015f;
    float scaledConeSize = baseConeSize * m_fScale;

    // Calculate geometric centroid offset to center arrow on player position
    // Centroid = (tip_forward + left_forward + back_forward + right_forward) / 4
    // Where: tip=1.0, left/right≈0.0704 (=0.45*cos(81°)), back=-0.2
    // Centroid ≈ 0.235 forward from screenX,screenY
    constexpr float CENTROID_OFFSET = 0.235f;  // Offset to center arrow shape

    // Get plugin data to access rider names/numbers
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // Render each rider position
    for (const auto& pos : m_riderPositions) {
        // Get rider entry data
        const RaceEntryData* entry = pluginData.getRaceEntry(pos.m_iRaceNum);
        if (!entry) {
            continue;  // Skip if we don't have race entry data
        }

        // For the active player with rotation mode enabled, use cached position if crashed
        // to keep screen position stable. But use actual yaw so arrow can spin in place.
        float renderX = pos.m_fPosX;
        float renderZ = pos.m_fPosZ;
        if (pos.m_iRaceNum == displayRaceNum && pos.m_iCrashed && m_bRotateToPlayer) {
            renderX = m_fLastPlayerX;
            renderZ = m_fLastPlayerZ;
        }
        float renderYaw = pos.m_fYaw;  // Always use current yaw for arrow direction

        // Convert world coordinates to screen coordinates
        // Use X and Z for ground plane (Y is altitude, not used for top-down map)
        float screenX, screenY;
        worldToScreen(renderX, renderZ, screenX, screenY, rotationAngle);
        screenY += titleOffset;

        // Add colored cone/arrow at rider position pointing in heading direction
        // Adjust yaw to compensate for world rotation (subtract because we're compensating)
        float adjustedYaw = renderYaw - rotationAngle;
        float yawRad = adjustedYaw * DEG_TO_RAD;

        // Center the arrow on player position by offsetting backward along yaw direction
        // This ensures the geometric center of the arrow aligns with the player's actual position
        float centeredX = screenX - (std::sin(yawRad) * scaledConeSize * CENTROID_OFFSET) / UI_ASPECT_RATIO;
        float centeredY = screenY + std::cos(yawRad) * scaledConeSize * CENTROID_OFFSET;

        // Create simple kite/cone shape: front tip + two sides + back point
        // Apply aspect ratio correction to X offsets to maintain proper proportions
        // Front tip (pointing in yaw direction)
        float tipX = centeredX + (std::sin(yawRad) * scaledConeSize) / UI_ASPECT_RATIO;
        float tipY = centeredY - std::cos(yawRad) * scaledConeSize;

        // Left side point (narrower for easier direction identification)
        float leftAngle = yawRad + (PI * 0.45f);  // 81 degrees left
        float leftX = centeredX + (std::sin(leftAngle) * scaledConeSize * 0.45f) / UI_ASPECT_RATIO;
        float leftY = centeredY - std::cos(leftAngle) * scaledConeSize * 0.45f;

        // Back point (goes backward from center for sharper arrow shape)
        float backX = centeredX - (std::sin(yawRad) * scaledConeSize * 0.2f) / UI_ASPECT_RATIO;
        float backY = centeredY + std::cos(yawRad) * scaledConeSize * 0.2f;

        // Right side point (narrower for easier direction identification)
        float rightAngle = yawRad - (PI * 0.45f);  // 81 degrees right
        float rightX = centeredX + (std::sin(rightAngle) * scaledConeSize * 0.45f) / UI_ASPECT_RATIO;
        float rightY = centeredY - std::cos(rightAngle) * scaledConeSize * 0.45f;

        // Apply HUD offset
        applyOffset(tipX, tipY);
        applyOffset(leftX, leftY);
        applyOffset(backX, backY);
        applyOffset(rightX, rightY);

        // Create quad following clockwise ordering: tip -> right -> back -> left
        // NOTE: Must use clockwise for proper rendering (counter-clockwise gets face-culled)
        SPluginQuad_t cone;
        cone.m_aafPos[0][0] = tipX;      // Front tip
        cone.m_aafPos[0][1] = tipY;
        cone.m_aafPos[1][0] = rightX;    // Right side (clockwise from tip)
        cone.m_aafPos[1][1] = rightY;
        cone.m_aafPos[2][0] = backX;     // Back center
        cone.m_aafPos[2][1] = backY;
        cone.m_aafPos[3][0] = leftX;     // Left side (completes the kite)
        cone.m_aafPos[3][1] = leftY;

        cone.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
        // Determine rider color: player always colorized, others based on setting
        bool isLocalPlayer = (pos.m_iRaceNum == displayRaceNum);
        if (isLocalPlayer) {
            // Player always shows their bike brand color
            cone.m_ulColor = entry->bikeBrandColor;
        } else if (m_bColorizeRiders) {
            // Colorized: Others at 75% opacity
            cone.m_ulColor = PluginUtils::applyOpacity(entry->bikeBrandColor, 0.75f);
        } else {
            // Non-colorized: Others in uniform tertiary color
            cone.m_ulColor = TextColors::TERTIARY;
        }
        m_quads.push_back(cone);

        // Render label centered on arrow based on label mode
        if (m_labelMode != LabelMode::NONE) {
            // Center text on arrow's center point (adjust for text baseline)
            float offsetY = screenY - (dim.fontSizeSmall * 0.5f);

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
                unsigned long labelColor = TextColors::PRIMARY;
                if (m_labelMode == LabelMode::POSITION || m_labelMode == LabelMode::BOTH) {
                    if (position == Position::FIRST) {
                        labelColor = PodiumColors::GOLD;
                    } else if (position == Position::SECOND) {
                        labelColor = PodiumColors::SILVER;
                    } else if (position == Position::THIRD) {
                        labelColor = PodiumColors::BRONZE;
                    }
                }

                addString(labelStr, screenX, offsetY, Justify::CENTER,
                         Fonts::TINY5, labelColor, dim.fontSizeSmall);
            }
        }
    }
}

void MapHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    // Don't render until we have track data
    // TrackCenterline callback fires during track load, before first render
    if (!m_bHasTrackData) {
        return;
    }

    // Calculate scaled dimensions
    auto dim = getScaledDimensions();

    // Calculate actual rotation angle for rendering
    float rotationAngle = calculateRotationAngle();

    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float width, height, x, y;

    if (m_bRotateToPlayer) {
        // ROTATION MODE: Background stays fixed size, positioned to center the track
        // Calculate maximum bounds at current scale (needed when scale changes)
        float maxWidth = 0.0f;
        float maxHeight = 0.0f;

        float testAngles[] = {0.0f, 45.0f, 90.0f, 135.0f};
        for (float angle : testAngles) {
            float minX, maxX, minY, maxY;
            calculateTrackScreenBounds(angle, minX, maxX, minY, maxY);
            maxWidth = std::max(maxWidth, maxX - minX);
            maxHeight = std::max(maxHeight, maxY - minY);
        }

        // Calculate current track bounds at actual rotation angle
        float currMinX, currMaxX, currMinY, currMaxY;
        calculateTrackScreenBounds(rotationAngle, currMinX, currMaxX, currMinY, currMaxY);

        float currWidth = currMaxX - currMinX;
        float currHeight = currMaxY - currMinY;

        // Background size is fixed to maximum
        width = maxWidth;
        height = maxHeight + titleHeight;

        // Center the track in the background (horizontally and vertically)
        x = currMinX - (maxWidth - currWidth) / 2.0f;
        y = currMinY - (maxHeight - currHeight) / 2.0f;
    } else {
        // NON-ROTATION MODE: Background fits actual track at 0° rotation
        float trackMinX, trackMaxX, trackMinY, trackMaxY;
        calculateTrackScreenBounds(0.0f, trackMinX, trackMaxX, trackMinY, trackMaxY);

        // Size to actual track bounds and position where track renders
        width = trackMaxX - trackMinX;
        height = (trackMaxY - trackMinY) + titleHeight;
        x = trackMinX;
        y = trackMinY;
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
    addTitleString("MAP", titleX, titleY, Justify::LEFT,
                  Fonts::TINY5, TextColors::PRIMARY, dim.fontSizeLarge);

    // Render track with optional outline effect (two passes for visual clarity)
    size_t quadsBeforeTrack = m_quads.size();
    constexpr float OUTLINE_WIDTH_MULTIPLIER = 1.4f;  // Outline is 40% wider than track
    if (m_bShowOutline) {
        renderTrack(rotationAngle, TextColors::PRIMARY, OUTLINE_WIDTH_MULTIPLIER);  // White outline
    }
    renderTrack(rotationAngle, TextColors::BACKGROUND, 1.0f);  // Black fill
    size_t trackQuads = m_quads.size() - quadsBeforeTrack;

    // Render start marker on top of track
    renderStartMarker(rotationAngle);

    // Render rider positions on top of track
    renderRiders(rotationAngle);

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
}

void MapHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    m_bShowBackgroundTexture = false;  // No texture by default
    m_fBackgroundOpacity = 0.0f;
    m_fScale = 1.0f;
    m_anchorPoint = AnchorPoint::TOP_RIGHT;
    m_fAnchorX = 0.994125f;
    m_fAnchorY = 0.0113039f;
    m_bRotateToPlayer = false;
    m_bShowOutline = true;  // Enable outline by default
    m_bColorizeRiders = false;  // Disable rider colorization by default
    m_labelMode = LabelMode::POSITION;
    m_fTrackLineWidthMeters = DEFAULT_TRACK_LINE_WIDTH;
    // Reset bounds to trigger "first rebuild" behavior in rebuildRenderData
    // This ensures position is recalculated from anchor values
    setBounds(0.0f, 0.0f, 0.0f, 0.0f);
    setDataDirty();
}
