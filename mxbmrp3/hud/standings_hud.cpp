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

void StandingsHud::CachedIcons::ensureInitialized() {
    if (initialized) return;
    const AssetManager& assets = AssetManager::getInstance();
    circleExclamation = assets.getIconSpriteIndex("circle-exclamation");
    triangleExclamation = assets.getIconSpriteIndex("triangle-exclamation");
    flag = assets.getIconSpriteIndex("flag");
    flagCheckered = assets.getIconSpriteIndex("flag-checkered");
    wrench = assets.getIconSpriteIndex("wrench");
    initialized = true;
}

StandingsHud::ColumnPositions::ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns, int nameWidth) {
    float scaledFontSize = FontSizes::NORMAL * scale;
    float current = contentStartX;

    // Use helper function to set column positions (eliminates duplicated lambda)
    // All columns (race mode everywhere)
    PluginUtils::setColumnPosition(enabledColumns, COL_TRACKED, COL_TRACKED_WIDTH, scaledFontSize, current, tracked);
    PluginUtils::setColumnPosition(enabledColumns, COL_POS, COL_POS_WIDTH, scaledFontSize, current, pos);
    PluginUtils::setColumnPosition(enabledColumns, COL_RACENUM, COL_RACENUM_WIDTH, scaledFontSize, current, raceNum);
    PluginUtils::setColumnPosition(enabledColumns, COL_NAME, nameWidth, scaledFontSize, current, name);
    PluginUtils::setColumnPosition(enabledColumns, COL_BIKE, COL_BIKE_WIDTH, scaledFontSize, current, bike);
    PluginUtils::setColumnPosition(enabledColumns, COL_STATUS, COL_STATUS_WIDTH, scaledFontSize, current, status);
    PluginUtils::setColumnPosition(enabledColumns, COL_PENALTY, COL_PENALTY_WIDTH, scaledFontSize, current, penalty);
    PluginUtils::setColumnPosition(enabledColumns, COL_BEST_LAP, COL_BEST_LAP_WIDTH, scaledFontSize, current, bestLap);
    PluginUtils::setColumnPosition(enabledColumns, COL_GAP, COL_GAP_WIDTH, scaledFontSize, current, gap);
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
        result.numLapsAtLeaderFinish = standings->numLapsAtLeaderFinish;
        result.sessionFinished = standings->sessionFinished;
    }

    // Copy full name (truncation handled at render time based on name mode)
    strncpy_s(result.name, sizeof(result.name), entry.name, sizeof(result.name) - 1);
    result.name[sizeof(result.name) - 1] = '\0';

    // Use cached abbreviation pointer
    strncpy_s(result.bikeShortName, sizeof(result.bikeShortName),
        entry.bikeAbbr, sizeof(result.bikeShortName) - 1);
    result.bikeShortName[sizeof(result.bikeShortName) - 1] = '\0';

    // Copy pre-computed bike brand color
    result.bikeBrandColor = entry.bikeBrandColor;

    // Format race number without # prefix (standings uses number plate quads instead)
    snprintf(result.formattedRaceNum, sizeof(result.formattedRaceNum), "%d", entry.raceNum);

    return result;
}

void StandingsHud::formatStatus(DisplayEntry& entry, const SessionData& sessionData) const {
    entry.isFinishedRace = false;
    const char* stateAbbr = PluginUtils::getRiderStateAbbreviation(entry.state);

    if (stateAbbr[0] != '\0') {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), stateAbbr);
    }
    else if (sessionData.isRiderFinished(entry.numLaps, entry.numLapsAtLeaderFinish) || entry.sessionFinished) {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), "FIN");
        entry.isFinishedRace = true;
    }
    else if (entry.pit == 1) {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), "PIT");
    }
    else if (sessionData.isRiderOnLastLap(entry.numLaps, entry.numLapsAtLeaderFinish)) {
        strcpy_s(entry.formattedStatus, sizeof(entry.formattedStatus), "LL");
    }
    else {
        snprintf(entry.formattedStatus, sizeof(entry.formattedStatus), "L%d", entry.numLaps + 1);
    }
}

