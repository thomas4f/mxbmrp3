// ============================================================================
// hud/split_crossing.cpp
// See split_crossing.h.
// ============================================================================
#include "split_crossing.h"

#include "../core/plugin_data.h"

SplitCrossingDetector::Result SplitCrossingDetector::poll() {
    const PluginData& data = PluginData::getInstance();
    const CurrentLapData* currentLap = data.getCurrentLapData();
    const IdealLapData* idealLapData = data.getIdealLapData();
    const int splits[3] = { currentLap ? currentLap->split1 : -1,
                            currentLap ? currentLap->split2 : -1,
                            currentLap ? currentLap->split3 : -1 };
    Result result;

    if (!m_adopted ||
        data.getSessionData().sessionGeneration != m_sessionGeneration ||
        data.getDisplayRaceNum() != m_displayRaceNum) {
        for (int i = 0; i < NUM_SPLITS; ++i) m_cachedSplits[i] = splits[i];
        m_cachedLastCompletedLapNum = idealLapData ? idealLapData->lastCompletedLapNum : -1;
        m_sessionGeneration = data.getSessionData().sessionGeneration;
        m_displayRaceNum = data.getDisplayRaceNum();
        m_adopted = true;
        result.adopted = true;
        return result;
    }

    if (idealLapData && idealLapData->lastCompletedLapNum >= 0 &&
        idealLapData->lastCompletedLapNum != m_cachedLastCompletedLapNum) {
        m_cachedLastCompletedLapNum = idealLapData->lastCompletedLapNum;
        for (int i = 0; i < NUM_SPLITS; ++i) m_cachedSplits[i] = -1;
        result.line = true;
        result.lapNum = idealLapData->lastCompletedLapNum;
        result.lapTime = idealLapData->lastLapTime;
        const std::deque<LapLogEntry>* lapLog = data.getLapLog();
        if (lapLog && !lapLog->empty()) {
            const LapLogEntry& lap = (*lapLog)[0];
            result.lapValid = lap.isValid;
            result.lapViaPits = lap.viaPits;
            if (lap.lapNum >= 0) result.lapNum = lap.lapNum;
        }
    }

    for (int i = 0; i < NUM_SPLITS; ++i) {
        if (splits[i] <= 0 || splits[i] == m_cachedSplits[i]) continue;
        m_cachedSplits[i] = splits[i];
        result.splitIndex = i;
        result.splitTime = splits[i];
        break;
    }
    return result;
}
