// ============================================================================
// hud/radar_hud.cpp
// Radar HUD implementation - displays a top-down view of nearby riders
// ============================================================================
#include "radar_hud.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_utils.h"
#include "../core/color_config.h"
#include "../core/asset_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../diagnostics/logger.h"
#include <cmath>
#include <algorithm>

using namespace PluginConstants;
using namespace PluginConstants::Math;

RadarHud::RadarHud()
    : m_fRadarRangeMeters(DEFAULT_RADAR_RANGE),
      m_riderColorMode(RiderColorMode::BRAND),
      m_bShowPlayerArrow(false),
      m_bFadeWhenEmpty(false),
      m_fAlertDistance(DEFAULT_ALERT_DISTANCE),
      m_labelMode(LabelMode::POSITION),
      m_riderShape(RiderShape::CIRCLE),
      m_fMarkerScale(DEFAULT_MARKER_SCALE) {

    // One-time setup
    DEBUG_INFO("RadarHud created");
    setDraggable(true);
    m_riderPositions.reserve(RESERVE_MAX_RIDERS);
    m_quads.reserve(RESERVE_QUADS);
    m_strings.reserve(RESERVE_STRINGS);

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("radar_hud");

    // Set all configurable defaults
    resetToDefaults();
}

void RadarHud::update() {
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();
    } else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

bool RadarHud::handlesDataType(DataChangeType dataType) const {
    // Rebuild when standings change (for position labels)
    // Also rebuild when tracked riders change (color/shape)
    return dataType == DataChangeType::Standings ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::TrackedRiders;
}

void RadarHud::setRadarRange(float rangeMeters) {
    // Clamp to valid range
    if (rangeMeters < MIN_RADAR_RANGE) rangeMeters = MIN_RADAR_RANGE;
    if (rangeMeters > MAX_RADAR_RANGE) rangeMeters = MAX_RADAR_RANGE;

    if (m_fRadarRangeMeters != rangeMeters) {
        m_fRadarRangeMeters = rangeMeters;
        setDataDirty();
    }
}

void RadarHud::setAlertDistance(float meters) {
    // Clamp to valid range
    if (meters < MIN_ALERT_DISTANCE) meters = MIN_ALERT_DISTANCE;
    if (meters > MAX_ALERT_DISTANCE) meters = MAX_ALERT_DISTANCE;

    if (m_fAlertDistance != meters) {
        m_fAlertDistance = meters;
        setDataDirty();
    }
}

void RadarHud::setMarkerScale(float scale) {
    // Clamp to valid range
    if (scale < MIN_MARKER_SCALE) scale = MIN_MARKER_SCALE;
    if (scale > MAX_MARKER_SCALE) scale = MAX_MARKER_SCALE;

    if (m_fMarkerScale != scale) {
        m_fMarkerScale = scale;
        setDataDirty();
    }
}

void RadarHud::updateRiderPositions(int numVehicles, const SPluginsRaceTrackPosition_t* positions) {
    if (numVehicles <= 0 || positions == nullptr) {
        m_riderPositions.clear();
        return;
    }

    // Copy rider positions (fast operation - runs at high frequency)
    m_riderPositions.assign(positions, positions + numVehicles);

    // Mark data as dirty to trigger render update
    setDataDirty();
}

