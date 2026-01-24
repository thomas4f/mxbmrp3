// ============================================================================
// handlers/race_event_handler.cpp
// Processes race event lifecycle data (race init/deinit)
// ============================================================================
#include "race_event_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../game/game_config.h"
#if defined(GAME_MXBIKES)
#include "../game/connection_detector.h"
#endif

DEFINE_HANDLER_SINGLETON(RaceEventHandler)

void RaceEventHandler::handleRaceEvent(Unified::RaceEventData* psRaceEvent) {
    HANDLER_NULL_CHECK(psRaceEvent);

    // Event logging now handled by PluginManager

#if defined(GAME_MXBIKES)
    // Detect connection type (Offline/Host/Client) via memory reading
    // Note: Memory reading is MX Bikes-specific due to hardcoded offsets
    auto& detector = Memory::ConnectionDetector::getInstance();
    auto connectionType = detector.detect();
    PluginData::getInstance().setConnectionType(static_cast<int>(connectionType));

    // Set server info if online (Host or Client connections)
    if (connectionType == Memory::ConnectionType::Host ||
        connectionType == Memory::ConnectionType::Client) {
        const auto& serverName = detector.getServerName();
        if (!serverName.empty()) {
            PluginData::getInstance().setServerName(serverName.c_str());
        }
        const auto& serverPassword = detector.getServerPassword();
        if (!serverPassword.empty()) {
            PluginData::getInstance().setServerPassword(serverPassword.c_str());
        }
        PluginData::getInstance().setServerClientsCount(detector.getServerClientsCount());
        PluginData::getInstance().setServerMaxClients(detector.getServerMaxClients());
    }
#endif

    // Update plugin data store
    // Note: RaceEventData (spectating) doesn't provide trackId, only trackName.
    // Don't touch trackId - preserve any value already set by EventHandler (if user was on track first).
    // RecordsHud checks if trackId is available and disables Compare button if not.
    PluginData::getInstance().setTrackName(psRaceEvent->trackName);
    PluginData::getInstance().setTrackLength(psRaceEvent->trackLength);
}

void RaceEventHandler::handleRaceDeinit() {
    // Event logging now handled by PluginManager

#if defined(GAME_MXBIKES)
    // Reset connection detector state
    Memory::ConnectionDetector::getInstance().reset();
#endif

    // Clear data when race ends
    PluginData::getInstance().clear();
}
