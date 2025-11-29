// ============================================================================
// handlers/race_entry_handler.h
// Processes race entry data (rider/bike information)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RaceEntryHandler {
public:
    static RaceEntryHandler& getInstance();

    void handleRaceAddEntry(SPluginsRaceAddEntry_t* psRaceAddEntry);
    void handleRaceRemoveEntry(SPluginsRaceRemoveEntry_t* psRaceRemoveEntry);

private:
    RaceEntryHandler() {}
    ~RaceEntryHandler() {}
    RaceEntryHandler(const RaceEntryHandler&) = delete;
    RaceEntryHandler& operator=(const RaceEntryHandler&) = delete;
};
