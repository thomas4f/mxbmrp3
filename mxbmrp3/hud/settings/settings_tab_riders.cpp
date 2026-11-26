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

    // Grid layout - two scrolling lists (scroll_view.h), each a fixed number of
    // rows tall. The row counts keep this tab under the panel height the other
    // tabs set (the Widgets table binds it).
    constexpr int SERVER_PLAYERS_PER_ROW = 3;
    constexpr int SERVER_PLAYERS_ROWS = 8;
    constexpr int TRACKED_PER_ROW = 2;   // two: room for a whole name after the dropdowns
    constexpr int TRACKED_ROWS = 13;

    // Cell widths share the row span (the scrollbars sit in the panel's padding).
    ctx.addSectionHeading(TR("Server Players"), TR("(click to track/untrack)"));
    ctx.beginScroll(ScrollView::SERVER_PLAYERS, static_cast<float>(SERVER_PLAYERS_ROWS));
    float availableGridWidth = ctx.rowSpanWidth();

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
    // SECTION 1: Server Players Grid (its heading and list opened above)
    // =====================================================
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

    // Every player, a row of three at a time; the list draws the rows in view.
    int totalServerPlayers = static_cast<int>(serverPlayers.size());
    const int serverRows = (totalServerPlayers + SERVER_PLAYERS_PER_ROW - 1) / SERVER_PLAYERS_PER_ROW;
    for (int row = 0; row < serverRows; row++) {
        if (!ctx.scrollRow(1.0f)) continue;
        const float rowY = ctx.currentY;
        for (int col = 0; col < SERVER_PLAYERS_PER_ROW; col++) {
            int playerIndex = row * SERVER_PLAYERS_PER_ROW + col;
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
        ctx.currentY += cellHeight;
    }
    ctx.endScroll();


    // =====================================================
    // SECTION 2: Tracked Riders Grid
    // =====================================================
    ctx.addSectionHeading(TR("Tracked Riders"));

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

    int totalTrackedRiders = static_cast<int>(trackedList.size());
    ctx.beginScroll(ScrollView::TRACKED_RIDERS, static_cast<float>(TRACKED_ROWS));

    // Store layout info for hover tracking: where row 0 is DRAWN, so the
    // hovered cell is a list index wherever the list stands.
    ctx.parent->m_trackedRidersStartY =
        ctx.currentY - ctx.parent->m_scrolls[ScrollView::TRACKED_RIDERS].shown * ctx.lineHeightNormal;
    ctx.parent->m_trackedRidersStartX = ctx.labelX;
    ctx.parent->m_trackedRidersCellHeight = cellHeight;
    ctx.parent->m_trackedRidersCellWidth = trackedCellWidth;
    ctx.parent->m_trackedRidersPerRow = TRACKED_PER_ROW;

    // Sprite sizing - match StandingsHud icon size (0.006f base)
    constexpr float baseConeSize = 0.006f;
    float baseHalfSize = baseConeSize;  // Same as StandingsHud

    // Every tracked rider, two to a row; the list draws the rows in view.
    const int trackedRows = (totalTrackedRiders + TRACKED_PER_ROW - 1) / TRACKED_PER_ROW;
    for (int row = 0; row < trackedRows; row++) {
        if (!ctx.scrollRow(1.0f)) continue;
        const float rowY = ctx.currentY;
        for (int col = 0; col < TRACKED_PER_ROW; col++) {
            int trackedIndex = row * TRACKED_PER_ROW + col;
            if (trackedIndex >= totalTrackedRiders) break;

            float cellX = ctx.labelX + col * trackedCellWidth;
            const TrackedRiderConfig* config = trackedList[trackedIndex];
            const std::string& riderName = config->name;
            unsigned long riderColor = config->color;
            int shapeIndex = config->shapeIndex;

            bool isHovered = (trackedIndex == ctx.parent->m_hoveredTrackedRiderIndex);

            float x = cellX;

            // Two swatch dropdowns ahead of the name: the rider's colour, then its
            // icon (in that colour), each a box with a caret opening its list --
            // the Appearance tab's colour cells, small.
            {
                const int colorIdx = static_cast<int>(ctx.parent->m_cycleControls.size());
                ctx.parent->m_cycleControls.push_back(paletteCycle(
                    [riderName]() {
                        const TrackedRiderConfig* r = TrackedRidersManager::getInstance().getTrackedRider(riderName.c_str());
                        return r ? r->color : 0UL;
                    },
                    [riderName](unsigned long c) { TrackedRidersManager::getInstance().setTrackedRiderColor(riderName, c); },
                    nullptr));
                const int shapeIdx = static_cast<int>(ctx.parent->m_cycleControls.size());
                ctx.parent->m_cycleControls.push_back(iconCycle(
                    [riderName]() {
                        const TrackedRiderConfig* r = TrackedRidersManager::getInstance().getTrackedRider(riderName.c_str());
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
        ctx.currentY += cellHeight;
    }
    ctx.endScroll();

    // No active HUD for riders settings
    return nullptr;
}
