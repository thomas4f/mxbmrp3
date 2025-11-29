// ============================================================================
// handlers/race_session_handler.cpp
// Processes race session lifecycle data (race session init/deinit)
// ============================================================================
#include "race_session_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RaceSessionHandler)

void RaceSessionHandler::handleRaceSession(SPluginsRaceSession_t* psRaceSession) {
    HANDLER_NULL_CHECK(psRaceSession);

    DEBUG_INFO_F("RaceSession changed: session=%d, state=%d, length=%d, numLaps=%d",
        psRaceSession->m_iSession, psRaceSession->m_iSessionState,
        psRaceSession->m_iSessionLength, psRaceSession->m_iSessionNumLaps);

    // Clear session-specific data when a new session starts
    PluginData::getInstance().clearAllSessionBest();
    PluginData::getInstance().clearAllLapLog();
    PluginData::getInstance().clearLiveGapTimingPoints();

    // Reset race finish tracking (overtime and leader finish time)
    PluginData::getInstance().setOvertimeStarted(false);
    PluginData::getInstance().setFinishLap(-1);
    PluginData::getInstance().setLastSessionTime(0);
    PluginData::getInstance().setLeaderFinishTime(-1);

    // Update plugin data store
    PluginData::getInstance().setSession(psRaceSession->m_iSession);
    PluginData::getInstance().setSessionState(psRaceSession->m_iSessionState);
    PluginData::getInstance().setSessionLength(psRaceSession->m_iSessionLength);
    PluginData::getInstance().setSessionNumLaps(psRaceSession->m_iSessionNumLaps);
    PluginData::getInstance().setConditions(psRaceSession->m_iConditions);
    PluginData::getInstance().setAirTemperature(psRaceSession->m_fAirTemperature);
}

void RaceSessionHandler::handleRaceSessionState(SPluginsRaceSessionState_t* psRaceSessionState) {
    HANDLER_NULL_CHECK(psRaceSessionState);

    DEBUG_INFO_F("RaceSessionState changed: session=%d, state=%d",
        psRaceSessionState->m_iSession, psRaceSessionState->m_iSessionState);

    // Update plugin data store
    // Note: Do NOT update sessionLength here - it changes during the race to countdown/other values
    // We keep the initial sessionLength from RaceSession for race format display
    PluginData::getInstance().setSession(psRaceSessionState->m_iSession);
    PluginData::getInstance().setSessionState(psRaceSessionState->m_iSessionState);
}
