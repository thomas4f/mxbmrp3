// ============================================================================
// hud/settings/settings_tab_riders.cpp
// Tab renderer for Tracked Riders settings (server players and tracked list)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../../core/plugin_utils.h"
#include "../../core/plugin_constants.h"
#include "../../core/tracked_riders_manager.h"
#include "../../core/plugin_data.h"
#include "../../core/asset_manager.h"
#include "../../core/color_config.h"
#include <algorithm>

using namespace PluginConstants;

// Member function of SettingsHud - handles click events for Riders tab
bool SettingsHud::handleClickTabRiders(const ClickRegion& region) {
    switch (region.type) {
        case ClickRegion::RIDER_ADD:
            {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().addTrackedRider(*namePtr);
                    rebuildRenderData();
                }
            }
            return true;

        case ClickRegion::RIDER_REMOVE:
            {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().removeTrackedRider(*namePtr);
                    rebuildRenderData();
                }
            }
            return true;

        case ClickRegion::RIDER_COLOR_PREV:
            {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().cycleTrackedRiderColor(*namePtr, false);
                    rebuildRenderData();
                }
            }
            return true;

        case ClickRegion::RIDER_COLOR_NEXT:
            {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().cycleTrackedRiderColor(*namePtr, true);
                    rebuildRenderData();
                }
            }
            return true;

        case ClickRegion::RIDER_SHAPE_PREV:
            {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().cycleTrackedRiderShape(*namePtr, false);
                    rebuildRenderData();
                }
            }
            return true;

        case ClickRegion::RIDER_SHAPE_NEXT:
            {
                auto* namePtr = std::get_if<std::string>(&region.targetPointer);
                if (namePtr) {
                    TrackedRidersManager::getInstance().cycleTrackedRiderShape(*namePtr, true);
                    rebuildRenderData();
                }
            }
            return true;

        case ClickRegion::SERVER_PAGE_PREV:
            if (m_serverPlayersPage > 0) {
                m_serverPlayersPage--;
                rebuildRenderData();
            }
            return true;

        case ClickRegion::SERVER_PAGE_NEXT:
            m_serverPlayersPage++;
            rebuildRenderData();
            return true;

        case ClickRegion::TRACKED_PAGE_PREV:
            if (m_trackedRidersPage > 0) {
                m_trackedRidersPage--;
                rebuildRenderData();
            }
            return true;

        case ClickRegion::TRACKED_PAGE_NEXT:
            m_trackedRidersPage++;
            rebuildRenderData();
            return true;

        default:
            return false;
    }
}

