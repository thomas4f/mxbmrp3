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
#include <cstring>    // std::strlen
#include <cmath>
#include <string>
#include <chrono>
#include <algorithm>  // std::max

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

    // Default vertical position: LAST in the center-top stack (GapBar -> Notices -> Timing, one
    // grid snap between each). The Timing HUD grows DOWN, so placing it at the bottom means it
    // never overlaps the notice/gapbar above no matter how many comparison rows are enabled.
    //   notice bottom (0.117336) + 1 vertical cell (0.011734) = 0.129069 (== notices divider).
    // All three boxes are now lineHeightLarge tall (4 cells), so the whole stack lands on the
    // grid: box tops at cells 1/6/11, box bottoms at cells 5/10.
    constexpr float DEFAULT_POSITION_Y = 0.129069f;
}

TimingHud::TimingHud()
    : m_displayDurationMs(DEFAULT_DURATION_MS)
    , m_showTime(true)
    , m_enabledComparisons(GAP_DEFAULT_ENABLED)
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
    m_quads.reserve(1);    // Single background quad (values carry colour via text, no strips)
    m_strings.reserve(8);  // Time + (name + value) per comparison row

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
    if (!isVisibleAnySurface()) {
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
        // A lap during which the rider was in the pits is not a genuine timed lap: the live
        // timer is dropped on pit exit and re-anchored at the next S/F crossing (that S/F
        // crossing is where this lap "completes"). Remember it so the pit out-lap's completion
        // doesn't flash INVALID - there's no timing to invalidate. Cleared when the lap
        // completes (processTimingUpdates) or on a session/spectate reset.
        //
        // Only latch while a timed lap is actually underway (the lap timer is anchored). At the
        // START of a practice/qualify session the rider sits in the garage/pit (pit==1) BEFORE
        // ever crossing S/F, so there is no lap to interrupt yet - the out-lap from the garage
        // produces no lap-completion event, so nothing here would consume the flag, and it would
        // wrongly carry the pre-lap garage sit into the FIRST genuine flying lap and suppress its
        // freeze (the reported "first lap didn't freeze" bug). A real mid-lap pit keeps the
        // anchor valid until pit EXIT, so it still latches here. (In spectate/replay the anchor
        // is the only gate; on track isLapTimerValid also requires the sim to be running, which
        // it is while riding through the pits.)
        if (currentPitState == 1 && pluginData.isLapTimerValid()) {
            m_lapInterruptedByPit = true;
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

        // A lap that passed through the pits isn't a genuine timed lap: the live timer was
        // reset on pit exit and re-anchors at this very S/F crossing. There's no timing to
        // invalidate, so don't freeze on it or flash INVALID - just let the freshly started
        // lap tick. (A lap invalidated by cuts, with the timer running throughout, still
        // freezes and shows INVALID.) Consume the flag: the fresh lap starts clean.
        bool pitLap = m_lapInterruptedByPit;
        m_lapInterruptedByPit = false;

        // Update official data cache
        m_officialData.time = lapTime;
        m_officialData.splitIndex = -1;  // Indicates lap complete
        m_officialData.lapNum = completedLapNum;
        m_officialData.isInvalid = !isValid && !pitLap;

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

        // Freeze display (if freeze is enabled). Skip the freeze entirely for a pit-interrupted
        // lap - there's nothing meaningful to hold, so the live timer keeps counting the new lap.
        if (m_displayDurationMs > 0 && !pitLap) {
            m_isFrozen = true;
            m_frozenAt = std::chrono::steady_clock::now();
        }

        m_cachedLastCompletedLapNum = idealLapData->lastCompletedLapNum;
        DEBUG_INFO_F("TimingHud: Lap %d completed, time=%d ms, valid=%d, pitLap=%d",
            completedLapNum, lapTime, isValid, pitLap ? 1 : 0);
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

bool TimingHud::contentVisible() const {
    switch (m_displayMode) {
        case ColumnMode::OFF:
            return false;
        case ColumnMode::SPLITS:
            // In segment mode the panel shows continuously (like ALWAYS), not just on freeze.
            if (PluginData::getInstance().getSegmentTimer().segmentCount() >= 1) return true;
            return m_isFrozen;  // Only during the split/lap freeze
        case ColumnMode::ALWAYS:
            return true;
    }
    return false;
}

bool TimingHud::showingInvalid() const {
    // In segment mode the official split/lap machinery is swapped out, so INVALID never shows.
    if (PluginData::getInstance().getSegmentTimer().segmentCount() >= 1) return false;
    return m_isFrozen && m_officialData.isInvalid;
}

bool TimingHud::needsFrequentUpdates() const {
    // Segment mode: a running segment ticks live regardless of display mode / official timer state.
    if (PluginData::getInstance().getSegmentTimer().runningSeg >= 0) return true;

    // Need frequent updates when the ticking time is shown (ALWAYS mode), not frozen, timer valid.
    if (m_isFrozen) return false;
    if (m_displayMode != ColumnMode::ALWAYS || !m_showTime) return false;

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

void TimingHud::setComparisonEnabled(GapTypeFlags flag, bool enabled) {
    if (enabled) {
        m_enabledComparisons |= flag;
    } else {
        m_enabledComparisons &= ~flag;
    }
    setDataDirty();
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

void TimingHud::resetLiveTimingState() {
    // Note: Anchor and track monitor are now managed centrally by PluginData
    // Reset local display state only
    m_isFrozen = false;
    m_officialData.reset();
    m_cachedSplit1 = -1;
    m_cachedSplit2 = -1;
    m_cachedSplit3 = -1;
    m_cachedLastCompletedLapNum = -1;
    m_lapInterruptedByPit = false;

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

// Which split boundary the rider is currently driving toward, from the lap timer's sector
// (track-position driven, so it is correct from the first flying lap — unlike the discrete
// CurrentLapData splits, which aren't populated until the first split of a fresh lap is
// crossed). currentSector: 0=before S1, 1=before S2, …, (GAME_SECTOR_COUNT-1)=before the
// finish; the last one (and a stopped/finished timer) maps to -1 (=> full lap).
int TimingHud::currentTargetSplit() const {
    const PluginData& pluginData = PluginData::getInstance();
    if (pluginData.isLapTimerValid() && !pluginData.isDisplayRiderFinished()) {
        int sec = pluginData.getLapTimerCurrentSector();
        if (sec >= 0 && sec < GAME_SECTOR_COUNT - 1) return sec;
    }
    return -1;
}

// Whole-lap reference (target) time for a gap type (segment-agnostic; the render path handles
// segment mode before calling). -1 when unavailable.
int TimingHud::fullLapReferenceMs(GapTypeFlags type) const {
    const PluginData& pluginData = PluginData::getInstance();
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
        return HudManager::getInstance().getRecordsHud().getFastestRecordLapTime();
    }
#endif
    else if (type == GAP_TO_OVERALL) {
        return getOverallBestLapTime();  // Scan standings for best lap
    }
    else if (type == GAP_TO_LASTLAP) {
        // Compare against the most recent VALID completed lap (skip invalid ones).
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
}

// Cumulative reference (target) time up to and including targetSplit (0 = S1, 1 = S1+S2,
// 2 = S1+S2+S3 on 4-sector, -1 = full lap), so the ticking clock can be read against the
// sector being driven. Segment-agnostic. -1 when the reference lacks the needed sectors.
int TimingHud::cumulativeReferenceMs(GapTypeFlags type, int targetSplit) const {
    if (targetSplit < 0) return fullLapReferenceMs(type);
    const PluginData& pluginData = PluginData::getInstance();

    // All-Time PB keeps its cumulative points pre-summed (cacheAllTimePB()).
    if (type == GAP_TO_ALLTIME) {
        if (targetSplit == 0) return m_previousAllTimeSector1;
        if (targetSplit == 1) return m_previousAllTimeS1PlusS2;
#if GAME_SECTOR_COUNT >= 4
        if (targetSplit == 2) return m_previousAllTimeS1PlusS2PlusS3;
#endif
        return m_previousAllTimeLap;
    }

    // Everything else exposes per-sector reference times; sum the first (targetSplit+1).
    int s1 = -1, s2 = -1, s3 = -1;
    switch (type) {
        case GAP_TO_PB: {
            const LapLogEntry* pb = pluginData.getBestLapEntry();
            if (pb) { s1 = pb->sector1; s2 = pb->sector2; s3 = pb->sector3; }
            break;
        }
        case GAP_TO_IDEAL: {
            const IdealLapData* il = pluginData.getIdealLapData();
            if (il) { s1 = il->bestSector1; s2 = il->bestSector2; s3 = il->bestSector3; }
            break;
        }
        case GAP_TO_OVERALL: {
            const LapLogEntry* ob = pluginData.getOverallBestLap();
            if (ob) { s1 = ob->sector1; s2 = ob->sector2; s3 = ob->sector3; }
            break;
        }
#if GAME_HAS_RECORDS_PROVIDER
        case GAP_TO_RECORD: {
            const RecordsHud& recordsHud = HudManager::getInstance().getRecordsHud();
            int r1 = 0, r2 = 0, r3 = 0, r4 = 0;
            if (recordsHud.getFastestRecordSectors(r1, r2, r3, r4)) { s1 = r1; s2 = r2; s3 = r3; }
            break;
        }
#endif
        case GAP_TO_LASTLAP: {
            const std::deque<LapLogEntry>* lapLog = pluginData.getLapLog();
            if (lapLog) {
                for (size_t i = 0; i < lapLog->size(); ++i) {
                    if (!(*lapLog)[i].isValid) continue;  // most recent VALID lap
                    s1 = (*lapLog)[i].sector1; s2 = (*lapLog)[i].sector2; s3 = (*lapLog)[i].sector3;
                    break;
                }
            }
            break;
        }
        default: break;
    }

    if (s1 <= 0) return -1;
    int sum = s1;
    if (targetSplit >= 1) { if (s2 <= 0) return -1; sum += s2; }
#if GAME_SECTOR_COUNT >= 4
    if (targetSplit >= 2) { if (s3 <= 0) return -1; sum += s3; }
#endif
    return sum;
}

int TimingHud::passiveReferenceMs(GapTypeFlags type) const {
    return cumulativeReferenceMs(type, currentTargetSplit());
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

    // Nothing to show right now (Off, or At-Splits between freezes) -> collapse to zero size.
    if (!contentVisible()) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // Rider finished -> hold the total race time.
    bool riderFinished = pluginData.isDisplayRiderFinished();
    int riderFinishTime = -1;
    if (riderFinished) {
        const StandingsData* standing = pluginData.getStanding(pluginData.getDisplayRaceNum());
        if (standing) riderFinishTime = standing->finishTime;
    }

    // === TIME CONTENT ===
    // Invalid lap -> "INVALID" in the time cell (comparisons just fall back to their reference).
    // Otherwise: frozen official split/lap time -> finish time -> live elapsed time -> placeholder.
    char timeBuffer[32];
    bool timePlaceholder = false;
    bool timeInvalid = showingInvalid();  // (segmentMode already excluded inside)
    if (timeInvalid) {
        strcpy_s(timeBuffer, sizeof(timeBuffer), "INVALID");
    } else if (m_isFrozen) {
        if (m_officialData.time > 0) {
            PluginUtils::formatLapTime(m_officialData.time, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
        }
    } else if (riderFinished && riderFinishTime > 0) {
        PluginUtils::formatLapTime(riderFinishTime, timeBuffer, sizeof(timeBuffer));
    } else {
        int elapsed = pluginData.getElapsedLapTime();
        if (elapsed >= 0) {
            PluginUtils::formatLapTime(elapsed, timeBuffer, sizeof(timeBuffer));
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
        }
    }

    // === SEGMENT MODE OVERRIDE ===
    // Swap the time for the shown segment's, and stage its delta-to-best (rendered below as the
    // single "Best" comparison row).
    if (segmentMode) {
        if (segShowFrozen) {
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
            long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - seg.runStart).count();
            PluginUtils::formatLapTime(static_cast<int>(ms), timeBuffer, sizeof(timeBuffer));
            timePlaceholder = false;
            segGap.reset();
        } else {
            strcpy_s(timeBuffer, sizeof(timeBuffer), Placeholders::LAP_TIME);
            timePlaceholder = true;
            segGap.reset();
        }
    }

    // === COMPARISON VALUE RESOLUTION (normal, non-segment rows) ===
    auto getGapDataForType = [&](GapTypeFlags type) -> const GapData* {
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
    // The split boundary the rider is driving toward, so the passive reference tracks the sector.
    int targetSplit = segmentMode ? -1 : currentTargetSplit();
    // Show the +/- delta while frozen on a split/lap; otherwise the progressive reference time.
    // (An invalid lap clears the gaps, so those cells just fall back to their reference — the
    // "INVALID" flag is shown once, in the time cell.)
    bool showGapData = segmentMode ? (segShowFrozen && seg.lastHasDelta) : m_isFrozen;

    // One rendered comparison value: the +/- delta (active), the target time (passive), or a
    // "-"/"N/A" placeholder. isFaster/isSlower drive the semantic text colour.
    struct RowValue {
        char value[16] = "";
        bool isFaster = false;
        bool isSlower = false;
        bool isReference = false;   // a target time (neutral) vs a delta (green/red) / placeholder (muted)
    };
    auto buildComparison = [&](GapTypeFlags type) -> RowValue {
        RowValue out;
        const GapData* gapData = getGapDataForType(type);
        if (showGapData && gapData && gapData->hasGap) {
            PluginUtils::formatTimeDiff(out.value, sizeof(out.value), gapData->gap);
            out.isFaster = gapData->isFaster;
            out.isSlower = gapData->isSlower;
        } else {
            int refTime = cumulativeReferenceMs(type, targetSplit);
            if (refTime > 0) {
                PluginUtils::formatLapTime(refTime, out.value, sizeof(out.value));
                out.isReference = true;
            } else {
                const char* missing = (type == GAP_TO_RECORD) ? Placeholders::NOT_AVAILABLE : Placeholders::GENERIC;
                strcpy_s(out.value, sizeof(out.value), missing);
            }
        }
        return out;
    };
    auto valueColor = [&](const RowValue& g) -> unsigned long {
        if (g.isFaster) return this->getColor(ColorSlot::POSITIVE);
        if (g.isSlower) return this->getColor(ColorSlot::NEGATIVE);
        if (g.isReference) return this->getColor(ColorSlot::SECONDARY);
        return this->getColor(ColorSlot::MUTED);
    };

    // === BUILD THE ROW LIST (name + value) ===
    struct Row { const char* name; RowValue val; };
    Row rows[GAP_TYPE_COUNT + 1];   // +1 for the segment "Best" row
    int rowCount = 0;
    if (segmentMode) {
        // A custom segment has only its own session best, shown as a single "Best" row.
        RowValue segRow;
        if (showGapData && segGap.hasGap) {
            PluginUtils::formatTimeDiff(segRow.value, sizeof(segRow.value), segGap.gap);
            segRow.isFaster = segGap.isFaster;
            segRow.isSlower = segGap.isSlower;
        } else if (segRefBestMs > 0) {
            PluginUtils::formatLapTime(segRefBestMs, segRow.value, sizeof(segRow.value));
            segRow.isReference = true;
        } else {
            strcpy_s(segRow.value, sizeof(segRow.value), Placeholders::GENERIC);
        }
        rows[rowCount++] = { "Best", segRow };
    } else {
        for (int i = 0; i < GAP_TYPE_COUNT; i++) {
            GapTypeFlags flag = GAP_TYPE_INFO[i].flag;
            if (!(m_enabledComparisons & flag)) continue;
            rows[rowCount++] = { GAP_TYPE_INFO[i].name, buildComparison(flag) };
        }
    }

    if (!m_showTime && rowCount == 0) {
        setBounds(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    // === LAYOUT: a centered vertical stack (big time on top, comparison rows below) ===
    auto dim = getScaledDimensions();

    // Center the panel on CENTER_X, quantizing the anchor to the grid when snapping is on (so the
    // snapped drag offset keeps the left edge on the shared lattice — like the other centered HUDs).
    auto snapCenteringToGrid = [](float x) -> float {
        return UiConfig::getInstance().getGridSnapping()
            ? PluginConstants::HudGrid::SNAP_TO_GRID_X(x)
            : x;
    };

    // Fixed width, matching the NoticesHud (CENTER_STACK_WIDTH_CHARS at the large font + padding),
    // so the two centered top-stack panels line up. Comfortably fits the time and any comparison
    // row (name + value), and a fixed width keeps the panel from jittering as a value flips
    // between a delta and a reference time.
    float innerW = PluginUtils::calculateMonospaceTextWidth(
        WidgetDimensions::CENTER_STACK_WIDTH_CHARS, dim.fontSizeLarge);
    float backgroundWidth = dim.paddingH + innerW + dim.paddingH;

    float bgLeftX = snapCenteringToGrid(CENTER_X - backgroundWidth / 2.0f);

    // Height is a stack of grid-aligned bands: the time row is one lineHeightLarge band (4 snap
    // cells, glyph centered), each comparison row a lineHeightNormal band (2 cells, content
    // centered). No outer padding, so a time-only panel is exactly lineHeightLarge tall —
    // identical to the Notices and Gap Bar boxes — and the whole center-top stack lands on the
    // vertical grid (see TIMING_DIVIDER_Y in notices_hud.cpp).
    float backgroundHeight = (m_showTime ? dim.lineHeightLarge : 0.0f)
                           + rowCount * dim.lineHeightNormal;

    addBackgroundQuad(bgLeftX, START_Y, backgroundWidth, backgroundHeight);

    // Text inset is HALF the width padding (1 grid cell instead of 2), so the horizontal gap
    // from the box edge to the edge-aligned label/value matches the vertical gap of the LARGE
    // glyph in its band — roughly uniform padding all round. The box WIDTH still budgets the
    // full HUD_HORIZONTAL each side above (it stays locked to the Notices / center-stack width),
    // so this only shifts the left/right-aligned text one grid cell outward each side; both
    // insets remain on the snap grid (backgroundWidth is a whole number of cells). The centered
    // time is unaffected. (Notices needs no equivalent — its text is center-justified.)
    const float textInsetH = dim.paddingH * 0.5f;
    const float leftTextX  = bgLeftX + textInsetH;
    const float rightTextX = bgLeftX + backgroundWidth - textInsetH;
    const float centerX    = bgLeftX + backgroundWidth / 2.0f;

    float y = START_Y;

    // Big time row: the large glyph centered in its lineHeightLarge band, using the same
    // centering formula as the Notices/Gap Bar boxes so the time value sits at an identical
    // vertical position within its band. Red on an invalid lap, muted for a placeholder, else
    // primary.
    if (m_showTime) {
        unsigned long timeColor = timeInvalid   ? this->getColor(ColorSlot::NEGATIVE)
                                : timePlaceholder ? this->getColor(ColorSlot::MUTED)
                                                  : this->getColor(ColorSlot::PRIMARY);
        float timeY = y + (dim.lineHeightLarge - dim.fontSizeLarge) * 0.5f;
        addString(timeBuffer, centerX, timeY, Justify::CENTER,
            this->getFont(FontCategory::DIGITS), timeColor, dim.fontSizeLarge);
        y += dim.lineHeightLarge;
    }

    // Comparison rows: name (left) + value (right), each vertically centered in its
    // lineHeightNormal band. The name is a row label — STRONG font at the Small size,
    // row-centered — via addLabel(), matching the other HUDs' labels (and the old secondary
    // chips); the value is the data font (DIGITS) at the normal size, centered in the band the
    // same way. Only the VALUE carries the semantic colour (green faster / red slower / neutral
    // reference); no colored background strips.
    float valueRowOffset = (dim.lineHeightNormal - dim.fontSize) * 0.5f;
    for (int i = 0; i < rowCount; i++) {
        const Row& r = rows[i];
        addLabel(r.name, leftTextX, y, Justify::LEFT,
            this->getFont(FontCategory::STRONG), this->getColor(ColorSlot::TERTIARY), dim);
        addString(r.val.value, rightTextX, y + valueRowOffset, Justify::RIGHT,
            this->getFont(FontCategory::DIGITS), valueColor(r.val), dim.fontSize);
        y += dim.lineHeightNormal;
    }

    setBounds(bgLeftX, START_Y, bgLeftX + backgroundWidth, START_Y + backgroundHeight);
}


void TimingHud::resetToDefaults() {
    // On by default (changed from off in v1.27.1). UPGRADE NOTE: under sparse-save,
    // a user who explicitly disabled Timing while OFF was the default saved no
    // `visible` key (it matched the default), so on upgrade they are indistinguishable
    // from "never touched" and Timing re-appears. This is inherent to any default
    // flip with sparse persistence — call it out in the release notes.
    m_bVisible = true;
    m_bShowTitle = false;
    setTextureVariant(0);  // No texture by default
    m_fBackgroundOpacity = 0.1f;
    m_fScale = 1.0f;
    setPosition(0.0f, DEFAULT_POSITION_Y);

    // Show mode: Always show by default (content shows continuously, references passive)
    m_displayMode = ColumnMode::ALWAYS;
    m_showTime = true;                           // big time row on by default
    m_displayDurationMs = DEFAULT_DURATION_MS;   // 5 seconds freeze

    // Comparison rows: Session PB + All-Time PB by default
    m_enabledComparisons = GAP_DEFAULT_ENABLED;

    // Reset live timing state
    resetLiveTimingState();

    setDataDirty();
}
