// ============================================================================
// handlers/race_entry_handler.cpp
// Processes race entry data (rider/bike information)
// ============================================================================
#include "race_entry_handler.h"
#include "../core/handler_singleton.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_data.h"
#include "../core/plugin_utils.h"

DEFINE_HANDLER_SINGLETON(RaceEntryHandler)

void RaceEntryHandler::handleRaceAddEntry(SPluginsRaceAddEntry_t* psRaceAddEntry) {
    HANDLER_NULL_CHECK(psRaceAddEntry);

    DEBUG_INFO_F("RaceAddEntry: raceNum=%d, name='%s', unactive=%d",
                 psRaceAddEntry->m_iRaceNum,
                 psRaceAddEntry->m_szName,
                 psRaceAddEntry->m_iUnactive);

    // Store race entry data
    PluginData::getInstance().addRaceEntry(
        psRaceAddEntry->m_iRaceNum,
        psRaceAddEntry->m_szName,
        psRaceAddEntry->m_szBikeName
    );

    // Identify local player: first RaceAddEntry with unactive=0 after EventInit is the player
    // This is more reliable than name matching since servers can modify rider names
    if (psRaceAddEntry->m_iUnactive == 0) {
        if (PluginData::getInstance().isWaitingForPlayerEntry()) {
            // EventInit already fired - this is our entry
            PluginData::getInstance().setWaitingForPlayerEntry(false);
            PluginData::getInstance().clearPendingPlayerRaceNum();
            PluginData::getInstance().setPlayerRaceNum(psRaceAddEntry->m_iRaceNum);
            DEBUG_INFO_F("Local player identified: raceNum=%d, name='%s'",
                         psRaceAddEntry->m_iRaceNum,
                         psRaceAddEntry->m_szName);

            // FALLBACK: If EventInit() was not called (e.g., joined mid-session),
            // extract category from player's entry
            const SessionData& data = PluginData::getInstance().getSessionData();
            if (data.category[0] == '\0' && psRaceAddEntry->m_szCategory[0] != '\0') {
                DEBUG_INFO_F("FALLBACK: Extracting category from RaceAddEntry: %s", psRaceAddEntry->m_szCategory);
                PluginData::getInstance().setCategory(psRaceAddEntry->m_szCategory);
            }
        } else if (PluginData::getInstance().getPlayerRaceNum() < 0) {
            // EventInit hasn't fired yet and player not identified - store as pending
            // This handles spectate-first case where RaceAddEntry arrives before EventInit
            PluginData::getInstance().setPendingPlayerRaceNum(psRaceAddEntry->m_iRaceNum);
        }
    }
}

void RaceEntryHandler::handleRaceRemoveEntry(SPluginsRaceRemoveEntry_t* psRaceRemoveEntry) {
    HANDLER_NULL_CHECK(psRaceRemoveEntry);

    // Event logging now handled by PluginManager

    // Remove race entry data
    PluginData::getInstance().removeRaceEntry(psRaceRemoveEntry->m_iRaceNum);
}
