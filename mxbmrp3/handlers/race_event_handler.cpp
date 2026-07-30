// ============================================================================
// handlers/race_event_handler.cpp
// Processes race event lifecycle data (race init/deinit)
// ============================================================================
#include "race_event_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"

void Handlers::handleRaceEvent(Unified::RaceEventData* psRaceEvent) {
    HANDLER_NULL_CHECK(psRaceEvent);

    // Update plugin data store
    // Note: RaceEventData (spectating) doesn't provide trackId, only trackName.
    // Don't touch trackId - preserve any value already set by EventHandler (if user was on track first).
    PluginData::getInstance().setTrackName(psRaceEvent->trackName);
    PluginData::getInstance().setTrackLength(psRaceEvent->trackLength);
}

void Handlers::handleRaceDeinit() {
    // Clear data when race ends
    PluginData::getInstance().clear();
}
