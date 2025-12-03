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

    // Check if this entry is the player by comparing with stored rider name
    // Handles exact match and server-forced rating prefixes (e.g., "B1 | Thomas" matches "Thomas")
    const SessionData& data = PluginData::getInstance().getSessionData();
    if (PluginUtils::matchRiderName(psRaceAddEntry->m_szName, data.riderName,
                                     PluginConstants::GameLimits::RACE_ENTRY_NAME_MAX)) {
        // This is the player - cache the race number directly to avoid name-based lookup
        PluginData::getInstance().setPlayerRaceNum(psRaceAddEntry->m_iRaceNum);
        DEBUG_INFO_F("Local player identified: raceNum=%d, entry='%s', player='%s'",
                     psRaceAddEntry->m_iRaceNum,
                     psRaceAddEntry->m_szName,
                     data.riderName);

        // FALLBACK: If EventInit() was not called (e.g., joined mid-session),
        // extract category from player's entry
        if (data.category[0] == '\0' && psRaceAddEntry->m_szCategory[0] != '\0') {
            DEBUG_INFO_F("FALLBACK: Extracting category from RaceAddEntry: %s", psRaceAddEntry->m_szCategory);
            PluginData::getInstance().setCategory(psRaceAddEntry->m_szCategory);
        }
    }
}

void RaceEntryHandler::handleRaceRemoveEntry(SPluginsRaceRemoveEntry_t* psRaceRemoveEntry) {
    HANDLER_NULL_CHECK(psRaceRemoveEntry);

    // Event logging now handled by PluginManager

    // Remove race entry data
    PluginData::getInstance().removeRaceEntry(psRaceRemoveEntry->m_iRaceNum);
}
