// ============================================================================
// handlers/race_entry_handler.h
// Processes race entry data (rider/vehicle information)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceEntryHandler {
public:
    static RaceEntryHandler& getInstance();

    void handleRaceAddEntry(Unified::RaceEntryData* psRaceAddEntry);
    void handleRaceRemoveEntry(int raceNum);

private:
    RaceEntryHandler() {}
    ~RaceEntryHandler() {}
    RaceEntryHandler(const RaceEntryHandler&) = delete;
    RaceEntryHandler& operator=(const RaceEntryHandler&) = delete;
};
