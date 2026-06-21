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
#include <chrono>

#include "../diagnostics/logger.h"
#include "../diagnostics/timer.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"
#include "../core/stats_manager.h"
#include "../core/hud_manager.h"

using namespace PluginConstants;

// Positioning constants
namespace {
    // Both layouts center horizontally on screen (around CENTER_X) so toggling layout doesn't jump
    constexpr float CENTER_X = 0.5f;
    constexpr float START_X = 0.0f;
    constexpr float START_Y = 0.0f;

    // Default vertical position. The layout is origin-based at the top of the draw area, so with a
    // zero offset the HUD sits flush against the top edge. Earlier releases anchored the horizontal
    // row's box top near y=0.1715 (the old TIMING_DIVIDER_Y 0.1665 + DIVIDER_GAP 0.005), so default
    // the offset to land the box top there instead. The box top sits 0.5*paddingV (0.0111 at scale 1)
    // below the content origin, hence 0.1715 - 0.0111. Saved user positions override this.
    constexpr float DEFAULT_POSITION_Y = 0.1604f;
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
    , m_cachedSessionGeneration(-1)
    , m_cachedPBScope(PBScope::CATEGORY)
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

    // Detect session changes and reset state
    // sessionGeneration is incremented on every RaceSession callback (track switch,
    // bike change, practice→race, etc.), so comparing it reliably catches all transitions.
    int currentGeneration = sessionData.sessionGeneration;

    if (currentGeneration != m_cachedSessionGeneration) {
        DEBUG_INFO_F("TimingHud: New session detected (generation %d -> %d)",
            m_cachedSessionGeneration, currentGeneration);
        resetLiveTimingState();
        m_cachedSessionGeneration = currentGeneration;
        m_cachedPitState = -1;  // Reset pit state cache for new session
        setDataDirty();
    }

    // Detect PB scope change (user toggled Bike/Category in settings)
    PBScope currentPBScope = UiConfig::getInstance().getPBScope();
    if (currentPBScope != m_cachedPBScope) {
        cacheAllTimePB();
        m_cachedPBScope = currentPBScope;
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

    // Segment-timer state changes (points added/removed, run start, chain index) must
    // refresh the line even when no official timing event fires. Live ticking while a
    // segment runs is covered by needsFrequentUpdates above; this catches transitions.
    {
        const PluginData::SegmentTimerData& seg = pluginData.getSegmentTimer();
        long long sig = static_cast<long long>(seg.points.size())
                      | (static_cast<long long>(seg.runningSeg + 1) << 16)
                      | (static_cast<long long>(seg.completionCounter) << 32);
        if (sig != m_cachedSegmentSig) {
            m_cachedSegmentSig = sig;
            setDataDirty();
        }

        // A new segment completion starts the split-style freeze (hold its time on
        // screen for the display duration). With duration 0, no freeze - just live.
        if (seg.completionCounter != m_segCachedCompletion) {
            m_segCachedCompletion = seg.completionCounter;
            if (m_displayDurationMs > 0 && seg.lastSeg >= 0) {
                m_segFrozen = true;
                m_segFrozenAt = std::chrono::steady_clock::now();
            }
            setDataDirty();
        }
        if (m_segFrozen) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - m_segFrozenAt).count();
            if (elapsed >= m_displayDurationMs) {
                m_segFrozen = false;
                setDataDirty();
            }
        }
        if (seg.segmentCount() < 1) m_segFrozen = false;  // no segments -> nothing to hold
    }

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
            m_officialData.gapToLastLap.reset();
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
        // This captures the new PB (if set) after race_lap_handler has updated StatsManager
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

        case ColumnMode::SPLITS: {
            // In segment mode the line is shown continuously (like ALWAYS), not just on freeze.
            if (PluginData::getInstance().getSegmentTimer().segmentCount() >= 1) return true;
            return m_isFrozen;  // Only during freeze
        }

        case ColumnMode::ALWAYS:
            return true;  // Always visible
    }

    return false;
}