void StandingsHud::renderRiderRow(const DisplayEntry& entry, bool isPlaceholder, float currentY, const ScaledDimensions& dim, int rowIndex) {

    const char* placeholder = Placeholders::GENERIC;
    const char* lapTimePlaceholder = Placeholders::LAP_TIME;

    // Determine text color
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);
    if (!isPlaceholder) {
        // Use muted for DNS/DSQ/RETIRED
        using namespace PluginConstants::RiderState;
        if (entry.state == DNS || entry.state == DSQ || entry.state == RETIRED) {
            textColor = this->getColor(ColorSlot::MUTED);
        }
    }

    // Table-driven rendering - loop only through enabled columns
    for (const auto& col : m_columnTable) {
        // Special handling for TRACKED column - render sprite instead of text
        if (col.columnIndex == COL_IDX_TRACKED) {
            // Only render tracked/hazard indicator for non-placeholder rows with valid race number
            if (!isPlaceholder && entry.raceNum > 0) {
                const PluginData& pluginData = PluginData::getInstance();

                // No status icons for non-participating riders (DNS/Retired/DSQ)
                bool isNonParticipant = (entry.state == static_cast<int>(Unified::EntryState::DNS) ||
                                         entry.state == static_cast<int>(Unified::EntryState::Retired) ||
                                         entry.state == static_cast<int>(Unified::EntryState::DSQ));

                // Check for hazard icon override first (hazard takes priority over tracked icon)
                HazardType hazardType = isNonParticipant ? HazardType::None : pluginData.getRiderHazardType(entry.raceNum);
                bool showHazardIcon = (hazardType != HazardType::None);

                int spriteIndex = -1;
                unsigned long spriteColor = 0;

                if (showHazardIcon) {
                    m_iconCache.ensureInitialized();
                    if (hazardType == HazardType::WrongWay) {
                        if (m_iconCache.circleExclamation > 0) { spriteIndex = m_iconCache.circleExclamation; spriteColor = ColorPalette::RED; }
                    } else {
                        if (m_iconCache.triangleExclamation > 0) { spriteIndex = m_iconCache.triangleExclamation; spriteColor = ColorPalette::YELLOW; }
                    }
                } else if (!isNonParticipant && pluginData.isRiderBlueFlagged(entry.raceNum)) {
                    // Blue flag icon (lower priority than hazard, higher than tracked)
                    m_iconCache.ensureInitialized();
                    if (m_iconCache.flag > 0) { spriteIndex = m_iconCache.flag; spriteColor = ColorPalette::BLUE; }
                } else if (!isNonParticipant) {
                    const StandingsData* sd = pluginData.getStanding(entry.raceNum);
                    const SessionData& session = pluginData.getSessionData();
                    if (sd && sd->pit == 1) {
                        // Wrench icon for riders in pits (higher priority than checkered/last lap)
                        m_iconCache.ensureInitialized();
                        if (m_iconCache.wrench > 0) { spriteIndex = m_iconCache.wrench; spriteColor = ColorPalette::GRAY; }
                    } else if (entry.isFinishedRace || entry.sessionFinished) {
                        // Checkered flag for finished riders (race finish or non-race session finish)
                        m_iconCache.ensureInitialized();
                        if (m_iconCache.flagCheckered > 0) { spriteIndex = m_iconCache.flagCheckered; spriteColor = ColorPalette::WHITE; }
                    } else if (sd && session.isRiderOnLastLap(sd->numLaps, sd->numLapsAtLeaderFinish)) {
                        // White flag for riders on last lap
                        m_iconCache.ensureInitialized();
                        if (m_iconCache.flag > 0) { spriteIndex = m_iconCache.flag; spriteColor = ColorPalette::WHITE; }
                    } else {
                        // Fall back to tracked rider icon
                        const RaceEntryData* raceEntry = pluginData.getRaceEntry(entry.raceNum);
                        if (raceEntry) {
                            const TrackedRidersManager& trackedMgr = TrackedRidersManager::getInstance();
                            const TrackedRiderConfig* trackedConfig = trackedMgr.getTrackedRider(raceEntry->name);
                            if (trackedConfig) {
                                spriteIndex = AssetManager::getInstance().getFirstIconSpriteIndex() + trackedConfig->shapeIndex - 1;
                                spriteColor = trackedConfig->color;
                            }
                        }
                    }
                }

                if (spriteIndex > 0) {
                    // Render icon sprite (tracked or hazard)
                    constexpr float baseConeSize = 0.006f;
                    float baseHalfSize = baseConeSize * m_fScale;
                    float spriteHalfSize = baseHalfSize;

                    float colWidth = PluginUtils::calculateMonospaceTextWidth(COL_TRACKED_WIDTH, dim.fontSize);
                    float spriteCenterX = col.position + colWidth * 0.25f;
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
                    sprite.m_ulColor = spriteColor;

                    // Track the quad index for position updates in rebuildLayout
                    m_trackedIconQuads.push_back({m_quads.size(), rowIndex});
                    m_quads.push_back(sprite);
                }
            }
            continue;  // Skip text rendering for tracked column
        }

        const char* text;

        if (isPlaceholder) {
            text = col.useEmptyForPlaceholder ? "" : placeholder;
        } else {
            // Select data field based on column index (TRACKED handled above as sprite)
            switch (col.columnIndex) {
                case COL_IDX_POS:         text = entry.formattedPosition; break;
                case COL_IDX_RACENUM:     text = entry.formattedRaceNum; break;
                case COL_IDX_NAME:        text = entry.name; break;
                case COL_IDX_BIKE:        text = entry.bikeShortName; break;
                case COL_IDX_STATUS:      text = entry.formattedStatus; break;
                case COL_IDX_PENALTY:     text = entry.formattedPenalty; break;
                case COL_IDX_BEST_LAP:    text = entry.formattedLapTime; break;
                case COL_IDX_GAP:         text = entry.formattedGap; break;
                default: text = ""; break;
            }
        }

        // Use podium colors for position column (P1/P2/P3), secondary for others
        // Skip for non-participants (DNS/DSQ/RET) — they use muted for all columns
        unsigned long columnColor = textColor;
        if (col.columnIndex == COL_IDX_POS && !isPlaceholder && entry.position > 0 && textColor != mutedColor) {
            if (entry.position == Position::FIRST) {
                columnColor = PodiumColors::GOLD;
            } else if (entry.position == Position::SECOND) {
                columnColor = PodiumColors::SILVER;
            } else if (entry.position == Position::THIRD) {
                columnColor = PodiumColors::BRONZE;
            } else {
                columnColor = this->getColor(ColorSlot::SECONDARY);
            }
        }
        // Race number text color: dark on plate background, or secondary in classic layout
        if (col.columnIndex == COL_IDX_RACENUM && !isPlaceholder) {
            columnColor = m_bClassicLayout ? this->getColor(ColorSlot::SECONDARY) : this->getColor(ColorSlot::BACKGROUND);
        } else if (col.columnIndex == COL_IDX_BIKE && !isPlaceholder) {
            columnColor = this->getColor(ColorSlot::SECONDARY);
        } else if (col.columnIndex == COL_IDX_STATUS && !isPlaceholder && textColor != mutedColor) {
            columnColor = this->getColor(ColorSlot::SECONDARY);
        } else if (col.columnIndex == COL_IDX_PENALTY && !isPlaceholder && entry.penalty > 0) {
            columnColor = this->getColor(ColorSlot::NEGATIVE);
        }

        // Use muted color for placeholder values
        if (strcmp(text, placeholder) == 0 || strcmp(text, lapTimePlaceholder) == 0 ||
            strcmp(text, Placeholders::NOT_AVAILABLE) == 0) {
            columnColor = mutedColor;
        }

        // Gap column styling based on GapStyle enum
        if (col.columnIndex == COL_IDX_GAP && !isPlaceholder &&
            strcmp(text, Placeholders::GENERIC) != 0) {
            if (entry.gapColorOverride != 0) {
                // Adjacent mode coloring takes priority (green for ahead, red for behind)
                columnColor = entry.gapColorOverride;
            } else {
                switch (entry.gapStyle) {
                    case DisplayEntry::GapStyle::LABEL: columnColor = this->getColor(ColorSlot::TERTIARY); break;
                    case DisplayEntry::GapStyle::LIVE:  columnColor = this->getColor(ColorSlot::SECONDARY); break;
                    default: break;  // OFFICIAL keeps primary
                }
            }
        }

        // Use Digits font for numeric columns (BEST_LAP, GAP), except text gap labels use normal font
        bool isTextGapLabel = (col.columnIndex == COL_IDX_GAP && !isPlaceholder && entry.gapStyle == DisplayEntry::GapStyle::LABEL && text[0] != '\0' && !isdigit(static_cast<unsigned char>(text[0])));
        int font = (col.columnIndex == COL_IDX_BEST_LAP || (col.columnIndex == COL_IDX_GAP && !isTextGapLabel))
            ? this->getFont(FontCategory::DIGITS) : this->getFont(FontCategory::NORMAL);

        // Race number positioning: centered on plate, or left-aligned with # prefix in classic
        float textX = col.position;
        int justify = static_cast<int>(col.justify);
        char classicRaceNum[16];
        if (col.columnIndex == COL_IDX_RACENUM && !isPlaceholder) {
            if (m_bClassicLayout) {
                snprintf(classicRaceNum, sizeof(classicRaceNum), "#%s", text);
                text = classicRaceNum;
            } else {
                float charWidth = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                textX = col.position + charWidth * 2.0f;  // Center of 4-char plate
                justify = Justify::CENTER;
            }
        }
        bool skipShadow = (col.columnIndex == COL_IDX_RACENUM && !isPlaceholder && !m_bClassicLayout);
        addString(text, textX, currentY, justify, font, columnColor, dim.fontSize, skipShadow);
    }
}