void RadarHud::renderRiderSprite(float radarX, float radarY, float yaw, unsigned long color,
                                   float centerX, float centerY, float radarRadius,
                                   int shapeOverride) {
    // Scale sprite size by HUD scale factor and marker scale
    constexpr float baseConeSize = 0.006f;
    float scaledConeSize = baseConeSize * m_fScale * m_fMarkerScale;
    float spriteHalfSize = scaledConeSize;

    // Determine effective shape (use override if provided, otherwise use global setting)
    // shapeOverride uses TrackedRidersManager values: 1-N for all rider icons
    // m_riderShape uses RadarHud values: 0-9 for the 10 selectable icons
    const int iconCount = static_cast<int>(AssetManager::getInstance().getIconCount());
    int effectiveShape;
    if (shapeOverride >= 1 && shapeOverride <= iconCount) {
        effectiveShape = shapeOverride;
    } else {
        // Map RadarHud::RiderShape to TrackedRidersManager shape values
        switch (m_riderShape) {
            case RiderShape::ARROWUP:    effectiveShape = TrackedRidersManager::SHAPE_ARROWUP; break;
            case RiderShape::CHEVRON:    effectiveShape = TrackedRidersManager::SHAPE_CHEVRON; break;
            case RiderShape::CIRCLEPLAY: effectiveShape = TrackedRidersManager::SHAPE_CIRCLEPLAY; break;
            case RiderShape::CIRCLEUP:   effectiveShape = TrackedRidersManager::SHAPE_CIRCLEUP; break;
            case RiderShape::DOT:        effectiveShape = TrackedRidersManager::SHAPE_DOT; break;
            case RiderShape::LOCATION:   effectiveShape = TrackedRidersManager::SHAPE_LOCATION; break;
            case RiderShape::PIN:        effectiveShape = TrackedRidersManager::SHAPE_PIN; break;
            case RiderShape::PLANE:      effectiveShape = TrackedRidersManager::SHAPE_PLANE; break;
            case RiderShape::VINYL:      effectiveShape = TrackedRidersManager::SHAPE_VINYL; break;
            case RiderShape::CIRCLE:
            default:                     effectiveShape = TrackedRidersManager::SHAPE_CIRCLE; break;
        }
    }

    // All icons use uniform baseline scale

    // Convert radar coordinates (-1 to 1) to screen coordinates
    float screenX = centerX + (radarX * radarRadius) / UI_ASPECT_RATIO;
    float screenY = centerY - radarY * radarRadius;

    // Calculate rotation only for directional icons
    float cosYaw = 1.0f;
    float sinYaw = 0.0f;
    if (TrackedRidersManager::shouldRotate(effectiveShape)) {
        float yawRad = yaw * DEG_TO_RAD;
        cosYaw = std::cos(yawRad);
        sinYaw = std::sin(yawRad);
    }

    // Determine sprite index - directly convert shape index to sprite index
    // shapeIndex 1-N maps to icon sprite indices (dynamically assigned)
    int spriteIndex = AssetManager::getInstance().getFirstIconSpriteIndex() + effectiveShape - 1;

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
    createRotatedSprite(spriteHalfSize, color);
}

void RadarHud::renderRiderLabel(float radarX, float radarY, int raceNum, int position,
                                  float centerX, float centerY, float radarRadius, float opacity) {
    if (m_labelMode == LabelMode::NONE) return;

    auto dim = getScaledDimensions();

    // Scale font size by marker scale
    float labelFontSize = dim.fontSizeSmall * m_fMarkerScale;

    // Calculate scaled icon size (must match renderRiderSprite)
    constexpr float baseConeSize = 0.006f;
    float scaledConeSize = baseConeSize * m_fScale * m_fMarkerScale;

    // Convert radar coordinates to screen coordinates
    float screenX = centerX + (radarX * radarRadius) / UI_ASPECT_RATIO;
    float screenY = centerY - radarY * radarRadius;

    // Offset label below the icon (based on icon size plus small gap)
    float labelY = screenY + scaledConeSize + (dim.fontSizeSmall * 0.3f * m_fMarkerScale);

    char labelStr[20];
    switch (m_labelMode) {
        case LabelMode::POSITION:
            if (position > 0) {
                snprintf(labelStr, sizeof(labelStr), "P%d", position);
            } else {
                labelStr[0] = '\0';
            }
            break;

        case LabelMode::RACE_NUM:
            snprintf(labelStr, sizeof(labelStr), "%d", raceNum);
            break;

        case LabelMode::BOTH:
            if (position > 0) {
                snprintf(labelStr, sizeof(labelStr), "P%d #%d", position, raceNum);
            } else {
                snprintf(labelStr, sizeof(labelStr), "#%d", raceNum);
            }
            break;

        default:
            labelStr[0] = '\0';
            break;
    }

    if (labelStr[0] != '\0') {
        // Use podium colors for position labels
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

        // Apply opacity to colors to match sprite fading
        labelColor = PluginUtils::applyOpacity(labelColor, opacity);
        unsigned long outlineColor = PluginUtils::applyOpacity(0x000000, opacity);  // Black with matching opacity

        // Create text outline by rendering dark text at offsets first
        float outlineOffset = labelFontSize * 0.05f;  // Small offset for outline

        // Render outline at 4 cardinal directions
        addString(labelStr, screenX - outlineOffset, labelY, Justify::CENTER,
                 Fonts::getSmall(), outlineColor, labelFontSize);
        addString(labelStr, screenX + outlineOffset, labelY, Justify::CENTER,
                 Fonts::getSmall(), outlineColor, labelFontSize);
        addString(labelStr, screenX, labelY - outlineOffset, Justify::CENTER,
                 Fonts::getSmall(), outlineColor, labelFontSize);
        addString(labelStr, screenX, labelY + outlineOffset, Justify::CENTER,
                 Fonts::getSmall(), outlineColor, labelFontSize);

        // Render main text on top
        addString(labelStr, screenX, labelY, Justify::CENTER,
                 Fonts::getSmall(), labelColor, labelFontSize);
    }
}

