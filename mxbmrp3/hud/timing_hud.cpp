// ============================================================================
// hud/timing_hud.cpp
// Timing HUD - displays accumulated split and lap times as they happen
// Shows accumulated times and gaps (default position: center of screen)
// Supports real-time elapsed timer with per-column visibility modes
// Example: S1: 30.00s, S2: 60.00s (accumulated), Lap: 90.00s
// ============================================================================
#include "timing_hud.h"
#include "records_hud.h"

#include "../game/game_config.h"

#include <cstdio>
#include <cmath>
#include <string>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"
#include "../core/personal_best_manager.h"
#include "../core/hud_manager.h"

using namespace PluginConstants;

// Center display positioning constants (fixed center-screen layout)
namespace {
    constexpr float CENTER_X = 0.5f;
    constexpr float TIMING_DIVIDER_Y = 0.1665f;
    constexpr float DIVIDER_GAP = 0.005f;
}

TimingHud::TimingHud()
    : m_displayDurationMs(DEFAULT_DURATION_MS)
    , m_primaryGapType(GAP_DEFAULT_PRIMARY)
    , m_secondaryGapTypes(GAP_DEFAULT_SECONDARY)
    , m_layoutVertical(false)
    , m_cachedSplit1(-1)
    , m_cachedSplit2(-1)
    , m_cachedSplit3(-1)
    , m_cachedLastCompletedLapNum(-1)
    , m_cachedDisplayRaceNum(-1)
    , m_cachedSession(-1)
    , m_cachedPitState(-1)
    , m_previousAllTimeLap(-1)
    , m_previousAllTimeSector1(-1)
    , m_previousAllTimeS1PlusS2(-1)
    , m_previousAllTimeS1PlusS2PlusS3(-1)
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
    // OPTIMIZATION: Skip all processing when not visible
    // State tracking (splits, gaps) is only meaningful when displaying
    if (!isVisible()) {
        clearDataDirty();
        clearLayoutDirty();
        return;
    }

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
            m_cachedSplit3 = currentLap->split3;
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

    // Handle dirty flags using base class helper
    processDirtyFlags();
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
#if GAME_SECTOR_COUNT >= 4
        // Check split 3 (accumulated time to S3) - 4-sector games only
        else if (currentLap->split3 > 0 && currentLap->split3 != m_cachedSplit3) {
            int splitTime = currentLap->split3;

            // Update official data cache
            m_officialData.time = splitTime;
            m_officialData.splitIndex = 2;
            m_officialData.lapNum = currentLap->lapNum;
            m_officialData.isInvalid = false;

            // Calculate gaps for all enabled types
            calculateAllGaps(splitTime, 2, false);

            // Freeze display (if freeze is enabled)
            if (m_displayDurationMs > 0) {
                m_isFrozen = true;
                m_frozenAt = std::chrono::steady_clock::now();
            }

            m_cachedSplit3 = currentLap->split3;
            DEBUG_INFO_F("TimingHud: Split 3 crossed, accumulated=%d ms, lap=%d", splitTime, currentLap->lapNum);
            setDataDirty();
        }
