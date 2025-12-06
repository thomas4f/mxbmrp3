// ============================================================================
// hud/pitboard_hud.cpp
// Displays pitboard-style information: rider ID, session, position, time, lap,
// split/lap times, gap to leader
// ============================================================================
#include "pitboard_hud.h"

#include <cstring>
#include <cstdio>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_constants.h"
#include "../core/plugin_data.h"

using namespace PluginConstants;

PitboardHud::PitboardHud()
{
    DEBUG_INFO("PitboardHud created");
    setDraggable(true);

    // Set defaults
    m_bShowTitle = false;  // Pitboard typically doesn't have a title
    m_bShowBackgroundTexture = true;  // Show texture by default
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.0f;  // 100% default scale
    m_displayMode = MODE_SPLITS;  // Show at splits by default
    setPosition(0.0f, 0.0222f);

    // Pre-allocate vectors
    m_quads.reserve(1);
    m_strings.reserve(8);  // Up to 7 data elements + optional title

    rebuildRenderData();
}

bool PitboardHud::handlesDataType(DataChangeType dataType) const {
    return (dataType == DataChangeType::Standings ||
            dataType == DataChangeType::SessionBest ||
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
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;
    float padding = dim.lineHeightNormal * 1.0f;
    return padding + titleHeight + (MAX_ROW_COUNT * dim.lineHeightNormal) + padding;
}

bool PitboardHud::shouldBeVisible() const {
    // Always mode - always visible
    if (m_displayMode == MODE_ALWAYS) {
        return true;
    }

    const PluginData& data = PluginData::getInstance();
    const TrackPositionData* trackPos = data.getPlayerTrackPosition();

    // Pit mode - show from 75% to 95% track position
    if (m_displayMode == MODE_PIT) {
        if (trackPos) {
            float pos = trackPos->trackPos;
            return (pos >= PIT_TRACK_START && pos <= PIT_TRACK_END);
        }
        return false;
    }

    // Splits mode - show for 10 seconds when passing splits or s/f
    if (m_displayMode == MODE_SPLITS) {
        if (m_bIsDisplayingTimed) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_displayStartTime).count();
            return elapsed < DISPLAY_DURATION_MS;
        }
        return false;
    }

    return true;
}