void RadarHud::rebuildRenderData() {
    m_quads.clear();
    m_strings.clear();

    // Calculate dimensions
    auto dim = getScaledDimensions();

    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Radar size based on screen height
    float radarDiameter = RADAR_SIZE * m_fScale;
    float radarRadius = radarDiameter * 0.5f;

    // Calculate width in screen coords (account for aspect ratio)
    float width = radarDiameter / UI_ASPECT_RATIO + dim.paddingH * 2;
    float height = radarDiameter + titleHeight + dim.paddingV * 2;

    float x = 0.0f;
    float y = 0.0f;

    // Set bounds for dragging
    setBounds(x, y, x + width, y + height);

    // Get plugin data and find local player (needed for opacity calculation)
    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();

    const SPluginsRaceTrackPosition_t* localPlayer = nullptr;
    for (const auto& pos : m_riderPositions) {
        if (pos.m_iRaceNum == displayRaceNum) {
            localPlayer = &pos;
            break;
        }
    }

    // Pre-calculate max rider opacity for background fade (only if fade is enabled)
    float maxRiderOpacity = 1.0f;  // Default: fully visible
    if (m_bFadeWhenEmpty && localPlayer) {
        maxRiderOpacity = 0.0f;  // Start at 0, find max
        float playerX = localPlayer->m_fPosX;
        float playerZ = localPlayer->m_fPosZ;
        float trackLength = pluginData.getSessionData().trackLength;

        for (const auto& pos : m_riderPositions) {
            if (pos.m_iRaceNum == displayRaceNum) continue;

            float relX = pos.m_fPosX - playerX;
            float relZ = pos.m_fPosZ - playerZ;
            float distance = std::sqrt(relX * relX + relZ * relZ);
            if (distance > m_fRadarRangeMeters) continue;

            // Calculate track distance fade
            float trackDist = std::abs(pos.m_fTrackPos - localPlayer->m_fTrackPos);
            if (trackDist > 0.5f) trackDist = 1.0f - trackDist;

            float trackFadeOpacity = 1.0f;
            if (trackLength > 0.0f) {
                float trackDistMeters = trackDist * trackLength;
                if (trackDistMeters >= m_fRadarRangeMeters) continue;
                trackFadeOpacity = 1.0f - (trackDistMeters / m_fRadarRangeMeters);
            } else {
                constexpr float FALLBACK_THRESHOLD = 0.05f;
                if (trackDist >= FALLBACK_THRESHOLD) continue;
                trackFadeOpacity = 1.0f - (trackDist / FALLBACK_THRESHOLD);
            }

            maxRiderOpacity = std::max(maxRiderOpacity, trackFadeOpacity);
        }
    }

    // Add background (opacity scaled by max rider visibility when fade enabled)
    float savedOpacity = m_fBackgroundOpacity;
    m_fBackgroundOpacity = savedOpacity * maxRiderOpacity;
    addBackgroundQuad(x, y, width, height);
    m_fBackgroundOpacity = savedOpacity;

    // Add title (also fades with background when fade enabled)
    if (m_bShowTitle) {
        float titleX = x + dim.paddingH;
        float titleY = y + dim.paddingV;
        unsigned long titleColor = PluginUtils::applyOpacity(
            ColorConfig::getInstance().getPrimary(), maxRiderOpacity);
        addTitleString("RADAR", titleX, titleY, Justify::LEFT,
                      Fonts::getSmall(), titleColor, dim.fontSizeLarge);
    }

    // Calculate radar center position
    float centerX = x + width * 0.5f;
    float centerY = y + titleHeight + dim.paddingV + radarRadius;

    // Number of sectors for proximity highlighting (4 = front, right, back, left)
    constexpr int NUM_SECTORS = 4;

    // Track closest rider distance per section (for intensity-based highlighting)
    // Section angles (in radar space where 0° = forward/up, 90° each):
    // Section 0: 315°-45° (front)
    // Section 1: 45°-135° (right)
    // Section 2: 135°-225° (back)
    // Section 3: 225°-315° (left)
    float sectionClosestDist[NUM_SECTORS] = { -1.0f, -1.0f, -1.0f, -1.0f };

    // Pre-calculate player rotation if we have a local player
    float playerYawRad = 0.0f;
    float cosYaw = 1.0f;
    float sinYaw = 0.0f;
    float playerX = 0.0f;
    float playerZ = 0.0f;

    if (localPlayer) {
        playerX = localPlayer->m_fPosX;
        playerZ = localPlayer->m_fPosZ;
        playerYawRad = localPlayer->m_fYaw * DEG_TO_RAD;
        cosYaw = std::cos(playerYawRad);
        sinYaw = std::sin(playerYawRad);

        // First pass: calculate section distances for all riders
        for (const auto& pos : m_riderPositions) {
            if (pos.m_iRaceNum == displayRaceNum) continue;

            float relX = pos.m_fPosX - playerX;
            float relZ = pos.m_fPosZ - playerZ;

            // Rotate to radar space
            float rotatedX = relX * cosYaw - relZ * sinYaw;
            float rotatedZ = relX * sinYaw + relZ * cosYaw;

            float distance = std::sqrt(rotatedX * rotatedX + rotatedZ * rotatedZ);

            // Only track section distances for riders within alert distance
            if (distance > m_fAlertDistance) continue;

            // Filter by track distance (skip riders on parallel straights)
            float trackDist = std::abs(pos.m_fTrackPos - localPlayer->m_fTrackPos);
            if (trackDist > 0.5f) trackDist = 1.0f - trackDist;  // Handle wraparound

            float trackLength = pluginData.getSessionData().trackLength;
            if (trackLength > 0.0f) {
                float trackDistMeters = trackDist * trackLength;
                if (trackDistMeters > m_fAlertDistance) continue;
            } else {
                // Fallback: use 5% of track as threshold if track length unknown
                constexpr float FALLBACK_THRESHOLD = 0.05f;
                if (trackDist > FALLBACK_THRESHOLD) continue;
            }

            // Calculate angle in radar space (0° = forward, clockwise positive)
            // atan2(x, z) gives angle where 0 = forward (+Z), 90 = right (+X)
            float angle = std::atan2(rotatedX, rotatedZ);  // radians, -PI to PI

            // Convert to degrees and normalize to 0-360
            float angleDeg = angle * RAD_TO_DEG;
            if (angleDeg < 0) angleDeg += 360.0f;

            // Determine section (each section is 90°)
            // Section 0: 315-360 and 0-45 (front)
            // Section 1: 45-135 (right)
            // Section 2: 135-225 (back)
            // Section 3: 225-315 (left)
            int section;
            if (angleDeg >= 315.0f || angleDeg < 45.0f) {
                section = 0;  // Front
            } else if (angleDeg < 135.0f) {
                section = 1;  // Right
            } else if (angleDeg < 225.0f) {
                section = 2;  // Back
            } else {
                section = 3;  // Left
            }

            // Track closest distance in this section
            if (sectionClosestDist[section] < 0 || distance < sectionClosestDist[section]) {
                sectionClosestDist[section] = distance;
            }
        }
    }

    // Draw proximity highlight sectors using rotated sprite
    // The sprite points up (0°/front), rotate for each sector direction
    // Section angles: 0=0°(front), 1=90°(right), 2=180°(back), 3=270°(left)
    // Skip front sector (0) - you can see ahead anyway
    for (int i = 1; i < NUM_SECTORS; ++i) {
        if (sectionClosestDist[i] < 0) continue;  // No rider in this section

        // Calculate normalized distance (0 = touching, 1 = at max alert distance)
        float normalizedDist = sectionClosestDist[i] / m_fAlertDistance;

        // Calculate color gradient: Red (close) -> Yellow (mid) -> Green (far)
        constexpr unsigned char RED_R = 0xFF, RED_G = 0x40, RED_B = 0x40;
        constexpr unsigned char YEL_R = 0xFF, YEL_G = 0xD0, YEL_B = 0x40;
        constexpr unsigned char GRN_R = 0x40, GRN_G = 0xFF, GRN_B = 0x40;

        unsigned char r, g, b;
        if (normalizedDist < 0.5f) {
            // Red to Yellow (0.0 to 0.5)
            float t = normalizedDist * 2.0f;
            r = static_cast<unsigned char>(RED_R + t * (YEL_R - RED_R));
            g = static_cast<unsigned char>(RED_G + t * (YEL_G - RED_G));
            b = static_cast<unsigned char>(RED_B + t * (YEL_B - RED_B));
        } else {
            // Yellow to Green (0.5 to 1.0)
            float t = (normalizedDist - 0.5f) * 2.0f;
            r = static_cast<unsigned char>(YEL_R + t * (GRN_R - YEL_R));
            g = static_cast<unsigned char>(YEL_G + t * (GRN_G - YEL_G));
            b = static_cast<unsigned char>(YEL_B + t * (GRN_B - YEL_B));
        }

        // Intensity affects opacity (closer = more opaque)
        float intensity = 0.4f + 0.6f * (1.0f - normalizedDist);  // 0.4 to 1.0 range
        unsigned char alpha = static_cast<unsigned char>(255 * intensity);

        // Build color using ABGR format (matching PluginUtils::makeColor)
        unsigned long sectorColor = PluginUtils::makeColor(r, g, b, alpha);

        // Section rotation angle (in radians, clockwise from up)
        float sectionAngle = (i * 90.0f) * DEG_TO_RAD;

        // Create a square quad centered on radar, sized to fit the radar
        // Sprite quad corners (relative to center, before rotation)
        float halfSize = radarRadius;
        float halfSizeX = halfSize / UI_ASPECT_RATIO;

        // Vertex order: TL, BL, BR, TR (counter-clockwise, matching setQuadPositions)
        float corners[4][2] = {
            { -halfSizeX, -halfSize },  // [0] top-left
            { -halfSizeX,  halfSize },  // [1] bottom-left
            {  halfSizeX,  halfSize },  // [2] bottom-right
            {  halfSizeX, -halfSize }   // [3] top-right
        };

        // Rotate corners around center by section angle
        float cosA = std::cos(sectionAngle);
        float sinA = std::sin(sectionAngle);

        SPluginQuad_t sector;
        for (int j = 0; j < 4; ++j) {
            // Rotate (need to account for aspect ratio when rotating)
            float rx = corners[j][0] * UI_ASPECT_RATIO;  // Convert to uniform space
            float ry = corners[j][1];
            float rotX = rx * cosA - ry * sinA;
            float rotY = rx * sinA + ry * cosA;
            rotX /= UI_ASPECT_RATIO;  // Convert back to screen space

            float px = centerX + rotX;
            float py = centerY + rotY;
            applyOffset(px, py);
            sector.m_aafPos[j][0] = px;
            sector.m_aafPos[j][1] = py;
        }

        sector.m_iSprite = AssetManager::getInstance().getSpriteIndex("radar_sector", 1);
        sector.m_ulColor = sectorColor;
        m_quads.push_back(sector);
    }

    // If no local player found, just show the radar background
    if (!localPlayer) {
        return;
    }

    // Render other riders first (player rendered last to appear on top)
    for (const auto& pos : m_riderPositions) {
        if (pos.m_iRaceNum == displayRaceNum) continue;

        const RaceEntryData* entry = pluginData.getRaceEntry(pos.m_iRaceNum);
        if (!entry) continue;

        float relX = pos.m_fPosX - playerX;
        float relZ = pos.m_fPosZ - playerZ;

        float rotatedX = relX * cosYaw - relZ * sinYaw;
        float rotatedZ = relX * sinYaw + relZ * cosYaw;

        float distance = std::sqrt(rotatedX * rotatedX + rotatedZ * rotatedZ);
        if (distance > m_fRadarRangeMeters) continue;

        // Calculate track distance fade (riders on parallel straights fade out)
        float trackFadeOpacity = 1.0f;
        float trackDist = std::abs(pos.m_fTrackPos - localPlayer->m_fTrackPos);
        if (trackDist > 0.5f) trackDist = 1.0f - trackDist;  // Handle wraparound

        float trackLength = pluginData.getSessionData().trackLength;
        if (trackLength > 0.0f) {
            // Use actual track distance in meters, tied to radar range
            float trackDistMeters = trackDist * trackLength;
            if (trackDistMeters >= m_fRadarRangeMeters) {
                continue;  // Beyond radar range on track
            }
            trackFadeOpacity = 1.0f - (trackDistMeters / m_fRadarRangeMeters);
        } else {
            // Fallback: use 5% of track as threshold if track length unknown
            constexpr float FALLBACK_THRESHOLD = 0.05f;
            if (trackDist >= FALLBACK_THRESHOLD) {
                continue;
            }
            trackFadeOpacity = 1.0f - (trackDist / FALLBACK_THRESHOLD);
        }

        float radarX = rotatedX / m_fRadarRangeMeters;
        float radarY = rotatedZ / m_fRadarRangeMeters;

        float relativeYaw = pos.m_fYaw - localPlayer->m_fYaw;
        while (relativeYaw > 180.0f) relativeYaw -= 360.0f;
        while (relativeYaw < -180.0f) relativeYaw += 360.0f;

        unsigned long riderColor;
        int trackedShape = -1;  // -1 = use global shape, 1-3 = tracked rider's shape

        // Check if rider is tracked - tracked riders use their configured color with position modulation
        const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
        const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(entry->name);

        if (trackedConfig) {
            // Tracked rider - use their configured color with position-based modulation
            unsigned long baseColor = trackedConfig->color;
            trackedShape = trackedConfig->shapeIndex;

            // Apply position-based color modulation (lighten if ahead by laps, darken if behind by laps)
            const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
            const StandingsData* riderStanding = pluginData.getStanding(pos.m_iRaceNum);
            int playerLaps = playerStanding ? playerStanding->numLaps : 0;
            int riderLaps = riderStanding ? riderStanding->numLaps : 0;
            int lapDiff = riderLaps - playerLaps;

            if (lapDiff >= 1) {
                // Rider is ahead by laps - lighten color
                baseColor = PluginUtils::lightenColor(baseColor, 0.4f);
            } else if (lapDiff <= -1) {
                // Rider is behind by laps - darken color
                baseColor = PluginUtils::darkenColor(baseColor, 0.6f);
            }

            riderColor = PluginUtils::applyOpacity(baseColor, trackFadeOpacity);
        } else if (m_riderColorMode == RiderColorMode::RELATIVE_POS) {
            // Relative position coloring: color based on position/lap relative to player
            const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
            const StandingsData* riderStanding = pluginData.getStanding(pos.m_iRaceNum);
            int playerPosition = pluginData.getPositionForRaceNum(displayRaceNum);
            int riderPosition = pluginData.getPositionForRaceNum(pos.m_iRaceNum);
            int playerLaps = playerStanding ? playerStanding->numLaps : 0;
            int riderLaps = riderStanding ? riderStanding->numLaps : 0;

            unsigned long baseColor = PluginUtils::getRelativePositionColor(
                playerPosition, riderPosition, playerLaps, riderLaps,
                ColorConfig::getInstance().getNeutral(),
                ColorConfig::getInstance().getWarning(),
                ColorConfig::getInstance().getTertiary());
            riderColor = PluginUtils::applyOpacity(baseColor, trackFadeOpacity);
        } else if (m_riderColorMode == RiderColorMode::BRAND) {
            riderColor = PluginUtils::applyOpacity(entry->bikeBrandColor, 0.75f * trackFadeOpacity);
        } else {
            riderColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getTertiary(), trackFadeOpacity);
        }

        // Render rider sprite with relative heading (pass tracked shape if available)
        renderRiderSprite(radarX, radarY, relativeYaw, riderColor,
                         centerX, centerY, radarRadius, trackedShape);

        // Render label with matching fade opacity
        int position = pluginData.getPositionForRaceNum(pos.m_iRaceNum);
        renderRiderLabel(radarX, radarY, pos.m_iRaceNum, position,
                        centerX, centerY, radarRadius, trackFadeOpacity);
    }

    // Draw the local player at center LAST (always on top, always pointing up = 0 yaw)
    if (m_bShowPlayerArrow) {
        const RaceEntryData* localEntry = pluginData.getRaceEntry(localPlayer->m_iRaceNum);
        if (localEntry) {
            unsigned long playerColor;
            int playerTrackedShape = -1;  // -1 = use global shape

            // Check if player is tracked - use their configured color and shape
            const TrackedRiderConfig* playerTrackedConfig = TrackedRidersManager::getInstance().getTrackedRider(localEntry->name);
            if (playerTrackedConfig) {
                playerColor = playerTrackedConfig->color;
                playerTrackedShape = playerTrackedConfig->shapeIndex;
            } else if (m_riderColorMode == RiderColorMode::RELATIVE_POS) {
                // Player shows green in relative position mode
                playerColor = ColorConfig::getInstance().getPositive();
            } else {
                // Otherwise use bike brand color
                playerColor = localEntry->bikeBrandColor;
            }

            renderRiderSprite(0.0f, 0.0f, 0.0f, playerColor,
                             centerX, centerY, radarRadius, playerTrackedShape);

            int playerPosition = pluginData.getPositionForRaceNum(localPlayer->m_iRaceNum);
            renderRiderLabel(0.0f, 0.0f, localPlayer->m_iRaceNum, playerPosition,
                            centerX, centerY, radarRadius, 1.0f);  // Player always fully visible
        }
    }
}

