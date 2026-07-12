// ============================================================================
// mxbmrp3/hud/map_hud_riders.cpp
// Rider icon/label rendering
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

        // The local player's own marker (and its label below) render a touch larger than
        // the pack so it's easy to pick out at a glance — the accent colour alone can be
        // hard to spot on a busy map.
        constexpr float PLAYER_MARKER_BOOST = 1.35f;
        const float playerBoost = isLocalPlayer ? PLAYER_MARKER_BOOST : 1.0f;

        // Render sprite quad centered on rider position, rotated to match heading
        float spriteHalfSize = scaledConeSize * playerBoost;

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
            // Scale font size by marker scale (and the local-player boost, so the player's
            // label grows in step with their enlarged marker).
            float labelFontSize = dim.fontSizeSmall * m_fMarkerScale * playerBoost;

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

                // Render the label with the standard drop shadow (single bottom-right
                // offset from [Display] dropShadowOffsetX/Y, honoring the global
                // toggle) — same convention as every other HUD text, applied by
                // HudManager::collectRenderData for non-skipped strings. (Map rider
                // ICONS stay separate sprite quads with their own baked outlines and
                // never take the drop shadow — this is only the text label.)
                addString(labelStr, labelX, labelY, labelJustify,
                         this->getFont(FontCategory::SMALL), labelColor, labelFontSize, false);
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
