// ============================================================================
// handlers/race_entry_handler.h
// Processes race entry data (rider/vehicle information)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRaceAddEntry(Unified::RaceEntryData* psRaceAddEntry);
void handleRaceRemoveEntry(int raceNum);

}  // namespace Handlers
