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
    flag = assets.getIconSpriteIndex("flag");
    flagCheckered = assets.getIconSpriteIndex("flag-checkered");
    wrench = assets.getIconSpriteIndex("wrench");
    caretUp = assets.getIconSpriteIndex("caret-up");
    initialized = true;
}

StandingsHud::ColumnPositions::ColumnPositions(float contentStartX, float scale, uint32_t enabledColumns, int nameWidth, int raceNumWidth) {
    float scaledFontSize = FontSizes::NORMAL * scale;
    float current = contentStartX;

    // Use helper function to set column positions (eliminates duplicated lambda)
    // All columns (race mode everywhere)
    PluginUtils::setColumnPosition(enabledColumns, COL_TRACKED, COL_TRACKED_WIDTH, scaledFontSize, current, tracked);
    PluginUtils::setColumnPosition(enabledColumns, COL_POS, COL_POS_WIDTH, scaledFontSize, current, pos);
    PluginUtils::setColumnPosition(enabledColumns, COL_POSGAIN, COL_POSGAIN_WIDTH, scaledFontSize, current, posGain);
    PluginUtils::setColumnPosition(enabledColumns, COL_RACENUM, raceNumWidth, scaledFontSize, current, raceNum);
    PluginUtils::setColumnPosition(enabledColumns, COL_NAME, nameWidth, scaledFontSize, current, name);
    PluginUtils::setColumnPosition(enabledColumns, COL_BIKE, COL_BIKE_WIDTH, scaledFontSize, current, bike);
    PluginUtils::setColumnPosition(enabledColumns, COL_BEST_LAP, COL_BEST_LAP_WIDTH, scaledFontSize, current, bestLap);
    PluginUtils::setColumnPosition(enabledColumns, COL_LAST_LAP, COL_LAST_LAP_WIDTH, scaledFontSize, current, lastLap);
    PluginUtils::setColumnPosition(enabledColumns, COL_GAP, COL_GAP_WIDTH, scaledFontSize, current, gap);
    PluginUtils::setColumnPosition(enabledColumns, COL_PENALTY, COL_PENALTY_WIDTH, scaledFontSize, current, penalty);
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

    // Cache the tracked-rider color (0 when not tracked). Looked up by name, the
    // same way the tracked-column icon resolves it. Used to tint the number plate
    // so broadcasters can show e.g. a red points-leader plate.
    const TrackedRiderConfig* trackedConfig =
        TrackedRidersManager::getInstance().getTrackedRider(entry.name);
    result.trackedColor = trackedConfig ? trackedConfig->color : 0;

    // Format race number without # prefix (standings uses number plate quads instead)
    snprintf(result.formattedRaceNum, sizeof(result.formattedRaceNum), "%d", entry.raceNum);

    return result;
}

float StandingsHud::getColumnTextX(uint8_t columnIndex, float columnPosition, float fontSize, bool isPlaceholder, bool gapRightAlign) const {
    if (isPlaceholder) return columnPosition;
    float charW = PluginUtils::calculateMonospaceTextWidth(1, fontSize);
    if (columnIndex == COL_IDX_POS) {
        // 2-char content field: center (classic) or right edge (modern). A rank is a
        // number, so it right-aligns in the modern layout to stack place values.
        return columnPosition + charW * (m_bClassicLayout ? 1.0f : 2.0f);
    }
    if (columnIndex == COL_IDX_RACENUM) {
        // Classic: right edge of 3-char field. Modern: center of 4-char plate.
        return columnPosition + charW * (m_bClassicLayout ? 3.0f : 2.0f);
    }
    if (columnIndex == COL_IDX_POSGAIN) {
        // Caret + count render as a left-aligned group: the caret sits ~0.9 char in (see
        // renderRiderRow) and the count is left-aligned at ~1.5 char, just right of it. So
        // every row's carets and number left-edges line up, and a 2-digit count ends near
        // ~3.5 char — clear of the 4-char column edge and the # plate beyond it.
        return columnPosition + charW * 1.5f;
    }
    if (columnIndex == COL_IDX_BEST_LAP) {
        // Lap times right-align to the content edge so the fractional seconds line up
        // regardless of minute presence (sub-minute vs M:SS.mmm laps), like the gap column.
        return columnPosition + charW * (COL_BEST_LAP_WIDTH - 1);
    }
    if (columnIndex == COL_IDX_LAST_LAP) {
        // Same right-alignment as the best-lap column (identical M:SS.mmm format).
        return columnPosition + charW * (COL_LAST_LAP_WIDTH - 1);
    }
    if (columnIndex == COL_IDX_GAP && gapRightAlign) {
        // Gap column right-aligns to the content edge (numeric values and labels alike).
        return columnPosition + charW * (COL_GAP_WIDTH - 1);
    }
    if (columnIndex == COL_IDX_PENALTY) {
        // Penalty values ("+5s" / "-") right-align to the content edge.
        return columnPosition + charW * (COL_PENALTY_WIDTH - 1);
    }
    return columnPosition;
}

const char* StandingsHud::getColumnHeaderLabel(uint8_t columnIndex) {
    switch (columnIndex) {
        case COL_IDX_POS:      return "P";
        case COL_IDX_POSGAIN:  return "+/-";
        case COL_IDX_RACENUM:  return "#";
        case COL_IDX_NAME:     return "Name";
        case COL_IDX_BIKE:     return "Bike";
        case COL_IDX_BEST_LAP: return "Best";
        case COL_IDX_LAST_LAP: return "Last";
        case COL_IDX_GAP:      return "Gap";
        case COL_IDX_PENALTY:  return "Pen";
        default:               return "";
    }
}

float StandingsHud::getColumnHeaderTextX(uint8_t columnIndex, float columnPosition, float fontSize, int* outJustify) const {
    // Headers right-align with the gap column the same way data values do.
    bool gapRightAlign = (columnIndex == COL_IDX_GAP);
    float textX = getColumnTextX(columnIndex, columnPosition, fontSize, false, gapRightAlign);
    if (outJustify) {
        int justify = Justify::LEFT;
        if (columnIndex == COL_IDX_POS) {
            justify = m_bClassicLayout ? Justify::CENTER : Justify::RIGHT;
        } else if (columnIndex == COL_IDX_POSGAIN) {
            justify = Justify::CENTER;
        } else if (columnIndex == COL_IDX_RACENUM) {
            justify = m_bClassicLayout ? Justify::RIGHT : Justify::CENTER;
        } else if (gapRightAlign || columnIndex == COL_IDX_PENALTY || columnIndex == COL_IDX_BEST_LAP
                   || columnIndex == COL_IDX_LAST_LAP) {
            justify = Justify::RIGHT;
        }
        *outJustify = justify;
    }
    return textX;
}

