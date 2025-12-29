// ============================================================================
// hud/timing_hud.cpp
// Timing HUD - displays accumulated split and lap times as they happen
// Shows accumulated times and gaps (default position: center of screen)
// Supports real-time elapsed timer with per-column visibility modes
// Example: S1: 30.00s, S2: 60.00s (accumulated), Lap: 90.00s
// ============================================================================
#include "timing_hud.h"

#include <cstdio>
#include <cmath>
#include <string>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"
#include "../core/personal_best_manager.h"

using namespace PluginConstants;

// Center display positioning constants (fixed center-screen layout)
namespace {
    constexpr float CENTER_X = 0.5f;
    constexpr float TIMING_DIVIDER_Y = 0.1665f;
    constexpr float DIVIDER_GAP = 0.005f;
}

TimingHud::TimingHud()
    : m_displayDurationMs(DEFAULT_DURATION_MS)
    , m_gapTypes(GAP_DEFAULT)
    , m_cachedSplit1(-1)
    , m_cachedSplit2(-1)
    , m_cachedLastCompletedLapNum(-1)
    , m_cachedDisplayRaceNum(-1)
    , m_cachedSession(-1)
    , m_cachedPitState(-1)
    , m_previousAllTimeLap(-1)
    , m_previousAllTimeSector1(-1)
    , m_previousAllTimeS1PlusS2(-1)
    , m_isFrozen(false)
{
    // One-time setup
    DEBUG_INFO("TimingHud created");
    setDraggable(true);
    m_quads.reserve(6);    // Background quads (label + time + up to 3 gap rows)
    m_strings.reserve(6);  // Label + time + up to 3 gap strings

    // Set texture base name for dynamic texture discovery
    setTextureBaseName("timing_hud");

    // Set all configurable defaults
    resetToDefaults();

    rebuildRenderData();
}

bool TimingHud::handlesDataType(DataChangeType dataType) const {
    return dataType == DataChangeType::IdealLap ||
           dataType == DataChangeType::SpectateTarget ||
           dataType == DataChangeType::SessionData ||  // Reset on new session/event
           dataType == DataChangeType::Standings;       // Detect pit entry/exit
}

void TimingHud::update() {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();

    // Detect session changes (new event) and reset state
    // Check both session type AND if session data was cleared (lastCompletedLapNum reset to -1)
    int currentSession = sessionData.session;
    const IdealLapData* idealLapData = pluginData.getIdealLapData();
    int currentLastCompletedLap = idealLapData ? idealLapData->lastCompletedLapNum : -1;

    bool sessionTypeChanged = (currentSession != m_cachedSession);
    bool sessionDataCleared = (m_cachedLastCompletedLapNum >= 0 && currentLastCompletedLap < 0);

    if (sessionTypeChanged || sessionDataCleared) {
        DEBUG_INFO_F("TimingHud: Session reset detected (type changed: %d, data cleared: %d)",
            sessionTypeChanged, sessionDataCleared);
        resetLiveTimingState();
        m_cachedSession = currentSession;
        m_cachedPitState = -1;  // Reset pit state cache for new session
        setDataDirty();
    }

    // Detect spectate target changes and reset state
    int currentDisplayRaceNum = pluginData.getDisplayRaceNum();
    if (currentDisplayRaceNum != m_cachedDisplayRaceNum) {
        DEBUG_INFO_F("TimingHud: Spectate target changed from %d to %d", m_cachedDisplayRaceNum, currentDisplayRaceNum);

        // Full reset on spectate change
        resetLiveTimingState();
        m_cachedDisplayRaceNum = currentDisplayRaceNum;
        m_cachedPitState = -1;  // Reset pit state cache for new rider

        // Update cached values with new rider's current data (without triggering display)
        const CurrentLapData* currentLap = pluginData.getCurrentLapData();
        const IdealLapData* idealLap = pluginData.getIdealLapData();
        if (currentLap) {
            m_cachedSplit1 = currentLap->split1;
            m_cachedSplit2 = currentLap->split2;
        }
        if (idealLap) {
            m_cachedLastCompletedLapNum = idealLap->lastCompletedLapNum;
        }

        setDataDirty();
    }

    // Detect pit entry/exit (for cache tracking)
    // Note: Anchor reset is now handled centrally by PluginData's track position monitoring
    const StandingsData* standing = pluginData.getStanding(currentDisplayRaceNum);
    if (standing) {
        int currentPitState = standing->pit;
        if (m_cachedPitState != -1 && currentPitState != m_cachedPitState) {
            DEBUG_INFO_F("TimingHud: Pit state changed from %d to %d", m_cachedPitState, currentPitState);
            // Just trigger a redraw - centralized timer handles anchor reset automatically
            setDataDirty();
        }
        m_cachedPitState = currentPitState;
    }

    // Process any split/lap completion updates
    processTimingUpdates();

    // Check if freeze period has expired
    checkFreezeExpiration();

    // Check if we need frequent updates for ticking timer (uses BaseHud helper)
    checkFrequentUpdates();

    // Check data dirty first (takes precedence)
    if (isDataDirty()) {
        rebuildRenderData();
        clearDataDirty();
        clearLayoutDirty();
    }
    else if (isLayoutDirty()) {
        rebuildLayout();
        clearLayoutDirty();
    }
}

