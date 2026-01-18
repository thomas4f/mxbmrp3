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
    Unified::RaceClassificationData* psRaceClassification,
    Unified::RaceClassificationEntry* pasRaceClassificationEntry,
    int iNumEntries)
{
    HANDLER_NULL_CHECK(psRaceClassification);
    HANDLER_NULL_CHECK(pasRaceClassificationEntry);
    if (iNumEntries <= 0) return;  // Defensive bounds check

    // Event logging handled by PluginManager
    PluginData& pluginData = PluginData::getInstance();

    // Store current session time and state for real-time gap calculations
    int currentTime = psRaceClassification->sessionTime;
    pluginData.setSessionTime(currentTime);
    pluginData.setSessionState(psRaceClassification->sessionState);

    // Batch update all standings AND build classification order in single pass
    // This eliminates the duplicate iteration - both done in one tight loop
    pluginData.batchUpdateStandings(pasRaceClassificationEntry, iNumEntries);

    // Detect overtime start for time+laps races (skip if already detected)
    const SessionData& sessionData = pluginData.getSessionData();

    if (!sessionData.overtimeStarted && sessionData.sessionNumLaps > 0 && sessionData.sessionLength > 0) {
        // This is a time+laps race, check if overtime just started
        // Only detect when race is in progress (state 16), not during pre-start (256)
        // Pre-start can have negative sessionTime (countdown) which would falsely trigger this
        if (psRaceClassification->sessionState == 16 &&
            sessionData.lastSessionTime > 0 && psRaceClassification->sessionTime < 0 &&
            pasRaceClassificationEntry && iNumEntries > 0) {
            // Overtime just started! Capture leader's current lap
            // numLaps is the lap currently being raced (1-indexed), not completed laps
            const Unified::RaceClassificationEntry& leader = pasRaceClassificationEntry[0];
            int leaderCurrentLap = leader.numLaps;

            // Calculate finish lap: current lap + extra laps to complete
            // Leader finishes when numLaps > finishLap (i.e., they've COMPLETED finishLap)
            int finishLap = leaderCurrentLap + sessionData.sessionNumLaps;

            DEBUG_INFO_F("[OVERTIME STARTED] leader on lap %d, finishLap=%d (+%d laps), sessionNumLaps=%d",
                leaderCurrentLap, finishLap, sessionData.sessionNumLaps, sessionData.sessionNumLaps);

            pluginData.setFinishLap(finishLap);
            pluginData.setOvertimeStarted(true);
        }
    }

    pluginData.setLastSessionTime(psRaceClassification->sessionTime);
}