void StandingsHud::renderRiderRow(const DisplayEntry& entry, bool isPlaceholder, float currentY, const ScaledDimensions& dim, int rowIndex) {

    const char* placeholder = Placeholders::GENERIC;
    const char* lapTimePlaceholder = Placeholders::LAP_TIME;

    // Determine text color
    unsigned long textColor = this->getColor(ColorSlot::PRIMARY);
    unsigned long mutedColor = this->getColor(ColorSlot::MUTED);
    // Track non-participant (DNS/DSQ/RETIRED) state by rider state, not by comparing
    // the resolved color to muted: a palette may set primary==muted, which would make
    // every rider read as "muted" and suppress the accent/podium/gap styling below.
    bool isMutedRider = false;
    if (!isPlaceholder) {
        using namespace PluginConstants::RiderState;
        if (entry.state == DNS || entry.state == DSQ || entry.state == RETIRED) {
            textColor = this->getColor(ColorSlot::MUTED);
            isMutedRider = true;
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
                        if (m_iconCache.flag > 0) { spriteIndex = m_iconCache.flag; spriteColor = ColorPalette::YELLOW; }
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
                    } else if (sd && pluginData.isRaceSession() && session.isRiderOnLastLap(sd->numLaps, sd->numLapsAtLeaderFinish)) {
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

        // Positions-gained/lost column emits a caret sprite (up = gained, flipped down =
        // lost) alongside the count text. The count is rendered by the normal text path
        // below, so this only adds the sprite and falls through (no 'continue').
        if (col.columnIndex == COL_IDX_POSGAIN && !isPlaceholder && !isMutedRider &&
                entry.hasPosDelta && entry.posDelta != 0) {
            m_iconCache.ensureInitialized();
            if (m_iconCache.caretUp > 0) {
                bool down = (entry.posDelta < 0);
                // Smaller than the status-flag icons (0.006f): caret-up.tga is a solid
                // filled triangle, so it reads chunkier than a flag at the same size.
                constexpr float baseConeSize = 0.0045f;
                float spriteHalfSize = baseConeSize * m_fScale;
                float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
                float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);
                float spriteCenterX = col.position + charW * 0.9f;
                float spriteCenterY = currentY + dim.lineHeightNormal * 0.5f;

                SPluginQuad_t sprite;
                float x = spriteCenterX, y = spriteCenterY;
                applyOffset(x, y);
                // Down = 180° rotation of the up caret (negate both axes), NOT a vertical
                // flip: a flip reverses the vertex winding and the engine back-face-culls
                // non-CCW quads, so the sprite would never draw.
                float sx = down ? -spriteHalfWidth : spriteHalfWidth;
                float sy = down ? -spriteHalfSize  : spriteHalfSize;
                sprite.m_aafPos[0][0] = x - sx; sprite.m_aafPos[0][1] = y - sy;  // Top-left
                sprite.m_aafPos[1][0] = x - sx; sprite.m_aafPos[1][1] = y + sy;  // Bottom-left
                sprite.m_aafPos[2][0] = x + sx; sprite.m_aafPos[2][1] = y + sy;  // Bottom-right
                sprite.m_aafPos[3][0] = x + sx; sprite.m_aafPos[3][1] = y - sy;  // Top-right
                sprite.m_iSprite = m_iconCache.caretUp;
                sprite.m_ulColor = (entry.posDelta > 0) ? this->getColor(ColorSlot::POSITIVE)
                                                        : this->getColor(ColorSlot::NEGATIVE);
                m_posGainIconQuads.push_back({m_quads.size(), rowIndex, down});
                m_quads.push_back(sprite);
            }
        }

        const char* text;

        if (isPlaceholder) {
            text = col.useEmptyForPlaceholder ? "" : placeholder;
        } else {
            // Select data field based on column index (TRACKED handled above as sprite)
            switch (col.columnIndex) {
                case COL_IDX_POS:         text = entry.formattedPosition; break;
                case COL_IDX_POSGAIN:     text = isMutedRider ? placeholder : entry.formattedPosDelta; break;
                case COL_IDX_RACENUM:     text = entry.formattedRaceNum; break;
                case COL_IDX_NAME:        text = entry.name; break;
                case COL_IDX_BIKE:        text = entry.bikeShortName; break;
                case COL_IDX_PENALTY:     text = entry.formattedPenalty; break;
                case COL_IDX_BEST_LAP:    text = entry.formattedLapTime; break;
                case COL_IDX_LAST_LAP:    text = entry.formattedLastLap; break;
                case COL_IDX_GAP:         text = entry.formattedGap; break;
                default: text = ""; break;
            }
        }

        // Use podium colors for position column (P1/P2/P3), secondary for others
        // Skip for non-participants (DNS/DSQ/RET) — they use muted for all columns
        unsigned long columnColor = textColor;

        // Fallback player/spectated rider marker: tint their NAME with the accent
        // color. Only used when the full-row highlight is disabled via INI; with
        // the highlight on (the default) the row background marks the player and
        // the name stays primary.
        if (col.columnIndex == COL_IDX_NAME && !isPlaceholder && rowIndex == m_cachedPlayerIndex
                && !isMutedRider && !m_bPlayerRowHighlight) {
            columnColor = this->getColor(ColorSlot::ACCENT);
        }

        if (col.columnIndex == COL_IDX_POS && !isPlaceholder && entry.position > 0 && !isMutedRider) {
            if (entry.position == Position::FIRST) {
                columnColor = PodiumColors::GOLD;
            } else if (entry.position == Position::SECOND) {
                columnColor = PodiumColors::SILVER;
            } else if (entry.position == Position::THIRD) {
                columnColor = PodiumColors::BRONZE;
            } else {
                columnColor = this->getColor(ColorSlot::TERTIARY);
            }
        }
        // Race number text color: dark on plate background (modern), or secondary in
        // classic. Muted riders (DNS/DSQ/RET) keep the muted color in classic so the
        // whole row dims; in modern the plate itself is muted, so the number stays
        // dark-on-plate for contrast.
        if (col.columnIndex == COL_IDX_RACENUM && !isPlaceholder) {
            if (!m_bClassicLayout) {
                columnColor = this->getColor(ColorSlot::BACKGROUND);
                // White number on a dark custom plate (e.g. black) so it stays
                // legible. The tracked colour now drives the plate whenever the
                // rider isn't muted (it beats the podium plates), so flip the
                // number to light whenever that tracked plate is dark.
                bool plateUsesTracked = !isMutedRider && entry.trackedColor != 0;
                if (plateUsesTracked && PluginUtils::isColorDark(entry.trackedColor)) {
                    columnColor = this->getColor(ColorSlot::PRIMARY);
                }
            } else if (!isMutedRider) {
                columnColor = this->getColor(ColorSlot::SECONDARY);
            }
        } else if (col.columnIndex == COL_IDX_BIKE && !isPlaceholder && !isMutedRider) {
            columnColor = this->getColor(ColorSlot::SECONDARY);
        } else if (col.columnIndex == COL_IDX_PENALTY && !isPlaceholder && entry.penalty > 0) {
            columnColor = this->getColor(ColorSlot::WARNING);
        } else if (col.columnIndex == COL_IDX_POSGAIN && !isPlaceholder && !isMutedRider && entry.hasPosDelta) {
            // Gained = positive color, lost = negative. Held (delta 0) renders the generic
            // placeholder "-" and is muted by the placeholder branch below.
            if (entry.posDelta > 0)      columnColor = this->getColor(ColorSlot::POSITIVE);
            else if (entry.posDelta < 0) columnColor = this->getColor(ColorSlot::NEGATIVE);
        }

        // Use muted color for placeholder values
        if (strcmp(text, placeholder) == 0 || strcmp(text, lapTimePlaceholder) == 0 ||
            strcmp(text, Placeholders::NOT_AVAILABLE) == 0) {
            columnColor = mutedColor;
        }

        // Gap column styling based on GapStyle enum (skip for non-participants, they use muted)
        if (col.columnIndex == COL_IDX_GAP && !isPlaceholder &&
            !isMutedRider && strcmp(text, Placeholders::GENERIC) != 0) {
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

        // Last-lap column: hidden faster/slower coding vs the player's last lap (INI-only,
        // m_bLastLapColorCode). The override is only set for real times on non-player rows,
        // so a 0 override leaves the default/muted color untouched.
        if (col.columnIndex == COL_IDX_LAST_LAP && !isPlaceholder && !isMutedRider &&
            entry.lastLapColorOverride != 0) {
            columnColor = entry.lastLapColorOverride;
        }

        // Use Digits font for numeric columns (BEST_LAP, LAST_LAP, GAP), except text gap labels use normal font
        bool isTextGapLabel = (col.columnIndex == COL_IDX_GAP && !isPlaceholder && entry.gapStyle == DisplayEntry::GapStyle::LABEL && text[0] != '\0' && !isdigit(static_cast<unsigned char>(text[0])));
        int font = (col.columnIndex == COL_IDX_BEST_LAP || col.columnIndex == COL_IDX_LAST_LAP || (col.columnIndex == COL_IDX_GAP && !isTextGapLabel))
            ? this->getFont(FontCategory::DIGITS) : this->getFont(FontCategory::NORMAL);

        // Column alignment differs by layout/content.
        //   Modern:  position right-aligned,  race number centered on the plate.
        //   Classic: position centered,       race number right-aligned (no plate).
        //   PosGain: caret+count group centered (see getColumnTextX).
        //   Best:    right-aligned (lap times line up by fractional seconds).
        //   Gap:     right-aligned (numeric values and text labels alike).
        //   Penalty: right-aligned ("+5s" / "-").
        // X anchor comes from getColumnTextX so the drag fast path stays in sync.
        bool gapRightAlign = (col.columnIndex == COL_IDX_GAP && !isPlaceholder);
        float textX = getColumnTextX(col.columnIndex, col.position, dim.fontSize, isPlaceholder, gapRightAlign);
        int justify = static_cast<int>(col.justify);
        if (!isPlaceholder) {
            if (col.columnIndex == COL_IDX_POS) {
                justify = m_bClassicLayout ? Justify::CENTER : Justify::RIGHT;
            } else if (col.columnIndex == COL_IDX_RACENUM) {
                justify = m_bClassicLayout ? Justify::RIGHT : Justify::CENTER;
            } else if (gapRightAlign || col.columnIndex == COL_IDX_PENALTY || col.columnIndex == COL_IDX_BEST_LAP
                       || col.columnIndex == COL_IDX_LAST_LAP) {
                justify = Justify::RIGHT;
            }
        }
        bool skipShadow = (col.columnIndex == COL_IDX_RACENUM && !isPlaceholder && !m_bClassicLayout);
        addString(text, textX, currentY, justify, font, columnColor, dim.fontSize, skipShadow);
    }
}

void StandingsHud::DisplayEntry::updateFormattedStrings() {
    hasBestLap = (bestLap > 0);
    hasLastLap = (lastLap > 0);

    if (position > 0) {
        snprintf(formattedPosition, sizeof(formattedPosition), "%d", position);
    }
    else {
        strcpy_s(formattedPosition, sizeof(formattedPosition), Placeholders::GENERIC);
    }

    // Positions gained/lost: caret shows direction, so the count is the absolute value.
    // No change (held position) or no reference yet → leave the cell blank rather than a
    // placeholder; only actual gains/losses get a caret + count. (Muted DNS/DSQ riders
    // still render the generic placeholder via the isMutedRider branch in renderRiderRow.)
    // Blank is an empty string the column still emits via addString("") — see addString's
    // contract; it must stay one string per column for the drag fast-path index sync.
    if (hasPosDelta && posDelta != 0) {
        snprintf(formattedPosDelta, sizeof(formattedPosDelta), "%d", posDelta < 0 ? -posDelta : posDelta);
    } else {
        formattedPosDelta[0] = '\0';
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

            // Last lap from the per-rider lap log (newest entry). The game's classification
            // doesn't expose last lap (only best), so it comes from the lap log we build from
            // RaceLap events. Cuts are included on purpose (this column is for race awareness):
            // race-invalid laps keep their real time and are shown; practice-invalid laps come
            // through as 0 and simply render no time.
            const std::deque<LapLogEntry>* riderLog = pluginData.getLapLog(raceNum);
            if (riderLog && !riderLog->empty()) {
                displayEntry.lastLap = (*riderLog)[0].lapTime;
            }

            // Positions gained/lost, measured against the reference for the selected mode.
            // Both ends use the official classification position so the delta is independent
            // of DNS-filter display toggles. References only exist during a race, so this is
            // empty in non-race sessions. RACE_START silently falls back to the last-S/F
            // reference when we joined mid-race (no grid snapshot), so a joiner still sees
            // useful per-lap movement instead of a blank column.
            int refPos = -1;
            switch (m_posGainMode) {
                case PosGainMode::RACE_START:
                    refPos = pd.getRaceStartPosition(raceNum);
                    if (refPos <= 0) refPos = pd.getSfReferencePosition(raceNum);
                    break;
                case PosGainMode::LAST_SF:
                    refPos = pd.getSfReferencePosition(raceNum);
                    break;
                case PosGainMode::LAST_SPLIT:
                    refPos = pd.getSplitReferencePosition(raceNum);
                    break;
                default:
                    break;  // OFF: column not shown
            }
            int curPos = pluginData.getPositionForRaceNum(raceNum);
            if (refPos > 0 && curPos > 0) {
                displayEntry.posDelta = refPos - curPos;
                displayEntry.hasPosDelta = true;
            }

            m_displayEntries.push_back(displayEntry);
        }
    }
}

