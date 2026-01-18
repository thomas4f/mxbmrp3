// ============================================================================
// handlers/race_entry_handler.cpp
// Processes race entry data (rider/vehicle information)
// ============================================================================
#include "race_entry_handler.h"
#include "../core/handler_singleton.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"

DEFINE_HANDLER_SINGLETON(RaceEntryHandler)

void RaceEntryHandler::handleRaceAddEntry(Unified::RaceEntryData* psRaceAddEntry) {
    HANDLER_NULL_CHECK(psRaceAddEntry);

    // DEBUG_INFO_F("RaceAddEntry: raceNum=%d, name='%s', inactive=%d",
    //              psRaceAddEntry->raceNum,
    //              psRaceAddEntry->name,
    //              psRaceAddEntry->inactive);

    // Store race entry data
    PluginData::getInstance().addRaceEntry(
        psRaceAddEntry->raceNum,
        psRaceAddEntry->name,
        psRaceAddEntry->vehicleName
    );

    // Identify local player: first RaceAddEntry with inactive=false after EventInit is the player
    // This is more reliable than name matching since servers can modify rider names
    if (!psRaceAddEntry->inactive) {
        if (PluginData::getInstance().isWaitingForPlayerEntry()) {
            // EventInit already fired - this is our entry
            PluginData::getInstance().setWaitingForPlayerEntry(false);
            PluginData::getInstance().clearPendingPlayerRaceNum();
            PluginData::getInstance().setPlayerRaceNum(psRaceAddEntry->raceNum);
            DEBUG_INFO_F("Local player identified: raceNum=%d, name='%s'",
                         psRaceAddEntry->raceNum,
                         psRaceAddEntry->name);

            // FALLBACK: If EventInit() was not called (e.g., joined mid-session),
            // extract category from player's entry
            const SessionData& data = PluginData::getInstance().getSessionData();
            if (data.category[0] == '\0' && psRaceAddEntry->category[0] != '\0') {
                DEBUG_INFO_F("FALLBACK: Extracting category from RaceAddEntry: %s", psRaceAddEntry->category);
                PluginData::getInstance().setCategory(psRaceAddEntry->category);
            }
        } else if (PluginData::getInstance().getPlayerRaceNum() < 0) {
            // EventInit hasn't fired yet and player not identified - store as pending
            // This handles spectate-first case where RaceAddEntry arrives before EventInit
            PluginData::getInstance().setPendingPlayerRaceNum(psRaceAddEntry->raceNum);
        }
    }
}

void RaceEntryHandler::handleRaceRemoveEntry(int raceNum) {
    // Event logging now handled by PluginManager

    // Remove race entry data
    PluginData::getInstance().removeRaceEntry(raceNum);
}
