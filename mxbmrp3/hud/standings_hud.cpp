// ============================================================================
// hud/standings_hud.cpp
// Displays race standings and lap times with position, gaps, and rider information
// ============================================================================
#include "standings_hud.h"
#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/color_config.h"
#include "../core/input_manager.h"
#include "../core/plugin_manager.h"
#include "../core/tracked_riders_manager.h"
#include "../core/asset_manager.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

using namespace PluginConstants;

StandingsHud::ColumnPositions::ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns) {
    float scaledFontSize = FontSizes::NORMAL * scale;
    float current = contentStartX;

    // Use helper function to set column positions (eliminates duplicated lambda)
    // All columns (race mode everywhere)
    PluginUtils::setColumnPosition(enabledColumns, COL_TRACKED, COL_TRACKED_WIDTH, scaledFontSize, current, tracked);
    PluginUtils::setColumnPosition(enabledColumns, COL_POS, COL_POS_WIDTH, scaledFontSize, current, pos);
    PluginUtils::setColumnPosition(enabledColumns, COL_RACENUM, COL_RACENUM_WIDTH, scaledFontSize, current, raceNum);
    PluginUtils::setColumnPosition(enabledColumns, COL_NAME, COL_NAME_WIDTH, scaledFontSize, current, name);
    PluginUtils::setColumnPosition(enabledColumns, COL_BIKE, COL_BIKE_WIDTH, scaledFontSize, current, bike);
    PluginUtils::setColumnPosition(enabledColumns, COL_STATUS, COL_STATUS_WIDTH, scaledFontSize, current, status);
    PluginUtils::setColumnPosition(enabledColumns, COL_PENALTY, COL_PENALTY_WIDTH, scaledFontSize, current, penalty);
    PluginUtils::setColumnPosition(enabledColumns, COL_BEST_LAP, COL_BEST_LAP_WIDTH, scaledFontSize, current, bestLap);
    PluginUtils::setColumnPosition(enabledColumns, COL_OFFICIAL_GAP, COL_OFFICIAL_GAP_WIDTH, scaledFontSize, current, officialGap);
    PluginUtils::setColumnPosition(enabledColumns, COL_LIVE_GAP, COL_LIVE_GAP_WIDTH, scaledFontSize, current, liveGap);
    PluginUtils::setColumnPosition(enabledColumns, COL_DEBUG, COL_DEBUG_WIDTH, scaledFontSize, current, debug);
}

StandingsHud::DisplayEntry StandingsHud::DisplayEntry::fromRaceEntry(const RaceEntryData& entry, const StandingsData* standings) {

    DisplayEntry result;
    result.raceNum = entry.raceNum;

    if (standings) {
        result.officialGap = standings->gap;
        result.gapLaps = standings->gapLaps;
        result.realTimeGap = standings->realTimeGap;
        result.penalty = standings->penalty;
        result.state = standings->state;
        result.pit = standings->pit;
        result.numLaps = standings->numLaps;
        result.bestLap = standings->bestLap;
    }

    // Copy pre-truncated name
    strcpy_s(result.name, sizeof(result.name), entry.truncatedName);

    // Use cached abbreviation pointer
    strncpy_s(result.bikeShortName, sizeof(result.bikeShortName),
        entry.bikeAbbr, sizeof(result.bikeShortName) - 1);
    result.bikeShortName[sizeof(result.bikeShortName) - 1] = '\0';

    // Copy pre-computed bike brand color
    result.bikeBrandColor = entry.bikeBrandColor;

    // Copy pre-formatted race number
    strcpy_s(result.formattedRaceNum, sizeof(result.formattedRaceNum), entry.formattedRaceNum);

    return result;
}

void StandingsHud::formatStatus(DisplayEntry& entry, const SessionData& sessionData) const {
    entry.isFinishedRace = false;
    const char* stateAbbr = PluginUtils::getRiderStateAbbreviation(entry.state);

    if (stateAbbr[0] != '\0') {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), stateAbbr);
    }
    else if (sessionData.isRiderFinished(entry.numLaps)) {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), "FIN");
        entry.isFinishedRace = true;
    }
    else if (entry.pit == 1) {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), "PIT");
    }
    else if (sessionData.isRiderOnLastLap(entry.numLaps)) {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), "LL");
    }
    else {
        snprintf(entry.formattedStatus, sizeof(entry.formattedStatus), "L%d", entry.numLaps + 1);
    }
}

