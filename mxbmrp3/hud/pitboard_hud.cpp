// ============================================================================
// hud/pitboard_hud.cpp
// Displays pitboard-style information: rider ID, session, position, time, lap,
// split/lap times, gap comparison
// ============================================================================
#include "pitboard_hud.h"

#include <cstring>
#include <cstdio>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"
#include "../core/stats_manager.h"
#include "../core/hud_manager.h"
#include "records_hud.h"

using namespace PluginConstants;

PitboardHud::PitboardHud()
{
    // No caption on this panel -- see BaseHud::m_titleSupported.
    disableTitle();
    // The board artwork IS this HUD -- see BaseHud::m_textureRequired.
    m_textureRequired = true;
    m_packKind = PackKind::Pitboard;
    // One-time setup
    setDraggable(true);
    // Body card: this HUD draws a content BLOCK under its title, which is what the
    // themed card frames. Opt-in; see BaseHud::m_bContentCard.
    m_bContentCard = true;
    m_quads.reserve(1);
    m_strings.reserve(8);  // Up to 7 data elements + optional title

    // No setTextureBaseName, and none passed at registration either: the board's
    // background is the selected PACK's art, resolved through activePack() rather
    // than through BaseHud's texture-variant machinery.

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

void PitboardHud::setPitboardPack(const std::string& name) {
    if (m_pitboardPack == name) return;
    m_pitboardPack = name;
    setDataDirty();
}

const PitboardAsset* PitboardHud::activePack() const {
    const AssetManager& assets = AssetManager::getInstance();
    // Degrade, do not blank: a name this install has no folder for falls back to
    // the shipped board, and m_pitboardPack is deliberately NOT rewritten -- putting
    // the folder back restores the user's choice without them re-picking it.
    if (const PitboardAsset* named = assets.getPitboardByName(m_pitboardPack)) return named;
    return assets.getDefaultPitboard();
}

int PitboardHud::packSprite(PitboardSprite::Part part) const {
    const PitboardAsset* pack = activePack();
    return pack ? pack->sprites[part] : 0;
}

bool PitboardHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::Standings ||
            dataType == DataChangeType::IdealLap ||
            dataType == DataChangeType::SessionData ||
            dataType == DataChangeType::RaceEntries ||
            dataType == DataChangeType::SpectateTarget);
}

int PitboardHud::getEnabledRowCount() const {
    int count = 0;
    if (m_enabledRows & ROW_RIDER_ID) count++;
    if (m_enabledRows & ROW_SESSION) count++;
    if (m_enabledRows & ROW_POSITION) count++;
    if (m_enabledRows & ROW_TIME) count++;
    if (m_enabledRows & ROW_LAP) count++;
    if (m_enabledRows & ROW_LAST_LAP) count++;
    if (m_enabledRows & ROW_GAP) count++;
    return count;
}

float PitboardHud::calculateBackgroundHeight(int /*rowCount*/) const {
    // Layout: 1.0 row padding + title + rows + 1.0 row padding
    auto dim = getScaledDimensions();
    // title-row-exempt: the ONE panel that pairs a NORMAL caption with a LARGE row,
    // and it is deliberate. The board is a picture with its own printed header area;
    // the caption is drawn small (MARKER font at dim.fontSize, in the pack's text colour, over the art)
    // while the row reserved for it is the header band the artwork already has. Routing
    // this through reservedTitleHeight would have to pick a tier, and both are wrong:
    // Normal shrinks the board's header by a cell, Large claims a caption size this
    // panel does not use. The bare row is larger than either tier's band needs, so it
    // over-reserves -- which is the safe direction, and the reason this one may stay.
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    // panelHeight(), not a locally spelled `dim.lineHeightNormal * 1.0f`.
    //
    // The two are EQUAL unthemed -- a normal row is two cells and so is [panel]
    // padding-y -- so the local reads as a harmless synonym. It is not one:
    // dim.paddingV IS contentPaddingY(), which widens the base padding to push
    // content clear of the frame's edge slices, and a panel spelling it locally opts
    // out of that: at the shipped metrics, 12.67px short per side under a themed
    // panel and 50.69px under Debug, with the rows sitting inside the frame.
    //
    // NOT REACHABLE TODAY, and kept anyway. A background texture supersedes the theme
    // (activeTheme() returns nullptr for it), and this HUD's artwork cannot be
    // switched off (m_textureRequired), so there is no state where this panel draws a
    // frame at all. The correct spelling costs nothing and stops the divergence
    // appearing the day that changes -- which is the whole argument for one spelling.
    return panelHeight(dim, titleHeight + MAX_ROW_COUNT * dim.lineHeightNormal);
}

