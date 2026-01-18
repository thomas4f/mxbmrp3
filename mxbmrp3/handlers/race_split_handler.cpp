// ============================================================================
// handlers/race_split_handler.cpp
// Processes race split timing data for current lap tracking
// ============================================================================
#include "race_split_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RaceSplitHandler)

void RaceSplitHandler::handleRaceSplit(Unified::RaceSplitData* psRaceSplit) {
    HANDLER_NULL_CHECK(psRaceSplit);

    // RaceSplit events fire for ALL riders (includes spectated players)
    // Defensive: validate timing data
    if (psRaceSplit->splitTime <= 0) {
        return;  // Invalid timing data, skip processing
    }

    PluginData& data = PluginData::getInstance();

    // Filter out historical split events from previous sessions
    // When joining mid-race, the game sends RaceSplit events from earlier sessions
    // which would create phantom "current lap" data
    int currentSession = data.getSessionData().session;
    if (psRaceSplit->session != currentSession) {
        DEBUG_INFO_F("RaceSplit: Ignoring event from session %d (current session is %d)",
                     psRaceSplit->session, currentSession);
        return;
    }

    int raceNum = psRaceSplit->raceNum;
    int lapNum = psRaceSplit->lapNum;
    int splitIndex = psRaceSplit->splitIndex;
    int splitTime = psRaceSplit->splitTime;

    // Validate split index (expected range: 0-2 for split1, split2, finish)
    if (splitIndex < 0 || splitIndex > 2) {
        DEBUG_WARN_F("RaceSplit: Invalid split index %d (expected 0-2), raceNum=%d", splitIndex, raceNum);
        return;
    }

    // Update current lap split data (used by IdealLapHud for real-time tracking)
    // splitIndex is 0-indexed (0 = split 1, 1 = split 2, 2 = split 3/finish line)
    data.updateCurrentLapSplit(raceNum, lapNum, splitIndex, splitTime);

    // Update centralized lap timer anchor for real-time elapsed time calculation
    // This allows HUDs to show continuously ticking time from last split
    data.setLapTimerAnchor(raceNum, splitTime, lapNum, splitIndex);

    // Note: Lap log is NOT updated here - it only updates on RaceLap events
    // This keeps lap log simple and consistent with historical lap data
}
