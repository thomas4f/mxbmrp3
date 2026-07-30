// ============================================================================
// handlers/race_session_handler.h
// Processes race session lifecycle data (race session init/deinit)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRaceSession(Unified::RaceSessionData* psRaceSession);
void handleRaceSessionState(Unified::RaceSessionStateData* psRaceSessionState);

}  // namespace Handlers