// Static member function of SettingsHud
BaseHud* SettingsHud::renderTabRiders(SettingsLayoutContext& ctx) {
    ctx.addTabTooltip("riders");

    // Tracked Riders tab - two-section layout:
    // Top: Server players grid (clickable to add)
    // Bottom: Tracked riders with icon (left=color, right=shape), hover shows remove on right
    TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
    const PluginData& pluginData = PluginData::getInstance();
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    const ColorConfig& colors = ColorConfig::getInstance();

    // Use normal font for grid content (readable size)
    float gridFontSize = ctx.fontSize;
    float gridLineHeight = ctx.lineHeightNormal;
    float gridCharWidth = charWidth;

    // Grid layout constants - 3 columns with pagination
    constexpr int SERVER_PLAYERS_PER_ROW = 3;
    constexpr int SERVER_PLAYERS_ROWS = 6;
    constexpr int SERVER_PLAYERS_PER_PAGE = SERVER_PLAYERS_PER_ROW * SERVER_PLAYERS_ROWS;  // 18 per page
    constexpr int TRACKED_PER_ROW = 3;
    constexpr int TRACKED_ROWS = 12;
    constexpr int TRACKED_PER_PAGE = TRACKED_PER_ROW * TRACKED_ROWS;  // 36 per page

    // Calculate available content width (same method as version number)
    float rightEdgeX = ctx.contentAreaStartX + ctx.panelWidth - ctx.paddingH - ctx.paddingH;
    float availableGridWidth = rightEdgeX - ctx.labelX;

    // Calculate cell dimensions based on available width
    float serverCellWidth = availableGridWidth / SERVER_PLAYERS_PER_ROW;
    float trackedCellWidth = availableGridWidth / TRACKED_PER_ROW;

    // Calculate cell chars for name truncation (cell width in chars)
    int serverCellChars = static_cast<int>(serverCellWidth / gridCharWidth);
    int trackedCellChars = static_cast<int>(trackedCellWidth / gridCharWidth);

    // Server cell format: "#123 Name" - race num takes 5 chars, 1 char buffer, rest for name
    int serverNameChars = serverCellChars - 6;  // 5 = "#" + 3 digits + space, +1 buffer
    if (serverNameChars < 5) serverNameChars = 5;  // Minimum name length

    // Tracked cell format: "[ico] Name-" - icon takes 3 chars, remove takes 2, 1 char buffer
    int trackedNameChars = trackedCellChars - 6;  // 3 for icon, 2 for remove, 1 buffer
    if (trackedNameChars < 5) trackedNameChars = 5;  // Minimum name length

    float cellHeight = gridLineHeight;

    // Helper lambda to render pagination controls (reduces duplication)
    // Returns the updated Y position after rendering
    auto renderPagination = [&](float& y, int currentPage, int totalPages,
                                SettingsHud::ClickRegion::Type prevType,
                                SettingsHud::ClickRegion::Type nextType) {
        if (totalPages <= 1) return;

        y += ctx.lineHeightNormal * 0.5f;  // Gap before pagination
        char pageText[16];
        snprintf(pageText, sizeof(pageText), "Page %d/%d", currentPage + 1, totalPages);
        float pageTextWidth = PluginUtils::calculateMonospaceTextWidth(
            static_cast<int>(strlen(pageText)), gridFontSize);

        // Position pagination at right edge
        // Format: "< Page x/y >" with spaces around arrows
        float paginationTotalWidth = gridCharWidth * 2 + pageTextWidth + gridCharWidth * 2;
        float paginationX = rightEdgeX - paginationTotalWidth;

        // "< " button
        ctx.parent->addString("< ", paginationX, y, Justify::LEFT, Fonts::getNormal(),
                     colors.getAccent(), gridFontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(paginationX, y, gridCharWidth * 2, cellHeight,
            prevType, nullptr, 0, false, 0));
        paginationX += gridCharWidth * 2;

        // Page text
        ctx.parent->addString(pageText, paginationX, y, Justify::LEFT, Fonts::getNormal(),
                     colors.getSecondary(), gridFontSize);
        paginationX += pageTextWidth;

        // " >" button
        ctx.parent->addString(" >", paginationX, y, Justify::LEFT, Fonts::getNormal(),
                     colors.getAccent(), gridFontSize);
        ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(paginationX, y, gridCharWidth * 2, cellHeight,
            nextType, nullptr, 0, false, 0));

        y += ctx.lineHeightNormal;
    };

    // =====================================================
    // SECTION 1: Server Players Grid
    // =====================================================
    ctx.parent->addString("Server Players", ctx.labelX, ctx.currentY, Justify::LEFT,
        Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("(click to track/untrack)", ctx.labelX + charWidth * 16, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getMuted(), ctx.fontSize * 0.9f);
    ctx.currentY += ctx.lineHeightNormal;

    // Get all race entries and build display list
    const auto& raceEntries = pluginData.getRaceEntries();
    std::vector<const RaceEntryData*> serverPlayers;
    for (const auto& pair : raceEntries) {
        serverPlayers.push_back(&pair.second);
    }

    // Sort by race number
    std::sort(serverPlayers.begin(), serverPlayers.end(),
        [](const RaceEntryData* a, const RaceEntryData* b) {
            return a->raceNum < b->raceNum;
        });

    // Calculate total server players and pagination
    int totalServerPlayers = static_cast<int>(serverPlayers.size());
    int serverTotalPages = (totalServerPlayers + SERVER_PLAYERS_PER_PAGE - 1) / SERVER_PLAYERS_PER_PAGE;
    if (serverTotalPages < 1) serverTotalPages = 1;
    if (ctx.parent->m_serverPlayersPage >= serverTotalPages) ctx.parent->m_serverPlayersPage = serverTotalPages - 1;
    if (ctx.parent->m_serverPlayersPage < 0) ctx.parent->m_serverPlayersPage = 0;
    int serverStartIndex = ctx.parent->m_serverPlayersPage * SERVER_PLAYERS_PER_PAGE;

    // Render server players grid (current page only)
    float serverGridStartY = ctx.currentY;
    for (int row = 0; row < SERVER_PLAYERS_ROWS; row++) {
        float rowY = serverGridStartY + row * cellHeight;
        for (int col = 0; col < SERVER_PLAYERS_PER_ROW; col++) {
            int playerIndex = serverStartIndex + row * SERVER_PLAYERS_PER_ROW + col;
            if (playerIndex >= totalServerPlayers) break;

            float cellX = ctx.labelX + col * serverCellWidth;
            const RaceEntryData* player = serverPlayers[playerIndex];
            bool isTracked = trackedMgr.isTracked(player->name);

            // Format: "#123 Name" (dynamic width based on available space)
            char cellText[48];
            snprintf(cellText, sizeof(cellText), "#%-3d %-*.*s", player->raceNum, serverNameChars, serverNameChars, player->name);

            unsigned long textColor = isTracked ? colors.getPositive() : colors.getSecondary();
            ctx.parent->addString(cellText, cellX, rowY, Justify::LEFT,
                Fonts::getNormal(), textColor, gridFontSize);

            // Click region to add/remove tracking
            if (isTracked) {
                ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                    cellX, rowY, serverCellWidth, cellHeight,
                    SettingsHud::ClickRegion::RIDER_REMOVE, std::string(player->name)
                ));
            } else {
                ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                    cellX, rowY, serverCellWidth, cellHeight,
                    SettingsHud::ClickRegion::RIDER_ADD, std::string(player->name)
                ));
            }
        }
    }
    ctx.currentY = serverGridStartY + SERVER_PLAYERS_ROWS * cellHeight;

    // Server pagination
    renderPagination(ctx.currentY, ctx.parent->m_serverPlayersPage, serverTotalPages,
                    SettingsHud::ClickRegion::SERVER_PAGE_PREV, SettingsHud::ClickRegion::SERVER_PAGE_NEXT);

    ctx.currentY += ctx.lineHeightNormal * 0.3f;

    // =====================================================
    // SECTION 2: Tracked Riders Grid
    // =====================================================
    ctx.parent->addString("Tracked Riders", ctx.labelX, ctx.currentY, Justify::LEFT,
        Fonts::getStrong(), colors.getPrimary(), ctx.fontSize);
    ctx.parent->addString("(L-click: color, R-click: icon)", ctx.labelX + charWidth * 16, ctx.currentY, Justify::LEFT,
        Fonts::getNormal(), colors.getMuted(), ctx.fontSize * 0.9f);
    ctx.currentY += ctx.lineHeightNormal;

    // Get tracked riders
    const auto& allTracked = trackedMgr.getAllTrackedRiders();
    std::vector<const TrackedRiderConfig*> trackedList;
    for (const auto& pair : allTracked) {
        trackedList.push_back(&pair.second);
    }

    // Sort tracked by name
    std::sort(trackedList.begin(), trackedList.end(),
        [](const TrackedRiderConfig* a, const TrackedRiderConfig* b) {
            return a->name < b->name;
        });

    // Calculate total tracked riders and pagination
    int totalTrackedRiders = static_cast<int>(trackedList.size());
    int trackedTotalPages = (totalTrackedRiders + TRACKED_PER_PAGE - 1) / TRACKED_PER_PAGE;
    if (trackedTotalPages < 1) trackedTotalPages = 1;
    if (ctx.parent->m_trackedRidersPage >= trackedTotalPages) ctx.parent->m_trackedRidersPage = trackedTotalPages - 1;
    if (ctx.parent->m_trackedRidersPage < 0) ctx.parent->m_trackedRidersPage = 0;
    int trackedStartIndex = ctx.parent->m_trackedRidersPage * TRACKED_PER_PAGE;

    // Store layout info for hover tracking
    ctx.parent->m_trackedRidersStartY = ctx.currentY;
    ctx.parent->m_trackedRidersStartX = ctx.labelX;
    ctx.parent->m_trackedRidersCellHeight = cellHeight;
    ctx.parent->m_trackedRidersCellWidth = trackedCellWidth;
    ctx.parent->m_trackedRidersPerRow = TRACKED_PER_ROW;

    // Sprite sizing - match StandingsHud icon size (0.006f base)
    constexpr float baseConeSize = 0.006f;
    float baseHalfSize = baseConeSize;  // Same as StandingsHud

    // Render tracked riders grid (current page only)
    float trackedGridStartY = ctx.currentY;
    for (int row = 0; row < TRACKED_ROWS; row++) {
        float rowY = trackedGridStartY + row * cellHeight;
        for (int col = 0; col < TRACKED_PER_ROW; col++) {
            int trackedIndex = trackedStartIndex + row * TRACKED_PER_ROW + col;
            if (trackedIndex >= totalTrackedRiders) break;

            float cellX = ctx.labelX + col * trackedCellWidth;
            const TrackedRiderConfig* config = trackedList[trackedIndex];
            const std::string& riderName = config->name;
            unsigned long riderColor = config->color;
            int shapeIndex = config->shapeIndex;

            // Adjust hover index for pagination
            int displayIndex = trackedIndex - trackedStartIndex;
            bool isHovered = (displayIndex == ctx.parent->m_hoveredTrackedRiderIndex);

            float x = cellX;

            // Icon sprite (clickable for color on left-click, icon on right-click)
            {
                float spriteHalfSize = baseHalfSize;
                int spriteIndex = AssetManager::getInstance().getFirstIconSpriteIndex() + shapeIndex - 1;

                float spriteCenterX = x + gridCharWidth * 1.5f;  // Center icon in 3-char space
                float spriteCenterY = rowY + cellHeight * 0.5f;
                float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;

                SPluginQuad_t sprite;
                float sx = spriteCenterX, sy = spriteCenterY;
                ctx.parent->applyOffset(sx, sy);
                sprite.m_aafPos[0][0] = sx - spriteHalfWidth;
                sprite.m_aafPos[0][1] = sy - spriteHalfSize;
                sprite.m_aafPos[1][0] = sx - spriteHalfWidth;
                sprite.m_aafPos[1][1] = sy + spriteHalfSize;
                sprite.m_aafPos[2][0] = sx + spriteHalfWidth;
                sprite.m_aafPos[2][1] = sy + spriteHalfSize;
                sprite.m_aafPos[3][0] = sx + spriteHalfWidth;
                sprite.m_aafPos[3][1] = sy - spriteHalfSize;
                sprite.m_iSprite = spriteIndex;
                sprite.m_ulColor = riderColor;
                ctx.parent->m_quads.push_back(sprite);

                // Click region for color cycling (left-click) and shape cycling (right-click)
                // Covers icon + name area (icon3 + nameChars = trackedCellChars - 2 for remove)
                ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                    x, rowY, gridCharWidth * (3 + trackedNameChars), cellHeight,
                    SettingsHud::ClickRegion::RIDER_COLOR_NEXT, riderName
                ));
            }
            x += gridCharWidth * 3;  // Space for icon (3 chars)

            // Name (dynamic width based on available space)
            char truncName[48];
            snprintf(truncName, sizeof(truncName), "%-*.*s", trackedNameChars, trackedNameChars, riderName.c_str());
            ctx.parent->addString(truncName, x, rowY, Justify::LEFT,
                Fonts::getNormal(), riderColor, gridFontSize);

            // Remove "x" only shown on hover, fixed at right edge of cell
            if (isHovered) {
                float removeX = cellX + trackedCellWidth - gridCharWidth * 2;
                ctx.parent->addString("x", removeX, rowY, Justify::LEFT,
                    Fonts::getNormal(), colors.getNegative(), gridFontSize);
                ctx.parent->m_clickRegions.push_back(SettingsHud::ClickRegion(
                    removeX, rowY, gridCharWidth * 2, cellHeight,
                    SettingsHud::ClickRegion::RIDER_REMOVE, riderName
                ));
            }
        }
    }
    ctx.currentY = trackedGridStartY + TRACKED_ROWS * cellHeight;

    // Tracked pagination
    renderPagination(ctx.currentY, ctx.parent->m_trackedRidersPage, trackedTotalPages,
                    SettingsHud::ClickRegion::TRACKED_PAGE_PREV, SettingsHud::ClickRegion::TRACKED_PAGE_NEXT);

    // Info text
    ctx.currentY += ctx.lineHeightNormal * 0.5f;
    ctx.parent->addString("Tracked riders are saved to mxbmrp3_tracked_riders.json", ctx.labelX, ctx.currentY,
        Justify::LEFT, Fonts::getNormal(), colors.getMuted(), ctx.fontSize * 0.9f);

    // No active HUD for riders settings
    return nullptr;
}
