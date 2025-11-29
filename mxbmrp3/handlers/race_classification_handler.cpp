// ============================================================================
// handlers/race_classification_handler.cpp
// Processes race classification and standings calculations
// ============================================================================
#include "race_classification_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/hud_manager.h"
#include "draw_handler.h"

DEFINE_HANDLER_SINGLETON(RaceClassificationHandler)

void RaceClassificationHandler::handleRaceClassification(
    SPluginsRaceClassification_t* psRaceClassification,
    SPluginsRaceClassificationEntry_t* pasRaceClassificationEntry,
    int iNumEntries)
{
    HANDLER_NULL_CHECK(psRaceClassification);
    HANDLER_NULL_CHECK(pasRaceClassificationEntry);
    if (iNumEntries <= 0) return;  // Defensive bounds check

    // Event logging handled by PluginManager
    PluginData& pluginData = PluginData::getInstance();

    // Store current session time and state for real-time gap calculations
    int currentTime = psRaceClassification->m_iSessionTime;
    pluginData.setSessionTime(currentTime);
    pluginData.setSessionState(psRaceClassification->m_iSessionState);

    // Batch update all standings AND build classification order in single pass
    // This eliminates the duplicate iteration - both done in one tight loop
    pluginData.batchUpdateStandings(pasRaceClassificationEntry, iNumEntries);

    // Detect overtime start for time+laps races (skip if already detected)
    const SessionData& sessionData = pluginData.getSessionData();

    if (!sessionData.overtimeStarted && sessionData.sessionNumLaps > 0 && sessionData.sessionLength > 0) {
        // This is a time+laps race, check if overtime just started
        if (sessionData.lastSessionTime > 0 && psRaceClassification->m_iSessionTime < 0 &&
            pasRaceClassificationEntry && iNumEntries > 0) {
            // Overtime just started! Capture leader's lap count
            const SPluginsRaceClassificationEntry_t& leader = pasRaceClassificationEntry[0];
            int leaderCompletedLaps = leader.m_iNumLaps;

            // Calculate finish lap: current lap (completed + 1) + sessionNumLaps more laps
            int finishLap = leaderCompletedLaps + 1 + sessionData.sessionNumLaps;

            pluginData.setFinishLap(finishLap);
            pluginData.setOvertimeStarted(true);
        }
    }
    pluginData.setLastSessionTime(psRaceClassification->m_iSessionTime);
}
