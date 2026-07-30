// ============================================================================
// handlers/race_event_handler.h
// Processes race event lifecycle data (race init/deinit)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRaceEvent(Unified::RaceEventData* psRaceEvent);
void handleRaceDeinit();

}  // namespace Handlers