void TimingHud::processTimingUpdates() {
    const PluginData& pluginData = PluginData::getInstance();
    const CurrentLapData* currentLap = pluginData.getCurrentLapData();
    const IdealLapData* idealLapData = pluginData.getIdealLapData();

    // Check current lap splits (CurrentLapData tracks accumulated times for current lap)
    if (currentLap) {
        // Check split 1 (accumulated time to S1)
        if (currentLap->split1 > 0 && currentLap->split1 != m_cachedSplit1) {
            int splitTime = currentLap->split1;

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.splitIndex = 0;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.isInvalid = false;

            // Calculate gaps for all enabled types
            calculateAllGaps(splitTime, 0, false);

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit1 = currentLap->split1;
            DEBUG_INFO_F("TimingHud: Split 1 crossed, accumulated=%d ms, lap=%d", splitTime, currentLap->lapNum);
            setDataDirty();
        }
        // Check split 2 (accumulated time to S2)
        else if (currentLap->split2 > 0 && currentLap->split2 != m_cachedSplit2) {
            int splitTime = currentLap->split2;

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.splitIndex = 1;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.isInvalid = false;

            // Calculate gaps for all enabled types
            calculateAllGaps(splitTime, 1, false);

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit2 = currentLap->split2;
            DEBUG_INFO_F("TimingHud: Split 2 crossed, accumulated=%d ms, lap=%d", splitTime, currentLap->lapNum);
            setDataDirty();
        }
    }

    // Check for lap completion (split 3 / finish line)
    if (idealLapData && idealLapData->lastCompletedLapNum >= 0 &&
        idealLapData->lastCompletedLapNum != m_cachedLastCompletedLapNum) {

        int lapTime = idealLapData->lastLapTime;

        // Check if this lap was valid by looking at the lap log
        bool isValid = true;
        int completedLapNum = idealLapData->lastCompletedLapNum;
        const std::deque<LapLogEntry>* lapLog = pluginData.getLapLog();
        if (lapLog && !lapLog->empty()) {
            const LapLogEntry& mostRecentLap = (*lapLog)[0];
            isValid = mostRecentLap.isValid;
            if (mostRecentLap.lapNum >= 0) {
                completedLapNum = mostRecentLap.lapNum;
            }
        }

        // Update official data cache
        m_officialData.time = lapTime;
        m_officialData.splitIndex = -1;  // Indicates lap complete
        m_officialData.lapNum = completedLapNum;
        m_officialData.isInvalid = !isValid;

        // Calculate gaps for all enabled types (only if valid lap)
        if (isValid && lapTime > 0) {
            calculateAllGaps(lapTime, -1, true);
        } else {
            // Invalid lap - clear all gaps
            m_officialData.gapToPB.reset();
            m_officialData.gapToIdeal.reset();
            m_officialData.gapToOverall.reset();
            m_officialData.gapToAllTime.reset();
        }

        // Reset split caches for next lap
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;

        // Freeze display (if freeze is enabled)
        if (m_displayDurationMs > 0) {
            m_isFrozen = true;
            m_frozenAt = std::chrono::steady_clock::now();
        }

        m_cachedLastCompletedLapNum = idealLapData->lastCompletedLapNum;
        DEBUG_INFO_F("TimingHud: Lap %d completed, time=%d ms, valid=%d", completedLapNum, lapTime, isValid);
        setDataDirty();

        // Cache the updated all-time PB for next lap comparison
        // This captures the new PB (if set) after race_lap_handler has updated PersonalBestManager
        cacheAllTimePB();
    }
}