#endif
    }

    // Check for lap completion (split 3/4 / finish line)
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
            m_officialData.gapToRecord.reset();
        }

        // Reset split caches for next lap
        m_cachedSplit1 = -1;
        m_cachedSplit2 = -1;
        m_cachedSplit3 = -1;

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
    // Check if column is enabled
    if (!m_columnEnabled[col]) return false;

    // Check display mode
    switch (m_displayMode) {
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
    // Need frequent updates when time column is visible (ALWAYS mode), not frozen, and timer is valid
    if (m_isFrozen) return false;
    if (m_displayMode != ColumnMode::ALWAYS || !m_columnEnabled[COL_TIME]) return false;

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

void TimingHud::setPrimaryGapType(GapTypeFlags type) {
    if (m_primaryGapType != type) {
        m_primaryGapType = type;
        // Don't modify secondaries - user intent preserved, filtered at display time
        setDataDirty();
    }
}

void TimingHud::cyclePrimaryGapType(bool forward) {
    // Find current index
    int currentIndex = -1;
    for (int i = 0; i < GAP_TYPE_COUNT; i++) {
        if (GAP_TYPE_INFO[i].flag == m_primaryGapType) {
            currentIndex = i;
            break;
        }
    }

    // Cycle to next/previous
    if (forward) {
        currentIndex = (currentIndex + 1) % GAP_TYPE_COUNT;
    } else {
        currentIndex = (currentIndex + GAP_TYPE_COUNT - 1) % GAP_TYPE_COUNT;
    }

    setPrimaryGapType(GAP_TYPE_INFO[currentIndex].flag);
}

void TimingHud::setSecondaryGapType(GapTypeFlags flag, bool enabled) {
    // Store user intent - filtering happens at display time
    if (enabled) {
        m_secondaryGapTypes |= flag;
    } else {
        m_secondaryGapTypes &= ~flag;
    }
    setDataDirty();
}

int TimingHud::getEnabledSecondaryGapCount(bool skipPrimaryType) const {
    int count = 0;
    for (int i = 0; i < GAP_TYPE_COUNT; i++) {
        GapTypeFlags flag = GAP_TYPE_INFO[i].flag;
        // Skip primary gap type only when requested (when primary gap column is visible)
        if (skipPrimaryType && flag == m_primaryGapType) continue;
        if (m_secondaryGapTypes & flag) {
            count++;
        }
    }
    return count;
}

const GapTypeInfo* TimingHud::getGapTypeInfo(GapTypeFlags flag) {
    for (int i = 0; i < GAP_TYPE_COUNT; i++) {
        if (GAP_TYPE_INFO[i].flag == flag) {
            return &GAP_TYPE_INFO[i];
        }
    }
    return nullptr;
}

const char* TimingHud::getGapTypeName(GapTypeFlags flag) {
    const GapTypeInfo* info = getGapTypeInfo(flag);
    return info ? info->name : "Unknown";
}

const char* TimingHud::getGapTypeAbbrev(GapTypeFlags flag) {
    const GapTypeInfo* info = getGapTypeInfo(flag);
    return info ? info->abbrev : "??";
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
#if GAME_SECTOR_COUNT >= 4
        else if (splitIndex == 2) {
            // Split 3 comparison (accumulated S1+S2+S3)
            if (personalBest && personalBest->sector1 > 0 && personalBest->sector2 > 0 && personalBest->sector3 > 0) {
                pbTime = personalBest->sector1 + personalBest->sector2 + personalBest->sector3;
            }
            if (idealLapData && idealLapData->previousBestSector1 > 0 && idealLapData->previousBestSector2 > 0 &&
                idealLapData->previousBestSector3 > 0) {
                previousPbTime = idealLapData->previousBestSector1 + idealLapData->previousBestSector2 +
                                 idealLapData->previousBestSector3;
            }
        }
#endif

        int gap = calculateGap(splitTime, pbTime);
        int refTime = pbTime;
        if (gap == 0 && previousPbTime > 0) {
            gap = splitTime - previousPbTime;  // New PB - compare to previous
            refTime = previousPbTime;
        }
        m_officialData.gapToPB.set(gap, refTime);
    }

    // === Gap to Ideal (sum of best sectors) ===
    {
        int idealTime = -1;
        int previousIdealTime = -1;

        if (isLapComplete) {
            // Full lap: compare to ideal lap time
            idealTime = idealLapData ? idealLapData->getIdealLapTime() : -1;
            previousIdealTime = idealLapData ? idealLapData->getPreviousIdealLapTime() : -1;
        } else if (splitIndex == 0) {
            // Split 1: compare to best sector 1
            idealTime = idealLapData ? idealLapData->bestSector1 : -1;
            previousIdealTime = idealLapData ? idealLapData->previousIdealSector1 : -1;
        } else if (splitIndex == 1) {
            // Split 2: compare to best S1 + best S2
            if (idealLapData && idealLapData->bestSector1 > 0 && idealLapData->bestSector2 > 0) {
                idealTime = idealLapData->bestSector1 + idealLapData->bestSector2;
            }
            // Previous ideal S1+S2 (use current S1 if it wasn't improved, previous S1 if it was)
            if (idealLapData) {
                int prevS1 = idealLapData->previousIdealSector1 > 0 ? idealLapData->previousIdealSector1 : idealLapData->bestSector1;
                int prevS2 = idealLapData->previousIdealSector2 > 0 ? idealLapData->previousIdealSector2 : idealLapData->bestSector2;
                if (prevS1 > 0 && prevS2 > 0) {
                    previousIdealTime = prevS1 + prevS2;
                }
            }
        }
#if GAME_SECTOR_COUNT >= 4
        else if (splitIndex == 2) {
            // Split 3: compare to best S1 + best S2 + best S3
            if (idealLapData && idealLapData->bestSector1 > 0 && idealLapData->bestSector2 > 0 &&
                idealLapData->bestSector3 > 0) {
                idealTime = idealLapData->bestSector1 + idealLapData->bestSector2 + idealLapData->bestSector3;
            }
            // Previous ideal S1+S2+S3
            if (idealLapData) {
                int prevS1 = idealLapData->previousIdealSector1 > 0 ? idealLapData->previousIdealSector1 : idealLapData->bestSector1;
                int prevS2 = idealLapData->previousIdealSector2 > 0 ? idealLapData->previousIdealSector2 : idealLapData->bestSector2;
                int prevS3 = idealLapData->previousIdealSector3 > 0 ? idealLapData->previousIdealSector3 : idealLapData->bestSector3;
                if (prevS1 > 0 && prevS2 > 0 && prevS3 > 0) {
                    previousIdealTime = prevS1 + prevS2 + prevS3;
                }
            }
        }
#endif

        int gap = calculateGap(splitTime, idealTime);
        int refTime = idealTime;
        // If gap is 0, we just set a new best sector - compare to previous
        if (gap == 0 && previousIdealTime > 0) {
            gap = splitTime - previousIdealTime;
            refTime = previousIdealTime;
        }
        m_officialData.gapToIdeal.set(gap, refTime);
    }

    // === Gap to Overall (overall best lap by anyone in session) ===
    {
        int overallBestTime = -1;
        int previousOverallTime = -1;
        const LapLogEntry* overallBest = pluginData.getOverallBestLap();
        const LapLogEntry* previousOverall = pluginData.getPreviousOverallBestLap();

        if (isLapComplete) {
            // Full lap: compare to overall best lap in session
            overallBestTime = getOverallBestLapTime();
            previousOverallTime = previousOverall ? previousOverall->lapTime : -1;
        } else if (overallBest) {
            // Split comparisons using stored overall best splits
            if (splitIndex == 0) {
                overallBestTime = overallBest->sector1;
                previousOverallTime = previousOverall ? previousOverall->sector1 : -1;
            } else if (splitIndex == 1) {
                // S1 + S2 cumulative time
                if (overallBest->sector1 > 0 && overallBest->sector2 > 0) {
                    overallBestTime = overallBest->sector1 + overallBest->sector2;
                }
                if (previousOverall && previousOverall->sector1 > 0 && previousOverall->sector2 > 0) {
                    previousOverallTime = previousOverall->sector1 + previousOverall->sector2;
                }
            }
#if GAME_SECTOR_COUNT >= 4
            else if (splitIndex == 2) {
                // S1 + S2 + S3 cumulative time
                if (overallBest->sector1 > 0 && overallBest->sector2 > 0 && overallBest->sector3 > 0) {
                    overallBestTime = overallBest->sector1 + overallBest->sector2 + overallBest->sector3;
                }
                if (previousOverall && previousOverall->sector1 > 0 && previousOverall->sector2 > 0 &&
                    previousOverall->sector3 > 0) {
                    previousOverallTime = previousOverall->sector1 + previousOverall->sector2 + previousOverall->sector3;
                }
            }
#endif
        }

        int gap = calculateGap(splitTime, overallBestTime);
        int refTime = overallBestTime;
        // If gap is 0, we just set a new overall best - compare to previous
        if (gap == 0 && previousOverallTime > 0) {
            gap = splitTime - previousOverallTime;
            refTime = previousOverallTime;
        }
        m_officialData.gapToOverall.set(gap, refTime);
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
#if GAME_SECTOR_COUNT >= 4
        else if (splitIndex == 2) {
            previousAllTimeTime = m_previousAllTimeS1PlusS2PlusS3;
        }
#endif

        int gap = calculateGap(splitTime, previousAllTimeTime);
        m_officialData.gapToAllTime.set(gap, previousAllTimeTime);
    }

    // === Gap to Record (fastest from server records) ===
#if GAME_HAS_RECORDS_PROVIDER
    {
        int recordTime = -1;
        const RecordsHud& recordsHud = HudManager::getInstance().getRecordsHud();

        // Only compare full laps to records (no sector data for CBR provider)
        if (isLapComplete) {
            recordTime = recordsHud.getFastestRecordLapTime();
        } else {
            // For splits, try to get sector times if available (MXB-Ranked only)
            int s1 = 0, s2 = 0, s3 = 0, s4 = 0;
            if (recordsHud.getFastestRecordSectors(s1, s2, s3, s4)) {
                if (splitIndex == 0) {
                    recordTime = s1;
                } else if (splitIndex == 1) {
                    recordTime = s1 + s2;
                }
#if GAME_SECTOR_COUNT >= 4
                else if (splitIndex == 2) {
                    recordTime = s1 + s2 + s3;
                }
#endif
            }
        }

        int gap = calculateGap(splitTime, recordTime);
        m_officialData.gapToRecord.set(gap, recordTime);
    }
#endif
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
    m_cachedSplit3 = -1;
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
#if GAME_SECTOR_COUNT >= 4
        if (allTimePB->sector1 > 0 && allTimePB->sector2 > 0 && allTimePB->sector3 > 0) {
            m_previousAllTimeS1PlusS2PlusS3 = allTimePB->sector1 + allTimePB->sector2 + allTimePB->sector3;
        } else {
            m_previousAllTimeS1PlusS2PlusS3 = -1;
        }
#endif
    } else {
        m_previousAllTimeLap = -1;
        m_previousAllTimeSector1 = -1;
        m_previousAllTimeS1PlusS2 = -1;
#if GAME_SECTOR_COUNT >= 4
        m_previousAllTimeS1PlusS2PlusS3 = -1;
#endif
    }
}