void StandingsHud::renderRiderRow(const DisplayEntry& entry, bool isPlaceholder, float currentY, const ScaledDimensions& dim) {

    const char* placeholder = Placeholders::GENERIC;
    const char* lapTimePlaceholder = Placeholders::LAP_TIME;

    // Determine text color
    const ColorConfig& colors = ColorConfig::getInstance();
    unsigned long textColor = colors.getPrimary();
    unsigned long mutedColor = colors.getMuted();
    if (!isPlaceholder && entry.isGapRow) {
        if (entry.isGapInverted) {
            // Inverted positions on track vs classification - use warning color
            textColor = colors.getWarning();
        } else {
            // Gap rows: red for rider ahead (you're losing), green for rider behind (you're gaining)
            textColor = entry.isGapToRiderAhead ? colors.getNegative() : colors.getPositive();
        }
    } else if (!isPlaceholder && !entry.isGapRow) {
        // Regular riders: use muted for DNS/DSQ/RETIRED
        using namespace PluginConstants::RiderState;
        if (entry.state == DNS || entry.state == DSQ || entry.state == RETIRED) {
            textColor = colors.getMuted();
        }
    }

    // Table-driven rendering - loop only through enabled columns
    for (const auto& col : m_columnTable) {
        // Special handling for TRACKED column (columnIndex 0) - render sprite instead of text
        if (col.columnIndex == 0) {
            // Only render tracked indicator for non-placeholder, non-gap rows with valid race number
            if (!isPlaceholder && !entry.isGapRow && entry.raceNum > 0) {
                const PluginData& pluginData = PluginData::getInstance();
                const RaceEntryData* raceEntry = pluginData.getRaceEntry(entry.raceNum);
                if (raceEntry) {
                    const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
                    const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(raceEntry->name);
                    if (trackedConfig) {
                        // Render tracked rider icon sprite
                        // Use same base size constant as map (0.006f)
                        constexpr float baseConeSize = 0.006f;
                        float baseHalfSize = baseConeSize * m_fScale;  // Match map exactly
                        float baseCenterOffset = baseHalfSize;  // Fixed center offset for positioning
                        float spriteHalfSize = baseHalfSize;  // Visual half size (will be scaled per shape)

                        // Convert shapeIndex to sprite index (dynamically assigned)
                        int spriteIndex = AssetManager::getInstance().getFirstIconSpriteIndex() + trackedConfig->shapeIndex - 1;
                        // All icons use uniform baseline scale

                        // Create sprite quad centered vertically on the row
                        // Use baseCenterOffset for consistent positioning across all shapes
                        float spriteCenterX = col.position + baseCenterOffset;
                        float spriteCenterY = currentY + dim.lineHeightNormal * 0.5f;
                        float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;

                        SPluginQuad_t sprite;
                        float x = spriteCenterX, y = spriteCenterY;
                        applyOffset(x, y);
                        sprite.m_aafPos[0][0] = x - spriteHalfWidth;  // Top-left
                        sprite.m_aafPos[0][1] = y - spriteHalfSize;
                        sprite.m_aafPos[1][0] = x - spriteHalfWidth;  // Bottom-left
                        sprite.m_aafPos[1][1] = y + spriteHalfSize;
                        sprite.m_aafPos[2][0] = x + spriteHalfWidth;  // Bottom-right
                        sprite.m_aafPos[2][1] = y + spriteHalfSize;
                        sprite.m_aafPos[3][0] = x + spriteHalfWidth;  // Top-right
                        sprite.m_aafPos[3][1] = y - spriteHalfSize;
                        sprite.m_iSprite = spriteIndex;
                        sprite.m_ulColor = trackedConfig->color;
                        m_quads.push_back(sprite);
                    }
                }
            }
            continue;  // Skip text rendering for tracked column
        }

        const char* text;

        if (isPlaceholder) {
            text = col.useEmptyForPlaceholder ? "" : placeholder;
        } else if (entry.isGapRow) {
            // Gap rows show text in official gap, live gap, and debug columns
            if (col.columnIndex == 8) {  // OFFICIAL_GAP column
                text = entry.formattedOfficialGap;
            } else if (col.columnIndex == 9) {  // LIVE_GAP column
                text = entry.formattedLiveGap;
            } else if (col.columnIndex == 10) {  // DEBUG column
                text = entry.formattedDebug;
            } else {
                text = "";
            }
        } else {
            // Select data field based on column index
            // 0=TRACKED (handled above), 1=POS, 2=RACENUM, 3=NAME, 4=BIKE, 5=STATUS, 6=PENALTY, 7=BEST_LAP, 8=OFFICIAL_GAP, 9=LIVE_GAP, 10=DEBUG
            switch (col.columnIndex) {
                case 1: text = entry.formattedPosition; break;
                case 2: text = entry.formattedRaceNum; break;
                case 3: text = entry.name; break;
                case 4: text = entry.bikeShortName; break;
                case 5: text = entry.formattedStatus; break;
                case 6: text = entry.formattedPenalty; break;
                case 7: text = entry.formattedLapTime; break;
                case 8: text = entry.formattedOfficialGap; break;
                case 9: text = entry.formattedLiveGap; break;
                case 10: text = entry.formattedDebug; break;
                default: text = ""; break;
            }
        }

        // Use podium colors for position column (P1/P2/P3), secondary for others
        unsigned long columnColor = textColor;
        if (col.columnIndex == 1 && !isPlaceholder && !entry.isGapRow && entry.position > 0) {
            if (entry.position == Position::FIRST) {
                columnColor = PodiumColors::GOLD;
            } else if (entry.position == Position::SECOND) {
                columnColor = PodiumColors::SILVER;
            } else if (entry.position == Position::THIRD) {
                columnColor = PodiumColors::BRONZE;
            } else {
                columnColor = colors.getSecondary();
            }
        }
        // Race number and bike columns use tertiary color
        if ((col.columnIndex == 2 || col.columnIndex == 4) && !isPlaceholder && !entry.isGapRow) {
            columnColor = colors.getTertiary();
        }

        // Use muted color for placeholder values
        if (strcmp(text, placeholder) == 0 || strcmp(text, lapTimePlaceholder) == 0 ||
            strcmp(text, Placeholders::NOT_AVAILABLE) == 0) {
            columnColor = mutedColor;
        }

        addString(text, col.position, currentY, static_cast<int>(col.justify), Fonts::getNormal(), columnColor, dim.fontSize);
    }
}

void StandingsHud::DisplayEntry::updateFormattedStrings() {
    hasBestLap = (bestLap > 0);

    if (position > 0) {
        snprintf(formattedPosition, sizeof(formattedPosition), "P%d", position);
    }
    else {
        strcpy_s(formattedPosition, sizeof(formattedPosition), Placeholders::GENERIC);
    }
}

bool StandingsHud::shouldShowGapForMode(GapMode mode, bool isPlayerRow) const {
    return mode != GapMode::PLAYER || isPlayerRow;
}

void StandingsHud::addDisplayEntries(int startIdx, int endIdx, int positionBase,
                                     const std::vector<int>& classificationOrder, const PluginData& pluginData) {
    const PluginData& pd = PluginData::getInstance();
    int displayRaceNum = pd.getDisplayRaceNum();

    for (int i = startIdx; i <= endIdx && i < static_cast<int>(classificationOrder.size()); ++i) {
        int raceNum = classificationOrder[i];
        const RaceEntryData* entry = pluginData.getRaceEntry(raceNum);
        const StandingsData* standing = pluginData.getStanding(raceNum);

        if (entry) {
            if (raceNum == displayRaceNum) {
                m_cachedPlayerIndex = static_cast<int>(m_displayEntries.size());
            }

            DisplayEntry displayEntry = DisplayEntry::fromRaceEntry(*entry, standing);
            displayEntry.position = positionBase + (i - startIdx);
            m_displayEntries.push_back(displayEntry);
        }
    }
}

