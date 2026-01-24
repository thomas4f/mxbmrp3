// ============================================================================
// handlers/event_handler.cpp
// Processes event lifecycle data (event init/deinit)
// ============================================================================
#include "event_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#if GAME_HAS_DISCORD
#include "../core/discord_manager.h"
#endif

DEFINE_HANDLER_SINGLETON(EventHandler)

void EventHandler::handleEventInit(Unified::VehicleEventData* psEventData) {
    HANDLER_NULL_CHECK(psEventData);

    // Event logging now handled by PluginManager

    // Update plugin data store
    PluginData::getInstance().setRiderName(psEventData->pilotName);
    PluginData::getInstance().setBikeName(psEventData->vehicleName);
    PluginData::getInstance().setCategory(psEventData->category);
    PluginData::getInstance().setTrackId(psEventData->trackId);
    PluginData::getInstance().setTrackName(psEventData->trackName);
    PluginData::getInstance().setEventType(static_cast<int>(psEventData->eventType));
    PluginData::getInstance().setShiftRPM(psEventData->shiftRPM);
    PluginData::getInstance().setLimiterRPM(psEventData->limiterRPM);
    PluginData::getInstance().setSteerLock(psEventData->steerLock);
    PluginData::getInstance().setMaxFuel(psEventData->maxFuel);
    PluginData::getInstance().setNumberOfGears(psEventData->numberOfGears);
    PluginData::getInstance().updateSuspensionMaxTravel(
        psEventData->suspMaxTravel[0],  // Front suspension max travel
        psEventData->suspMaxTravel[1]   // Rear suspension max travel
    );
    PluginData::getInstance().setEngineTemperatureThresholds(
        psEventData->engineOptTemperature,
        psEventData->engineTempAlarmLow,
        psEventData->engineTempAlarmHigh
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

#if GAME_HAS_DISCORD
    // Update Discord presence to show "In Menus" (track is now empty)
    DiscordManager::getInstance().onEventEnd();
#endif
}