void TimingHud::rebuildLayout() {
    // Layout changes require full rebuild since columns are dynamic
    rebuildRenderData();
}

void TimingHud::rebuildRenderData() {
    // Clear render data
    clearStrings();
    m_quads.clear();

    const PluginData& pluginData = PluginData::getInstance();

    // Check if any columns or secondary gaps should be visible
    int visibleCount = getVisibleColumnCount();
    int secondaryCount = getEnabledSecondaryGapCount(shouldShowColumn(COL_GAP));

    // Secondaries also respect display mode
    bool showSecondaries = (m_displayMode == ColumnMode::ALWAYS) ||
                           (m_displayMode == ColumnMode::SPLITS && m_isFrozen);

    if (visibleCount == 0 && (secondaryCount == 0 || !showSecondaries)) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Check if reference should be shown in gap column (applies to both primary and secondary)
    bool showRefInGap = m_showReference;

    // Per-column dimensions based on content character counts
    float labelTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::TIMING_LABEL_WIDTH, dim.fontSizeLarge);
    float timeTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::TIMING_TIME_WIDTH, dim.fontSizeLarge);
    int gapChars = showRefInGap ? WidgetDimensions::TIMING_GAP_WITH_REF_WIDTH : WidgetDimensions::TIMING_GAP_WIDTH;
    float gapTextWidth = PluginUtils::calculateMonospaceTextWidth(gapChars, dim.fontSizeLarge);

    // Use smaller vertical padding for compact widget appearance
    float compactPaddingV = dim.paddingV * 0.35f;

    // Calculate individual quad widths
    float labelQuadWidth = dim.paddingH + labelTextWidth + dim.paddingH;
    float timeQuadWidth = dim.paddingH + timeTextWidth + dim.paddingH;
    float gapQuadWidth = dim.paddingH + gapTextWidth + dim.paddingH;

    // Use uniform column width (widest of all columns) for consistent alignment
    float columnWidth = std::max({labelQuadWidth, timeQuadWidth, gapQuadWidth});

    // Gap between columns (half char width for tighter spacing)
    float charGap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeLarge) * 0.5f;

    // Quad height: in vertical mode use lineHeightLarge to match row step, otherwise use compact padding
    float quadHeight = m_layoutVertical ? dim.lineHeightLarge : (compactPaddingV + dim.fontSizeLarge + compactPaddingV);

    // Layout depends on mode: horizontal (side by side) or vertical (stacked)
    float quadY = TIMING_DIVIDER_Y + DIVIDER_GAP;
    float verticalGap = charGap * UI_ASPECT_RATIO;  // Gap between rows in vertical mode

    // Column positions - calculated differently based on layout mode
    float labelColumnX, timeColumnX, gapColumnX;
    float labelRowY, timeRowY, gapRowY;  // Y positions for vertical mode
    float leftX, rightX;

    if (m_layoutVertical) {
        // Vertical layout: all primary elements in one column, stacked vertically
        // Primary column is centered, secondary column will be to the right
        // Add horizontal padding around the column (like LapLogHud/StandingsHud)
        float primaryColumnX = CENTER_X - columnWidth / 2.0f;
        labelColumnX = timeColumnX = gapColumnX = primaryColumnX;

        // Stack only enabled rows vertically (no gaps for disabled elements)
        // Use lineHeightLarge for primary rows (larger font), secondaries use lineHeightNormal
        // Start content after paddingV from top (like LapLogHud/StandingsHud)
        float currentY = quadY + dim.paddingV;
        float rowStep = dim.lineHeightLarge;

        if (shouldShowColumn(COL_LABEL)) {
            labelRowY = currentY;
            currentY += rowStep;
        } else {
            labelRowY = quadY;  // Won't be used, but initialize
        }

        if (shouldShowColumn(COL_TIME)) {
            timeRowY = currentY;
            currentY += rowStep;
        } else {
            timeRowY = quadY;
        }

        if (shouldShowColumn(COL_GAP)) {
            gapRowY = currentY;
        } else {
            gapRowY = quadY;
        }

        // Include horizontal padding in bounds (like LapLogHud/StandingsHud)
        leftX = primaryColumnX - dim.paddingH;
        rightX = primaryColumnX + columnWidth + dim.paddingH;
    } else {
        // Horizontal layout: columns side by side
        // TIME column is centered, LABEL to left, GAP to right
        timeColumnX = CENTER_X - columnWidth / 2.0f;
        labelColumnX = timeColumnX - charGap - columnWidth;
        gapColumnX = timeColumnX + columnWidth + charGap;

        // All on same row
        labelRowY = timeRowY = gapRowY = quadY;

        // Set leftX/rightX based on actual visible columns
        if (shouldShowColumn(COL_LABEL)) {
            leftX = labelColumnX;
        } else if (shouldShowColumn(COL_TIME)) {
            leftX = timeColumnX;
        } else if (shouldShowColumn(COL_GAP)) {
            leftX = gapColumnX;
        } else {
            leftX = timeColumnX;  // Fallback
        }

        if (shouldShowColumn(COL_GAP)) {
            rightX = gapColumnX + columnWidth;
        } else if (shouldShowColumn(COL_TIME)) {
            rightX = timeColumnX + columnWidth;
        } else if (shouldShowColumn(COL_LABEL)) {
            rightX = labelColumnX + columnWidth;
        } else {
            rightX = timeColumnX + columnWidth;  // Fallback
        }
    }

    // Prepare content for each column
    char labelBuffer[16];
    char timeBuffer[32];

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
        }
