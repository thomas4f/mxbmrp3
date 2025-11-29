// ============================================================================
// handlers/race_split_handler.cpp
// Processes race split timing data for current lap tracking
// ============================================================================
#include "race_split_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RaceSplitHandler)

void RaceSplitHandler::handleRaceSplit(SPluginsRaceSplit_t* psRaceSplit) {
    HANDLER_NULL_CHECK(psRaceSplit);

    // RaceSplit events fire for ALL riders (includes spectated players)
    // Defensive: validate timing data
    if (psRaceSplit->m_iSplitTime <= 0) {
        return;  // Invalid timing data, skip processing
    }

    PluginData& data = PluginData::getInstance();
    int raceNum = psRaceSplit->m_iRaceNum;

    // Update current lap split data (used by SessionBestHud for real-time tracking)
    // m_iSplit is 0-indexed (0 = split 1, 1 = split 2, 2 = split 3/finish line)
    data.updateCurrentLapSplit(
        raceNum,
        psRaceSplit->m_iLapNum,  // Use lap number directly from event
        psRaceSplit->m_iSplit,
        psRaceSplit->m_iSplitTime
    );

    // Note: Lap log is NOT updated here - it only updates on RaceLap events
    // This keeps lap log simple and consistent with historical lap data
}
