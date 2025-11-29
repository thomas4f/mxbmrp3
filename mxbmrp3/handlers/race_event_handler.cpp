// ============================================================================
// handlers/race_event_handler.cpp
// Processes race event lifecycle data (race init/deinit)
// ============================================================================
#include "race_event_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RaceEventHandler)

void RaceEventHandler::handleRaceEvent(SPluginsRaceEvent_t* psRaceEvent) {
    HANDLER_NULL_CHECK(psRaceEvent);

    // Event logging now handled by PluginManager

    // Update plugin data store
    PluginData::getInstance().setTrackName(psRaceEvent->m_szTrackName);
    PluginData::getInstance().setTrackLength(psRaceEvent->m_fTrackLength);
}

void RaceEventHandler::handleRaceDeinit() {
    // Event logging now handled by PluginManager

    // Clear data when race ends
    PluginData::getInstance().clear();
}