#if GAME_SECTOR_COUNT >= 4
        else if (m_officialData.splitIndex == 2) {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Split 3");
        }
#endif
        else {
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

    // === HELPER LAMBDAS ===
    auto getGapDataForType = [&](GapTypeFlags type) -> const GapData* {
        switch (type) {
            case GAP_TO_PB: return &m_officialData.gapToPB;
            case GAP_TO_ALLTIME: return &m_officialData.gapToAllTime;
            case GAP_TO_IDEAL: return &m_officialData.gapToIdeal;
            case GAP_TO_OVERALL: return &m_officialData.gapToOverall;
#if GAME_HAS_RECORDS_PROVIDER
            case GAP_TO_RECORD: return &m_officialData.gapToRecord;
#endif
            default: return nullptr;
        }
    };

    auto typeUsesInvalid = [](GapTypeFlags type) -> bool {
        return type == GAP_TO_PB || type == GAP_TO_ALLTIME;
    };

    // Get lap-level reference time for a gap type before any splits are crossed
    // This allows showing reference times immediately when available
    auto getPreSplitRefTime = [&](GapTypeFlags type) -> int {
        if (type == GAP_TO_PB) {
            const LapLogEntry* personalBest = pluginData.getBestLapEntry();
            return (personalBest && personalBest->lapTime > 0) ? personalBest->lapTime : -1;
        } else if (type == GAP_TO_ALLTIME) {
            return m_previousAllTimeLap;  // Cached at session start
        } else if (type == GAP_TO_IDEAL) {
            const IdealLapData* idealLapData = pluginData.getIdealLapData();
            return idealLapData ? idealLapData->getIdealLapTime() : -1;
        }
#if GAME_HAS_RECORDS_PROVIDER
        else if (type == GAP_TO_RECORD) {
            const RecordsHud& recordsHud = HudManager::getInstance().getRecordsHud();
            return recordsHud.getFastestRecordLapTime();
        }
#endif
        else if (type == GAP_TO_OVERALL) {
            return getOverallBestLapTime();  // Scan standings for best lap
        }
        return -1;
    };

    // Determine if we should show gaps or placeholders
    bool showGapData = m_isFrozen;
    bool showInvalid = m_officialData.isInvalid;

    // === RENDER PRIMARY ROW COLUMNS ===
    // All columns use uniform width for consistent alignment

    // For vertical mode, add a single background quad covering the entire HUD area (like LapLogHud)
    // This must be done before individual colored quads so they render on top
    if (m_layoutVertical) {
        // Pre-calculate total height for vertical layout
        int primaryRowCount = 0;
        if (shouldShowColumn(COL_LABEL)) primaryRowCount++;
        if (shouldShowColumn(COL_TIME)) primaryRowCount++;
        if (shouldShowColumn(COL_GAP)) primaryRowCount++;

        // Count secondary gaps
        bool primaryGapVisible = shouldShowColumn(COL_GAP);
        int secondaryCount = getEnabledSecondaryGapCount(primaryGapVisible);
        bool showSecondaries = (m_displayMode == ColumnMode::ALWAYS) ||
                               (m_displayMode == ColumnMode::SPLITS && m_isFrozen);

        // Calculate heights
        float primaryHeight = primaryRowCount * dim.lineHeightLarge;
        float secondaryHeight = (secondaryCount > 0 && showSecondaries) ? (secondaryCount * dim.lineHeightNormal) : 0.0f;
        float totalContentHeight = primaryHeight + secondaryHeight;
        float backgroundHeight = dim.paddingV + totalContentHeight + dim.paddingV;

        // Calculate width (may need to expand for wider chips)
        int chipChars = m_showReference ? WidgetDimensions::TIMING_CHIP_WITH_REF_WIDTH : WidgetDimensions::TIMING_CHIP_WIDTH;
        float chipTextWidth = PluginUtils::calculateMonospaceTextWidth(chipChars, dim.fontSize);
        float actualChipWidth = dim.paddingH + chipTextWidth + dim.paddingH;
        // Only use columnWidth for chips when primary columns are visible
        float chipWidth = (primaryRowCount > 0) ? std::max(columnWidth, actualChipWidth) : actualChipWidth;
        float contentWidth = (secondaryCount > 0 && showSecondaries) ? std::max(columnWidth, chipWidth) : columnWidth;
        // When only chips are showing, use chip width not column width and recenter
        float bgLeftX = leftX;
        float backgroundWidth = dim.paddingH + contentWidth + dim.paddingH;
        if (primaryRowCount == 0 && secondaryCount > 0 && showSecondaries) {
            contentWidth = chipWidth;
            // Chip-only mode: no extra padding around the chip (tighter layout)
            bgLeftX = CENTER_X - contentWidth / 2.0f;
            backgroundWidth = contentWidth;  // Chip already includes internal padding
            // Update leftX/rightX for bounds calculation
            leftX = bgLeftX;
            rightX = bgLeftX + contentWidth;
            // Also update labelColumnX for chip positioning
            labelColumnX = timeColumnX = gapColumnX = CENTER_X - contentWidth / 2.0f;
        }

        // Add single background quad for entire vertical HUD
        addBackgroundQuad(bgLeftX, quadY, backgroundWidth, backgroundHeight);
    }

    // Add LABEL column if visible
    if (shouldShowColumn(COL_LABEL)) {
        // In vertical mode, text aligns with row; in horizontal mode, use compact padding
        float labelTextY = m_layoutVertical ? labelRowY : (labelRowY + compactPaddingV);
        // In vertical mode, skip per-row background (using single HUD background above)
        if (!m_layoutVertical) {
            addBackgroundQuad(labelColumnX, labelRowY, columnWidth, quadHeight);
        }
        if (m_layoutVertical) {
            // Vertical: center the label
            float labelX = labelColumnX + columnWidth / 2.0f;
            addString(labelBuffer, labelX, labelTextY, Justify::CENTER,
                Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
        } else {
            // Horizontal: right-align in column
            float labelX = labelColumnX + columnWidth - dim.paddingH;
            addString(labelBuffer, labelX, labelTextY, Justify::RIGHT,
                Fonts::getNormal(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
        }
    }

    // Add TIME column if visible (always centered)
    if (shouldShowColumn(COL_TIME)) {
        // In vertical mode, text aligns with row; in horizontal mode, use compact padding
        float timeTextY = m_layoutVertical ? timeRowY : (timeRowY + compactPaddingV);
        // In vertical mode, skip per-row background (using single HUD background above)
        if (!m_layoutVertical) {
            addBackgroundQuad(timeColumnX, timeRowY, columnWidth, quadHeight);
        }
        float timeX = timeColumnX + columnWidth / 2.0f;
        addString(timeBuffer, timeX, timeTextY, Justify::CENTER,
            Fonts::getStrong(), ColorConfig::getInstance().getPrimary(), dim.fontSizeLarge);
    }

    // Track bottom of primary elements for bounds
    float bottomY;
    if (m_layoutVertical) {
        // In vertical mode, bottom depends on which elements are visible
        if (shouldShowColumn(COL_GAP)) {
            bottomY = gapRowY + quadHeight;
        } else if (shouldShowColumn(COL_TIME)) {
            bottomY = timeRowY + quadHeight;
        } else if (shouldShowColumn(COL_LABEL)) {
            bottomY = labelRowY + quadHeight;
        } else {
            // No primary columns visible - chips start after top padding
            bottomY = quadY + dim.paddingV;
        }
    } else {
        bottomY = quadY + quadHeight;
    }

    // Show primary gap when gap column is visible
    if (shouldShowColumn(COL_GAP)) {

        char gapBuffer[16];
        char refBuffer[16];
        bool gapIsFaster = false;
        bool gapIsSlower = false;
        bool hasRefValue = false;

        const GapData* primaryGap = getGapDataForType(m_primaryGapType);
        bool primaryShowInvalid = showInvalid && typeUsesInvalid(m_primaryGapType);

        if (showGapData && primaryShowInvalid) {
            strcpy_s(gapBuffer, sizeof(gapBuffer), "INVALID");
            gapIsSlower = true;
        } else if (!showGapData || !primaryGap || !primaryGap->hasGap) {
            strcpy_s(gapBuffer, sizeof(gapBuffer), Placeholders::GENERIC);
            if (showRefInGap) {
                // For AT and RC, try to show actual reference time before splits
                int preSplitRef = getPreSplitRefTime(m_primaryGapType);
                if (preSplitRef > 0) {
                    PluginUtils::formatLapTime(preSplitRef, refBuffer, sizeof(refBuffer));
                } else {
                    strcpy_s(refBuffer, sizeof(refBuffer), Placeholders::LAP_TIME);
                }
                hasRefValue = true;
            }
        } else {
            PluginUtils::formatTimeDiff(gapBuffer, sizeof(gapBuffer), primaryGap->gap);
            if (showRefInGap && primaryGap->refTime > 0) {
                PluginUtils::formatLapTime(primaryGap->refTime, refBuffer, sizeof(refBuffer));
                hasRefValue = true;
            }
            gapIsFaster = primaryGap->isFaster;
            gapIsSlower = primaryGap->isSlower;
        }

        // In vertical mode, text aligns with row; in horizontal mode, use compact padding
        float gapTextY = m_layoutVertical ? gapRowY : (gapRowY + compactPaddingV);

        // Create colored background quad for primary gap
        // In vertical mode, only add quad when colored (faster/slower) - neutral uses HUD background
        bool needsGapQuad = !m_layoutVertical || gapIsFaster || gapIsSlower;
        if (needsGapQuad) {
            SPluginQuad_t gapQuad;
            float gapQuadX = gapColumnX;
            float gapQuadY = gapRowY;
            applyOffset(gapQuadX, gapQuadY);
            setQuadPositions(gapQuad, gapQuadX, gapQuadY, columnWidth, quadHeight);
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
        }

        // Determine gap text color
        unsigned long gapTextColor;
        if (gapIsFaster) {
            gapTextColor = ColorConfig::getInstance().getPositive();
        } else if (gapIsSlower) {
            gapTextColor = ColorConfig::getInstance().getNegative();
        } else {
            gapTextColor = ColorConfig::getInstance().getPrimary();
        }

        // Add gap value (centered in vertical mode without reference, left-aligned otherwise)
        if (m_layoutVertical && !showRefInGap) {
            float gapX = gapColumnX + columnWidth / 2.0f;
            addString(gapBuffer, gapX, gapTextY, Justify::CENTER,
                Fonts::getNormal(), gapTextColor, dim.fontSizeLarge);
        } else {
            float gapX = gapColumnX + dim.paddingH;
            addString(gapBuffer, gapX, gapTextY, Justify::LEFT,
                Fonts::getNormal(), gapTextColor, dim.fontSizeLarge);
        }

        // Add reference value (right-aligned) in secondary color
        if (hasRefValue) {
            float refX = gapColumnX + columnWidth - dim.paddingH;
            addString(refBuffer, refX, gapTextY, Justify::RIGHT,
                Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), dim.fontSizeLarge);
        }
    }

    // === RENDER SECONDARY GAPS AS CHIPS ===
    // Secondary gaps shown as chips (horizontal below primary, or vertical beside primary)
    // Note: Secondaries are independent of primary gap column visibility
    {
        bool primaryGapVisible = shouldShowColumn(COL_GAP);
        int secondaryCount = getEnabledSecondaryGapCount(primaryGapVisible);

        // Secondaries also respect display mode (SPLITS only during freeze, ALWAYS shows always)
        bool showSecondaries = (m_displayMode == ColumnMode::ALWAYS) ||
                               (m_displayMode == ColumnMode::SPLITS && m_isFrozen);

        if (secondaryCount > 0 && showSecondaries) {
            // Chip dimensions (smaller than primary, using compact vertical padding)
            float chipFontSize = dim.fontSize;
            float chipCompactPaddingV = dim.paddingV * 0.35f;
            // In vertical mode, use lineHeightNormal for quad height to match row step
            float chipQuadHeight = m_layoutVertical ? dim.lineHeightNormal : (chipCompactPaddingV + chipFontSize + chipCompactPaddingV);

            // Vertical gap uses same base as horizontal charGap, adjusted for aspect ratio
            float verticalGap = charGap * UI_ASPECT_RATIO;

            // Calculate chip width based on reference setting
            int chipChars = showRefInGap ? WidgetDimensions::TIMING_CHIP_WITH_REF_WIDTH : WidgetDimensions::TIMING_CHIP_WIDTH;
            float chipTextWidth = PluginUtils::calculateMonospaceTextWidth(chipChars, chipFontSize);
            float actualChipWidth = dim.paddingH + chipTextWidth + dim.paddingH;
            // Only use columnWidth for chips when primary columns are visible
            float chipWidth = (visibleCount > 0) ? std::max(columnWidth, actualChipWidth) : actualChipWidth;

            // Position chips based on layout mode
            float chipStartX, chipStartY;
            if (m_layoutVertical) {
                // Vertical layout: chips continue below primary elements in same column
                // Use primaryColumnX (content area), not leftX (which includes padding)
                chipStartX = labelColumnX;  // Same X as primary content column
                chipStartY = bottomY;  // Continue directly below last primary element (no gap)
            } else {
                // Horizontal layout: chips in a row below primary, aligned to leftmost visible column
                chipStartX = leftX;
                chipStartY = quadY + quadHeight + verticalGap;
            }

            float chipX = chipStartX;
            float chipY = chipStartY;

            // Render each enabled secondary gap type as a chip
            for (int i = 0; i < GAP_TYPE_COUNT; i++) {
                GapTypeFlags secType = GAP_TYPE_INFO[i].flag;

                // Skip primary gap type from secondaries only when primary gap is visible
                if (primaryGapVisible && secType == m_primaryGapType) continue;
                // Skip disabled secondary gaps
                if (!(m_secondaryGapTypes & secType)) continue;

                const GapData* secGapData = getGapDataForType(secType);
                bool secShowInvalid = showInvalid && typeUsesInvalid(secType);

                // Format chip: label (left) + gap (center-left) + reference (center-right)
                const char* abbrev = getGapTypeAbbrev(secType);
                char gapValueBuffer[16];
                char refValueBuffer[16];
                bool chipIsFaster = false;
                bool chipIsSlower = false;
                bool hasRefValue = false;

                if (secShowInvalid) {
                    strcpy_s(gapValueBuffer, sizeof(gapValueBuffer), "INV");
                    chipIsSlower = true;
                } else if (!showGapData || !secGapData || !secGapData->hasGap) {
                    strcpy_s(gapValueBuffer, sizeof(gapValueBuffer), Placeholders::GENERIC);
                    if (showRefInGap) {
                        // For AT and RC, try to show actual reference time before splits
                        int preSplitRef = getPreSplitRefTime(secType);
                        if (preSplitRef > 0) {
                            PluginUtils::formatLapTime(preSplitRef, refValueBuffer, sizeof(refValueBuffer));
                        } else {
                            strcpy_s(refValueBuffer, sizeof(refValueBuffer), Placeholders::LAP_TIME);
                        }
                        hasRefValue = true;
                    }
                } else {
                    PluginUtils::formatTimeDiff(gapValueBuffer, sizeof(gapValueBuffer), secGapData->gap);
                    if (showRefInGap && secGapData->refTime > 0) {
                        PluginUtils::formatLapTime(secGapData->refTime, refValueBuffer, sizeof(refValueBuffer));
                        hasRefValue = true;
                    }
                    chipIsFaster = secGapData->isFaster;
                    chipIsSlower = secGapData->isSlower;
                }

                // In vertical mode, text aligns with row; in horizontal mode, use compact padding
                float chipTextY = m_layoutVertical ? chipY : (chipY + chipCompactPaddingV);

                // Create colored background quad for chip
                // In vertical mode, only add quad when colored (faster/slower) - neutral uses HUD background
                bool needsChipQuad = !m_layoutVertical || chipIsFaster || chipIsSlower;
                if (needsChipQuad) {
                    SPluginQuad_t chipQuad;
                    float chipQuadX = chipX;
                    float chipQuadY = chipY;
                    applyOffset(chipQuadX, chipQuadY);
                    setQuadPositions(chipQuad, chipQuadX, chipQuadY, chipWidth, chipQuadHeight);
                    chipQuad.m_iSprite = SpriteIndex::SOLID_COLOR;

                    unsigned long chipBgColor;
                    if (chipIsFaster) {
                        chipBgColor = ColorConfig::getInstance().getPositive();
                    } else if (chipIsSlower) {
                        chipBgColor = ColorConfig::getInstance().getNegative();
                    } else {
                        chipBgColor = ColorConfig::getInstance().getBackground();
                    }
                    chipQuad.m_ulColor = PluginUtils::applyOpacity(chipBgColor, m_fBackgroundOpacity);
                    m_quads.push_back(chipQuad);
                }

                // Add chip text (centered in chip)
                unsigned long chipTextColor;
                if (chipIsFaster) {
                    chipTextColor = ColorConfig::getInstance().getPositive();
                } else if (chipIsSlower) {
                    chipTextColor = ColorConfig::getInstance().getNegative();
                } else {
                    chipTextColor = ColorConfig::getInstance().getPrimary();
                }

                // Add label (left-aligned) in secondary color
                float labelWidth = PluginUtils::calculateMonospaceTextWidth(2, chipFontSize);  // "PB", "AT", etc.
                addString(abbrev, chipX + dim.paddingH, chipTextY, Justify::LEFT,
                    Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), chipFontSize);

                // Calculate gap center position (centered between label and reference)
                float gapAreaLeft = chipX + dim.paddingH + labelWidth;
                float gapAreaRight = chipX + chipWidth - dim.paddingH;
                if (showRefInGap) {
                    float refWidth = PluginUtils::calculateMonospaceTextWidth(8, chipFontSize);  // LAP_TIME width
                    gapAreaRight -= refWidth;
                }
                float gapCenterX = (gapAreaLeft + gapAreaRight) / 2.0f;

                // Add gap value in gap color
                addString(gapValueBuffer, gapCenterX, chipTextY, Justify::CENTER,
                    Fonts::getNormal(), chipTextColor, chipFontSize);

                // Add reference value (right-aligned) in secondary color
                if (hasRefValue) {
                    addString(refValueBuffer, chipX + chipWidth - dim.paddingH, chipTextY, Justify::RIGHT,
                        Fonts::getNormal(), ColorConfig::getInstance().getSecondary(), chipFontSize);
                }

                // Advance position based on layout mode
                if (m_layoutVertical) {
                    // Vertical: move down for next chip (use lineHeightNormal to match other HUDs)
                    chipY += dim.lineHeightNormal;
                } else {
                    // Horizontal: move right for next chip
                    chipX += chipWidth + charGap;
                }
            }

            // Update bounds based on layout mode
            if (m_layoutVertical) {
                // Vertical: chips are below primary, extend down and possibly widen
                // Include horizontal padding if chips are wider than primary column
                rightX = std::max(rightX, chipStartX + chipWidth + dim.paddingH);
                bottomY = chipY;  // chipY is at the position of the next chip (not yet placed)
            } else {
                // Horizontal: extend down and possibly right (chips start at leftX)
                bottomY = chipStartY + chipQuadHeight;
                float chipsRowWidth = secondaryCount * chipWidth + (secondaryCount - 1) * charGap;
                rightX = std::max(rightX, chipStartX + chipsRowWidth);
            }
        }
    }

    // Set bounds (leftX/rightX already calculated from fixed column positions)
    // For vertical mode, include paddingV at the bottom for consistent padding around all sides
    if (m_layoutVertical) {
        setBounds(leftX, quadY, rightX, bottomY + dim.paddingV);
    } else {
        setBounds(leftX, quadY, rightX, bottomY);
    }
}

void TimingHud::resetToDefaults() {
    m_bVisible = false;  // Off by default
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, 0.0f);

    // Reset display mode and column visibility
    m_displayMode = ColumnMode::ALWAYS;  // Always show by default
    m_columnEnabled[COL_LABEL] = true;
    m_columnEnabled[COL_TIME] = true;
    m_columnEnabled[COL_GAP] = true;
    m_showReference = false;  // Reference off by default

    m_displayDurationMs = DEFAULT_DURATION_MS;  // 5 seconds freeze

    // Primary gap shown large, secondary gaps as chips below
    m_primaryGapType = GAP_DEFAULT_PRIMARY;      // Session PB
    m_secondaryGapTypes = GAP_DEFAULT_SECONDARY; // Alltime, Ideal, Overall as chips
    m_layoutVertical = false;                    // Horizontal layout by default

    // Reset live timing state
    resetLiveTimingState();

    setDataDirty();
}
