// ============================================================================
// hud/timing_hud_gaps.cpp
// TimingHud gap + reference-time calculation — the timing math behind the
// comparison columns: per-split live gaps (calculateAllGaps), the full-lap /
// cumulative / passive reference lookups, the all-time-PB cache, and the small
// gap/best-lap helpers. Extracted verbatim from timing_hud.cpp when that file
// grew past ~1.2k lines; the TimingHud class, members, and public API are
// unchanged — only where these method bodies live moves. Same byte-identical-
// extraction pattern as the plugin_data / http_server splits.
// ============================================================================
#include "timing_hud.h"
#include "records_hud.h"

#include "../game/game_config.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <chrono>
#include <algorithm>

#include "../diagnostics/logger.h"
#include "../core/plugin_utils.h"
#include "../core/widget_constants.h"
#include "../core/color_config.h"
#include "../core/stats_manager.h"
#include "../core/hud_manager.h"

using namespace PluginConstants;

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