StandingsHud::DisplayEntry StandingsHud::buildGapRow(int displayRaceNum, int neighborRaceNum, bool isGapToRiderAhead,
                                                      int currentElapsedTime, const PluginData& pluginData) {
    DisplayEntry gapRow;
    gapRow.isGapRow = true;
    gapRow.isGapToRiderAhead = isGapToRiderAhead;

    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    const StandingsData* neighborStanding = pluginData.getStanding(neighborRaceNum);

    // Check if gap should be visible (display rider or neighbor must have recent update)
    bool shouldShowGap = false;
    auto playerUpdateIt = m_lastOfficialGapUpdateTime.find(displayRaceNum);
    auto neighborUpdateIt = m_lastOfficialGapUpdateTime.find(neighborRaceNum);

    if (playerUpdateIt != m_lastOfficialGapUpdateTime.end()) {
        int timeSinceUpdate = currentElapsedTime - playerUpdateIt->second;
        shouldShowGap = shouldShowGap || (timeSinceUpdate >= 0 && timeSinceUpdate < OFFICIAL_GAP_DISPLAY_DURATION_MS);
    }
    if (neighborUpdateIt != m_lastOfficialGapUpdateTime.end()) {
        int timeSinceUpdate = currentElapsedTime - neighborUpdateIt->second;
        shouldShowGap = shouldShowGap || (timeSinceUpdate >= 0 && timeSinceUpdate < OFFICIAL_GAP_DISPLAY_DURATION_MS);
    }

    // Calculate official gap (from standings)
    if (!shouldShowGap) {
        strcpy_s(gapRow.formattedOfficialGap, sizeof(gapRow.formattedOfficialGap), "");
    }
    else if (playerStanding && neighborStanding) {
        int relativeGap;
        if (isGapToRiderAhead) {
            // Gap to rider ahead: player gap - ahead gap
            relativeGap = (playerStanding->gap > 0 && neighborStanding->gap >= 0)
                ? playerStanding->gap - neighborStanding->gap : 0;
        } else {
            // Gap to rider behind: behind gap - player gap
            relativeGap = (neighborStanding->gap > 0 && playerStanding->gap >= 0)
                ? neighborStanding->gap - playerStanding->gap : 0;
        }

        if (relativeGap > 0) {
            char tempGap[12];
            PluginUtils::formatTimeDiff(tempGap, sizeof(tempGap), relativeGap);
            if (isGapToRiderAhead) {
                tempGap[0] = '-';  // Rider ahead shows negative gap (they're ahead of you)
            }
            strcpy_s(gapRow.formattedOfficialGap, sizeof(gapRow.formattedOfficialGap), tempGap);
        } else {
            strcpy_s(gapRow.formattedOfficialGap, sizeof(gapRow.formattedOfficialGap), "");
        }
    } else {
        strcpy_s(gapRow.formattedOfficialGap, sizeof(gapRow.formattedOfficialGap), "");
    }

    // Calculate live gap (real-time) and populate debug column with reason codes or RTG values
    // Hide live gaps if either rider has finished (they're stationary, gap is meaningless)
    bool playerFinished = playerStanding && pluginData.getSessionData().isRiderFinished(playerStanding->numLaps);
    bool neighborFinished = neighborStanding && pluginData.getSessionData().isRiderFinished(neighborStanding->numLaps);

    // Helper to format RTG debug string (D:A/B format)
    const char neighborChar = isGapToRiderAhead ? 'A' : 'B';
    auto formatRtgDebug = [&]() {
        if (playerStanding && neighborStanding) {
            char dTime[12], nTime[12];
            PluginUtils::formatTimeDiffTenths(dTime, sizeof(dTime), playerStanding->realTimeGap);
            PluginUtils::formatTimeDiffTenths(nTime, sizeof(nTime), neighborStanding->realTimeGap);
            snprintf(gapRow.formattedDebug, sizeof(gapRow.formattedDebug), "D%s:%c%s",
                     dTime, neighborChar, nTime);
        } else if (playerStanding) {
            char dTime[12];
            PluginUtils::formatTimeDiffTenths(dTime, sizeof(dTime), playerStanding->realTimeGap);
            snprintf(gapRow.formattedDebug, sizeof(gapRow.formattedDebug), "D%s:%c?",
                     dTime, neighborChar);
        } else if (neighborStanding) {
            char nTime[12];
            PluginUtils::formatTimeDiffTenths(nTime, sizeof(nTime), neighborStanding->realTimeGap);
            snprintf(gapRow.formattedDebug, sizeof(gapRow.formattedDebug), "D?:%c%s",
                     neighborChar, nTime);
        }
    };

    // Calculate live gap and set debug column with reason codes or RTG values
    if (playerFinished || neighborFinished) {
        strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), "");
        strcpy_s(gapRow.formattedDebug, sizeof(gapRow.formattedDebug), "FIN");
    }
    else if (!playerStanding || !neighborStanding) {
        strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), "");
        strcpy_s(gapRow.formattedDebug, sizeof(gapRow.formattedDebug), "NO STD");
    }
    else if (neighborStanding->realTimeGap == 0 && isGapToRiderAhead) {
        // Neighbor ahead is the LEADER (realTimeGap=0 is valid for leader)
        // Display rider's realTimeGap IS the gap to the leader
        char tempGap[12];
        PluginUtils::formatTimeDiffTenths(tempGap, sizeof(tempGap), playerStanding->realTimeGap);
        tempGap[0] = '-';  // Rider ahead shows negative gap
        strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), tempGap);
        formatRtgDebug();
    }
    else if (playerStanding->realTimeGap == 0 && !isGapToRiderAhead) {
        // Player IS the leader (realTimeGap=0 is valid), gap to rider behind uses neighbor's RTG directly
        char tempGap[12];
        PluginUtils::formatTimeDiffTenths(tempGap, sizeof(tempGap), neighborStanding->realTimeGap);
        // Positive value - we're ahead of the rider behind
        strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), tempGap);
        formatRtgDebug();
    }
    else if (playerStanding->realTimeGap < 0 || neighborStanding->realTimeGap < 0) {
        // Negative RTG is invalid (but 0 is valid for leader, handled above)
        strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), "");
        strcpy_s(gapRow.formattedDebug, sizeof(gapRow.formattedDebug), "RTG<0");
    }
    else {
        // Both have valid realTimeGap - calculate relative gap
        int relativeLiveGap;
        if (isGapToRiderAhead) {
            relativeLiveGap = playerStanding->realTimeGap - neighborStanding->realTimeGap;
        } else {
            relativeLiveGap = neighborStanding->realTimeGap - playerStanding->realTimeGap;
        }

        if (relativeLiveGap > 0) {
            char tempGap[12];
            PluginUtils::formatTimeDiffTenths(tempGap, sizeof(tempGap), relativeLiveGap);
            if (isGapToRiderAhead) {
                tempGap[0] = '-';  // Rider ahead shows negative gap (they're ahead of you)
            }
            strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), tempGap);
            formatRtgDebug();
        } else if (relativeLiveGap < 0) {
            // Positions inverted on track vs classification (e.g., you fell, they passed you)
            // Show the inverted gap with warning color
            gapRow.isGapInverted = true;
            char tempGap[12];
            PluginUtils::formatTimeDiffTenths(tempGap, sizeof(tempGap), -relativeLiveGap);  // Absolute value
            if (!isGapToRiderAhead) {
                tempGap[0] = '-';  // You're behind when you should be ahead
            }
            strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), tempGap);
            formatRtgDebug();
        } else {
            // Gap is exactly 0 - show nothing
            strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), "");
            formatRtgDebug();
        }
    }

    // Apply gap indicator mode filtering (hide gaps not selected by mode)
    if (m_gapIndicatorMode == GapIndicatorMode::OFFICIAL) {
        strcpy_s(gapRow.formattedLiveGap, sizeof(gapRow.formattedLiveGap), "");
    } else if (m_gapIndicatorMode == GapIndicatorMode::LIVE) {
        strcpy_s(gapRow.formattedOfficialGap, sizeof(gapRow.formattedOfficialGap), "");
    }

    return gapRow;
}