void TimingHud::checkFreezeExpiration() {
    if (!m_isFrozen) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_frozenAt
    ).count();

    if (elapsed >= m_displayDurationMs) {
        m_isFrozen = false;
        setDataDirty();
    }
}

bool TimingHud::shouldShowColumn(Column col) const {
    ColumnMode mode = m_columnModes[col];

    switch (mode) {
        case ColumnMode::OFF:
            return false;

        case ColumnMode::SPLITS:
            return m_isFrozen;  // Only during freeze

        case ColumnMode::ALWAYS:
            return true;  // Always visible
    }

    return false;
}

bool TimingHud::needsFrequentUpdates() const {
    // Need frequent updates when time column is in ALWAYS mode, not frozen, and timer is valid
    if (m_isFrozen) return false;
    if (m_columnModes[COL_TIME] != ColumnMode::ALWAYS) return false;

    const PluginData& data = PluginData::getInstance();
    if (!data.isLapTimerValid()) return false;
    if (data.isDisplayRiderFinished()) return false;  // Timer stopped after finish

    return true;
}

int TimingHud::calculateGap(int currentTime, int referenceTime) const {
    if (currentTime <= 0 || referenceTime <= 0) {
        return 0;
    }
    return currentTime - referenceTime;
}

void TimingHud::setGapType(GapTypeFlags flag, bool enabled) {
    if (enabled) {
        m_gapTypes |= flag;
    } else {
        m_gapTypes &= ~flag;
    }
    setDataDirty();
}

int TimingHud::getEnabledGapCount() const {
    int count = 0;
    if (m_gapTypes & GAP_TO_PB) count++;
    if (m_gapTypes & GAP_TO_ALLTIME) count++;
    if (m_gapTypes & GAP_TO_IDEAL) count++;
    if (m_gapTypes & GAP_TO_OVERALL) count++;
    return count;
}

int TimingHud::getOverallBestLapTime() const {
    const PluginData& pluginData = PluginData::getInstance();
    const auto& standings = pluginData.getStandings();

    int overallBest = -1;
    for (const auto& [raceNum, standing] : standings) {
        if (standing.bestLap > 0) {
            if (overallBest < 0 || standing.bestLap < overallBest) {
                overallBest = standing.bestLap;
            }
        }
    }
    return overallBest;
}

