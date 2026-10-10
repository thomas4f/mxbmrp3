// ============================================================================
// hud/settings/settings_tab_riders.cpp
// Tab renderer for Tracked Riders settings (server players and tracked list)
// ============================================================================
#include "settings_layout.h"
#include "../settings_hud.h"
#include "../standings_hud.h"
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
    // Bottom: Tracked riders, each with colour and icon dropdowns; hover shows remove on right
    TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
    const PluginData& pluginData = PluginData::getInstance();
    float charWidth = PluginUtils::calculateMonospaceTextWidth(1, ctx.fontSize);
    const ColorConfig& colors = ColorConfig::getInstance();

    // Use normal font for grid content (readable size)
    float gridLineHeight = ctx.lineHeightNormal;
    float gridFontSize = ctx.fontSize;
    float gridCharWidth = charWidth;

    // Grid layout constants - 3 columns with pagination.
    // Row counts keep this tab under the panel height the other tabs set (the
    // Widgets table binds it): a full page of either list used to make Riders the
    // tallest tab.
    constexpr int SERVER_PLAYERS_PER_ROW = 3;
    constexpr int SERVER_PLAYERS_ROWS = 8;
    constexpr int SERVER_PLAYERS_PER_PAGE = SERVER_PLAYERS_PER_ROW * SERVER_PLAYERS_ROWS;  // 24 per page
    constexpr int TRACKED_PER_ROW = 2;   // two: room for a whole name after the dropdowns
    constexpr int TRACKED_ROWS = 13;
    constexpr int TRACKED_PER_PAGE = TRACKED_PER_ROW * TRACKED_ROWS;  // 26 per page

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

    // Tracked cell format: "[colour v] [icon v] Name x" - two swatch dropdowns of
    // SWATCH_BOX_CHARS with a char after each, remove takes 2, 1 char buffer
    constexpr int SWATCH_BOX_CHARS = 3;
    int trackedNameChars = trackedCellChars - 2 * (SWATCH_BOX_CHARS + 1) - 3;
    if (trackedNameChars < 5) trackedNameChars = 5;  // Minimum name length

    float cellHeight = gridLineHeight;

    // =====================================================
    // SECTION 1: Server Players Grid
    // =====================================================
    ctx.addSectionHeading("Server Players", "(click to track/untrack)");

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
    if (serverTotalPages > 1) ctx.addSpacing();   // the junction above a button row
    ctx.addPager(ctx.parent->m_serverPlayersPage, serverTotalPages,
                 SettingsHud::ClickRegion::SERVER_PAGE_PREV, SettingsHud::ClickRegion::SERVER_PAGE_NEXT);


    // =====================================================
    // SECTION 2: Tracked Riders Grid
    // =====================================================
    ctx.addSectionHeading("Tracked Riders");

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

            // Two swatch dropdowns ahead of the name: the rider's colour, then its
            // icon (in that colour), each a box with a caret opening its list --
            // the Appearance tab's colour cells, small.
            {
                const int colorIdx = static_cast<int>(ctx.parent->m_cycleControls.size());
                ctx.parent->m_cycleControls.push_back(paletteCycle(
                    [riderName]() {
                        const TrackedRiderConfig* r = TrackedRidersManager::getInstance().getTrackedRider(riderName);
                        return r ? r->color : 0UL;
                    },
                    [riderName](unsigned long c) { TrackedRidersManager::getInstance().setTrackedRiderColor(riderName, c); },
                    nullptr));
                const int shapeIdx = static_cast<int>(ctx.parent->m_cycleControls.size());
                ctx.parent->m_cycleControls.push_back(iconCycle(
                    [riderName]() {
                        const TrackedRiderConfig* r = TrackedRidersManager::getInstance().getTrackedRider(riderName);
                        return r ? r->shapeIndex : 1;
                    },
                    [riderName](int v) { TrackedRidersManager::getInstance().setTrackedRiderShape(riderName, v); },
                    nullptr, /*allowOff=*/false));

                const float boxW = gridCharWidth * SWATCH_BOX_CHARS;
                const float swatchH = cellHeight * 0.5f;
                ctx.addDropdownBox(x, rowY, boxW, colorIdx, true, "riders.color");
                ctx.addSolidQuad(x + gridCharWidth * 0.1f, rowY + (cellHeight - swatchH) * 0.5f, gridCharWidth, swatchH, riderColor);
                x += boxW + gridCharWidth;

                // Sprite sizing - match StandingsHud icon size (0.006f base)
                ctx.addDropdownBox(x, rowY, boxW, shapeIdx, true, "riders.icon");
                const float spriteHalfSize = baseHalfSize;
                const int spriteIndex = AssetManager::getInstance().iconSpriteForShape(shapeIndex);
                const float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
                float sx = x + gridCharWidth * 0.6f, sy = rowY + cellHeight * 0.5f;
                ctx.parent->applyOffset(sx, sy);
                SPluginQuad_t sprite;
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
                x += boxW + gridCharWidth;
            }

            // Name (dynamic width based on available space)
            char truncName[48];
            snprintf(truncName, sizeof(truncName), "%-*.*s", trackedNameChars, trackedNameChars, riderName.c_str());
            ctx.parent->addString(truncName, x, rowY, Justify::LEFT,
                Fonts::getNormal(), colors.getSecondary(), gridFontSize);

            // Remove "x" only shown on hover, a space after the name
            if (isHovered) {
                const int shownChars = std::min(static_cast<int>(riderName.size()), trackedNameChars);
                float removeX = x + gridCharWidth * static_cast<float>(shownChars + 1);
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
    if (trackedTotalPages > 1) ctx.addSpacing();
    ctx.addPager(ctx.parent->m_trackedRidersPage, trackedTotalPages,
                 SettingsHud::ClickRegion::TRACKED_PAGE_PREV, SettingsHud::ClickRegion::TRACKED_PAGE_NEXT);

    // No active HUD for riders settings
    return nullptr;
}
