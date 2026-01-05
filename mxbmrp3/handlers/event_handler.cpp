// ============================================================================
// handlers/event_handler.cpp
// Processes event lifecycle data (event init/deinit)
// ============================================================================
#include "event_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(EventHandler)

void EventHandler::handleEventInit(SPluginsBikeEvent_t* psEventData) {
    HANDLER_NULL_CHECK(psEventData);

    // Event logging now handled by PluginManager

    // Update plugin data store
    PluginData::getInstance().setRiderName(psEventData->m_szRiderName);
    PluginData::getInstance().setBikeName(psEventData->m_szBikeName);
    PluginData::getInstance().setCategory(psEventData->m_szCategory);
    PluginData::getInstance().setTrackId(psEventData->m_szTrackID);
    PluginData::getInstance().setTrackName(psEventData->m_szTrackName);
    PluginData::getInstance().setEventType(psEventData->m_iType);
    PluginData::getInstance().setShiftRPM(psEventData->m_iShiftRPM);
    PluginData::getInstance().setLimiterRPM(psEventData->m_iLimiter);
    PluginData::getInstance().setSteerLock(psEventData->m_fSteerLock);
    PluginData::getInstance().setMaxFuel(psEventData->m_fMaxFuel);
    PluginData::getInstance().setNumberOfGears(psEventData->m_iNumberOfGears);
    PluginData::getInstance().updateSuspensionMaxTravel(
        psEventData->m_afSuspMaxTravel[0],  // Front suspension max travel
        psEventData->m_afSuspMaxTravel[1]   // Rear suspension max travel
    );

    // Check if a RaceAddEntry with unactive=0 already arrived (spectate-first case)
    int pendingRaceNum = PluginData::getInstance().getPendingPlayerRaceNum();
    if (pendingRaceNum >= 0) {
        // Use the pending entry - it arrived before EventInit
        PluginData::getInstance().setPlayerRaceNum(pendingRaceNum);
        PluginData::getInstance().clearPendingPlayerRaceNum();
        DEBUG_INFO_F("Local player identified from pending entry: raceNum=%d", pendingRaceNum);
    } else {
        // The next RaceAddEntry with unactive=0 will be the local player
        PluginData::getInstance().setWaitingForPlayerEntry(true);
    }
}

void EventHandler::handleEventDeinit() {
    // Event logging now handled by PluginManager

    // Clear data when event ends
    PluginData::getInstance().clear();
}
