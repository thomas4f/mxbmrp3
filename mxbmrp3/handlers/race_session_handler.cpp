// ============================================================================
// handlers/race_session_handler.cpp
// Processes race session lifecycle data (race session init/deinit)
// ============================================================================
#include "race_session_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/fmx_manager.h"

DEFINE_HANDLER_SINGLETON(RaceSessionHandler)

void RaceSessionHandler::handleRaceSession(Unified::RaceSessionData* psRaceSession) {
    HANDLER_NULL_CHECK(psRaceSession);

    DEBUG_INFO_F("RaceSession changed: session=%d, state=%d, length=%d, numLaps=%d",
        psRaceSession->session, psRaceSession->sessionState,
        psRaceSession->sessionLength, psRaceSession->sessionNumLaps);

    // Log race format interpretation for debugging
    if (psRaceSession->sessionLength > 0 && psRaceSession->sessionNumLaps > 0) {
        DEBUG_INFO_F("[RACE FORMAT] Timed+Laps race: %d ms + %d extra laps after timer",
            psRaceSession->sessionLength, psRaceSession->sessionNumLaps);
    } else if (psRaceSession->sessionLength > 0) {
        DEBUG_INFO_F("[RACE FORMAT] Pure timed race: %d ms", psRaceSession->sessionLength);
    } else if (psRaceSession->sessionNumLaps > 0) {
        DEBUG_INFO_F("[RACE FORMAT] Pure lap race: %d laps", psRaceSession->sessionNumLaps);
    }

    // Clear session-specific data when a new session starts
    FmxManager::getInstance().reset();
    PluginData::getInstance().clearAllIdealLap();
    PluginData::getInstance().clearAllLapLog();
    PluginData::getInstance().clearLiveGapTimingPoints();
    PluginData::getInstance().resetAllLapTimers();

    // Reset race finish tracking (overtime and leader finish time)
    PluginData::getInstance().setOvertimeStarted(false);
    PluginData::getInstance().setFinishLap(-1);
    PluginData::getInstance().setLastSessionTime(0);
    PluginData::getInstance().setLeaderFinishTime(-1);

    // Update plugin data store
    PluginData::getInstance().setSession(psRaceSession->session);
    PluginData::getInstance().setSessionState(psRaceSession->sessionState);
    PluginData::getInstance().setSessionLength(psRaceSession->sessionLength);
    PluginData::getInstance().setSessionNumLaps(psRaceSession->sessionNumLaps);
    PluginData::getInstance().setConditions(static_cast<int>(psRaceSession->conditions));
    PluginData::getInstance().setAirTemperature(psRaceSession->airTemperature);
    PluginData::getInstance().setTrackTemperature(psRaceSession->trackTemperature);
}

void RaceSessionHandler::handleRaceSessionState(Unified::RaceSessionStateData* psRaceSessionState) {
    HANDLER_NULL_CHECK(psRaceSessionState);

    DEBUG_INFO_F("RaceSessionState changed: session=%d, state=%d",
        psRaceSessionState->session, psRaceSessionState->sessionState);

    // When race transitions to "in progress" (state 16), reset timing state
    // This prevents false overtime detection when transitioning from pre-start (256)
    // where sessionTime values during countdown could falsely trigger the positive→negative transition
    // Also clear live gap timing points to prevent stale RTG values from pre-start
    // (track position updates during pre-start would otherwise contaminate RTG calculations)
    if (psRaceSessionState->sessionState == 16) {
        PluginData::getInstance().setLastSessionTime(0);
        PluginData::getInstance().clearLiveGapTimingPoints();
    }

    // Update plugin data store
    // Note: Do NOT update sessionLength here - it changes during the race to countdown/other values
    // We keep the initial sessionLength from RaceSession for race format display
    PluginData::getInstance().setSession(psRaceSessionState->session);
    PluginData::getInstance().setSessionState(psRaceSessionState->sessionState);
}