void TimingHud::calculateAllGaps(int splitTime, int splitIndex, bool isLapComplete) {
    const PluginData& pluginData = PluginData::getInstance();
    const LapLogEntry* personalBest = pluginData.getBestLapEntry();
    const IdealLapData* idealLapData = pluginData.getIdealLapData();

    // === Gap to Personal Best ===
    {
        int pbTime = -1;
        int previousPbTime = -1;

        if (isLapComplete) {
            // Full lap comparison
            pbTime = personalBest ? personalBest->lapTime : -1;
            previousPbTime = idealLapData ? idealLapData->previousBestLapTime : -1;
        } else if (splitIndex == 0) {
            // Split 1 comparison
            pbTime = personalBest ? personalBest->sector1 : -1;
            previousPbTime = idealLapData ? idealLapData->previousBestSector1 : -1;
        } else if (splitIndex == 1) {
            // Split 2 comparison (accumulated S1+S2)
            if (personalBest && personalBest->sector1 > 0 && personalBest->sector2 > 0) {
                pbTime = personalBest->sector1 + personalBest->sector2;
            }
            if (idealLapData && idealLapData->previousBestSector1 > 0 && idealLapData->previousBestSector2 > 0) {
                previousPbTime = idealLapData->previousBestSector1 + idealLapData->previousBestSector2;
            }
        }

        int gap = calculateGap(splitTime, pbTime);
        if (gap == 0 && previousPbTime > 0) {
            gap = splitTime - previousPbTime;  // New PB - compare to previous
        }
        m_officialData.gapToPB.set(gap, (pbTime > 0 || previousPbTime > 0) ? 1 : -1);
    }

    // === Gap to Ideal (sum of best sectors) ===
    {
        int idealTime = -1;

        if (isLapComplete) {
            // Full lap: compare to ideal lap time
            idealTime = idealLapData ? idealLapData->getIdealLapTime() : -1;
        } else if (splitIndex == 0) {
            // Split 1: compare to best sector 1
            idealTime = idealLapData ? idealLapData->bestSector1 : -1;
        } else if (splitIndex == 1) {
            // Split 2: compare to best S1 + best S2
            if (idealLapData && idealLapData->bestSector1 > 0 && idealLapData->bestSector2 > 0) {
                idealTime = idealLapData->bestSector1 + idealLapData->bestSector2;
            }
        }

        int gap = calculateGap(splitTime, idealTime);
        m_officialData.gapToIdeal.set(gap, idealTime);
    }

    // === Gap to Overall (overall best lap by anyone in session) ===
    {
        int overallBestTime = -1;
        const LapLogEntry* overallBest = pluginData.getOverallBestLap();

        if (isLapComplete) {
            // Full lap: compare to overall best lap in session
            overallBestTime = getOverallBestLapTime();
        } else if (overallBest) {
            // Split comparisons using stored overall best splits
            if (splitIndex == 0) {
                overallBestTime = overallBest->sector1;
            } else if (splitIndex == 1) {
                // S1 + S2 cumulative time
                if (overallBest->sector1 > 0 && overallBest->sector2 > 0) {
                    overallBestTime = overallBest->sector1 + overallBest->sector2;
                }
            }
        }

        int gap = calculateGap(splitTime, overallBestTime);
        m_officialData.gapToOverall.set(gap, overallBestTime);
    }

    // === Gap to All-Time PB (persisted across sessions) ===
    {
        // Use cached previous all-time PB values (captured at session start and after each lap)
        // This allows showing improvement when beating the PB, since PersonalBestManager
        // may have already been updated with the new time by race_lap_handler
        int previousAllTimeTime = -1;

        if (isLapComplete) {
            previousAllTimeTime = m_previousAllTimeLap;
        } else if (splitIndex == 0) {
            previousAllTimeTime = m_previousAllTimeSector1;
        } else if (splitIndex == 1) {
            previousAllTimeTime = m_previousAllTimeS1PlusS2;
        }

        int gap = calculateGap(splitTime, previousAllTimeTime);
        m_officialData.gapToAllTime.set(gap, previousAllTimeTime);
    }
}

int TimingHud::getVisibleColumnCount() const {
    int count = 0;
    for (int i = 0; i < COL_COUNT; i++) {
        if (shouldShowColumn(static_cast<Column>(i))) count++;
    }
    return count;
}

void TimingHud::resetLiveTimingState() {
    // Note: Anchor and track monitor are now managed centrally by PluginData
    // Reset local display state only
    m_isFrozen = false;
    m_officialData.reset();
    m_cachedSplit1 = -1;
    m_cachedSplit2 = -1;
    m_cachedLastCompletedLapNum = -1;

    // Cache current all-time PB for comparison when beating it
    cacheAllTimePB();
}

void TimingHud::cacheAllTimePB() {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    const PersonalBestEntry* allTimePB = PersonalBestManager::getInstance()
        .getPersonalBest(sessionData.trackId, sessionData.bikeName);

    if (allTimePB && allTimePB->isValid()) {
        m_previousAllTimeLap = allTimePB->lapTime;
        m_previousAllTimeSector1 = allTimePB->sector1;
        if (allTimePB->sector1 > 0 && allTimePB->sector2 > 0) {
            m_previousAllTimeS1PlusS2 = allTimePB->sector1 + allTimePB->sector2;
        } else {
            m_previousAllTimeS1PlusS2 = -1;
        }
    } else {
        m_previousAllTimeLap = -1;
        m_previousAllTimeSector1 = -1;
        m_previousAllTimeS1PlusS2 = -1;
    }
}

void TimingHud::rebuildLayout() {
    // Layout changes require full rebuild since columns are dynamic
    rebuildRenderData();
}

