// ============================================================================
// handlers/race_entry_handler.cpp
// Processes race entry data (rider/bike information)
// ============================================================================
#include "race_entry_handler.h"
#include "../core/handler_singleton.h"
#include "../diagnostics/logger.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RaceEntryHandler)

void RaceEntryHandler::handleRaceAddEntry(SPluginsRaceAddEntry_t* psRaceAddEntry) {
    HANDLER_NULL_CHECK(psRaceAddEntry);

    // Store race entry data
    PluginData::getInstance().addRaceEntry(
        psRaceAddEntry->m_iRaceNum,
        psRaceAddEntry->m_szName,
        psRaceAddEntry->m_szBikeName
    );

    // Check if this entry is the player by comparing with stored rider name
    // NOTE: The game truncates names in RaceAddEntry to ~31 chars, but EventInit sends full name (up to 100).
    // Use strncmp with the RaceAddEntry name length to handle long nicknames correctly.
    const SessionData& data = PluginData::getInstance().getSessionData();
    size_t entryNameLen = strlen(psRaceAddEntry->m_szName);
    if (data.riderName[0] != '\0' && strncmp(data.riderName, psRaceAddEntry->m_szName, entryNameLen) == 0
        && (data.riderName[entryNameLen] == '\0' || entryNameLen >= PluginConstants::GameLimits::RACE_ENTRY_NAME_MAX)) {
        // This is the player - cache the race number directly to avoid name-based lookup
        PluginData::getInstance().setPlayerRaceNum(psRaceAddEntry->m_iRaceNum);

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