void StandingsHud::DisplayEntry::updateFormattedStrings() {
    hasBestLap = (bestLap > 0);

    if (position > 0) {
        snprintf(formattedPosition, sizeof(formattedPosition), "%d", position);
    }
    else {
        strcpy_s(formattedPosition, sizeof(formattedPosition), Placeholders::GENERIC);
    }
}

bool StandingsHud::shouldShowGapForScope(bool isPlayerRow) const {
    return m_gapMode != GapMode::PLAYER || isPlayerRow;
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

void StandingsHud::buildColumnTable() {
    m_columnTable.clear();
    m_cachedBackgroundWidth = 0;

    // Build table of enabled columns only
    // Column indices: 0=TRACKED, 1=POS, 2=RACENUM, 3=NAME, 4=BIKE, 5=STATUS, 6=PENALTY, 7=BEST_LAP, 8=GAP, 9=DEBUG
    struct ColumnSpec {
        uint32_t flag;
        uint8_t index;
        float position;
        uint8_t justify;
        bool useEmpty;
        int width;
    };

    // useEmpty: true = show "" for placeholder rows, false = show "-"
    // Only position column shows "-" for placeholders to indicate HUD size
    const ColumnSpec specs[] = {
        {COL_TRACKED, 0, m_columns.tracked, Justify::LEFT, true, COL_TRACKED_WIDTH},
        {COL_POS, 1, m_columns.pos, Justify::LEFT, false, COL_POS_WIDTH},  // Show "-" for placeholders
        {COL_RACENUM, 2, m_columns.raceNum, Justify::LEFT, true, COL_RACENUM_WIDTH},
        {COL_NAME, 3, m_columns.name, Justify::LEFT, true, getNameColumnWidth()},
        {COL_BIKE, 4, m_columns.bike, Justify::LEFT, true, COL_BIKE_WIDTH},
        {COL_STATUS, 5, m_columns.status, Justify::LEFT, true, COL_STATUS_WIDTH},
        {COL_PENALTY, 6, m_columns.penalty, Justify::LEFT, true, COL_PENALTY_WIDTH},
        {COL_BEST_LAP, 7, m_columns.bestLap, Justify::LEFT, true, COL_BEST_LAP_WIDTH},
        {COL_GAP, 8, m_columns.gap, Justify::LEFT, true, COL_GAP_WIDTH}
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
    // OPTIMIZATION: Skip all processing when not visible
    // Mouse handling and hover tracking only matter when HUD is rendered
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

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
                        if (m_displayEntries[j].raceNum == region.raceNum) {
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

    // Check if hazard/blue flag icon state changed for any displayed rider
    if (m_enabledColumns & COL_TRACKED) {
        for (const auto& entry : m_displayEntries) {
            if (entry.isPlaceholder || entry.raceNum <= 0) continue;
            uint8_t state = static_cast<uint8_t>(pluginData.getRiderHazardType(entry.raceNum));
            if (pluginData.isRiderBlueFlagged(entry.raceNum)) state |= 0x10;
            const StandingsData* standing = pluginData.getStanding(entry.raceNum);
            if (standing && standing->pit == 1) state |= 0x20;
            if (standing && pluginData.getSessionData().isRiderOnLastLap(standing->numLaps, standing->numLapsAtLeaderFinish)) state |= 0x40;
            auto it = m_cachedIconStates.find(entry.raceNum);
            if (it == m_cachedIconStates.end() || it->second != state) {
                m_cachedIconStates[entry.raceNum] = state;
                setDataDirty();
            }
        }
    }

    // Capture frame time once for consistent timing across all animation logic
    m_frameTime = std::chrono::steady_clock::now();

    // Alternating gap reference: toggle between Leader and Player on interval
    if (m_gapReferenceMode == GapReferenceMode::ALTERNATING) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            m_frameTime - m_lastGapRefToggle).count();
        if (elapsed >= m_alternatingIntervalMs) {
            m_alternatingCurrent = (m_alternatingCurrent == GapReferenceMode::LEADER)
                ? GapReferenceMode::PLAYER : GapReferenceMode::LEADER;
            m_lastGapRefToggle = m_frameTime;
            setDataDirty();
        }
    }

    // Clean up finished animations before rebuilds so we don't do wasteful lookups
    if (hasActiveAnimations()) {
        for (auto it = m_activeAnimations.begin(); it != m_activeAnimations.end(); ) {
            float elapsedMs = std::chrono::duration<float, std::milli>(m_frameTime - it->second.startTime).count();
            if (elapsedMs >= m_animationDurationMs) {
                it = m_activeAnimations.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Keep updating layout during active animations (smooth per-frame interpolation)
    if (hasActiveAnimations()) {
        setLayoutDirty();
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void StandingsHud::rebuildLayout() {

    // Fast path - only update positions
    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // Render all display entries (rider rows + gap rows)
    int rowsToRender = static_cast<int>(m_displayEntries.size());
    auto hudDim = calculateHudDimensions(dim, rowsToRender);

    setBounds(START_X, START_Y, START_X + hudDim.backgroundWidth, START_Y + hudDim.backgroundHeight);

    // Update background quad position
    updateBackgroundQuadPosition(START_X, START_Y, hudDim.backgroundWidth, hudDim.backgroundHeight);

    // Update highlight quad position if it exists
    if (m_cachedHighlightQuadIndex >= 0 && m_cachedHighlightQuadIndex < static_cast<int>(m_quads.size()) &&
        m_cachedPlayerIndex >= 0 && m_cachedPlayerIndex < rowsToRender) {
        // Calculate highlight Y position with animation offset
        float highlightY = hudDim.contentStartY + hudDim.titleHeight + (m_cachedPlayerIndex * dim.lineHeightNormal);
        const auto& playerEntry = m_displayEntries[m_cachedPlayerIndex];
        if (!playerEntry.isPlaceholder && playerEntry.raceNum >= 0) {
            highlightY += getAnimatedRowOffset(playerEntry.raceNum, dim.lineHeightNormal);
        }
        float highlightX = START_X;
        applyOffset(highlightX, highlightY);
        setQuadPositions(m_quads[m_cachedHighlightQuadIndex], highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
    }

    // Update tracked icon quad positions
    if (!m_trackedIconQuads.empty()) {
        // Get tracked column position from column table
        float trackedColPosition = m_columns.tracked;

        // Same size calculations as in renderRiderRow
        constexpr float baseConeSize = 0.006f;
        float baseHalfSize = baseConeSize * m_fScale;
        float spriteHalfSize = baseHalfSize;
        float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
        float colWidth = PluginUtils::calculateMonospaceTextWidth(COL_TRACKED_WIDTH, dim.fontSize);

        for (const auto& iconInfo : m_trackedIconQuads) {
            if (iconInfo.quadIndex >= m_quads.size()) continue;

            // Calculate position for this row with animation offset
            float rowY = hudDim.contentStartY + hudDim.titleHeight + (iconInfo.rowIndex * dim.lineHeightNormal);
            if (iconInfo.rowIndex < static_cast<int>(m_displayEntries.size())) {
                const auto& entry = m_displayEntries[iconInfo.rowIndex];
                if (!entry.isPlaceholder && entry.raceNum >= 0) {
                    rowY += getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
                }
            }
            float spriteCenterX = trackedColPosition + colWidth * 0.25f;
            float spriteCenterY = rowY + dim.lineHeightNormal * 0.5f;

            float x = spriteCenterX, y = spriteCenterY;
            applyOffset(x, y);

            // Update quad positions
            SPluginQuad_t& sprite = m_quads[iconInfo.quadIndex];
            sprite.m_aafPos[0][0] = x - spriteHalfWidth;  // Top-left
            sprite.m_aafPos[0][1] = y - spriteHalfSize;
            sprite.m_aafPos[1][0] = x - spriteHalfWidth;  // Bottom-left
            sprite.m_aafPos[1][1] = y + spriteHalfSize;
            sprite.m_aafPos[2][0] = x + spriteHalfWidth;  // Bottom-right
            sprite.m_aafPos[2][1] = y + spriteHalfSize;
            sprite.m_aafPos[3][0] = x + spriteHalfWidth;  // Top-right
            sprite.m_aafPos[3][1] = y - spriteHalfSize;
        }
    }

    // Update race number plate quad positions
    if (!m_raceNumPlateQuads.empty()) {
        PlateGeometry pg(dim.fontSize, dim.lineHeightNormal);

        for (const auto& plate : m_raceNumPlateQuads) {
            if (plate.numberQuadIndex >= m_quads.size() || plate.brandQuadIndex >= m_quads.size()) continue;

            float rowY = hudDim.contentStartY + hudDim.titleHeight + (plate.rowIndex * dim.lineHeightNormal);
            if (plate.rowIndex < static_cast<int>(m_displayEntries.size())) {
                const auto& entry = m_displayEntries[plate.rowIndex];
                if (!entry.isPlaceholder && entry.raceNum >= 0) {
                    rowY += getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
                }
            }

            float npX = m_columns.raceNum, npY = rowY + pg.platePadY;
            applyOffset(npX, npY);

            // Update number plate quad
            SPluginQuad_t& numPlate = m_quads[plate.numberQuadIndex];
            setQuadPositions(numPlate, npX, npY, pg.plateWidth, pg.plateHeight);

            // Update brand strip quad
            SPluginQuad_t& brandStrip = m_quads[plate.brandQuadIndex];
            float bsLeftX = npX + pg.plateWidth + pg.stripGap;
            setQuadPositions(brandStrip, bsLeftX, npY, pg.brandStripWidth, pg.plateHeight);
        }
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
        // Calculate animation offset for this row
        float animOffset = 0.0f;
        if (i < static_cast<int>(m_displayEntries.size())) {
            const auto& entry = m_displayEntries[i];
            if (!entry.isPlaceholder && entry.raceNum >= 0) {
                animOffset = getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
            }
        }

        // Each row has strings for enabled columns only
        bool isPlaceholder = (i < static_cast<int>(m_displayEntries.size())) && m_displayEntries[i].isPlaceholder;
        for (const auto& col : m_columnTable) {
            // Skip tracked column - it uses quads, not strings
            if (col.columnIndex == COL_IDX_TRACKED) continue;

            if (stringIndex >= m_strings.size()) break;
            float x = col.position;
            // Center race number on plate (must match renderRiderRow logic)
            // Classic layout uses left-aligned #N format, so skip centering
            if (col.columnIndex == COL_IDX_RACENUM && !isPlaceholder && !m_bClassicLayout) {
                float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                x = col.position + charW * 2.0f;  // Center of 4-char plate
            }
            float y = currentY + animOffset;
            applyOffset(x, y);
            m_strings[stringIndex].m_afPos[0] = x;
            m_strings[stringIndex].m_afPos[1] = y;
            stringIndex++;
        }

        currentY += dim.lineHeightNormal;
    }
}

void StandingsHud::rebuildRenderData() {

    clearStrings();
    m_quads.clear();
    m_displayEntries.clear();
    m_cachedHighlightQuadIndex = -1;  // Reset highlight quad tracking
    m_trackedIconQuads.clear();  // Reset icon quad tracking
    m_raceNumPlateQuads.clear(); // Reset race number plate quad tracking

    const PluginData& pluginData = PluginData::getInstance();
    int displayRaceNum = pluginData.getDisplayRaceNum();
    const SessionData& sessionData = pluginData.getSessionData();
    const auto& classificationOrder = pluginData.getDisplayClassificationOrder();

    // Clear stale icon cache when grid changes (new session/race)
    if (classificationOrder.size() != m_cachedIconStates.size()) {
        m_cachedIconStates.clear();
    }

    // Column configuration is now managed by the profile system
    bool isRace = pluginData.isRaceSession();

    // Determine gap data source: use live gap in race sessions when enabled
    bool useLiveGap = isRace && pluginData.isLiveGapsEnabled();

    // Apply gap mode toggle
    uint32_t effectiveColumns = m_enabledColumns;
    if (m_gapMode != GapMode::OFF) {
        effectiveColumns |= COL_GAP;
    } else {
        effectiveColumns &= ~COL_GAP;
    }

    // Apply name mode: OFF removes the column, SHORT/LONG enables it
    if (m_nameMode == NameMode::OFF) {
        effectiveColumns &= ~COL_NAME;
    } else {
        effectiveColumns |= COL_NAME;
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

    const int SEPARATOR_ROWS = 0; // No separator (removed for cleaner display)

    m_cachedPlayerIndex = -1;

    // Show context when player is running OR when spectating/in replay
    int drawState = pluginData.getDrawState();
    bool shouldShowContext = pluginData.isPlayerRunning() ||
                             drawState == PluginConstants::ViewState::SPECTATE ||
                             drawState == PluginConstants::ViewState::REPLAY;

    if (playerPositionInClassification >= 0 && playerPositionInClassification < m_topPositionsCount && shouldShowContext) {
        // Display rider is in top N - show first m_displayRowCount riders
        int entriesToBuild = std::min(static_cast<int>(classificationOrder.size()), m_displayRowCount);
        m_displayEntries.reserve(entriesToBuild);
        addDisplayEntries(0, entriesToBuild - 1, 1, classificationOrder, pluginData);
    } else if (playerPositionInClassification >= m_topPositionsCount && shouldShowContext) {
        // Display rider is beyond top N and (running or spectating) - show top N + rider context
        m_displayEntries.reserve(m_displayRowCount);

        // 1. Add top N riders
        addDisplayEntries(0, m_topPositionsCount - 1, 1, classificationOrder, pluginData);

        // 2. Calculate rider context window
        int availableRows = m_displayRowCount - m_topPositionsCount - SEPARATOR_ROWS;
        int contextBefore = availableRows / 2;
        int contextAfter = availableRows - contextBefore - 1;  // -1 for rider row

        int startIndex = std::max(m_topPositionsCount, playerPositionInClassification - contextBefore);

        // Compensate endIndex when startIndex is clamped (overlap with top N)
        int lostBefore = startIndex - (playerPositionInClassification - contextBefore);
        int desiredEndIndex = playerPositionInClassification + contextAfter + lostBefore;
        int endIndex = std::min(static_cast<int>(classificationOrder.size()) - 1, desiredEndIndex);

        // Compensate startIndex when endIndex is clamped (player near end of field)
        int lostAfter = desiredEndIndex - endIndex;
        if (lostAfter > 0) {
            startIndex = std::max(m_topPositionsCount, startIndex - lostAfter);
        }

        // 3. Add rider context
        addDisplayEntries(startIndex, endIndex, startIndex + 1, classificationOrder, pluginData);
    } else {
        // Display rider not found or no classification - show first m_displayRowCount riders
        int entriesToBuild = std::min(static_cast<int>(classificationOrder.size()), m_displayRowCount);
        m_displayEntries.reserve(entriesToBuild);
        addDisplayEntries(0, entriesToBuild - 1, 1, classificationOrder, pluginData);
    }

    // Add placeholder rows to fill up to configured size
    // This shows the user how big the HUD will be when fully populated
    {
        int placeholderCount = m_displayRowCount - static_cast<int>(m_displayEntries.size());
        if (placeholderCount > 0) {
            // Reserve space for placeholders
            m_displayEntries.reserve(m_displayEntries.size() + placeholderCount);

            // Add placeholder rows at the end
            for (int i = 0; i < placeholderCount; ++i) {
                DisplayEntry placeholder;
                placeholder.isPlaceholder = true;
                strcpy_s(placeholder.formattedPosition, sizeof(placeholder.formattedPosition), Placeholders::GENERIC);
                m_displayEntries.push_back(placeholder);
            }
        }
    }

    // Format strings for all built entries (they're all displayed)
    // Resolve effective gap reference mode (ALTERNATING → current LEADER or PLAYER)
    const GapReferenceMode effectiveGapRef = getEffectiveGapReferenceMode();

    // Get player's gaps for player-relative mode
    const StandingsData* playerStanding = pluginData.getStanding(displayRaceNum);
    int playerOfficialGap = (playerStanding && playerStanding->gap > 0) ? playerStanding->gap : 0;
    int playerGapLaps = playerStanding ? playerStanding->gapLaps : 0;
    int playerLiveGap = (playerStanding && playerStanding->realTimeGap > 0) ? playerStanding->realTimeGap : 0;
    // Player needs valid gap data for relative comparisons to be meaningful.
    // The leader (P1) has gap == 0 legitimately — their zero is valid data, not missing.
    bool playerIsLeader = (!classificationOrder.empty() && classificationOrder[0] == displayRaceNum);
    bool playerHasGapData = playerStanding && (playerStanding->bestLap > 0 || playerStanding->gap > 0 || playerStanding->gapLaps > 0 || playerIsLeader);

    for (size_t entryIdx = 0; entryIdx < m_displayEntries.size(); ++entryIdx) {
        auto& entry = m_displayEntries[entryIdx];
        // Skip formatting for placeholders
        if (entry.isPlaceholder) {
            continue;
        }

        entry.updateFormattedStrings();
        formatStatus(entry, sessionData);

        // Format gap column
        // Two-phase approach:
        //   Phase 1: Compute the gap value (live for same-lap riders, official for lapped/finished)
        //   Phase 2: Apply reference mode offset, scope filtering, and formatting
        bool isPlayerRow = (entry.raceNum == displayRaceNum);
        bool shouldShowGap = shouldShowGapForScope(isPlayerRow);

        bool isGapReference = (effectiveGapRef == GapReferenceMode::LEADER && entry.position == Position::FIRST) ||
                              (effectiveGapRef == GapReferenceMode::PLAYER && isPlayerRow);

        if (entry.state != RiderState::NORMAL) {
            // Non-participants (DNS, retired, DSQ) get no gap
            entry.formattedGap[0] = '\0';
        }
        else if (isGapReference) {
            // Reference rider (leader or player): show contextual info instead of a gap value
            int leaderFinishTime = sessionData.leaderFinishTime;
            bool leaderFinished = (leaderFinishTime >= 0);

            if (!isRace) {
                // Non-race: show best lap time (space-prefixed to align with +/- gaps)
                if (entry.bestLap > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(entry.bestLap, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), " %s", tmp);
                } else {
                    strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                }
            } else if (leaderFinished) {
                // Race finished: show finish time (space-prefixed to align with +/- gaps)
                if (effectiveGapRef == GapReferenceMode::LEADER && leaderFinishTime > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(leaderFinishTime, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), " %s", tmp);
                } else if (effectiveGapRef == GapReferenceMode::PLAYER && playerStanding && playerStanding->finishTime > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(playerStanding->finishTime, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), " %s", tmp);
                } else {
                    entry.gapStyle = DisplayEntry::GapStyle::LABEL;
                    strcpy_s(entry.formattedGap, sizeof(entry.formattedGap),
                        effectiveGapRef == GapReferenceMode::LEADER ? "Leader" : "Player");
                }
            } else {
                // Race in progress: show label
                entry.gapStyle = DisplayEntry::GapStyle::LABEL;
                strcpy_s(entry.formattedGap, sizeof(entry.formattedGap),
                    effectiveGapRef == GapReferenceMode::LEADER ? "Leader" : "Player");
            }
        }
        else if (!shouldShowGap) {
            strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
        }
        else {
            // --- Phase 1: Determine visibility (ADJACENT scope filtering) ---
            bool showThisRow = true;
            int playerIdx = m_cachedPlayerIndex;
            int idx = static_cast<int>(entryIdx);

            if (m_gapMode == GapMode::ADJACENT) {
                showThisRow = (playerIdx >= 0) &&
                              (idx == playerIdx - 1 || idx == playerIdx || idx == playerIdx + 1);
            }

            if (!showThisRow) {
                strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
            } else {
                // --- Phase 2: Compute gap value ---
                // Lapped riders (gapLaps > 0) always use official lap gap — live timing
                // is only meaningful for riders on the same lap as the leader.
                // Finished riders also use official gap (their time is fixed).
                bool isLapped = (entry.gapLaps > 0);
                bool isFinished = pluginData.getSessionData().isRiderFinished(entry.numLaps, entry.numLapsAtLeaderFinish);
                bool canUseLive = useLiveGap && !isLapped && !isFinished && entry.realTimeGap > 0;

                bool isActiveTrackPos = pluginData.hasActiveTrackPos(entry.raceNum) || entry.position == Position::FIRST;
                bool canUseLiveForRider = canUseLive && isActiveTrackPos;

                if (effectiveGapRef == GapReferenceMode::PLAYER && !playerHasGapData) {
                    // Player has no lap time yet — show absolute best lap times as fallback
                    if (entry.bestLap > 0) {
                        char tmp[16];
                        PluginUtils::formatLapTime(entry.bestLap, tmp, sizeof(tmp));
                        snprintf(entry.formattedGap, sizeof(entry.formattedGap), " %s", tmp);
                    } else {
                        strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                    }
                } else if (effectiveGapRef == GapReferenceMode::PLAYER) {
                    // Player-relative gaps
                    if (canUseLiveForRider && (playerLiveGap > 0 || playerIsLeader) &&
                               (entry.realTimeGap > 0 || entry.position == Position::FIRST)) {
                        int relativeGap = entry.realTimeGap - playerLiveGap;
                        PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), relativeGap);
                        entry.gapStyle = DisplayEntry::GapStyle::LIVE;
                    } else if (entry.officialGap > 0 || entry.gapLaps > 0 || entry.position == Position::FIRST) {
                        int relativeLapGap = entry.gapLaps - playerGapLaps;
                        int relativeTimeGap = entry.officialGap - playerOfficialGap;
                        if (relativeLapGap != 0) {
                            snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%+dL", relativeLapGap);
                        } else if (relativeTimeGap != 0) {
                            PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), relativeTimeGap);
                        } else {
                            strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                        }
                    } else {
                        strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                    }
                } else {
                    // Leader-relative gaps
                    if (canUseLiveForRider) {
                        PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), entry.realTimeGap);
                        entry.gapStyle = DisplayEntry::GapStyle::LIVE;
                    } else if (isLapped) {
                        snprintf(entry.formattedGap, sizeof(entry.formattedGap), "+%dL", entry.gapLaps);
                    } else if (entry.officialGap > 0) {
                        PluginUtils::formatTimeDiff(entry.formattedGap, sizeof(entry.formattedGap), entry.officialGap);
                    } else {
                        strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                    }
                }

                // --- Phase 3: Adjacent mode coloring (Player ref only) ---
                // Skip coloring when showing absolute lap times (no player gap data yet)
                if (m_gapMode == GapMode::ADJACENT && effectiveGapRef == GapReferenceMode::PLAYER &&
                    playerHasGapData && playerIdx >= 0 && idx != playerIdx &&
                    strcmp(entry.formattedGap, Placeholders::GENERIC) != 0 && entry.formattedGap[0] != '\0') {
                    entry.gapColorOverride = (idx < playerIdx)
                        ? this->getColor(ColorSlot::NEGATIVE)
                        : this->getColor(ColorSlot::POSITIVE);
                }
            }
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

    // Apply name mode: truncate names for SHORT, calculate long width for LONG
    if (m_nameMode == NameMode::SHORT) {
        for (auto& entry : m_displayEntries) {
            if (!entry.isPlaceholder) {
                entry.name[3] = '\0';  // Truncate to 3 chars
            }
        }
    } else if (m_nameMode == NameMode::LONG) {
        // Find longest name to determine column width
        int maxLen = 3;  // Minimum 3 chars
        for (const auto& entry : m_displayEntries) {
            if (!entry.isPlaceholder) {
                int len = static_cast<int>(strlen(entry.name));
                if (len > maxLen) maxLen = len;
            }
        }
        m_longNameWidth = std::min(maxLen, MAX_LONG_NAME_CHARS) + 1;  // +1 for column spacing
        // Truncate names that exceed the column width cap
        for (auto& entry : m_displayEntries) {
            if (!entry.isPlaceholder && strlen(entry.name) > MAX_LONG_NAME_CHARS) {
                entry.name[MAX_LONG_NAME_CHARS] = '\0';
            }
        }
    }

    // Update animation state (detect position changes, start/clean animations)
    updateAnimationState();

    // Generate render data
    // Apply scale to all dimensions
    auto dim = getScaledDimensions();

    // IMPORTANT: Recalculate column positions and rebuild column table BEFORE calculating dimensions
    // This ensures m_cachedBackgroundWidth is updated before we create the background quad
    float contentStartX = START_X + dim.paddingH;
    int nameColWidth = getNameColumnWidth();
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns, nameColWidth);
    buildColumnTable();  // Rebuild column table and cache width

    // Render all display entries (rider rows + gap rows)
    int rowsToRender = static_cast<int>(m_displayEntries.size());

    // Calculate dimensions based on actual rows that will be rendered
    auto hudDim = calculateHudDimensions(dim, rowsToRender);

    setBounds(START_X, START_Y, START_X + hudDim.backgroundWidth, START_Y + hudDim.backgroundHeight);

    addBackgroundQuad(START_X, START_Y, hudDim.backgroundWidth, hudDim.backgroundHeight);

    float currentY = hudDim.contentStartY;

    // Title
    addTitleString("Standings", hudDim.contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);

    currentY += hudDim.titleHeight;

    // Clear and rebuild click regions for rider selection
    m_riderClickRegions.clear();

    // Render rows (no spacing between rows, consistent with other HUDs)
    for (int i = 0; i < rowsToRender; ++i) {
        const auto& entry = m_displayEntries[i];

        // Apply animation offset (slides row from old position to new position)
        float animOffset = (!entry.isPlaceholder && entry.raceNum >= 0)
            ? getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal) : 0.0f;
        float rowY = currentY + animOffset;

        // Skip highlights for placeholder rows
        if (!entry.isPlaceholder) {
            // Highlight player/spectated rider row with bike brand color background
            if (i == m_cachedPlayerIndex) {
                SPluginQuad_t highlight;
                float highlightX = START_X;
                float highlightY = rowY;
                applyOffset(highlightX, highlightY);  // Apply drag offset
                setQuadPositions(highlight, highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
                highlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

                // Apply transparency to bike brand color (or accent color if configured)
                unsigned long highlightColor = m_bUseAccentForHighlight
                    ? this->getColor(ColorSlot::ACCENT)
                    : entry.bikeBrandColor;
                highlight.m_ulColor = PluginUtils::applyOpacity(highlightColor, 80.0f / 255.0f);

                m_cachedHighlightQuadIndex = static_cast<int>(m_quads.size());  // Track quad index
                m_quads.push_back(highlight);
            }
            // Hover highlight for other riders (spectator mode only)
            else if (i == m_hoveredRowIndex && i != m_cachedPlayerIndex) {
                SPluginQuad_t hoverHighlight;
                float hoverX = START_X;
                float hoverY = rowY;
                applyOffset(hoverX, hoverY);
                setQuadPositions(hoverHighlight, hoverX, hoverY, hudDim.backgroundWidth, dim.lineHeightNormal);
                hoverHighlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

                // Use accent color with transparency for hover
                hoverHighlight.m_ulColor = PluginUtils::applyOpacity(this->getColor(ColorSlot::ACCENT), 60.0f / 255.0f);
                m_quads.push_back(hoverHighlight);
            }
        }

        // Race number plate: quad behind number (primary color) + brand color strip
        // Layout within COL_RACENUM_WIDTH: [plate 4 chars][strip ~0.5 chars][padding ~0.5 chars]
        // Skipped in classic layout (no plates, no brand strip)
        if (isColumnEnabled(COL_RACENUM) && !entry.isPlaceholder && entry.raceNum >= 0 && !m_bClassicLayout) {
            PlateGeometry pg(dim.fontSize, dim.lineHeightNormal);

            // Number plate quad
            SPluginQuad_t numPlate;
            float npX = m_columns.raceNum, npY = rowY + pg.platePadY;
            applyOffset(npX, npY);
            setQuadPositions(numPlate, npX, npY, pg.plateWidth, pg.plateHeight);
            numPlate.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

            // Determine plate color: podium colors for finished P1-P3, muted for DNS/DSQ/RET, primary otherwise
            bool isMutedRider = (entry.state == PluginConstants::RiderState::DNS ||
                                 entry.state == PluginConstants::RiderState::DSQ ||
                                 entry.state == PluginConstants::RiderState::RETIRED);
            unsigned long basePlateColor;
            if (isMutedRider) {
                basePlateColor = this->getColor(ColorSlot::MUTED);
            } else if (entry.isFinishedRace && entry.position == 1) {
                basePlateColor = PluginConstants::PodiumColors::GOLD;
            } else if (entry.isFinishedRace && entry.position == 2) {
                basePlateColor = PluginConstants::PodiumColors::SILVER;
            } else if (entry.isFinishedRace && entry.position == 3) {
                basePlateColor = PluginConstants::PodiumColors::BRONZE;
            } else {
                basePlateColor = this->getColor(ColorSlot::PRIMARY);
            }
            unsigned long plateColor = PluginUtils::applyOpacity(basePlateColor, 230.0f / 255.0f);
            numPlate.m_ulColor = plateColor;

            size_t numPlateIdx = m_quads.size();
            m_quads.push_back(numPlate);

            // Brand color strip quad (right of plate with gap)
            SPluginQuad_t brandStrip;
            float bsLeftX = npX + pg.plateWidth + pg.stripGap;
            setQuadPositions(brandStrip, bsLeftX, npY, pg.brandStripWidth, pg.plateHeight);
            brandStrip.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
            unsigned long stripColor = isMutedRider
                ? PluginUtils::applyOpacity(this->getColor(ColorSlot::MUTED), 180.0f / 255.0f)
                : PluginUtils::applyOpacity(entry.bikeBrandColor, 230.0f / 255.0f);
            brandStrip.m_ulColor = stripColor;

            size_t brandStripIdx = m_quads.size();
            m_quads.push_back(brandStrip);

            m_raceNumPlateQuads.push_back({numPlateIdx, brandStripIdx, i});
        }

        renderRiderRow(entry, entry.isPlaceholder, rowY, dim, i);

        // Add click region for this rider (skip placeholders)
        if (!entry.isPlaceholder && entry.raceNum >= 0) {
            RiderClickRegion region;
            region.x = START_X;
            region.y = rowY;
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

// ============================================================================
// Position Animation
// ============================================================================

float StandingsHud::getAnimatedRowOffset(int raceNum, float lineHeight) const {
    auto it = m_activeAnimations.find(raceNum);
    if (it == m_activeAnimations.end()) return 0.0f;

    const auto& anim = it->second;
    float elapsedMs = std::chrono::duration<float, std::milli>(m_frameTime - anim.startTime).count();
    float t = elapsedMs / m_animationDurationMs;

    if (t >= 1.0f) return 0.0f;  // Animation complete

    float progress = easeOutCubic(t);
    float slotDelta = static_cast<float>(anim.fromSlot - anim.toSlot);  // How many slots to travel
    float remaining = slotDelta * (1.0f - progress);  // Remaining offset (shrinks to 0)
    return remaining * lineHeight;
}

void StandingsHud::updateAnimationState() {
    if (!m_bAnimatePositions) {
        m_previousPositions.clear();
        m_previousSlots.clear();
        m_activeAnimations.clear();
        return;
    }

    auto now = std::chrono::steady_clock::now();

    // Build current maps: raceNum -> race position, raceNum -> display slot index
    std::unordered_map<int, int> currentPositions;
    std::unordered_map<int, int> currentSlots;
    for (int i = 0; i < static_cast<int>(m_displayEntries.size()); ++i) {
        const auto& entry = m_displayEntries[i];
        if (!entry.isPlaceholder && entry.raceNum >= 0) {
            currentPositions[entry.raceNum] = entry.position;
            currentSlots[entry.raceNum] = i;
        }
    }

    // Detect race position changes (not display slot changes from window scrolling)
    int maxSlot = static_cast<int>(m_displayEntries.size()) - 1;
    for (const auto& [raceNum, currentPos] : currentPositions) {
        auto prevIt = m_previousPositions.find(raceNum);
        if (prevIt != m_previousPositions.end() && prevIt->second != currentPos) {
            // Only animate riders that were also visible in the previous frame
            auto prevSlotIt = m_previousSlots.find(raceNum);
            if (prevSlotIt == m_previousSlots.end()) continue;

            auto slotIt = currentSlots.find(raceNum);
            if (slotIt != currentSlots.end()) {
                int currentSlot = slotIt->second;
                int posDelta = prevIt->second - currentPos;  // positive = moved up
                int estimatedPrevSlot = currentSlot - posDelta;
                // Clamp to visible range so animations never start from far off-screen
                estimatedPrevSlot = std::max(-1, std::min(estimatedPrevSlot, maxSlot + 1));
                m_activeAnimations[raceNum] = { estimatedPrevSlot, currentSlot, now };
            }
        }
    }

    // Note: cleanup of finished animations happens in update(), not here.
    // This method only runs on data change; cleanup must run every frame.

    // Update previous positions and slots for next comparison
    m_previousPositions = std::move(currentPositions);
    m_previousSlots = std::move(currentSlots);
}

bool StandingsHud::hasActiveAnimations() const {
    return !m_activeAnimations.empty();
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
    m_gapMode = GapMode::ALL;
    m_gapReferenceMode = GapReferenceMode::PLAYER;
    m_enabledColumns = COL_DEFAULT;
    m_nameMode = NameMode::SHORT;
    m_displayRowCount = DEFAULT_ROW_COUNT;
    m_bAnimatePositions = true;
    m_animationDurationMs = 250.0f;
    m_activeAnimations.clear();
    m_previousPositions.clear();
    m_previousSlots.clear();
    m_cachedIconStates.clear();
    setDataDirty();
}