void PitboardHud::update() {
    const PluginData& pluginData = PluginData::getInstance();

    // Detect spectate target changes and reset caches
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    bool targetChanged = (currentDisplayRaceNum != m_cachedDisplayRaceNum);

    // Also detect when underlying data has been cleared (session change)
    // If we have cached splits but CurrentLapData is null or empty, reset caches
    const CurrentLapData* currentLap = pluginData.getCurrentLapData();
    const SessionBestData* sessionBest = pluginData.getSessionBestData();
    bool dataCleared = (m_cachedSplit1 > 0 || m_cachedSplit2 > 0 || m_cachedLastLapTime > 0) &&
                       (!currentLap || (currentLap->split1 <= 0 && currentLap->split2 <= 0)) &&
                       (!sessionBest || sessionBest->lastLapTime <= 0);

    if (targetChanged || dataCleared) {
        // Reset all cached values
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;
        m_cachedLastLapTime = -1;
        m_cachedDisplayRaceNum = currentDisplayRaceNum;
        m_bIsDisplayingTimed = false;
        m_displayedTime = -1;
        m_splitType = LAP;

        // Update cached values with new rider's current data (without triggering display)
        if (currentLap) {
            m_cachedSplit1 = currentLap->split1;
            m_cachedSplit2 = currentLap->split2;
        }
        if (sessionBest) {
            m_cachedLastLapTime = sessionBest->lastLapTime;
        }
        setDataDirty();
    }

    // Always check for split times (for timing display in all modes)
    bool splitChanged = false;

    // Check current lap splits
    if (currentLap) {
        // Check split 1 (accumulated time to S1)
        if (currentLap->split1 > 0 && currentLap->split1 != m_cachedSplit1) {
            m_cachedSplit1 = currentLap->split1;
            m_displayedTime = currentLap->split1;
            m_splitType = SPLIT_1;
            splitChanged = true;
        }
        // Check split 2 (accumulated time to S2)
        if (currentLap->split2 > 0 && currentLap->split2 != m_cachedSplit2) {
            m_cachedSplit2 = currentLap->split2;
            m_displayedTime = currentLap->split2;
            m_splitType = SPLIT_2;
            splitChanged = true;
        }
    }

    // Check for lap completion (split 3 / finish line)
    if (sessionBest && sessionBest->lastLapTime > 0 &&
        sessionBest->lastLapTime != m_cachedLastLapTime) {
        m_cachedLastLapTime = sessionBest->lastLapTime;
        m_displayedTime = sessionBest->lastLapTime;
        m_splitType = LAP;
        // Reset split caches for next lap
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;
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
            m_displayStartTime = std::chrono::steady_clock::now();
            m_bIsDisplayingTimed = true;
            setDataDirty();
        }

        // Check if timed display should end
        if (m_bIsDisplayingTimed && !shouldBeVisible()) {
            m_bIsDisplayingTimed = false;
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

    // Check if data changed or layout dirty
    if (isDataDirty()) {
        // Data changed - full rebuild needed (also rebuilds layout)
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();  // Clear layout dirty too since full rebuild done
    }
    else if (isLayoutDirty()) {
        // Only layout changed (e.g., dragging) - fast path
        rebuildLayout();
        clearLayoutDirty();
    }
}

void PitboardHud::rebuildLayout() {
    // PitboardHud has complex conditional content (time depends on sessionLength,
    // split time depends on m_displayedTime, etc.) that makes a fast layout path
    // error-prone. Always do a full rebuild for reliability.
    rebuildRenderData();
}

void PitboardHud::rebuildRenderData() {
    m_strings.clear();
    m_quads.clear();

    // Check visibility based on display mode
    if (!shouldBeVisible()) {
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
    // Match pitboard_hud.tga aspect ratio (1920x1080), corrected for UI aspect ratio
    float backgroundWidth = (backgroundHeight * TEXTURE_ASPECT_RATIO) / UI_ASPECT_RATIO;

    // Get dimensions for positioning
    auto dim = getScaledDimensions();
    float titleHeight = m_bShowTitle ? dim.lineHeightLarge : 0.0f;

    setBounds(START_X, START_Y, START_X + backgroundWidth, START_Y + backgroundHeight);
    addBackgroundQuad(START_X, START_Y, backgroundWidth, backgroundHeight);

    // Layout with 1.0 row top padding
    float centerX = START_X + (backgroundWidth / 2.0f);
    float leftX = START_X + (backgroundWidth * LEFT_ALIGN_OFFSET);
    float rightX = START_X + (backgroundWidth * RIGHT_ALIGN_OFFSET);
    float currentY = START_Y + (dim.lineHeightNormal * 1.0f);

    // Title row (optional)
    if (m_bShowTitle) {
        addTitleString("Pitboard", centerX, currentY, Justify::CENTER,
            Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSize);
        currentY += titleHeight;
    }

    // Get rider data if available
    const RaceEntryData* raceEntry = (displayRaceNum > 0) ? data.getRaceEntry(displayRaceNum) : nullptr;
    const StandingsData* standing = (displayRaceNum > 0) ? data.getStanding(displayRaceNum) : nullptr;
    const SessionBestData* sessionBest = data.getSessionBestData();
    const SessionData& sessionData = data.getSessionData();
    int position = (displayRaceNum > 0) ? data.getPositionForRaceNum(displayRaceNum) : -1;

    // Row 1: Rider ID (race number + truncated name) - centered
    if (m_enabledRows & ROW_RIDER_ID) {
        char riderIdStr[32];
        if (raceEntry) {
            snprintf(riderIdStr, sizeof(riderIdStr), "%s %s",
                     raceEntry->formattedRaceNum, raceEntry->truncatedName);
        } else if (displayRaceNum > 0) {
            snprintf(riderIdStr, sizeof(riderIdStr), "#%d", displayRaceNum);
        } else {
            snprintf(riderIdStr, sizeof(riderIdStr), "%s", Placeholders::GENERIC);
        }
        addString(riderIdStr, centerX, currentY, Justify::CENTER,
                  Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSize);
    }
    currentY += dim.lineHeightNormal;

    // Row 2: Session name (e.g., "Practice", "Race 2") - centered
    if (m_enabledRows & ROW_SESSION) {
        const char* sessionName = PluginUtils::getSessionString(sessionData.eventType, sessionData.session);
        if (sessionName) {
            addString(sessionName, centerX, currentY, Justify::CENTER,
                      Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSize);
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
        addString(positionStr, leftX, plY, Justify::LEFT,
                  Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSizeLarge);
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
            addString(timeStr, centerX, currentY, Justify::CENTER,
                      Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSize);
        }
    }
    if (m_enabledRows & ROW_LAP) {
        char lapStr[16];
        bool showLap = false;
        if (standing && standing->numLaps >= 0) {
            if (m_displayMode == MODE_PIT) {
                // In Pit mode, show previous lap number (empty on lap 1)
                if (standing->numLaps > 0) {
                    snprintf(lapStr, sizeof(lapStr), "L%d", standing->numLaps);
                    showLap = true;
                }
            } else {
                // In other modes, show current lap number
                int currentLap = standing->numLaps + 1;
                snprintf(lapStr, sizeof(lapStr), "L%d", currentLap);
                showLap = true;
            }
        }
        if (showLap) {
            addString(lapStr, rightX, plY, Justify::RIGHT,
                      Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSizeLarge);
        }
    }
    currentY += dim.lineHeightNormal;

    // Row 4: Split/Lap time (centered)
    // In Pit mode, show last completed lap time only; in other modes, show current split/lap time
    if (m_enabledRows & ROW_LAST_LAP) {
        int timeToShow = 0;
        if (m_displayMode == MODE_PIT) {
            // Only show previous lap time (nothing on lap 1)
            if (sessionBest && sessionBest->lastLapTime > 0) {
                timeToShow = sessionBest->lastLapTime;
            }
        } else {
            // In other modes, show current split/lap time
            timeToShow = m_displayedTime;
        }
        if (timeToShow > 0) {
            char timeStr[16];
            PluginUtils::formatLapTimeTenths(timeToShow, timeStr, sizeof(timeStr));
            addString(timeStr, centerX, currentY, Justify::CENTER,
                      Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSize);
        }
    }
    currentY += dim.lineHeightNormal;

    // Row 5: Gap to leader (centered)
    if (m_enabledRows & ROW_GAP) {
        char gapStr[16];
        bool hasGap = false;
        if (standing && position > 1 && standing->gap > 0) {
            PluginUtils::formatGapCompact(gapStr, sizeof(gapStr), standing->gap);
            hasGap = true;
        } else if (position == 1) {
            snprintf(gapStr, sizeof(gapStr), "Leader");
            hasGap = true;
        }
        if (hasGap) {
            addString(gapStr, centerX, currentY, Justify::CENTER,
                      Fonts::FUZZY_BUBBLES, Colors::BLACK, dim.fontSize);
        }
    }
}

void PitboardHud::resetToDefaults() {
    m_bVisible = true;
    m_bShowTitle = false;
    m_bShowBackgroundTexture = true;  // Show texture by default
    m_fBackgroundOpacity = 1.0f;  // 100% opacity
    m_fScale = 1.0f;  // 100% default scale
    setPosition(0.0f, 0.0222f);
    m_enabledRows = ROW_DEFAULT;
    m_displayMode = MODE_SPLITS;  // Show at splits by default
    m_cachedSplit1 = -1;
    m_cachedSplit2 = -1;
    m_cachedLastLapTime = -1;
    m_cachedDisplayRaceNum = -1;
    m_bIsDisplayingTimed = false;
    m_bWasVisibleLastFrame = false;
    m_displayedTime = -1;
    m_splitType = LAP;
    m_cachedRenderedTime = -1;
    setDataDirty();
}
