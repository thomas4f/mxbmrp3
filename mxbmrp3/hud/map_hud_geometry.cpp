// ============================================================================
// mxbmrp3/hud/map_hud_geometry.cpp
// Zoom/screen bounds, rotation cache, world-to-screen projection
// (extracted verbatim from map_hud.cpp; no behavior change)
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

    // Degenerate-track guard: a zero-extent axis (a 1D or empty centerline — e.g.
    // a straight-only track, or malformed data) makes the scaleX/scaleY divisors
    // below zero, producing Inf/NaN vertices that poison the whole map (including
    // the container-size math that runs worldToScreen at 4 test angles, which is
    // how a bad track NaNs the HUD offset). Floor both extents to a tiny positive
    // value so the mapping is always finite. A real track is orders of magnitude
    // larger, so this is a no-op for it (output is byte-identical); a broken track
    // renders squished instead of corrupted.
    constexpr float MIN_TRACK_EXTENT = 1e-3f;
    trackWidth = std::max(trackWidth, MIN_TRACK_EXTENT);
    trackHeight = std::max(trackHeight, MIN_TRACK_EXTENT);
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