void StandingsHud::buildColumnTable() {
    m_columnTable.clear();
    m_cachedBackgroundWidth = 0;

    // Build table of enabled columns only
    // Column indices: 0=TRACKED, 1=POS, 2=RACENUM, 3=NAME, 4=BIKE, 5=STATUS, 6=PENALTY, 7=BEST_LAP, 8=OFFICIAL_GAP, 9=LIVE_GAP, 10=DEBUG
    struct ColumnSpec {
        uint32_t flag;
        uint8_t index;
        float position;
        uint8_t justify;
        bool useEmpty;
        int width;
    };

    const ColumnSpec specs[] = {
        {COL_TRACKED, 0, m_columns.tracked, Justify::LEFT, true, COL_TRACKED_WIDTH},  // Sprite column - handled specially
        {COL_POS, 1, m_columns.pos, Justify::LEFT, false, COL_POS_WIDTH},
        {COL_RACENUM, 2, m_columns.raceNum, Justify::LEFT, false, COL_RACENUM_WIDTH},
        {COL_NAME, 3, m_columns.name, Justify::LEFT, false, COL_NAME_WIDTH},
        {COL_BIKE, 4, m_columns.bike, Justify::LEFT, false, COL_BIKE_WIDTH},
        {COL_STATUS, 5, m_columns.status, Justify::LEFT, true, COL_STATUS_WIDTH},
        {COL_PENALTY, 6, m_columns.penalty, Justify::LEFT, false, COL_PENALTY_WIDTH},
        {COL_BEST_LAP, 7, m_columns.bestLap, Justify::LEFT, false, COL_BEST_LAP_WIDTH},
        {COL_OFFICIAL_GAP, 8, m_columns.officialGap, Justify::LEFT, false, COL_OFFICIAL_GAP_WIDTH},
        {COL_LIVE_GAP, 9, m_columns.liveGap, Justify::LEFT, false, COL_LIVE_GAP_WIDTH},
        {COL_DEBUG, 10, m_columns.debug, Justify::LEFT, true, COL_DEBUG_WIDTH}
    };

    for (const auto& spec : specs) {
        if (m_enabledColumns & spec.flag) {
            m_columnTable.push_back({spec.index, spec.position, spec.justify, spec.useEmpty});
            m_cachedBackgroundWidth += spec.width;
        }
    }
}

StandingsHud::HudDimensions StandingsHud::calculateHudDimensions(const ScaledDimensions& dim, int rowCount) const {
    HudDimensions result;

    int widthChars = getBackgroundWidthChars();
    result.backgroundWidth = PluginUtils::calculateMonospaceTextWidth(widthChars, dim.fontSize)
        + dim.paddingH + dim.paddingH;

    result.titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    // Use provided rowCount or fall back to m_displayRowCount
    int actualRowCount = (rowCount >= 0) ? rowCount : m_displayRowCount;

    // Calculate total height (no spacing between rows, consistent with other HUDs)
    float totalRowsHeight = actualRowCount * dim.lineHeightNormal;
    result.backgroundHeight = dim.paddingV + result.titleHeight + totalRowsHeight + dim.paddingV;

    result.contentStartX = START_X + dim.paddingH;
    result.contentStartY = START_Y + dim.paddingV;

    return result;
}

StandingsHud::StandingsHud()
    : m_columns(START_X + Padding::HUD_HORIZONTAL, m_fScale, m_enabledColumns)
{
    // One-time setup
    DEBUG_INFO("StandingsHud created");
    setDraggable(true);
    m_displayEntries.reserve(m_displayRowCount);  // Only build entries we display
    m_quads.reserve(1 + m_displayRowCount);       // Main background + row backgrounds
    m_strings.reserve(m_displayRowCount * 10);    // ~10 strings per entry (estimate for all columns)

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("standings_hud");

    // Set all configurable defaults
    resetToDefaults();

    buildColumnTable();  // Build column table and cache width
    rebuildRenderData();
}

bool StandingsHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::RaceEntries ||
        dataType == DataChangeType::Standings ||
        dataType == DataChangeType::SessionData ||  // Listen for session changes to update title
        dataType == DataChangeType::SpectateTarget ||
        dataType == DataChangeType::TrackedRiders;  // Rebuild when tracked riders change (color/shape)
}

int StandingsHud::getBackgroundWidthChars() const {
    return m_cachedBackgroundWidth;
}

void StandingsHud::update() {
    // Handle mouse input for rider selection (LMB for clicking, RMB for dragging)
    const InputManager& input = InputManager::getInstance();

    if (input.getLeftButton().isClicked()) {
        const CursorPosition& cursor = input.getCursorPosition();
        if (cursor.isValid) {
            handleClick(cursor.x, cursor.y);
        }
    }

    // Track hover state in spectator mode only
    const PluginData& pluginData = PluginData::getInstance();
    int drawState = pluginData.getDrawState();
    bool isSpectatorMode = (drawState == PluginConstants::ViewState::SPECTATE);

    if (isSpectatorMode) {
        const CursorPosition& cursor = input.getCursorPosition();
        int newHoveredRow = -1;

        if (cursor.isValid) {
            // Check which row (if any) the cursor is over
            for (size_t i = 0; i < m_riderClickRegions.size(); ++i) {
                const auto& region = m_riderClickRegions[i];
                if (isPointInRect(cursor.x, cursor.y, region.x, region.y, region.width, region.height)) {
                    // Find this rider's index in m_displayEntries
                    for (size_t j = 0; j < m_displayEntries.size(); ++j) {
                        if (!m_displayEntries[j].isGapRow && m_displayEntries[j].raceNum == region.raceNum) {
                            newHoveredRow = static_cast<int>(j);
                            break;
                        }
                    }
                    break;
                }
            }
        }

        // If hover state changed, trigger rebuild
        if (newHoveredRow != m_hoveredRowIndex) {
            m_hoveredRowIndex = newHoveredRow;
            setDataDirty();
        }
    } else if (m_hoveredRowIndex != -1) {
        // Clear hover state when not in spectator mode
        m_hoveredRowIndex = -1;
        setDataDirty();
    }

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        // Data changed - full rebuild needed
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        // Only layout changed (e.g., dragging) - fast path
        rebuildLayout();
        clearLayoutDirty();
    }
}