void TimingHud::rebuildRenderData() {
    // Clear render data
    m_strings.clear();
    m_quads.clear();

    const PluginData& pluginData = PluginData::getInstance();

    // Check if any columns are visible
    int visibleCount = getVisibleColumnCount();
    if (visibleCount == 0) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Column dimensions
    float columnTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::STANDARD_WIDTH, dim.fontSizeLarge);
    float charGap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeLarge);
    float columnQuadWidth = dim.paddingH + columnTextWidth + dim.paddingH;
    float quadHeight = dim.paddingV + dim.fontSizeLarge;

    // Calculate total width based on visible columns
    float totalWidth = visibleCount * columnQuadWidth + (visibleCount - 1) * charGap;

    // Starting X position (centered)
    float currentX = CENTER_X - totalWidth / 2.0f;
    float quadY = TIMING_DIVIDER_Y + DIVIDER_GAP;
    float textY = quadY + dim.paddingV * 0.5f;

    // Track bounds
    float leftX = currentX;
    float rightX = currentX;

    // Prepare content for each column
    char labelBuffer[16];
    char timeBuffer[32];
    bool gapIsFaster = false;
    bool gapIsSlower = false;

    // Check if display rider has finished the race
    bool riderFinished = pluginData.isDisplayRiderFinished();
    int riderFinishTime = -1;
    if (riderFinished) {
        int displayRaceNum = pluginData.getDisplayRaceNum();
        const StandingsData* standing = pluginData.getStanding(displayRaceNum);
        if (standing) {
            riderFinishTime = standing->finishTime;
        }
    }

    // === LABEL COLUMN CONTENT ===
    if (m_isFrozen) {
        // Show official label
        if (m_officialData.splitIndex == 0) {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Split 1");
        } else if (m_officialData.splitIndex == 1) {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Split 2");
        } else {
            // Lap complete
            if (m_officialData.lapNum >= 0) {
                snprintf(labelBuffer, sizeof(labelBuffer), "Lap %d", m_officialData.lapNum + 1);
            } else {
                strcpy_s(labelBuffer, sizeof(labelBuffer), "Lap -");
            }
        }
    } else if (riderFinished && riderFinishTime > 0) {
        // Rider finished - show "Finish" label
        strcpy_s(labelBuffer, sizeof(labelBuffer), "Finish");
    } else {
        // Ticking - show current lap (or placeholder if no timing context yet)
        if (pluginData.isLapTimerValid()) {
            int currentLapNum = pluginData.getLapTimerCurrentLap();
            snprintf(labelBuffer, sizeof(labelBuffer), "Lap %d", currentLapNum + 1);
        } else {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Lap -");
        }
    }

    // === TIME COLUMN CONTENT ===
    if (m_isFrozen) {
        // Show official time
        if (m_officialData.time > 0) {
            PluginUtils::formatLapTime(m_officialData.time, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
        }
    } else if (riderFinished && riderFinishTime > 0) {
        // Rider finished - show total race time
        PluginUtils::formatLapTime(riderFinishTime, timeBuffer, sizeof(timeBuffer));
    } else {
        // Get elapsed time from centralized timer
        int elapsed = pluginData.getElapsedLapTime();
        if (elapsed >= 0) {
            PluginUtils::formatLapTime(elapsed, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
        }
    }

    // === RENDER COLUMNS ===

    // Add LABEL column if visible
    if (shouldShowColumn(COL_LABEL)) {
        addBackgroundQuad(currentX, quadY, columnQuadWidth, quadHeight);
        float labelX = (visibleCount == 1)
            ? currentX + columnQuadWidth / 2.0f
            : currentX + columnQuadWidth - dim.paddingH;
        int labelJustify = (visibleCount == 1) ? Justify::CENTER : Justify::RIGHT;
        addString(labelBuffer, labelX, textY, labelJustify,
            Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
        currentX += columnQuadWidth + charGap;
    }

    // Add TIME column if visible
    if (shouldShowColumn(COL_TIME)) {
        addBackgroundQuad(currentX, quadY, columnQuadWidth, quadHeight);
        float timeX = currentX + columnQuadWidth / 2.0f;
        addString(timeBuffer, timeX, textY, Justify::CENTER,
            Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
        currentX += columnQuadWidth + charGap;
    }

    // Add GAP column(s) if visible - supports multiple gap types stacked vertically
    float gapColumnX = currentX;  // Save starting X for gap column
    float gapRowY = quadY;
    float gapTextY = textY;
    int gapRowsRendered = 0;

    if (shouldShowColumn(COL_GAP)) {
        // Helper lambda to render a single gap row
        auto renderGapRow = [&](const GapData& gapData, bool showInvalid) {
            char gapBuffer[32];
            bool gapIsFaster = false;
            bool gapIsSlower = false;

            if (showInvalid) {
                strcpy_s(gapBuffer, sizeof(gapBuffer), "INVALID");
                gapIsSlower = true;
            } else if (!gapData.hasGap) {
                strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
            } else {
                PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), gapData.gap);
                gapIsFaster = gapData.isFaster;
                gapIsSlower = gapData.isSlower;
            }

            // Create colored background quad
            SPluginQuad_t gapQuad;
            float gapQuadX = gapColumnX;
            float gapQuadY = gapRowY;
            applyOffset(gapQuadX, gapQuadY);
            setQuadPositions(gapQuad, gapQuadX, gapQuadY, columnQuadWidth, quadHeight);
            gapQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

            unsigned long baseColor;
            if (gapIsFaster) {
                baseColor = ColorConfig::getInstance().getPositive();
            } else if (gapIsSlower) {
                baseColor = ColorConfig::getInstance().getNegative();
            } else {
                baseColor = ColorConfig::getInstance().getBackground();
            }
            gapQuad.m_ulColor = PluginUtils::applyOpacity(baseColor, m_fBackgroundOpacity);
            m_quads.push_back(gapQuad);

            // Add gap text
            float gapX = (visibleCount == 1)
                ? gapColumnX + columnQuadWidth / 2.0f
                : gapColumnX + dim.paddingH;
            int gapJustify = (visibleCount == 1) ? Justify::CENTER : Justify::LEFT;

            unsigned long gapTextColor;
            if (gapIsFaster) {
                gapTextColor = ColorConfig::getInstance().getPositive();
            } else if (gapIsSlower) {
                gapTextColor = ColorConfig::getInstance().getNegative();
            } else {
                gapTextColor = ColorConfig::getInstance().getPrimary();
            }
            addString(gapBuffer, gapX, gapTextY, gapJustify,
                Fonts::getNormal(), gapTextColor, dim.fontSizeLarge);

            // Move to next row
            gapRowY += quadHeight;
            gapTextY += quadHeight;
            gapRowsRendered++;
        };

        // Determine if we should show gaps or placeholders
        // Only show actual gap data when frozen (displaying official split/lap time)
        bool showGapData = m_isFrozen;
        bool showInvalid = m_officialData.isInvalid;

        if (showGapData) {
            // Render enabled gap types in display order:
            // Session PB, All-Time PB, Ideal, Overall
            if (m_gapTypes & GAP_TO_PB) {
                renderGapRow(m_officialData.gapToPB, showInvalid);
            }
            if (m_gapTypes & GAP_TO_ALLTIME) {
                renderGapRow(m_officialData.gapToAllTime, showInvalid);  // All-time gap uses invalid
            }
            if (m_gapTypes & GAP_TO_IDEAL) {
                renderGapRow(m_officialData.gapToIdeal, false);  // Ideal gap doesn't use invalid
            }
            if (m_gapTypes & GAP_TO_OVERALL) {
                renderGapRow(m_officialData.gapToOverall, false);  // Overall doesn't use invalid
            }
        } else {
            // No gap data yet - show placeholder for each enabled gap type
            GapData emptyGap;
            int enabledCount = getEnabledGapCount();
            for (int i = 0; i < enabledCount; i++) {
                renderGapRow(emptyGap, false);
            }
        }

        currentX = gapColumnX + columnQuadWidth;
    }

    // Set bounds (account for multiple gap rows)
    rightX = currentX;
    float bottomY = (gapRowsRendered > 1) ? gapRowY : (quadY + quadHeight);
    setBounds(leftX, quadY, rightX, bottomY);
}

void TimingHud::resetToDefaults() {
    m_bVisible = false;  // Off by default
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, 0.0f);

    // Reset column modes to defaults
    m_columnModes[COL_LABEL] = ColumnMode::SPLITS;
    m_columnModes[COL_TIME] = ColumnMode::ALWAYS;
    m_columnModes[COL_GAP] = ColumnMode::SPLITS;

    m_displayDurationMs = DEFAULT_DURATION_MS;
    m_gapTypes = GAP_DEFAULT;  // Default: only gap to PB

    // Reset live timing state
    resetLiveTimingState();

    setDataDirty();
}
