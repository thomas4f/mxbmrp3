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
    PluginData::getInstance().setTrackName(psEventData->m_szTrackName);
    PluginData::getInstance().setEventType(psEventData->m_iType);
    PluginData::getInstance().setShiftRPM(psEventData->m_iShiftRPM);
    PluginData::getInstance().setLimiterRPM(psEventData->m_iLimiter);
    PluginData::getInstance().setMaxFuel(psEventData->m_fMaxFuel);
    PluginData::getInstance().setNumberOfGears(psEventData->m_iNumberOfGears);
    PluginData::getInstance().updateSuspensionMaxTravel(
        psEventData->m_afSuspMaxTravel[0],  // Front suspension max travel
        psEventData->m_afSuspMaxTravel[1]   // Rear suspension max travel
    );
}

void EventHandler::handleEventDeinit() {
    // Event logging now handled by PluginManager

    // Clear data when event ends
    PluginData::getInstance().clear();
}