void RadarHud::setScale(float scale) {
    if (scale <= 0.0f) scale = 0.1f;
    float oldScale = m_fScale;
    if (oldScale == scale) return;

    // Calculate current dimensions
    float oldWidth = m_fBoundsRight - m_fBoundsLeft;
    float oldHeight = m_fBoundsBottom - m_fBoundsTop;

    // Calculate new dimensions (scale changes proportionally)
    float ratio = scale / oldScale;
    float newWidth = oldWidth * ratio;
    float newHeight = oldHeight * ratio;

    // Adjust offset to keep center fixed
    float deltaX = (oldWidth - newWidth) / 2.0f;
    float deltaY = (oldHeight - newHeight) / 2.0f;
    setPosition(m_fOffsetX + deltaX, m_fOffsetY + deltaY);

    // Apply the new scale
    m_fScale = scale;
    setDataDirty();
}

void RadarHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;  // No title for radar (compact display)
    setTextureVariant(1);  // Use first texture variant by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    m_fRadarRangeMeters = DEFAULT_RADAR_RANGE;
    m_riderColorMode = RiderColorMode::BRAND;  // Default to bike brand colors
    m_bShowPlayerArrow = false;  // Hide player arrow by default
    m_bFadeWhenEmpty = false;    // Don't fade by default (always visible)
    m_fAlertDistance = DEFAULT_ALERT_DISTANCE;
    m_labelMode = LabelMode::POSITION;
    m_riderShape = RiderShape::CIRCLE;  // Default to circle sprite
    m_fMarkerScale = DEFAULT_MARKER_SCALE;
    setPosition(0.43275f, 0.0111f);  // Horizontally centered at scale 1.0
    setDataDirty();
}
