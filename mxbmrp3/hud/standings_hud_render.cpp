// ============================================================================
// mxbmrp3/hud/standings_hud_render.cpp
// Column/row rendering + layout helpers + DisplayEntry/CachedIcons
// (extracted verbatim from standings_hud.cpp; no behavior change)
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
#include "../core/director_manager.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#if defined(MXBMRP3_TEST_BUILD)
#include <chrono>
#endif

using namespace PluginConstants;

void StandingsHud::CachedIcons::ensureInitialized() {
    if (initialized) return;
    const AssetManager& assets = AssetManager::getInstance();
    circleExclamation = assets.getIconSpriteIndex("circle-exclamation");
    flag = assets.getIconSpriteIndex("flag");
    flagCheckered = assets.getIconSpriteIndex("flag-checkered");
    wrench = assets.getIconSpriteIndex("wrench");
    caretUp = assets.getIconSpriteIndex("caret-up");
    lock = assets.getIconSpriteIndex("lock");
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
#if defined(MXBMRP3_TEST_BUILD)
                auto _trkStart = std::chrono::steady_clock::now();
#endif
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

                if (!isNonParticipant && DirectorManager::getInstance().isLocked()
                           && DirectorManager::getInstance().getCurrentSubject() == entry.raceNum) {
                    // Director lock: the broadcaster has deliberately pinned the director on this
                    // rider so it won't auto-cut away. Highest priority — it's an explicit
                    // production action, so its confirmation shouldn't be masked by any transient
                    // race-status icon. Semantic WARNING tint.
                    m_iconCache.ensureInitialized();
                    if (m_iconCache.lock > 0) { spriteIndex = m_iconCache.lock; spriteColor = this->getColor(ColorSlot::WARNING); }
                } else if (showHazardIcon) {
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

#if defined(MXBMRP3_TEST_BUILD)
                g_standingsTrackedUs += std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - _trkStart).count();
#endif

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
                case COL_IDX_POSGAIN:     text = isMutedRider ? "" : entry.formattedPosDelta; break;
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
    // placeholder; only actual gains/losses get a caret + count. (Muted DNS/DSQ/RET riders
    // also render an empty cell — see the isMutedRider branch in renderRiderRow — since a
    // stale gain/loss for a rider who has quit is misleading.)
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