void StandingsHud::buildColumnTable() {
    m_columnTable.clear();
    m_cachedBackgroundWidth = 0;

    // Build table of enabled columns only (POSGAIN sits between POS and RACENUM in layout)
    // Column indices: 0=TRACKED, 1=POS, 8=POSGAIN, 2=RACENUM, 3=NAME, 4=BIKE, 5=BEST_LAP, 9=LAST_LAP, 6=GAP, 7=PENALTY
    struct ColumnSpec {
        uint32_t flag;
        uint8_t index;
        float position;
        uint8_t justify;
        bool useEmpty;
        int width;
    };

    // useEmpty: all placeholder rows render empty strings (HUD background defines extent)
    const ColumnSpec specs[] = {
        {COL_TRACKED, COL_IDX_TRACKED, m_columns.tracked, Justify::LEFT, true, COL_TRACKED_WIDTH},
        {COL_POS, COL_IDX_POS, m_columns.pos, Justify::LEFT, true, COL_POS_WIDTH},
        {COL_POSGAIN, COL_IDX_POSGAIN, m_columns.posGain, Justify::LEFT, true, COL_POSGAIN_WIDTH},
        {COL_RACENUM, COL_IDX_RACENUM, m_columns.raceNum, Justify::LEFT, true, getRaceNumColumnWidth()},
        {COL_NAME, COL_IDX_NAME, m_columns.name, Justify::LEFT, true, getNameColumnWidth()},
        {COL_BIKE, COL_IDX_BIKE, m_columns.bike, Justify::LEFT, true, COL_BIKE_WIDTH},
        {COL_BEST_LAP, COL_IDX_BEST_LAP, m_columns.bestLap, Justify::LEFT, true, COL_BEST_LAP_WIDTH},
        {COL_LAST_LAP, COL_IDX_LAST_LAP, m_columns.lastLap, Justify::LEFT, true, COL_LAST_LAP_WIDTH},
        {COL_GAP, COL_IDX_GAP, m_columns.gap, Justify::LEFT, true, COL_GAP_WIDTH},
        {COL_PENALTY, COL_IDX_PENALTY, m_columns.penalty, Justify::LEFT, true, COL_PENALTY_WIDTH}
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

    // The title block holds the plain session label (large) and, optionally, a
    // session-info row (normal) below it. Folding the info-row height into
    // titleHeight keeps every downstream "titleHeight + headerHeight" quad offset
    // correct without touching those sites.
    result.titleHeight = (m_bShowTitle ? dim.lineHeightLarge : 0.0f)
        + (m_bShowSessionInfo ? dim.lineHeightNormal : 0.0f);

    // Optional column-header row sits between the title and the rider rows.
    result.headerHeight = m_bShowHeaders ? dim.lineHeightNormal : 0.0f;

    // Use provided rowCount or fall back to m_displayRowCount
    int actualRowCount = (rowCount >= 0) ? rowCount : m_displayRowCount;

    // Calculate total height (no spacing between rows, consistent with other HUDs)
    float totalRowsHeight = actualRowCount * dim.lineHeightNormal;
    result.backgroundHeight = dim.paddingV + result.titleHeight + result.headerHeight + totalRowsHeight + dim.paddingV;

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
            if (standing && pluginData.isRaceSession() && pluginData.getSessionData().isRiderOnLastLap(standing->numLaps, standing->numLapsAtLeaderFinish)) state |= 0x40;
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

    // Keep updating layout during active animations (smooth per-frame interpolation).
    // Slide-highlight quads (COLORED mode) are emitted on the data-change rebuild that
    // started the animation and then updated per-frame in rebuildLayout, so a cheap
    // layout update is sufficient regardless of mode.
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

    // Update player row highlight quad position if the optional legacy row
    // background is enabled. Default behavior (accent-colored name) emits no
    // quad and skips this entirely.
    if (m_cachedHighlightQuadIndex >= 0 && m_cachedHighlightQuadIndex < static_cast<int>(m_quads.size()) &&
        m_cachedPlayerIndex >= 0 && m_cachedPlayerIndex < rowsToRender) {
        float highlightY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (m_cachedPlayerIndex * dim.lineHeightNormal);
        const auto& playerEntry = m_displayEntries[m_cachedPlayerIndex];
        if (!playerEntry.isPlaceholder && playerEntry.raceNum >= 0) {
            highlightY += getAnimatedRowOffset(playerEntry.raceNum, dim.lineHeightNormal);
        }
        float highlightX = START_X;
        applyOffset(highlightX, highlightY);
        setQuadPositions(m_quads[m_cachedHighlightQuadIndex], highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
    }

    // Update slide-highlight quads: position tracks the row's animation offset; alpha
    // fades linearly with animation progress. Finished slides have their alpha set to
    // zero here and remain in m_quads (invisible) until the next data-change rebuild
    // drops them — keeping cached indices for other quad lists stable.
    for (const auto& slide : m_slideHighlightQuads) {
        if (slide.quadIndex >= m_quads.size() || slide.rowIndex >= rowsToRender) continue;

        float fade = getSlideFade(slide.raceNum);

        float slideY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (slide.rowIndex * dim.lineHeightNormal);
        slideY += getAnimatedRowOffset(slide.raceNum, dim.lineHeightNormal);
        float slideX = START_X;
        applyOffset(slideX, slideY);
        setQuadPositions(m_quads[slide.quadIndex], slideX, slideY, hudDim.backgroundWidth, dim.lineHeightNormal);

        unsigned long tintColor = slide.promoted
            ? this->getColor(ColorSlot::POSITIVE)
            : this->getColor(ColorSlot::NEGATIVE);
        m_quads[slide.quadIndex].m_ulColor = PluginUtils::applyOpacity(tintColor, ROW_HIGHLIGHT_OPACITY * fade);
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
            float rowY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (iconInfo.rowIndex * dim.lineHeightNormal);
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

    // Update positions-gained/lost caret quad positions (mirror renderRiderRow geometry)
    if (!m_posGainIconQuads.empty()) {
        constexpr float baseConeSize = 0.0045f;  // keep in sync with renderRiderRow
        float spriteHalfSize = baseConeSize * m_fScale;
        float spriteHalfWidth = spriteHalfSize / UI_ASPECT_RATIO;
        float charW = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSize);

        for (const auto& iconInfo : m_posGainIconQuads) {
            if (iconInfo.quadIndex >= m_quads.size()) continue;

            float rowY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (iconInfo.rowIndex * dim.lineHeightNormal);
            if (iconInfo.rowIndex < static_cast<int>(m_displayEntries.size())) {
                const auto& entry = m_displayEntries[iconInfo.rowIndex];
                if (!entry.isPlaceholder && entry.raceNum >= 0) {
                    rowY += getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal);
                }
            }
            float spriteCenterX = m_columns.posGain + charW * 0.9f;
            float spriteCenterY = rowY + dim.lineHeightNormal * 0.5f;

            float x = spriteCenterX, y = spriteCenterY;
            applyOffset(x, y);
            // Down = 180° rotation (negate both axes) to keep CCW winding; see renderRiderRow.
            float sx = iconInfo.down ? -spriteHalfWidth : spriteHalfWidth;
            float sy = iconInfo.down ? -spriteHalfSize  : spriteHalfSize;

            SPluginQuad_t& sprite = m_quads[iconInfo.quadIndex];
            sprite.m_aafPos[0][0] = x - sx; sprite.m_aafPos[0][1] = y - sy;  // Top-left
            sprite.m_aafPos[1][0] = x - sx; sprite.m_aafPos[1][1] = y + sy;  // Bottom-left
            sprite.m_aafPos[2][0] = x + sx; sprite.m_aafPos[2][1] = y + sy;  // Bottom-right
            sprite.m_aafPos[3][0] = x + sx; sprite.m_aafPos[3][1] = y - sy;  // Top-right
        }
    }

    // Update race number plate quad positions
    if (!m_raceNumPlateQuads.empty()) {
        PlateGeometry pg(dim.fontSize, dim.lineHeightNormal);

        for (const auto& plate : m_raceNumPlateQuads) {
            if (plate.numberQuadIndex >= m_quads.size() || plate.brandQuadIndex >= m_quads.size()) continue;

            float rowY = hudDim.contentStartY + hudDim.titleHeight + hudDim.headerHeight + (plate.rowIndex * dim.lineHeightNormal);
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

    // Title string (index 0, always exists, but may be empty if hidden)
    if (stringIndex < m_strings.size()) {
        float x = hudDim.contentStartX;
        float y = currentY;
        applyOffset(x, y);
        m_strings[stringIndex].m_afPos[0] = x;
        m_strings[stringIndex].m_afPos[1] = y;
        stringIndex++;
    }
    if (m_bShowTitle) currentY += dim.lineHeightLarge;

    // Session-info string (index 1, always exists, but may be empty if disabled)
    if (stringIndex < m_strings.size()) {
        float x = hudDim.contentStartX;
        float y = currentY;
        applyOffset(x, y);
        m_strings[stringIndex].m_afPos[0] = x;
        m_strings[stringIndex].m_afPos[1] = y;
        stringIndex++;
    }
    if (m_bShowSessionInfo) currentY += dim.lineHeightNormal;

    // Column-header row strings (match the emit order/count in rebuildRenderData)
    if (m_bShowHeaders) {
        // Same vertical centering offset as rebuildRenderData (smaller header font)
        float headerY = currentY + labelRowYOffset(dim);
        for (const auto& col : m_columnTable) {
            if (col.columnIndex == COL_IDX_TRACKED) continue;
            if (stringIndex >= m_strings.size()) break;
            float x = getColumnHeaderTextX(col.columnIndex, col.position, dim.fontSize, nullptr);
            float y = headerY;
            applyOffset(x, y);
            m_strings[stringIndex].m_afPos[0] = x;
            m_strings[stringIndex].m_afPos[1] = y;
            stringIndex++;
        }
        currentY += hudDim.headerHeight;
    }

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
            // Same alignment anchor as renderRiderRow (position/race-number columns
            // shift within their cell, gap column right-aligns); shared helper keeps
            // the two paths in sync.
            bool gapRightAlign = (col.columnIndex == COL_IDX_GAP && !isPlaceholder);
            float x = getColumnTextX(col.columnIndex, col.position, dim.fontSize, isPlaceholder, gapRightAlign);
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
    m_posGainIconQuads.clear();  // Reset positions-gained/lost caret quad tracking
    m_raceNumPlateQuads.clear(); // Reset race number plate quad tracking
    m_slideHighlightQuads.clear(); // Reset slide-highlight quad tracking

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
    bool useLiveGap = isRace && m_bLiveGaps;

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

    // Apply positions-gained mode: OFF removes the column, any reference mode enables it
    if (m_posGainMode == PosGainMode::OFF) {
        effectiveColumns &= ~COL_POSGAIN;
    } else {
        effectiveColumns |= COL_POSGAIN;
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

        // Determine if rider has finished (used for icon display and gap logic)
        entry.isFinishedRace = (entry.state == RiderState::NORMAL) &&
            (sessionData.isRiderFinished(entry.numLaps, entry.numLapsAtLeaderFinish) || entry.sessionFinished);

        // Format gap column
        // Two-phase approach:
        //   Phase 1: Compute the gap value (live for same-lap riders, official for lapped/finished)
        //   Phase 2: Apply reference mode offset, scope filtering, and formatting
        bool isPlayerRow = (entry.raceNum == displayRaceNum);
        bool shouldShowGap = shouldShowGapForScope(isPlayerRow);

        bool isGapReference = (effectiveGapRef == GapReferenceMode::LEADER && entry.position == Position::FIRST) ||
                              (effectiveGapRef == GapReferenceMode::PLAYER && isPlayerRow);

        if (entry.state != RiderState::NORMAL) {
            // Non-participants (DNS, retired, DSQ) show status in gap column
            const char* stateAbbr = PluginUtils::getRiderStateAbbreviation(entry.state);
            if (stateAbbr[0] != '\0') {
                strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), stateAbbr);
                entry.gapStyle = DisplayEntry::GapStyle::LABEL;
            } else {
                entry.formattedGap[0] = '\0';
            }
        }
        else if (isGapReference) {
            // Reference rider (leader or player): show contextual info instead of a gap value
            int leaderFinishTime = sessionData.leaderFinishTime;
            bool leaderFinished = (leaderFinishTime >= 0);

            if (!isRace) {
                // Non-race: show best lap time (right-aligned like numeric gaps)
                if (entry.bestLap > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(entry.bestLap, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
                } else {
                    strcpy_s(entry.formattedGap, sizeof(entry.formattedGap), Placeholders::GENERIC);
                }
            } else if (leaderFinished) {
                // Race finished: show finish time (right-aligned like numeric gaps)
                if (effectiveGapRef == GapReferenceMode::LEADER && leaderFinishTime > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(leaderFinishTime, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
                } else if (effectiveGapRef == GapReferenceMode::PLAYER && playerStanding && playerStanding->finishTime > 0) {
                    char tmp[16];
                    PluginUtils::formatLapTime(playerStanding->finishTime, tmp, sizeof(tmp));
                    snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
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
                        snprintf(entry.formattedGap, sizeof(entry.formattedGap), "%s", tmp);
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

        // Format last lap time (cuts included; 0/none -> placeholder)
        if (entry.hasLastLap) {
            PluginUtils::formatLapTime(entry.lastLap, entry.formattedLastLap, sizeof(entry.formattedLastLap));
        }
        else {
            strcpy_s(entry.formattedLastLap, sizeof(entry.formattedLastLap), Placeholders::LAP_TIME);
        }

        // Hidden INI faster/slower coding vs the LOCAL rider's last lap (the display
        // target - your own bike, or the rider you're spectating). Uses the semantic
        // POSITIVE/NEGATIVE palette slots (default green/red, but follows the user's
        // theme), not literal colors. Default off (m_bLastLapColorCode). Skip the local
        // rider's own row and any row without a comparable time; leave the override at 0
        // so the default color applies.
        entry.lastLapColorOverride = 0;
        if (m_bLastLapColorCode && entry.hasLastLap &&
            m_cachedPlayerIndex >= 0 && m_cachedPlayerIndex < static_cast<int>(m_displayEntries.size()) &&
            static_cast<int>(entryIdx) != m_cachedPlayerIndex) {
            int playerLastLap = m_displayEntries[m_cachedPlayerIndex].lastLap;
            if (playerLastLap > 0 && entry.lastLap != playerLastLap) {
                entry.lastLapColorOverride = (entry.lastLap > playerLastLap)
                    ? this->getColor(ColorSlot::POSITIVE)   // slower than you → POSITIVE slot
                    : this->getColor(ColorSlot::NEGATIVE);  // faster than you → NEGATIVE slot
            }
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
            if (!entry.isPlaceholder && static_cast<int>(strlen(entry.name)) > m_shortNameChars) {
                entry.name[m_shortNameChars] = '\0';  // Truncate to configured char count
            }
        }
    } else if (m_nameMode == NameMode::LONG) {
        // Static column width (m_longNameChars); names beyond it get the shared
        // ellipsis truncation. No longest-name scan, so the table doesn't reflow
        // as riders join/leave. SHORT mode above stays a deliberate hard cut.
        for (auto& entry : m_displayEntries) {
            if (!entry.isPlaceholder && static_cast<int>(strlen(entry.name)) > m_longNameChars) {
                std::string fitted = PluginUtils::fitText(entry.name, m_longNameChars);
                strncpy_s(entry.name, sizeof(entry.name), fitted.c_str(), _TRUNCATE);
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
    m_columns = ColumnPositions(contentStartX, m_fScale, m_enabledColumns, nameColWidth, getRaceNumColumnWidth());
    buildColumnTable();  // Rebuild column table and cache width

    // Render all display entries (rider rows + gap rows)
    int rowsToRender = static_cast<int>(m_displayEntries.size());

    // Calculate dimensions based on actual rows that will be rendered
    auto hudDim = calculateHudDimensions(dim, rowsToRender);

    setBounds(START_X, START_Y, START_X + hudDim.backgroundWidth, START_Y + hudDim.backgroundHeight);

    addBackgroundQuad(START_X, START_Y, hudDim.backgroundWidth, hudDim.backgroundHeight);

    float currentY = hudDim.contentStartY;

    // Title: static "Standings" caption in the standard title style, toggled by
    // the shared title control. addTitleString keeps string index 0 stable
    // (emits an empty string when the title is hidden).
    addTitleString("Standings", hudDim.contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSizeLarge);
    if (m_bShowTitle) currentY += dim.lineHeightLarge;

    // Session-info row: context-aware "<session>: <clock / leader lap / overtime>"
    // on a single line below the title (e.g. "Race 2: FINAL LAP"). The overtime
    // label ("N TO GO" / "FINAL LAP" / "CHECKERED") replaces the frozen 00:00 once
    // a time+lap clock expires. Always emitted (empty when disabled) so string
    // index 1 stays stable for the rebuildLayout fast path.
    char sessionInfoBuf[48] = "";
    if (m_bShowSessionInfo) {
        const char* sessionLabel = PluginUtils::getSessionString(sessionData.eventType, sessionData.session);
        if (!sessionLabel) sessionLabel = Placeholders::GENERIC;

        char value[24] = "";
        if (sessionData.sessionLength > 0) {
            // Timed (or timed+lap) session: live countdown, or the overtime label.
            PluginUtils::formatSessionClock(pluginData.getLeaderLapsToGo(),
                pluginData.getSessionTime(), value, sizeof(value));
        } else if (sessionData.sessionNumLaps > 0) {
            // Pure lap race: "CHECKERED" once the leader crosses the line on the final
            // lap; the session clock (a count-up elapsed timer for lap races) before the
            // race goes green; otherwise the leader's current lap / total laps. Reuse
            // isRiderFinished so the threshold matches the FinalLap/finished logic, and
            // so laps-only races read consistently with the time+lap overtime label.
            const StandingsData* leaderStanding = classificationOrder.empty()
                ? nullptr : pluginData.getStanding(classificationOrder[0]);
            bool leaderFinished = leaderStanding && sessionData.isRiderFinished(
                leaderStanding->numLaps, leaderStanding->numLapsAtLeaderFinish);
            bool raceInProgress = (sessionData.sessionState & SessionState::IN_PROGRESS) != 0;
            if (leaderFinished) {
                // Checked first so the post-race state (also "not in progress") keeps the
                // checkered label instead of falling back to the pre-race timer below.
                strcpy_s(value, sizeof(value), "CHECKERED");
            } else if (!raceInProgress) {
                // Pre-race (not yet green): show the session clock like timed races do,
                // instead of a static "Lap 1/N" before anyone has turned a lap.
                PluginUtils::formatSessionClock(pluginData.getLeaderLapsToGo(),
                    pluginData.getSessionTime(), value, sizeof(value));
            } else {
                int leaderLap = leaderStanding ? leaderStanding->numLaps + 1 : 1;  // numLaps = completed → 1-based current
                if (leaderLap < 1) leaderLap = 1;
                if (leaderLap > sessionData.sessionNumLaps) leaderLap = sessionData.sessionNumLaps;
                snprintf(value, sizeof(value), "Lap %d/%d", leaderLap, sessionData.sessionNumLaps);
            }
        }

        if (value[0] != '\0') {
            snprintf(sessionInfoBuf, sizeof(sessionInfoBuf), "%s: %s", sessionLabel, value);
        } else {
            snprintf(sessionInfoBuf, sizeof(sessionInfoBuf), "%s", sessionLabel);
        }
    }
    addString(sessionInfoBuf, hudDim.contentStartX, currentY, Justify::LEFT,
        this->getFont(FontCategory::TITLE), this->getColor(ColorSlot::PRIMARY), dim.fontSize);
    if (m_bShowSessionInfo) currentY += dim.lineHeightNormal;

    // Optional column-header row. Emits one string per enabled column (skipping the
    // status-icon column, which has no label), in the same column-table order the
    // per-row strings use so the rebuildLayout fast path can reposition them by index.
    if (m_bShowHeaders) {
        unsigned long headerColor = this->getColor(ColorSlot::TERTIARY);
        int headerFont = this->getFont(FontCategory::STRONG);
        for (const auto& col : m_columnTable) {
            if (col.columnIndex == COL_IDX_TRACKED) continue;
            int justify = Justify::LEFT;
            float textX = getColumnHeaderTextX(col.columnIndex, col.position, dim.fontSize, &justify);
            addLabel(getColumnHeaderLabel(col.columnIndex), textX, currentY, justify,
                headerFont, headerColor, dim);
        }
        currentY += hudDim.headerHeight;
    }

    // Clear and rebuild click regions for rider selection
    m_riderClickRegions.clear();

    // Render rows (no spacing between rows, consistent with other HUDs)
    for (int i = 0; i < rowsToRender; ++i) {
        const auto& entry = m_displayEntries[i];

        // Apply animation offset (slides row from old position to new position)
        float animOffset = (!entry.isPlaceholder && entry.raceNum >= 0)
            ? getAnimatedRowOffset(entry.raceNum, dim.lineHeightNormal) : 0.0f;
        float rowY = currentY + animOffset;

        // Colored animation mode: tint row positive/negative while animating.
        // Skipped on the player row while the row highlight is on (the default), so
        // the accent/brand background stays unobstructed (no crossfade, no flicker —
        // slide direction on the player row is conveyed by the row-position change itself).
        // The quad index is cached so rebuildLayout can update its position + alpha
        // each frame without forcing a full data rebuild.
        bool suppressSlideForPlayerRow = (m_bPlayerRowHighlight && i == m_cachedPlayerIndex);
        if (m_animationMode == AnimationMode::COLORED && !entry.isPlaceholder && entry.raceNum >= 0
                && !suppressSlideForPlayerRow) {
            float slideFade = getSlideFade(entry.raceNum);
            if (slideFade > 0.0f) {
                auto animIt = m_activeAnimations.find(entry.raceNum);
                bool promoted = (animIt != m_activeAnimations.end())
                    && (animIt->second.fromSlot > animIt->second.toSlot);
                unsigned long tintColor = promoted
                    ? this->getColor(ColorSlot::POSITIVE)
                    : this->getColor(ColorSlot::NEGATIVE);

                SPluginQuad_t slide;
                float slideX = START_X;
                float slideY = rowY;
                applyOffset(slideX, slideY);
                setQuadPositions(slide, slideX, slideY, hudDim.backgroundWidth, dim.lineHeightNormal);
                slide.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                slide.m_ulColor = PluginUtils::applyOpacity(tintColor, ROW_HIGHLIGHT_OPACITY * slideFade);
                m_slideHighlightQuads.push_back({m_quads.size(), i, entry.raceNum, promoted});
                m_quads.push_back(slide);
            }
        }

        // Skip highlights for placeholder rows
        if (!entry.isPlaceholder) {
            // Player/spectated row highlight (full-row background, accent color by
            // default, bike brand color via INI). On by default; when disabled via
            // INI the accent-colored name marker in renderRiderRow takes over.
            if (m_bPlayerRowHighlight && i == m_cachedPlayerIndex) {
                SPluginQuad_t highlight;
                float highlightX = START_X;
                float highlightY = rowY;
                applyOffset(highlightX, highlightY);
                setQuadPositions(highlight, highlightX, highlightY, hudDim.backgroundWidth, dim.lineHeightNormal);
                highlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;

                unsigned long highlightColor;
                if (m_bPlayerRowHighlightBrand) {
                    // Brand mode: use the bike's brand color, but fall back to the
                    // muted slot when the bike has no real brand mapping (all GPB/KRP
                    // bikes and brand-less MXB bikes resolve to the neutral gray
                    // sentinel) so the bar stays theme-aware instead of off-palette gray.
                    highlightColor = (entry.bikeBrandColor == PluginConstants::BrandColors::DEFAULT)
                        ? this->getColor(ColorSlot::MUTED)
                        : entry.bikeBrandColor;
                } else {
                    highlightColor = this->getColor(ColorSlot::ACCENT);
                }
                highlight.m_ulColor = PluginUtils::applyOpacity(highlightColor, ROW_HIGHLIGHT_OPACITY);

                m_cachedHighlightQuadIndex = static_cast<int>(m_quads.size());
                m_quads.push_back(highlight);
            }
            // Hover highlight for other riders (spectator mode only). Uses the muted
            // slot to stay visually distinct from the player's own accent highlight.
            else if (i == m_hoveredRowIndex && i != m_cachedPlayerIndex) {
                SPluginQuad_t hoverHighlight;
                float hoverX = START_X;
                float hoverY = rowY;
                applyOffset(hoverX, hoverY);
                setQuadPositions(hoverHighlight, hoverX, hoverY, hudDim.backgroundWidth, dim.lineHeightNormal);
                hoverHighlight.m_iSprite = PluginConstants::SpriteIndex::SOLID_COLOR;
                hoverHighlight.m_ulColor = PluginUtils::applyOpacity(
                    this->getColor(ColorSlot::MUTED), HOVER_HIGHLIGHT_OPACITY);
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
            } else if (entry.trackedColor != 0) {
                // Tracked riders keep their custom plate colour (e.g. a red
                // points-leader plate) even when finishing on the podium; only the
                // muted (DNS/RET/DSQ) state takes precedence over it.
                basePlateColor = entry.trackedColor;
            } else if (entry.isFinishedRace && entry.position == 1) {
                basePlateColor = PluginConstants::PodiumColors::GOLD;
            } else if (entry.isFinishedRace && entry.position == 2) {
                basePlateColor = PluginConstants::PodiumColors::SILVER;
            } else if (entry.isFinishedRace && entry.position == 3) {
                basePlateColor = PluginConstants::PodiumColors::BRONZE;
            } else {
                // Default plate: secondary colour (dark number stays legible on it).
                basePlateColor = this->getColor(ColorSlot::SECONDARY);
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
            // Brand color always visible; dimmed for non-participants (DNS/RET/DSQ)
            float stripOpacity = isMutedRider ? 100.0f / 255.0f : 230.0f / 255.0f;
            unsigned long stripColor = PluginUtils::applyOpacity(entry.bikeBrandColor, stripOpacity);
            brandStrip.m_ulColor = stripColor;

            size_t brandStripIdx = m_quads.size();
            m_quads.push_back(brandStrip);

            m_raceNumPlateQuads.push_back({numPlateIdx, brandStripIdx, i});
        }

        renderRiderRow(entry, entry.isPlaceholder, rowY, dim, i);

        // Add click region for this rider so they can be hover-highlighted and
        // clicked to spectate — but only for riders actually on track. Anyone who
        // can't be spectated (DNS/DSQ/retired/unknown, e.g. left the server) or is
        // sitting in the pits gets no region, so they're neither highlighted on
        // hover nor clickable. This is the single chokepoint for both behaviors.
        bool isSpectatable = (entry.state == PluginConstants::RiderState::NORMAL) && (entry.pit != 1);
        if (!entry.isPlaceholder && entry.raceNum >= 0 && isSpectatable) {
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

float StandingsHud::getSlideFade(int raceNum) const {
    auto it = m_activeAnimations.find(raceNum);
    if (it == m_activeAnimations.end()) return 0.0f;
    float elapsedMs = std::chrono::duration<float, std::milli>(m_frameTime - it->second.startTime).count();
    float t = elapsedMs / m_animationDurationMs;
    if (t >= 1.0f) return 0.0f;
    return 1.0f - t;
}

void StandingsHud::updateAnimationState() {
    if (m_animationMode == AnimationMode::OFF) {
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
                // Estimate the previous slot from the race-position delta instead of
                // reading m_previousSlots directly. m_previousSlots tracks the actual
                // slot occupied last frame, but the visible window can scroll
                // independently (DNS filter changes, top-N pinning, spectator switch),
                // and using the raw previous slot would inflate the slide distance
                // when only the window moved. Posing the delta in race-position space
                // produces the correct visual move for pure overtakes, and degrades
                // gracefully when both an overtake and a window shift happen in the
                // same frame.
                int posDelta = prevIt->second - currentPos;  // positive = moved up
                int estimatedPrevSlot = currentSlot + posDelta;
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
    m_posGainMode = PosGainMode::OFF;
    m_enabledColumns = COL_DEFAULT;
    m_nameMode = NameMode::SHORT;
    m_shortNameChars = DEFAULT_SHORT_NAME_CHARS;
    m_longNameChars = DEFAULT_LONG_NAME_CHARS;
    m_displayRowCount = DEFAULT_ROW_COUNT;
    m_topPositionsCount = DEFAULT_TOP_POSITIONS;
    m_bPlayerRowHighlight = true;
    m_bPlayerRowHighlightBrand = false;
    m_bClassicLayout = false;
    m_animationMode = AnimationMode::BASIC;
    m_animationDurationMs = 500.0f;
    m_bShowHeaders = false;
    m_bShowSessionInfo = true;
    m_bLiveGaps = false;
    m_activeAnimations.clear();
    m_previousPositions.clear();
    m_previousSlots.clear();
    m_cachedIconStates.clear();
    setDataDirty();
}