bool TimingHud::needsFrequentUpdates() const {
    // Segment mode: a running segment ticks live regardless of display mode / official timer state.
    if (PluginData::getInstance().getSegmentTimer().runningSeg >= 0) return true;

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
        // This allows showing improvement when beating the PB, since StatsManager
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

    // === Gap to Last Lap (the most recent VALID completed lap before the one measured) ===
    {
        // Skip invalid laps, for consistency with the other references (PB/Ideal/etc.),
        // which all use valid data. The lap log (newest-first) stores both valid and
        // invalid laps; the handler folds both cases into isValid (race-invalid laps
        // keep real times but isValid=false; practice-invalid laps come through as
        // lapTime=0 -> isValid=false), so a single isValid check covers both. At lap
        // completion the just-run lap is entry [0], so start the scan at [1] to find the
        // genuine previous lap. Compare accumulated split times the same way the other
        // references do (S1, S1+S2, S1+S2+S3, full lap).
        int lastLapRef = -1;
        const std::deque<LapLogEntry>* lapLog = pluginData.getLapLog();
        if (lapLog) {
            size_t startIdx = isLapComplete ? 1 : 0;
            for (size_t i = startIdx; i < lapLog->size(); ++i) {
                const LapLogEntry& ref = (*lapLog)[i];
                if (!ref.isValid) continue;  // skip invalid laps; keep scanning back
                // First valid lap = "the last lap". Use its relevant time; if that
                // particular split isn't available (broken markers), leave no gap
                // rather than reaching past it to an older lap.
                if (isLapComplete) {
                    lastLapRef = ref.lapTime;
                } else if (splitIndex == 0) {
                    lastLapRef = ref.sector1;
                } else if (splitIndex == 1) {
                    if (ref.sector1 > 0 && ref.sector2 > 0) {
                        lastLapRef = ref.sector1 + ref.sector2;
                    }
                }
#if GAME_SECTOR_COUNT >= 4
                else if (splitIndex == 2) {
                    if (ref.sector1 > 0 && ref.sector2 > 0 && ref.sector3 > 0) {
                        lastLapRef = ref.sector1 + ref.sector2 + ref.sector3;
                    }
                }
#endif
                break;
            }
        }

        int gap = calculateGap(splitTime, lastLapRef);
        m_officialData.gapToLastLap.set(gap, lastLapRef);
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
    m_cachedSplit3 = -1;
    m_cachedLastCompletedLapNum = -1;

    // Cache current all-time PB for comparison when beating it
    cacheAllTimePB();
}

void TimingHud::cacheAllTimePB() {
    const PluginData& pluginData = PluginData::getInstance();
    const SessionData& sessionData = pluginData.getSessionData();
    const StatsPersonalBestData* allTimePB = StatsManager::getInstance()
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

    // Segment mode: when at least one segment is armed (two boundary points), this
    // timing line shows the segment - label "Segment N", the live/finished segment
    // time, and the delta to that segment's session best - instead of the official
    // split/lap. The official-timing machinery still runs in update(); we only swap
    // what's rendered. The shown segment is a just-completed one held during the
    // split-style freeze, otherwise the one currently being driven.
    const PluginData::SegmentTimerData& seg = pluginData.getSegmentTimer();
    bool segmentMode = seg.segmentCount() >= 1;
    int segShownIndex = -1;
    bool segShowFrozen = false;
    if (segmentMode) {
        if (m_segFrozen && seg.lastSeg >= 0 && seg.lastSeg < seg.segmentCount()) {
            segShownIndex = seg.lastSeg;
            segShowFrozen = true;
        } else if (seg.runningSeg >= 0 && seg.runningSeg < seg.segmentCount()) {
            segShownIndex = seg.runningSeg;
        }
    }
    int segRefBestMs = (segShownIndex >= 0 && seg.hasBest[segShownIndex])
        ? static_cast<int>(seg.bests[segShownIndex] * 1000.0f + 0.5f) : -1;
    GapData segGap;  // delta-to-segment-best, used only in segment mode

    // Check if any columns or secondary gaps should be visible
    int visibleCount = getVisibleColumnCount();
    int secondaryCount = getEnabledSecondaryGapCount(shouldShowColumn(COL_GAP));

    // Secondaries also respect display mode - and are suppressed entirely in segment mode
    // (a custom segment has only its own best, no ideal/overall/record references).
    bool showSecondaries = !segmentMode &&
                           ((m_displayMode == ColumnMode::ALWAYS) ||
                            (m_displayMode == ColumnMode::SPLITS && m_isFrozen));

    if (visibleCount == 0 && (secondaryCount == 0 || !showSecondaries)) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    auto dim = getScaledDimensions();

    // Both layouts center the HUD on CENTER_X. Quantize that centering anchor to the horizontal grid
    // when grid snapping is on: the drag offset is snapped too, so left edge = snapped anchor + snapped
    // offset stays on the shared grid lattice and the HUD lines up with other HUDs (while still reading
    // as centered). With snapping off, leave it exactly centered for free placement.
    auto snapCenteringToGrid = [](float x) -> float {
        return UiConfig::getInstance().getGridSnapping()
            ? PluginConstants::HudGrid::SNAP_TO_GRID_X(x)
            : x;
    };

    // Check if reference should be shown in gap column (applies to both primary and secondary)
    bool showRefInGap = m_showReference;

    // Per-column dimensions based on content character counts
    float labelTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::TIMING_LABEL_WIDTH, dim.fontSizeLarge);
    float timeTextWidth = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::TIMING_TIME_WIDTH, dim.fontSizeLarge);
    // Primary gap is laid out like a featured chip (type label + gap + ref), so size it
    // with the chip widths rather than the bare-gap widths.
    int gapChars = showRefInGap
        ? (m_layoutVertical ? WidgetDimensions::TIMING_CHIP_WITH_REF_WIDTH_COMPACT : WidgetDimensions::TIMING_CHIP_WITH_REF_WIDTH)
        : WidgetDimensions::TIMING_CHIP_WIDTH;
    float gapTextWidth = PluginUtils::calculateMonospaceTextWidth(gapChars, dim.fontSizeLarge);

    // Calculate individual quad widths
    float labelQuadWidth = dim.paddingH + labelTextWidth + dim.paddingH;
    float timeQuadWidth = dim.paddingH + timeTextWidth + dim.paddingH;
    float gapQuadWidth = dim.paddingH + gapTextWidth + dim.paddingH;

    // Use uniform column width (widest of all columns) for consistent alignment
    float columnWidth = std::max({labelQuadWidth, timeQuadWidth, gapQuadWidth});

    // Gap between columns (half char width for tighter spacing)
    float charGap = PluginUtils::calculateMonospaceTextWidth(1, dim.fontSizeLarge) * 0.5f;

    // Width reserved for the gap/delta column, always at the large (primary-row) size so the
    // primary row, the secondary chips, and the vertical time value all right-align the
    // reference to the same X regardless of their own font size.
    float deltaColW = PluginUtils::calculateMonospaceTextWidth(WidgetDimensions::TIMING_GAP_WIDTH, dim.fontSizeLarge);

    // Primary rows mirror a HUD title: the large font (fontSizeLarge) in a lineHeightLarge band, in
    // both layouts. lineHeightLarge is exactly 2x lineHeightNormal, so the row tops land on the shared
    // half-line grid — a stacked vertical timing HUD lines up with a neighbouring HUD's title and rows,
    // and the secondary chips (normal font, lineHeightNormal) tuck in below on the same grid.
    float quadHeight = dim.lineHeightLarge;

    // Layout depends on mode: horizontal (side by side) or vertical (stacked)
    // Both layouts share the same Y origin and center horizontally on screen, so the HUD stays put
    // (centered) when switching between vertical and horizontal instead of jumping.
    float quadY = START_Y;

    // Column positions - calculated differently based on layout mode
    float labelColumnX, timeColumnX, gapColumnX;
    float labelRowY, timeRowY, gapRowY;  // Y positions for vertical mode
    float leftX, rightX;
    float vertBgLeftX = 0.0f, vertBgWidth = 0.0f;  // Full HUD bounds for vertical mode

    if (m_layoutVertical) {
        // Vertical layout: origin-based positioning (like LapLogHud/StandingsHud)
        // Column starts at START_X; offset handles screen placement
        labelColumnX = timeColumnX = gapColumnX = START_X;

        // Stack only enabled rows vertically (no gaps for disabled elements)
        // Primary rows step by the large band (quadHeight); secondaries use lineHeightNormal
        // Start content after paddingV from top (like LapLogHud/StandingsHud)
        float currentY = START_Y + dim.paddingV;
        float rowStep = quadHeight;

        // Label and time each get their own row (time row carries its own "Time" label)
        if (shouldShowColumn(COL_LABEL)) {
            labelRowY = currentY;
            currentY += rowStep;
        } else {
            labelRowY = START_Y;
        }

        if (shouldShowColumn(COL_TIME)) {
            timeRowY = currentY;
            currentY += rowStep;
        } else {
            timeRowY = START_Y;
        }

        if (shouldShowColumn(COL_GAP)) {
            gapRowY = currentY;
        } else {
            gapRowY = START_Y;
        }

        // columnWidth already includes paddingH on each side, so no extra padding needed
        leftX = START_X;
        rightX = START_X + columnWidth;
    } else {
        // Horizontal layout: only the VISIBLE columns are packed side by side (order: label, time,
        // gap), centered as a group — so hiding a column closes the gap rather than leaving a hole.
        float groupWidth = (visibleCount > 0)
            ? (visibleCount * columnWidth + (visibleCount - 1) * charGap)
            : columnWidth;
        float groupLeft = snapCenteringToGrid(CENTER_X - groupWidth / 2.0f);  // centered on screen, grid-aligned

        // Fallbacks (used only for a hidden column, never rendered)
        labelColumnX = timeColumnX = gapColumnX = groupLeft;
        float x = groupLeft;
        if (shouldShowColumn(COL_LABEL)) { labelColumnX = x; x += columnWidth + charGap; }
        if (shouldShowColumn(COL_TIME))  { timeColumnX = x;  x += columnWidth + charGap; }
        if (shouldShowColumn(COL_GAP))   { gapColumnX = x;   x += columnWidth + charGap; }

        // All on the same row, inset by paddingV from the HUD's top edge so the primary text lines up
        // with a neighbouring HUD's title (which also sits paddingV below its top edge, e.g. LapLog).
        // The per-column boxes extend up to the HUD top (see the background quads below), so this
        // paddingV becomes internal top padding above the text — matching a LapLog title's box.
        labelRowY = timeRowY = gapRowY = quadY + dim.paddingV;

        leftX = groupLeft;
        rightX = groupLeft + groupWidth;
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
    bool timePlaceholder = false;
    if (m_isFrozen) {
        // Show official time
        if (m_officialData.time > 0) {
            PluginUtils::formatLapTime(m_officialData.time, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
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
            timePlaceholder = true;
        }
    }

    // === SEGMENT MODE OVERRIDE ===
    // Replace label/time with the shown segment's, and stage its delta-to-best gap.
    if (segmentMode) {
        if (segShownIndex >= 0) {
            snprintf(labelBuffer, sizeof(labelBuffer), "Segment %d", segShownIndex + 1);
        } else {
            strcpy_s(labelBuffer, sizeof(labelBuffer), "Segment");
        }

        if (segShowFrozen) {
            // Hold the just-completed segment's time + delta-to-its-best.
            PluginUtils::formatLapTime(static_cast<int>(seg.lastTime * 1000.0f + 0.5f),
                                       timeBuffer, sizeof(timeBuffer));
            timePlaceholder = false;
            if (seg.lastHasDelta) {
                int deltaMs = static_cast<int>(seg.lastDelta * 1000.0f +
                                               (seg.lastDelta < 0.0f ? -0.5f : 0.5f));
                segGap.set(deltaMs, segRefBestMs);
            } else {
                segGap.reset();
            }
        } else if (seg.runningSeg >= 0) {
            // Live count-up off the wall clock (ticks even when stationary).
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - seg.runStart).count();
            PluginUtils::formatLapTime(static_cast<int>(ms), timeBuffer, sizeof(timeBuffer));
            timePlaceholder = false;
            segGap.reset();
        } else {
            // Armed but not currently in a segment (between runs / before the first point).
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
            segGap.reset();
        }
    }

    // === HELPER LAMBDAS ===
    auto getGapDataForType = [&](GapTypeFlags type) -> const GapData* {
        // Segment mode only has the primary row, fed by the staged segment delta.
        if (segmentMode) return (type == m_primaryGapType) ? &segGap : nullptr;
        switch (type) {
            case GAP_TO_PB: return &m_officialData.gapToPB;
            case GAP_TO_ALLTIME: return &m_officialData.gapToAllTime;
            case GAP_TO_IDEAL: return &m_officialData.gapToIdeal;
            case GAP_TO_OVERALL: return &m_officialData.gapToOverall;
#if GAME_HAS_RECORDS_PROVIDER
            case GAP_TO_RECORD: return &m_officialData.gapToRecord;
#endif
            case GAP_TO_LASTLAP: return &m_officialData.gapToLastLap;
            default: return nullptr;
        }
    };

    auto typeUsesInvalid = [](GapTypeFlags type) -> bool {
        return type == GAP_TO_PB || type == GAP_TO_ALLTIME;
    };

    // Get lap-level reference time for a gap type before any splits are crossed
    // This allows showing reference times immediately when available
    auto getPreSplitRefTime = [&](GapTypeFlags type) -> int {
        // Segment mode: the only reference/target is the shown segment's session best.
        if (segmentMode) return segRefBestMs;
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
        else if (type == GAP_TO_LASTLAP) {
            // Before any split is crossed, the current lap is compared against the most
            // recent VALID completed lap (skip invalid ones, matching calculateAllGaps).
            const std::deque<LapLogEntry>* lapLog = pluginData.getLapLog();
            if (lapLog) {
                for (size_t i = 0; i < lapLog->size(); ++i) {
                    if (!(*lapLog)[i].isValid) continue;
                    return (*lapLog)[i].lapTime > 0 ? (*lapLog)[i].lapTime : -1;
                }
            }
            return -1;
        }
        return -1;
    };

    // Determine if we should show gaps or placeholders
    // Segment mode shows the delta only during the freeze of a completed segment that
    // had a prior best to compare against.
    bool showGapData = segmentMode ? (segShowFrozen && seg.lastHasDelta) : m_isFrozen;
    bool showInvalid = !segmentMode && m_officialData.isInvalid;

    // Resolve the gap/reference text and state for one gap type. The primary row and the secondary
    // chips share this exact logic (only the "invalid" abbreviation differs), so it lives here once.
    struct GapColumnText {
        char gap[16] = "";
        char ref[16] = "";
        bool hasRef = false;
        bool refIsPlaceholder = false;
        bool isFaster = false;
        bool isSlower = false;
    };
    auto buildGapColumn = [&](GapTypeFlags type) -> GapColumnText {
        GapColumnText out;
        const GapData* gapData = getGapDataForType(type);
        if (showGapData && showInvalid && typeUsesInvalid(type)) {
            strcpy_s(out.gap, sizeof(out.gap), "INVALID");
            out.isSlower = true;
            // The lap is invalid, but the reference you're measured against still stands, so keep
            // showing it (falling back to the pre-split reference if the gap has no stored refTime).
            if (showRefInGap) {
                int ref = (gapData && gapData->refTime > 0) ? gapData->refTime : getPreSplitRefTime(type);
                if (ref > 0) {
                    PluginUtils::formatLapTime(ref, out.ref, sizeof(out.ref));
                    out.hasRef = true;
                }
            }
        } else if (!showGapData || !gapData || !gapData->hasGap) {
            int preSplitRef = getPreSplitRefTime(type);
            bool refAvailable = (preSplitRef > 0);
            // Only the Record source can be genuinely unavailable (provider hasn't fetched, or no
            // record for this track) -> "N/A". PB/Alltime/Ideal/Overall will be set soon -> "-".
            const char* missing = (type == GAP_TO_RECORD) ? Placeholders::NOT_AVAILABLE : Placeholders::GENERIC;
            strcpy_s(out.gap, sizeof(out.gap), refAvailable ? Placeholders::GENERIC : missing);
            if (showRefInGap) {
                if (refAvailable) {
                    PluginUtils::formatLapTime(preSplitRef, out.ref, sizeof(out.ref));
                } else {
                    strcpy_s(out.ref, sizeof(out.ref), missing);
                    out.refIsPlaceholder = true;
                }
                out.hasRef = true;
            }
        } else {
            PluginUtils::formatTimeDiff(out.gap, sizeof(out.gap), gapData->gap);
            if (showRefInGap && gapData->refTime > 0) {
                PluginUtils::formatLapTime(gapData->refTime, out.ref, sizeof(out.ref));
                out.hasRef = true;
            }
            out.isFaster = gapData->isFaster;
            out.isSlower = gapData->isSlower;
        }
        return out;
    };

    // Faster/slower/neutral colors, shared by the primary gap row and the chips. The colored
    // background strip uses the HUD background for neutral; the text uses muted for neutral.
    auto gapBgColor = [&](bool isFaster, bool isSlower) -> unsigned long {
        return this->getColor(isFaster ? ColorSlot::POSITIVE : isSlower ? ColorSlot::NEGATIVE : ColorSlot::BACKGROUND);
    };
    auto gapTextColor = [&](bool isFaster, bool isSlower) -> unsigned long {
        return this->getColor(isFaster ? ColorSlot::POSITIVE : isSlower ? ColorSlot::NEGATIVE : ColorSlot::MUTED);
    };

    // Emit a colored background strip (SOLID_COLOR sprite, opacity applied) behind a gap value.
    auto addGapQuad = [&](float x, float y, float w, float h, unsigned long baseColor) {
        SPluginQuad_t quad;
        applyOffset(x, y);
        setQuadPositions(quad, x, y, w, h);
        quad.m_iSprite = SpriteIndex::SOLID_COLOR;
        quad.m_ulColor = PluginUtils::applyOpacity(baseColor, m_fBackgroundOpacity);
        m_quads.push_back(quad);
    };

    // One gap row = colored strip (when faster/slower, or always in horizontal) + left type label +
    // right-aligned reference and gap values. The primary gap row and each secondary chip are the
    // same layout at different sizes, so both render through here.
    struct GapRowStyle {
        float textY;          // top of the row text
        float quadX, quadY;   // top-left of the colored strip
        float quadW, quadH;   // size of the colored strip
        float labelX;         // left edge for the type label
        float rightEdge;      // right edge for the gap value (reference sits deltaColW+charGap to its left)
        float valueFontSize;  // font size for the reference + gap values
        bool  largeLabel;     // true: label at fontSizeLarge (primary row); false: small label (chip)
        const char* label;    // type label text
    };
    auto renderGapRow = [&](const GapColumnText& g, const GapRowStyle& s) {
        // Vertical neutral rows sit on the single HUD background; everything else gets a strip.
        if (!m_layoutVertical || g.isFaster || g.isSlower) {
            addGapQuad(s.quadX, s.quadY, s.quadW, s.quadH, gapBgColor(g.isFaster, g.isSlower));
        }
        if (s.largeLabel) {
            addString(s.label, s.labelX, s.textY, Justify::LEFT,
                this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim.fontSizeLarge);
        } else {
            addLabel(s.label, s.labelX, s.textY, Justify::LEFT,
                this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim);
        }
        if (g.hasRef) {
            unsigned long refColor = g.refIsPlaceholder ? this->getColor(ColorSlot::MUTED) : this->getColor(ColorSlot::SECONDARY);
            addString(g.ref, s.rightEdge - deltaColW - charGap, s.textY, Justify::RIGHT,
                this->getFont(FontCategory::DIGITS), refColor, s.valueFontSize);
        }
        addString(g.gap, s.rightEdge, s.textY, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), gapTextColor(g.isFaster, g.isSlower), s.valueFontSize);
    };

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

        // secondaryCount / showSecondaries computed once at the top of rebuildRenderData

        // Calculate heights
        float primaryHeight = primaryRowCount * quadHeight;
        float secondaryHeight = (secondaryCount > 0 && showSecondaries) ? (secondaryCount * dim.lineHeightNormal) : 0.0f;
        float totalContentHeight = primaryHeight + secondaryHeight;
        float backgroundHeight = dim.paddingV + totalContentHeight + dim.paddingV;

        // Match the standard HUD widths so the timing HUD lines up with the rest of the UI:
        // references off -> 27 chars (FMX / IdealLap / Stats), on -> 43 (Event Log / Lap Log).
        // The fixed width also gives room to spell the gap labels out.
        float backgroundWidth = calculateBackgroundWidth(m_showReference ? 43 : 27);
        // Center the HUD horizontally on screen (around CENTER_X), like horizontal mode, so toggling
        // layout keeps it centered instead of flying off. Content + chips anchor to this left edge.
        // When grid snapping is on, quantize the centering anchor to the grid so the (also snapped)
        // drag offset keeps the left edge on the shared lattice — lets it line up with other HUDs
        // while staying centered. See horizontal mode for the matching treatment.
        float bgLeftX = snapCenteringToGrid(CENTER_X - backgroundWidth / 2.0f);
        labelColumnX = timeColumnX = gapColumnX = bgLeftX;
        leftX = bgLeftX;
        rightX = bgLeftX + backgroundWidth;

        // Add single background quad for entire vertical HUD
        addBackgroundQuad(bgLeftX, quadY, backgroundWidth, backgroundHeight);

        // Store full HUD bounds for edge-to-edge highlights and text alignment
        vertBgLeftX = bgLeftX;
        vertBgWidth = backgroundWidth;
    }

    // Horizontal per-column boxes hug the text itself (fontSizeLarge) with equal padding above and
    // below, rather than wrapping the full lineHeightLarge band (whose dead space sat under the text
    // and made the box look bottom-heavy). Half the normal vertical padding keeps the box compact.
    // The text stays at rowY (quadY + paddingV) so it still lines up with a neighbour HUD's title.
    const float boxPadV = dim.paddingV * 0.5f;
    const float primaryBoxHeight = dim.fontSizeLarge + 2.0f * boxPadV;

    // Add LABEL column if visible
    if (shouldShowColumn(COL_LABEL)) {
        // Text top-aligned at the row top in both modes (matches a title / the chips / other HUDs)
        float labelTextY = labelRowY;
        // In vertical mode, skip per-row background (using single HUD background above)
        if (!m_layoutVertical) {
            addBackgroundQuad(labelColumnX, labelRowY - boxPadV, columnWidth, primaryBoxHeight);
        }
        // Label is left-aligned in both modes (the row's identifier), rendered at the large title size
        float labelX = (m_layoutVertical ? vertBgLeftX : labelColumnX) + dim.paddingH;
        addString(labelBuffer, labelX, labelTextY, Justify::LEFT,
            this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim.fontSizeLarge);
    }

    // Add TIME column if visible
    if (shouldShowColumn(COL_TIME)) {
        // Text top-aligned at the row top in both modes
        float timeTextY = timeRowY;
        // In vertical mode, skip per-row background (using single HUD background above)
        if (!m_layoutVertical) {
            addBackgroundQuad(timeColumnX, timeRowY - boxPadV, columnWidth, primaryBoxHeight);
        }

        unsigned long timeColor = timePlaceholder ? this->getColor(ColorSlot::MUTED) : this->getColor(ColorSlot::PRIMARY);

        if (m_layoutVertical) {
            // Vertical: left-aligned "Time" label so the row reads label + value, like the gap rows/chips.
            float timeLabelX = vertBgLeftX + dim.paddingH;
            addString("Time", timeLabelX, timeTextY, Justify::LEFT,
                this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim.fontSizeLarge);

            // Value right-aligned over the reference column (left of the gap/delta column) so your time
            // stacks above the reference times; with no references it uses the content right edge.
            float contentRight = vertBgLeftX + vertBgWidth - dim.paddingH;
            float timeX = showRefInGap ? (contentRight - deltaColW - charGap) : contentRight;
            addString(timeBuffer, timeX, timeTextY, Justify::RIGHT,
                this->getFont(FontCategory::DIGITS), timeColor, dim.fontSizeLarge);
        } else {
            // Horizontal: no "Time" label — just the lap time, centered in the cell.
            float timeX = timeColumnX + columnWidth / 2.0f;
            addString(timeBuffer, timeX, timeTextY, Justify::CENTER,
                this->getFont(FontCategory::DIGITS), timeColor, dim.fontSizeLarge);
        }
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
    } else if (shouldShowColumn(COL_LABEL) || shouldShowColumn(COL_TIME) || shouldShowColumn(COL_GAP)) {
        // Bottom of the compact primary box (its top is rowY - boxPadV, height primaryBoxHeight).
        bottomY = (quadY + dim.paddingV - boxPadV) + primaryBoxHeight;
    } else {
        // No primary columns visible - don't reserve the primary row height (matches the
        // vertical branch); chips start from the top padding instead of below an empty box.
        bottomY = quadY + dim.paddingV;
    }

    // Show primary gap when gap column is visible
    if (shouldShowColumn(COL_GAP)) {
        GapColumnText primary = buildGapColumn(m_primaryGapType);

        // Primary gap is rendered like a featured chip: type label (left), then reference and gap
        // values right-aligned. Type label is spelled out in vertical (room there), abbreviated in
        // horizontal (narrow cell). The colored strip hugs the text in horizontal (like the label/time
        // boxes) and fills the band inside the single HUD background in vertical.
        GapRowStyle style;
        style.textY = gapRowY;
        style.quadX = m_layoutVertical ? vertBgLeftX : gapColumnX;
        style.quadW = m_layoutVertical ? vertBgWidth : columnWidth;
        style.quadY = m_layoutVertical ? gapRowY : (gapRowY - boxPadV);
        style.quadH = m_layoutVertical ? quadHeight : primaryBoxHeight;
        style.labelX = (m_layoutVertical ? vertBgLeftX : gapColumnX) + dim.paddingH;
        style.rightEdge = (m_layoutVertical ? (vertBgLeftX + vertBgWidth) : (gapColumnX + columnWidth)) - dim.paddingH;
        style.valueFontSize = dim.fontSizeLarge;
        style.largeLabel = true;
        style.label = segmentMode ? "Best"
                    : (m_layoutVertical ? getGapTypeName(m_primaryGapType) : getGapTypeAbbrev(m_primaryGapType));
        renderGapRow(primary, style);
    }

    // === RENDER SECONDARY GAPS AS CHIPS ===
    // Secondary gaps shown as chips (horizontal below primary, or vertical beside primary)
    // Note: Secondaries are independent of primary gap column visibility
    {
        // secondaryCount / showSecondaries computed once at the top of rebuildRenderData
        bool primaryGapVisible = shouldShowColumn(COL_GAP);

        if (secondaryCount > 0 && showSecondaries) {
            // Chips are the secondary (normal-font) rows. Both layouts put them on the lineHeightNormal
            // grid band, directly below the primary rows — matching vertical, the chips' own rhythm and
            // every other HUD, so the whole timing HUD stays on the shared grid.
            float chipFontSize = dim.fontSize;
            float chipQuadHeight = dim.lineHeightNormal;

            // Calculate chip width based on reference setting
            int chipChars = showRefInGap
                ? (m_layoutVertical ? WidgetDimensions::TIMING_CHIP_WITH_REF_WIDTH_COMPACT : WidgetDimensions::TIMING_CHIP_WITH_REF_WIDTH)
                : WidgetDimensions::TIMING_CHIP_WIDTH;
            float chipTextWidth = PluginUtils::calculateMonospaceTextWidth(chipChars, chipFontSize);
            float actualChipWidth = dim.paddingH + chipTextWidth + dim.paddingH;
            // Always floor chip width at columnWidth: the chip reference is reserved at the
            // primary's (large) font size, so the box must stay wide enough to host it even
            // when no primary columns are visible. Dropping to actualChipWidth there pushed
            // the right-aligned reference left into the label.
            float chipWidth = std::max(columnWidth, actualChipWidth);

            // Position chips based on layout mode
            float chipStartX, chipStartY;
            if (m_layoutVertical) {
                // Vertical layout: chips continue below primary elements in same column
                // Use primaryColumnX (content area), not leftX (which includes padding)
                chipStartX = labelColumnX;  // Same X as primary content column
                chipStartY = bottomY;  // Continue directly below last primary element (no gap)
            } else {
                // Horizontal layout: chips in a row below the primary row, aligned to the leftmost
                // visible column. The vertical gap below the primary boxes matches the horizontal gap
                // between them (charGap), so the spacing reads consistently in both directions.
                chipStartX = leftX;
                chipStartY = bottomY + charGap;
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

                GapColumnText chip = buildGapColumn(secType);

                // A chip is the same gap row as the primary, at the normal font with a small label.
                // The colored strip sits at chipY (no box padding); the label is spelled out in both
                // modes (room in the normal-font box). In vertical the chip spans the full HUD width,
                // so right-align to the HUD edge (same as the primary) not the chip's narrower box.
                GapRowStyle style;
                style.textY = chipY;
                style.quadX = m_layoutVertical ? vertBgLeftX : chipX;
                style.quadW = m_layoutVertical ? vertBgWidth : chipWidth;
                style.quadY = chipY;
                style.quadH = chipQuadHeight;
                style.labelX = chipX + dim.paddingH;
                style.rightEdge = (m_layoutVertical ? (vertBgLeftX + vertBgWidth) : (chipX + chipWidth)) - dim.paddingH;
                style.valueFontSize = chipFontSize;
                style.largeLabel = false;
                style.label = getGapTypeName(secType);
                renderGapRow(chip, style);

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
                // chipWidth already includes internal padding
                rightX = std::max(rightX, chipStartX + chipWidth);
                bottomY = chipY;  // chipY is at the position of the next chip (not yet placed)
            } else {
                // Horizontal: extend down and possibly right (chips start at leftX)
                bottomY = chipStartY + chipQuadHeight;
                float chipsRowWidth = secondaryCount * chipWidth + (secondaryCount - 1) * charGap;
                rightX = std::max(rightX, chipStartX + chipsRowWidth);
            }
        }
    }

    // Set bounds (leftX/rightX already calculated from fixed column positions). Both modes carry
    // paddingV top (baked into the row positions) and bottom, so the HUD has symmetric padding like
    // LapLog and lines up when placed against other HUDs.
    setBounds(leftX, quadY, rightX, bottomY + dim.paddingV);
}

void TimingHud::resetToDefaults() {
    m_bVisible = false;  // Off by default
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, DEFAULT_POSITION_Y);

    // Reset display mode and column visibility
    m_displayMode = ColumnMode::ALWAYS;  // Always show by default
    m_columnEnabled[COL_LABEL] = false;  // Label off by default
    m_columnEnabled[COL_TIME] = true;
    m_columnEnabled[COL_GAP] = false;    // Primary gap off by default
    m_showReference = false;  // Reference off by default

    m_displayDurationMs = DEFAULT_DURATION_MS;  // 5 seconds freeze

    // Primary gap shown large, secondary gaps as chips below
    m_primaryGapType = GAP_DEFAULT_PRIMARY;      // Session PB
    m_secondaryGapTypes = GAP_DEFAULT_SECONDARY; // All-Time PB as a chip
    m_layoutVertical = false;                    // Horizontal layout by default

    // Reset live timing state
    resetLiveTimingState();

    setDataDirty();
}