void StandingsHud::rebuildLayout() {

    // Fast path - only update positions
    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // Calculate actual rows to render (never more than entries available)
    int rowsToRender = std::min(m_displayRowCount, static_cast<int>(m_displayEntries.size()));
    auto hudDim = calculateHudDimensions(dim, rowsToRender);

    setBounds(START_X, START_Y, START_X + hudDim.backgroundWidth, START_Y + hudDim.backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(START_X, START_Y, hudDim.backgroundWidth, hudDim.backgroundHeight);

    // Update highlight quad position if it exists
    if (m_cachedHighlightQuadIndex >= 0 && m_cachedHighlightQuadIndex < static_cast<int>(m_quads.size()) &&
        m_cachedPlayerIndex >= 0 && m_cachedPlayerIndex < rowsToRender) {
        // Calculate highlight Y position
        float highlightY = hudDim.contentStartY + hudDim.titleHeight + (m_cachedPlayerIndex * dim.lineHeightNormal);
        float highlightX = START_X;
        applyOffset(highlightX, highlightY);
        setQuadPositions(m_quads[m_cachedHighlightQuadIndex], highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
    }

    // Update all string positions
    float currentY = hudDim.contentStartY;
    size_t stringIndex = 0;

    // Title string (always exists, but may be empty if hidden)
    if (stringIndex < m_strings.size()) {
        float x = hudDim.contentStartX;
        float y = currentY;
        applyOffset(x, y);
        m_strings[stringIndex].m_afPos[0] = x;
        m_strings[stringIndex].m_afPos[1] = y;
        stringIndex++;
    }
    currentY += hudDim.titleHeight;

    // Update positions for actual rows being rendered (no spacing, consistent with other HUDs)
    // Use table-driven approach (only loops over enabled columns)
    for (int i = 0; i < rowsToRender; ++i) {
        // Each row has strings for enabled columns only
        for (const auto& col : m_columnTable) {
            // Skip tracked column (index 0) - it uses quads, not strings
            if (col.columnIndex == 0) continue;

            if (stringIndex >= m_strings.size()) break;
            float x = col.position;
            float y = currentY;
            applyOffset(x, y);
            m_strings[stringIndex].m_afPos[0] = x;
            m_strings[stringIndex].m_afPos[1] = y;
            stringIndex++;
        }

        currentY += dim.lineHeightNormal;
    }
}

void StandingsHud::rebuildRenderData() {

    m_strings.clear();
    m_quads.clear();
    m_displayEntries.clear();
    m_cachedHighlightQuadIndex = -1;  // Reset highlight quad tracking

    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();
    const SessionData& sessionData = pluginData.getSessionData();
    const auto& classificationOrder = pluginData.getClassificationOrder();

    // Column configuration is now managed by the profile system
    // m_enabledColumns, m_officialGapMode, m_liveGapMode are set when profile is applied
    bool isRace = pluginData.isRaceSession();

    // Apply gap modes - disable columns if mode is OFF
    uint32_t effectiveColumns = m_enabledColumns;
    if (m_officialGapMode == GapMode::OFF) {
        effectiveColumns &= ~COL_OFFICIAL_GAP;
    }
    if (m_liveGapMode == GapMode::OFF) {
        effectiveColumns &= ~COL_LIVE_GAP;
    }

    // Only log when configuration actually changes
    static uint32_t prevEffectiveColumns = 0;
    if (effectiveColumns != prevEffectiveColumns) {
        DEBUG_INFO_F("StandingsHud column config: enabledColumns=0x%X, effective=0x%X",
            m_enabledColumns, effectiveColumns);
        prevEffectiveColumns = effectiveColumns;
    }

    // Use effective columns (with gap mode adjustments) for rendering
    uint32_t savedEnabledColumns = m_enabledColumns;
    m_enabledColumns = effectiveColumns;

    // Calculate current elapsed time (same logic as TimeWidget)
    int sessionTime = pluginData.getSessionTime();
    int currentElapsedTime = 0;
    if (sessionData.sessionLength > 0) {
        // Time-based race: elapsed = sessionLength - currentTime
        currentElapsedTime = sessionData.sessionLength - sessionTime;
    } else {
        // Lap-based race: sessionTime already represents elapsed time
        currentElapsedTime = sessionTime > 0 ? sessionTime : 0;
    }

    // Track official gap changes and update timestamps
    for (int raceNum : classificationOrder) {
        const StandingsData* standing = pluginData.getStanding(raceNum);
        if (standing) {
            int currentGap = standing->gap;

            // Check if gap value has changed
            auto lastValueIt = m_lastOfficialGapValue.find(raceNum);
            if (lastValueIt == m_lastOfficialGapValue.end() || lastValueIt->second != currentGap) {
                // Gap changed - record update time and new value
                m_lastOfficialGapUpdateTime[raceNum] = currentElapsedTime;
                m_lastOfficialGapValue[raceNum] = currentGap;
            }
        }
    }

    // Build display entries with smart pagination
    // Strategy:
    // - If display rider is in top 3 and (running or spectating): show first N riders (simple case)
    // - If display rider is beyond top 3 and (running or spectating): show top 3 + rider context
    // - Otherwise (rider not found): show first N riders (fallback)

    // Find display rider's position in classification
    int playerPositionInClassification = -1;
    for (size_t i = 0; i < classificationOrder.size(); ++i) {
        if (classificationOrder[i] == displayRaceNum) {
            playerPositionInClassification = static_cast<int>(i);
            break;
        }
    }

    const int TOP_POSITIONS = 3;  // Always show top 3
    const int MAX_GAP_ROWS = 2;   // Gap rows before and after player
    const int SEPARATOR_ROWS = 0; // No separator (removed for cleaner display)

    m_cachedPlayerIndex = -1;

    // Show context when player is running OR when spectating/in replay
    int drawState = pluginData.getDrawState();
    bool shouldShowContext = pluginData.isPlayerRunning() ||
                             drawState == PluginConstants::ViewState::SPECTATE ||
                             drawState == PluginConstants::ViewState::REPLAY;

    if (playerPositionInClassification >= 0 && playerPositionInClassification < TOP_POSITIONS && shouldShowContext) {
        // Display rider is in top 3 and (running or spectating) - use simple display (first N riders)
        // Gap rows are added on top of rider rows, not subtracted from the count
        int entriesToBuild = std::min(static_cast<int>(classificationOrder.size()), m_displayRowCount);
        m_displayEntries.reserve(entriesToBuild);
        addDisplayEntries(0, entriesToBuild - 1, 1, classificationOrder, pluginData);
    } else if (playerPositionInClassification >= TOP_POSITIONS && shouldShowContext) {
        // Display rider is beyond top 3 and (running or spectating) - show top 3 + rider context
        m_displayEntries.reserve(m_displayRowCount);

        // 1. Add top 3 riders
        addDisplayEntries(0, TOP_POSITIONS - 1, 1, classificationOrder, pluginData);

        // 2. Calculate rider context window (gap rows added on top, not subtracted)
        int availableRows = m_displayRowCount - TOP_POSITIONS - SEPARATOR_ROWS;
        int contextBefore = availableRows / 2;
        int contextAfter = availableRows - contextBefore - 1;  // -1 for rider row

        int startIndex = std::max(TOP_POSITIONS, playerPositionInClassification - contextBefore);
        int endIndex = std::min(static_cast<int>(classificationOrder.size()) - 1,
                               playerPositionInClassification + contextAfter);

        // 3. Add rider context
        addDisplayEntries(startIndex, endIndex, startIndex + 1, classificationOrder, pluginData);
    } else {
        // Display rider not found or no classification - show first N riders (fallback)
        // Gap rows are added on top of rider rows, not subtracted from the count
        int entriesToBuild = std::min(static_cast<int>(classificationOrder.size()), m_displayRowCount);
        m_displayEntries.reserve(entriesToBuild);
        addDisplayEntries(0, entriesToBuild - 1, 1, classificationOrder, pluginData);
    }

    // Insert gap indicator rows for displayed rider's neighbors
    // Only insert if:
    // 1. (Player is actively running OR spectating/replay) AND in a race session (gap rows are race-only)
    // 2. m_gapIndicatorMode is not OFF
    // 3. Required gap columns are enabled for the selected mode
    // Reuse playerPositionInClassification from pagination logic above (already searched)

    // Check if required columns are enabled for the gap indicator mode
    bool hasRequiredColumnsForGapMode = false;
    if (m_gapIndicatorMode == GapIndicatorMode::OFFICIAL) {
        hasRequiredColumnsForGapMode = (m_enabledColumns & COL_OFFICIAL_GAP) != 0;
    } else if (m_gapIndicatorMode == GapIndicatorMode::LIVE) {
        hasRequiredColumnsForGapMode = (m_enabledColumns & COL_LIVE_GAP) != 0;
    } else if (m_gapIndicatorMode == GapIndicatorMode::BOTH) {
        // For BOTH mode, require at least one gap column to be enabled
        hasRequiredColumnsForGapMode = ((m_enabledColumns & COL_OFFICIAL_GAP) != 0) ||
                                        ((m_enabledColumns & COL_LIVE_GAP) != 0);
    }

    // Show gap rows when player is running OR when spectating/in replay
    bool shouldShowGapRows = pluginData.isPlayerRunning() ||
                             drawState == PluginConstants::ViewState::SPECTATE ||
                             drawState == PluginConstants::ViewState::REPLAY;

    if (m_gapIndicatorMode != GapIndicatorMode::OFF && hasRequiredColumnsForGapMode &&
        m_cachedPlayerIndex != -1 && m_cachedPlayerIndex < static_cast<int>(m_displayEntries.size()) &&
        playerPositionInClassification >= 0 && shouldShowGapRows && pluginData.isRaceSession()) {

        // Build gap rows if needed
        bool hasGapAhead = (playerPositionInClassification > 0 &&
                           playerPositionInClassification - 1 < static_cast<int>(classificationOrder.size()));
        bool hasGapBehind = (playerPositionInClassification >= 0 &&
                            playerPositionInClassification + 1 < static_cast<int>(classificationOrder.size()));

        if (hasGapAhead || hasGapBehind) {
            // Build new vector with gap rows in correct positions (O(N) instead of O(N²))
            std::vector<DisplayEntry> newEntries;
            newEntries.reserve(m_displayEntries.size() + 2);

            for (int i = 0; i < static_cast<int>(m_displayEntries.size()); ++i) {
                // Insert gap row BEFORE player row
                if (i == m_cachedPlayerIndex && hasGapAhead) {
                    int riderAheadRaceNum = classificationOrder[playerPositionInClassification - 1];
                    newEntries.push_back(buildGapRow(displayRaceNum, riderAheadRaceNum, true,
                                                     currentElapsedTime, pluginData));
                }

                newEntries.push_back(std::move(m_displayEntries[i]));

                // Insert gap row AFTER player row
                if (i == m_cachedPlayerIndex && hasGapBehind) {
                    int riderBehindRaceNum = classificationOrder[playerPositionInClassification + 1];
                    newEntries.push_back(buildGapRow(displayRaceNum, riderBehindRaceNum, false,
                                                     currentElapsedTime, pluginData));
                }
            }

            // Update cached player index to account for gap row inserted before
            if (hasGapAhead) {
                m_cachedPlayerIndex++;
            }

            m_displayEntries = std::move(newEntries);
        }
    }

    // Format strings for all built entries (they're all displayed)
    // Get player's gaps for player-relative mode
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    int playerOfficialGap = (playerStanding && playerStanding->gap > 0) ? playerStanding->gap : 0;
    int playerGapLaps = playerStanding ? playerStanding->gapLaps : 0;
    int playerLiveGap = (playerStanding && playerStanding->realTimeGap > 0) ? playerStanding->realTimeGap : 0;

    for (auto& entry : m_displayEntries) {
        // Skip formatting for gap rows (already formatted)
        if (entry.isGapRow) {
            continue;
        }

        entry.updateFormattedStrings();
        formatStatus(entry, sessionData);

        // Format official gap using formatTimeDiff
        // In race mode: only show for a few seconds after update
        // In non-race mode: always show (no time restriction)
        // Gap mode filtering: ME mode only shows player's gap
        entry.hasOfficialGap = false;
        bool shouldShowOfficialGap = false;

        // Check gap mode filtering
        bool isPlayerRow = (entry.raceNum == displayRaceNum);
        if (shouldShowGapForMode(m_officialGapMode, isPlayerRow)) {
            if (!pluginData.isRaceSession()) {
                // Non-race mode: always show gaps (practice/qualify)
                shouldShowOfficialGap = true;
            } else if (entry.isFinishedRace) {
                // Finished riders: always show their final gap
                shouldShowOfficialGap = true;
            } else {
                // Race mode: check if gap should be visible based on time since last update
                auto updateTimeIt = m_lastOfficialGapUpdateTime.find(entry.raceNum);
                if (updateTimeIt != m_lastOfficialGapUpdateTime.end()) {
                    int timeSinceUpdate = currentElapsedTime - updateTimeIt->second;
                    shouldShowOfficialGap = (timeSinceUpdate >= 0 && timeSinceUpdate < OFFICIAL_GAP_DISPLAY_DURATION_MS);
                }
            }
        }

        if (entry.state != RiderState::NORMAL) {
            entry.formattedOfficialGap[0] = '\0';
        }
        else if (entry.position == Position::FIRST) {
            // Leader: always show finish time if finished, otherwise show their best lap
            if (entry.isFinishedRace) {
                int leaderFinishTime = pluginData.getLeaderFinishTime();
                if (leaderFinishTime > 0) {
                    char tempTime[16];
                    PluginUtils::formatLapTime(leaderFinishTime, tempTime, sizeof(tempTime));
                    snprintf(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), " %s", tempTime);
                } else {
                    entry.formattedOfficialGap[0] = '\0';
                }
            } else if (entry.bestLap > 0) {
                char tempTime[16];
                PluginUtils::formatLapTime(entry.bestLap, tempTime, sizeof(tempTime));
                snprintf(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), " %s", tempTime);
            } else {
                entry.formattedOfficialGap[0] = '\0';
            }
        }
        else if (m_gapReferenceMode == GapReferenceMode::PLAYER && isPlayerRow) {
            // Player-relative mode: player is the reference point, gap to self is meaningless
            strcpy_s(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), Placeholders::GENERIC);
        }
        else if (!shouldShowOfficialGap) {
            // Gap not visible (expired or filtered by mode) - show placeholder
            strcpy_s(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), Placeholders::GENERIC);
        }
        else if (m_gapReferenceMode == GapReferenceMode::PLAYER) {
            // Player-relative mode: calculate gap relative to player
            int relativeLapGap = entry.gapLaps - playerGapLaps;
            int relativeTimeGap = entry.officialGap - playerOfficialGap;

            if (relativeLapGap != 0) {
                entry.hasOfficialGap = true;
                snprintf(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), "%+dL", relativeLapGap);
            } else if (relativeTimeGap != 0 || entry.officialGap > 0) {
                entry.hasOfficialGap = true;
                PluginUtils::formatTimeDiff(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), relativeTimeGap);
            } else {
                strcpy_s(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), Placeholders::GENERIC);
            }
        }
        else if (entry.gapLaps > 0) {
            entry.hasOfficialGap = true;
            snprintf(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), "+%dL", entry.gapLaps);
        }
        else if (entry.officialGap > 0) {
            entry.hasOfficialGap = true;
            PluginUtils::formatTimeDiff(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), entry.officialGap);
        }
        else {
            strcpy_s(entry.formattedOfficialGap, sizeof(entry.formattedOfficialGap), Placeholders::GENERIC);
        }

        // Format live gap with tenths precision (M:SS.s format)
        // Only show live gaps during actual race sessions (not practice/qualify)
        // Gap mode filtering: ME mode only shows player's gap
        bool shouldShowLiveGap = shouldShowGapForMode(m_liveGapMode, isPlayerRow);

        // Determine if this is the reference point (leader in LEADER mode, player in PLAYER mode)
        bool isLiveGapReference = (m_gapReferenceMode == GapReferenceMode::LEADER && entry.position == Position::FIRST) ||
                                  (m_gapReferenceMode == GapReferenceMode::PLAYER && isPlayerRow);

        // Format live gap and populate debug column with reason codes or RTG values
        // Debug column shows WHY the gap is empty, or the RTG value if gap is shown
        char rtgTime[12];
        PluginUtils::formatTimeDiffTenths(rtgTime, sizeof(rtgTime), entry.realTimeGap);

        if (entry.state != RiderState::NORMAL) {
            entry.formattedLiveGap[0] = '\0';
            // Show rider state as reason
            switch (entry.state) {
                case RiderState::DNS: strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "DNS"); break;
                case RiderState::DSQ: strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "DSQ"); break;
                case RiderState::RETIRED: strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "RETIRED"); break;
                default: strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "UNKNOWN"); break;
            }
        }
        else if (!pluginData.isRaceSession()) {
            // Non-race session (practice/qualify) - live gaps don't exist
            strcpy_s(entry.formattedLiveGap, sizeof(entry.formattedLiveGap), Placeholders::NOT_AVAILABLE);
            strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "!RACE");
        }
        else if (isLiveGapReference) {
            entry.formattedLiveGap[0] = '\0';
            // Show reference point indicator
            if (m_gapReferenceMode == GapReferenceMode::LEADER) {
                strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "REF:LEADER");
            } else {
                strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "REF:PLAYER");
            }
        }
        else if (pluginData.getSessionData().isRiderFinished(entry.numLaps)) {
            entry.formattedLiveGap[0] = '\0';
            strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "FIN");
        }
        else if (!shouldShowLiveGap) {
            strcpy_s(entry.formattedLiveGap, sizeof(entry.formattedLiveGap), Placeholders::GENERIC);
            // Show which mode filtered it
            if (m_liveGapMode == GapMode::OFF) {
                strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "MODE:OFF");
            } else {
                strcpy_s(entry.formattedDebug, sizeof(entry.formattedDebug), "MODE:ME");
            }
        }
        else if (m_gapReferenceMode == GapReferenceMode::PLAYER) {
            int relativeLiveGap = entry.realTimeGap - playerLiveGap;
            if (relativeLiveGap != 0 || entry.realTimeGap > 0) {
                PluginUtils::formatTimeDiffTenths(entry.formattedLiveGap, sizeof(entry.formattedLiveGap), relativeLiveGap);
                snprintf(entry.formattedDebug, sizeof(entry.formattedDebug), "RTG:%s", rtgTime);
            } else {
                strcpy_s(entry.formattedLiveGap, sizeof(entry.formattedLiveGap), Placeholders::GENERIC);
                snprintf(entry.formattedDebug, sizeof(entry.formattedDebug), "RTG:%s", rtgTime);
            }
        }
        else if (entry.realTimeGap > 0) {
            PluginUtils::formatTimeDiffTenths(entry.formattedLiveGap, sizeof(entry.formattedLiveGap), entry.realTimeGap);
            snprintf(entry.formattedDebug, sizeof(entry.formattedDebug), "RTG:%s", rtgTime);
        }
        else {
            strcpy_s(entry.formattedLiveGap, sizeof(entry.formattedLiveGap), Placeholders::GENERIC);
            snprintf(entry.formattedDebug, sizeof(entry.formattedDebug), "RTG:%s", rtgTime);
        }

        // Format best lap time
        if (entry.hasBestLap) {
            PluginUtils::formatLapTime(entry.bestLap, entry.formattedLapTime, sizeof(entry.formattedLapTime));
        }
        else {
            strcpy_s(entry.formattedLapTime, sizeof(entry.formattedLapTime), Placeholders::LAP_TIME);
        }

        // Format penalty as whole seconds (e.g., "+5s" for 5 second penalty)
        if (entry.penalty > 0) {
            int penaltySeconds = (entry.penalty + MS_TO_SEC_ROUNDING_OFFSET) / MS_TO_SEC_DIVISOR;
            snprintf(entry.formattedPenalty, sizeof(entry.formattedPenalty), "+%ds", penaltySeconds);
        } else {
            // No penalty - show generic placeholder
            strcpy_s(entry.formattedPenalty, sizeof(entry.formattedPenalty), Placeholders::GENERIC);
        }
    }

    // Generate render data
    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // IMPORTANT: Recalculate column positions and rebuild column table BEFORE calculating dimensions
    // This ensures m_cachedBackgroundWidth is updated before we create the background quad
    float contentStartX = START_X + dim.paddingH;
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns);
    buildColumnTable();  // Rebuild column table and cache width

    // Calculate actual rows to render (never more than entries available)
    int rowsToRender = std::min(m_displayRowCount, static_cast<int>(m_displayEntries.size()));

    // Calculate dimensions based on actual rows that will be rendered
    auto hudDim = calculateHudDimensions(dim, rowsToRender);

    setBounds(START_X, START_Y, START_X + hudDim.backgroundWidth, START_Y + hudDim.backgroundHeight);

    addBackgroundQuad(START_X, START_Y, hudDim.backgroundWidth, hudDim.backgroundHeight);

    float currentY = hudDim.contentStartY;

    // Title
    addTitleString("Standings", hudDim.contentStartX, currentY, Justify::LEFT,
        Fonts::getTitle(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);

    currentY += hudDim.titleHeight;

    // Clear and rebuild click regions for rider selection
    m_riderClickRegions.clear();

    // Render rows (no spacing between rows, consistent with other HUDs)
    for (int i = 0; i < rowsToRender; ++i) {
        const auto& entry = m_displayEntries[i];

        // Highlight player/spectated rider row with bike brand color background
        if (i == m_cachedPlayerIndex) {
            SPluginQuad_t highlight;
            float highlightX = START_X;
            float highlightY = currentY;
            applyOffset(highlightX, highlightY);  // Apply drag offset
            setQuadPositions(highlight, highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
            highlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

            // Apply transparency to bike brand color
            highlight.m_ulColor = PluginUtils::applyOpacity(entry.bikeBrandColor, 80.0f / 255.0f);

            m_cachedHighlightQuadIndex = static_cast<int>(m_quads.size());  // Track quad index
            m_quads.push_back(highlight);
        }
        // Hover highlight for other riders (spectator mode only, skip gap rows and current player)
        else if (i == m_hoveredRowIndex && !entry.isGapRow && i != m_cachedPlayerIndex) {
            SPluginQuad_t hoverHighlight;
            float hoverX = START_X;
            float hoverY = currentY;
            applyOffset(hoverX, hoverY);
            setQuadPositions(hoverHighlight, hoverX, hoverY, hudDim.backgroundWidth, dim.lineHeightNormal);
            hoverHighlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

            // Use accent color with transparency for hover
            hoverHighlight.m_ulColor = PluginUtils::applyOpacity(ColorConfig::getInstance().getAccent(), 60.0f / 255.0f);
            m_quads.push_back(hoverHighlight);
        }

        renderRiderRow(entry, false, currentY, dim);

        // Add click region for this rider (skip gap rows)
        if (!entry.isGapRow && entry.raceNum >= 0) {
            RiderClickRegion region;
            region.x = START_X;
            region.y = currentY;
            region.width = hudDim.backgroundWidth;
            region.height = dim.lineHeightNormal;
            region.raceNum = entry.raceNum;
            applyOffset(region.x, region.y);  // Apply drag offset to region
            m_riderClickRegions.push_back(region);
        }

        currentY += dim.lineHeightNormal;
    }

    // Restore m_enabledColumns to the profile-set value (we temporarily modified it for gap mode filtering)
    m_enabledColumns = savedEnabledColumns;
}

void StandingsHud::handleClick(float mouseX, float mouseY) {
    // Check if click is within any rider row
    for (const auto& region : m_riderClickRegions) {
        if (isPointInRect(mouseX, mouseY, region.x, region.y, region.width, region.height)) {
            // Request to spectate this rider
            DEBUG_INFO_F("StandingsHud: Switching to rider #%d", region.raceNum);
            PluginManager::getInstance().requestSpectateRider(region.raceNum);
            return;  // Only process one click
        }
    }
}

void StandingsHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = true;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = SettingsLimits::DEFAULT_OPACITY;
    m_fScale = 1.0f;
    setPosition(0.0055f, 0.2997f);
    m_officialGapMode = GapMode::ALL;
    m_liveGapMode = GapMode::PLAYER;
    m_gapIndicatorMode = GapIndicatorMode::BOTH;
    m_gapReferenceMode = GapReferenceMode::LEADER;
    m_enabledColumns = COL_DEFAULT;
    m_displayRowCount = DEFAULT_ROW_COUNT;
    setDataDirty();
}