bool PitboardHud::shouldBeVisible() const {
    // Always mode - always visible
    if (m_displayMode == MODE_ALWAYS) {
        return true;
    }

    const PluginData& data = PluginData::getInstance();
    const RiderTrackState* trackPos = data.getPlayerTrackPosition();

    // Pit mode - show from 75% to 95% track position
    if (m_displayMode == MODE_PIT) {
        if (trackPos) {
            float pos = trackPos->trackPos;
            return (pos >= PIT_TRACK_START && pos <= PIT_TRACK_END);
        }
        return false;
    }

    // Splits mode - show for the freeze duration when passing splits or s/f
    if (m_displayMode == MODE_SPLITS) {
        return m_splitsHold.running(m_freezeDurationMs);
    }

    return true;
}

void PitboardHud::update() {
    // OPTIMIZATION: Skip all processing when HUD is disabled by user
    // Note: isVisible() checks m_bVisible (user setting), not shouldBeVisible() (dynamic visibility)
    if (!isVisibleAnySurface()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

    const PluginData& pluginData = PluginData::getInstance();

    // Split and line crossings (split_crossing.h). A new session or spectate
    // target adopts what is there and clears what the board showed.
    bool splitChanged = false;
    const SplitCrossingDetector::Result crossing = m_crossings.poll();
    if (crossing.adopted) {
        m_splitsHold.stop();
        m_displayedTime = -1;
        m_displayedSplit = -1;
        m_isInvalidLap = false;
        setDataDirty();
    }
    if (crossing.line) {
        if (crossing.lapViaPits) {
            // A pit lap is not a timed lap (as on the Timing panel): no time, no
            // INVALID, and in At Splits the board stays down.
            m_displayedTime = -1;
            m_displayedSplit = -1;
            m_isInvalidLap = false;
            setDataDirty();
        } else if (crossing.lapTime > 0) {
            m_displayedTime = crossing.lapTime;
            m_displayedSplit = -1;
            m_isInvalidLap = !crossing.lapValid;
            splitChanged = true;
        }
    }
    if (crossing.splitIndex >= 0) {
        m_displayedTime = crossing.splitTime;
        m_displayedSplit = crossing.splitIndex;
        m_isInvalidLap = false;  // Reset on new split
        splitChanged = true;
    }

    // Handle display mode-specific visibility logic
    if (m_displayMode == MODE_PIT) {
        // PIT mode - check if visibility changed based on track position
        bool isVisible = shouldBeVisible();
        if (isVisible != m_bWasVisibleLastFrame) {
            m_bWasVisibleLastFrame = isVisible;
            setDataDirty();  // Trigger rebuild when visibility changes
        }
    }
    else if (m_displayMode == MODE_SPLITS) {
        // SPLITS mode - trigger timed display when splits change
        if (splitChanged) {
            m_splitsHold.start();
            setDataDirty();
        }

        // Check if timed display should end
        if (m_splitsHold.expire(m_freezeDurationMs)) {
            setDataDirty();
        }
    }
    else if (splitChanged) {
        // ALWAYS mode - just mark dirty when splits change
        setDataDirty();
    }

    // Real-time updates: check if session time changed (like TimeWidget)
    // Only update when visible to avoid unnecessary rebuilds
    if (shouldBeVisible() && (m_enabledRows & ROW_TIME)) {
        int currentTime = pluginData.getSessionTime();
        int currentSeconds = currentTime / 1000;
        int lastSeconds = m_cachedRenderedTime / 1000;

        if (currentSeconds != lastSeconds) {
            m_cachedRenderedTime = currentTime;
            setDataDirty();
        }
    }

    // Handle dirty flags using base class helper
    processDirtyFlags();
}

void PitboardHud::rebuildLayout() {
    // PitboardHud has complex conditional content (time depends on sessionLength,
    // split time depends on m_displayedTime, etc.) that makes a fast layout path
    // error-prone. Always do a full rebuild for reliability.
    rebuildRenderData();
}

void PitboardHud::rebuildRenderData() {
    clearStrings();
    m_quads.clear();

    // Keep BaseHud's background sprite pointing at the active pack - BEFORE the
    // hidden early-out, so the board is known while it is not showing: that is
    // what HudManager::publishGlWarmList hands the GL backend to load ahead of the
    // first show. Assigned only on change: setBackgroundTextureIndex invalidates
    // the theme memo.
    const PitboardAsset* pack = activePack();
    const int packBackground = pack ? pack->sprites[PitboardSprite::BACKGROUND] : 0;
    if (getBackgroundTextureIndex() != packBackground) setBackgroundTextureIndex(packBackground);

    // Check visibility based on display mode.
    //
    // ...unless the Pitboard tab is open (isPreviewing): At-Splits and In-Pit both
    // show the board for a few seconds at a point on the lap, which is no time to
    // drag it anywhere. The board below already fills every row it has no data for
    // with a placeholder, so there is nothing to fake -- it just has to draw.
    if (!shouldBeVisible() && !isPreviewing()) {
        // Not visible - set empty bounds
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Get player data
    const PluginData& data = PluginData::getInstance();
    int displayRaceNum = data.getDisplayRaceNum();

    // Calculate enabled row count and background dimensions
    int enabledRows = getEnabledRowCount();
    float backgroundHeight = calculateBackgroundHeight(enabledRows);
    // Width from the ACTIVE PACK's own proportions, so a board drawn at any aspect
    // keeps its shape instead of being stretched to whatever the rows needed. A
    // compiled aspect would make a custom board at another aspect impossible to get
    // right.
    static const PitboardLayout::BoardGeometry kNoPackGeometry;
    const PitboardLayout::BoardGeometry& layout = pack ? pack->geometry : kNoPackGeometry;
    float backgroundWidth = layout.widthForHeight(backgroundHeight, UI_ASPECT_RATIO);

    // Get dimensions for positioning
    auto dim = getScaledDimensions();
    // title-row-exempt: the ONE panel that pairs a NORMAL caption with a LARGE row,
    // and it is deliberate. The board is a picture with its own printed header area;
    // the caption is drawn small (MARKER font at dim.fontSize, in the pack's text colour, over the art)
    // while the row reserved for it is the header band the artwork already has. Routing
    // this through reservedTitleHeight would have to pick a tier, and both are wrong:
    // Normal shrinks the board's header by a cell, Large claims a caption size this
    // panel does not use. No caption row is emitted here (m_bShowTitle is
    // unreachable-true: disableTitle() in the constructor, and setShowTitle() refuses
    // to set it); the HEIGHT calculation keeps the reservation -- see
    // calculateBackgroundHeight.
    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    float centerX = START_X + (backgroundWidth / 2.0f);
    float leftX = START_X + (backgroundWidth * LEFT_ALIGN_OFFSET);
    float rightX = START_X + (backgroundWidth * RIGHT_ALIGN_OFFSET);
    // The partner of calculateBackgroundHeight()'s panelHeight() -- see there for why
    // a local `dim.lineHeightNormal * 1.0f` is not a synonym.
    float currentY = panelContentY(dim, START_Y);

    // THE BODY CARD, asked for directly rather than through addTitleString: this
    // panel has no caption (disableTitle() in the constructor) and no layout fast
    // path (rebuildLayout does a full rebuild), so nothing wants the empty string a
    // title call would emit.
    //
    // Worth keeping rather than deleting: with a pit board pack installed the pack's
    // artwork supersedes the theme and this draws nothing, but the NO-PACK path is a
    // supported render (kNoPackGeometry above) and there the theme is live.
    emitContentCard(0.0f);

    // Get rider data if available
    const RaceEntryData* raceEntry = (displayRaceNum > 0) ? data.getRaceEntry(displayRaceNum) : nullptr;
    const StandingsData* standing = (displayRaceNum > 0) ? data.getStanding(displayRaceNum) : nullptr;
    const IdealLapData* idealLapData = data.getIdealLapData();
    const SessionData& sessionData = data.getSessionData();
    // Intentionally uses official position, not live — pitboard shows data from official event boundaries
    int position = (displayRaceNum > 0) ? data.getPositionForRaceNum(displayRaceNum) : -1;

    // Row 1: Rider ID (race number + truncated name) - centered
    if (m_enabledRows & ROW_RIDER_ID) {
        char riderIdStr[32];
        if (isPreviewing()) {
            // WHOSE BOARD THIS IS, is what this row answers -- so while the board
            // is only here because its tab is open, it answers that instead. Every
            // other row is the player's real data and stays that way; nothing on
            // this board is invented except this word.
            snprintf(riderIdStr, sizeof(riderIdStr), "%s", "PREVIEW");
        } else if (raceEntry) {
            snprintf(riderIdStr, sizeof(riderIdStr), "%s %s",
                     raceEntry->formattedRaceNum, raceEntry->truncatedName);
        } else if (displayRaceNum > 0) {
            snprintf(riderIdStr, sizeof(riderIdStr), "#%d", displayRaceNum);
        } else {
            snprintf(riderIdStr, sizeof(riderIdStr), "%s", Placeholders::GENERIC);
        }
        float riderIdPosX = centerX + (backgroundWidth * layout.riderIdX);
        float riderIdPosY = currentY + (backgroundHeight * layout.riderIdY);
        addString(riderIdStr, riderIdPosX, riderIdPosY, Justify::CENTER,
                  this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSize, true);
    }
    currentY += dim.lineHeightNormal;

    // Row 2: Session name (e.g., "Practice", "Race 2") - centered
    if (m_enabledRows & ROW_SESSION) {
        const char* sessionName = PluginUtils::getSessionString(sessionData.eventType, sessionData.session);
        if (sessionName) {
            float sessionPosX = centerX + (backgroundWidth * layout.sessionX);
            float sessionPosY = currentY + (backgroundHeight * layout.sessionY);
            addString(sessionName, sessionPosX, sessionPosY, Justify::CENTER,
                      this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSize, true);
        }
    }
    currentY += dim.lineHeightNormal;

    // Row 3: Position (left), Time (center), Lap (right)
    float plY = currentY - (dim.lineHeightNormal * 0.25f);  // Move P and L up a quarter
    if (m_enabledRows & ROW_POSITION) {
        char positionStr[16];
        if (position > 0) {
            snprintf(positionStr, sizeof(positionStr), "P%d", position);
        } else {
            snprintf(positionStr, sizeof(positionStr), "P%s", Placeholders::GENERIC);
        }
        float posPosX = leftX + (backgroundWidth * layout.positionX);
        float posPosY = plY + (backgroundHeight * layout.positionY);
        addString(positionStr, posPosX, posPosY, Justify::LEFT,
                  this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSizeLarge, true);
    }
    if (m_enabledRows & ROW_TIME) {
        char timeStr[24];
        bool isTimedRace = sessionData.sessionLength > 0;
        bool isLapsRace = sessionData.sessionNumLaps > 0;
        int sessionTime = data.getSessionTime();
        if (sessionTime > 0) {
            int minutes = sessionTime / 60000;
            if (isTimedRace && isLapsRace) {
                snprintf(timeStr, sizeof(timeStr), "%dm+%dL", minutes, sessionData.sessionNumLaps);
            } else {
                snprintf(timeStr, sizeof(timeStr), "%dm", minutes);
            }
            float timePosX = centerX + (backgroundWidth * layout.timeX);
            float timePosY = currentY + (backgroundHeight * layout.timeY);
            addString(timeStr, timePosX, timePosY, Justify::CENTER,
                      this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSize, true);
        }
    }
    if (m_enabledRows & ROW_LAP) {
        char lapStr[16];
        bool showLap = false;
        if (standing && standing->numLaps >= 0) {
            int numLaps = standing->numLaps;

            if (sessionData.isRiderFinished(numLaps, standing->numLapsAtLeaderFinish)) {
                strcpy_s(lapStr, sizeof(lapStr), PluginConstants::DisplayStrings::RaceStatus::FINISHED);
                showLap = true;
            } else if (data.isRaceSession() && sessionData.isRiderOnLastLap(numLaps, standing->numLapsAtLeaderFinish)) {
                strcpy_s(lapStr, sizeof(lapStr), PluginConstants::DisplayStrings::RaceStatus::LAST_LAP);
                showLap = true;
            } else if (m_displayMode == MODE_PIT && numLaps > 0) {
                snprintf(lapStr, sizeof(lapStr), "L%d", numLaps);
                showLap = true;
            } else {
                snprintf(lapStr, sizeof(lapStr), "L%d", numLaps + 1);
                showLap = true;
            }
        }
        if (showLap) {
            float lapPosX = rightX + (backgroundWidth * layout.lapX);
            float lapPosY = plY + (backgroundHeight * layout.lapY);
            addString(lapStr, lapPosX, lapPosY, Justify::RIGHT,
                      this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSizeLarge, true);
        }
    }
    currentY += dim.lineHeightNormal;

    // Row 4: Split/Lap time (centered)
    // In Pit mode, show last completed lap time only; in other modes, show current split/lap time
    bool isInvalidLap = false;  // Used by gap row too
    if (m_enabledRows & ROW_LAST_LAP) {
        int timeToShow = 0;
        if (m_displayMode == MODE_PIT) {
            // Only show previous lap time (nothing on lap 1)
            if (idealLapData && idealLapData->lastLapTime > 0) {
                // Check lap log for validity in pit mode. A pit lap is not a
                // timed lap (as on the Timing panel): no time, no INVALID.
                const auto* lapLog = PluginData::getInstance().getLapLog();
                const LapLogEntry* lastLap = (lapLog && !lapLog->empty()) ? &(*lapLog)[0] : nullptr;
                if (!(lastLap && lastLap->viaPits)) {
                    timeToShow = idealLapData->lastLapTime;
                    isInvalidLap = lastLap && !lastLap->isValid;
                }
            }
        } else {
            // In other modes, show current split/lap time
            timeToShow = m_displayedTime;
            isInvalidLap = (m_displayedSplit < 0 && m_isInvalidLap);
        }
        if (timeToShow > 0) {
            char timeStr[16];
            PluginUtils::formatLapTimeTenths(timeToShow, timeStr, sizeof(timeStr));
            float lastLapPosX = centerX + (backgroundWidth * layout.lastLapX);
            float lastLapPosY = currentY + (backgroundHeight * layout.lastLapY);
            addString(timeStr, lastLapPosX, lastLapPosY, Justify::CENTER,
                      this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSize, true);
        }
    }
    currentY += dim.lineHeightNormal;

    // Row 5: Gap comparison (centered)
    // For invalid laps, show "INVALID" on PB/all-time modes (consistent with TimingHud)
    // Leader and ideal gaps still show normally since they aren't personal records
    if (m_enabledRows & ROW_GAP) {
        char gapStr[16];
        bool hasGap = false;
        GapCompareMode effectiveMode = GAP_AUTO;
        int gapMs = calculateCompareGap(hasGap, effectiveMode);

        // Invalid laps show "INVALID" for PB/all-time modes (same as TimingHud)
        bool showInvalid = isInvalidLap &&
                           (effectiveMode == GAP_SESSION_PB || effectiveMode == GAP_ALLTIME_PB);

        if (showInvalid) {
            strcpy_s(gapStr, sizeof(gapStr), Placeholders::INVALID_LAP);
            float gapPosX = centerX + (backgroundWidth * layout.gapX);
            float gapPosY = currentY + (backgroundHeight * layout.gapY);
            addString(gapStr, gapPosX, gapPosY, Justify::CENTER,
                      this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSize, true);
        } else if (hasGap) {
            // Format the gap string
            if (effectiveMode == GAP_LEADER && gapMs <= 0) {
                strcpy_s(gapStr, sizeof(gapStr), PluginConstants::DisplayStrings::RaceStatus::LEADER);
            } else {
                PluginUtils::formatGapCompact(gapStr, sizeof(gapStr), gapMs);
            }

            float gapPosX = centerX + (backgroundWidth * layout.gapX);
            float gapPosY = currentY + (backgroundHeight * layout.gapY);
            addString(gapStr, gapPosX, gapPosY, Justify::CENTER,
                      this->getFont(FontCategory::MARKER), layout.textColor, dim.fontSize, true);
        }
    }
}

int PitboardHud::calculateCompareGap(bool& hasGap, GapCompareMode& effectiveMode) const {
    hasGap = false;
    const PluginData& data = PluginData::getInstance();
    int displayRaceNum = data.getDisplayRaceNum();
    // Intentionally uses official position — gap comparison needs consistent timing-point data
    int position = (displayRaceNum > 0) ? data.getPositionForRaceNum(displayRaceNum) : -1;
    const StandingsData* standing = (displayRaceNum > 0) ? data.getStanding(displayRaceNum) : nullptr;

    // Determine effective mode (resolve AUTO)
    effectiveMode = static_cast<GapCompareMode>(m_gapCompareMode);
    if (effectiveMode == GAP_AUTO) {
        // Auto: use leader gap when racing with others, session PB when solo
        bool isSolo = (data.getStandings().size() <= 1);
        effectiveMode = isSolo ? GAP_SESSION_PB : GAP_LEADER;
    }

    // Leader gap (original behavior)
    if (effectiveMode == GAP_LEADER) {
        if (standing && position > 1 && standing->gap > 0) {
            hasGap = true;
            return standing->gap;
        } else if (position == 1) {
            hasGap = true;
            return 0;  // Is the leader
        }
        return 0;
    }

    // For time-based comparisons, we need a split/lap time to compare against
    if (m_displayedTime <= 0) return 0;

    // The reference's time to the same crossing (split_crossing.h): its
    // accumulated sectors to the split, or its lap time at the line.
    const int split = m_displayedSplit;
    int refTime = -1;
    if (effectiveMode == GAP_SESSION_PB) {
        const LapLogEntry* bestLap = data.getBestLapEntry();
        if (bestLap) refTime = timeToCrossing(bestLap->lapTime, bestLap->sector1, bestLap->sector2, bestLap->sector3, split);
    }
    else if (effectiveMode == GAP_IDEAL) {
        const IdealLapData* idealLapData = data.getIdealLapData();
        if (idealLapData) {
            refTime = timeToCrossing(idealLapData->getIdealLapTime(), idealLapData->bestSector1,
                                     idealLapData->bestSector2, idealLapData->bestSector3, split);
        }
    }
    else if (effectiveMode == GAP_ALLTIME_PB) {
        const StatsPersonalBestData* pb = StatsManager::getInstance().getPersonalBest();
        if (pb && pb->isValid()) refTime = timeToCrossing(pb->lapTime, pb->sector1, pb->sector2, pb->sector3, split);
    }
    else if (effectiveMode == GAP_OVERALL) {
        const LapLogEntry* overallBest = data.getOverallBestLap();
        if (overallBest) {
            // The line against the best lap of any rider
            const int lapTime = data.getOverallBestLapTime();
            refTime = timeToCrossing(lapTime, overallBest->sector1, overallBest->sector2, overallBest->sector3, split);
        }
    }
#if GAME_HAS_RECORDS_PROVIDER
    else if (effectiveMode == GAP_RECORD) {
        // Only compare full laps to records (no sector data available)
        if (split < 0) {
            const RecordsHud& recordsHud = HudManager::getInstance().getRecordsHud();
            refTime = recordsHud.getFastestRecordLapTime();
        }
    }
#endif

    if (refTime > 0) {
        hasGap = true;
        return m_displayedTime - refTime;
    }
    return 0;
}

void PitboardHud::resetToDefaults() {
    m_bVisible = false;
    m_bShowTitle = false;
    // The board artwork IS this panel. Through the setter, not the member: it owns
    // the theme-memo invalidation (and check_hud_helpers.sh fails a HUD that touches
    // the member directly).
    setShowBackgroundTexture(true);
    m_pitboardPack = AssetManager::DEFAULT_PITBOARD;
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    setScale(1.0f);  // 100% default scale
    setPosition(cellsX(1), cellsY(11));
    m_enabledRows = ROW_DEFAULT;
    m_displayMode = MODE_SPLITS;  // Show at splits by default
    m_gapCompareMode = GAP_AUTO;  // Auto: leader when racing, session PB when solo
    m_freezeDurationMs = FreezeDuration::PITBOARD_DEFAULT_MS;
    m_crossings.reset();
    m_splitsHold.stop();
    m_bWasVisibleLastFrame = false;
    m_displayedTime = -1;
    m_displayedSplit = -1;
    m_isInvalidLap = false;
    m_cachedRenderedTime = -1;
    setDataDirty();
}
