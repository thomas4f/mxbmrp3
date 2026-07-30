// ============================================================================
// handlers/race_communication_handler.h
// Processes race communication messages (penalties, warnings)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRaceCommunication(Unified::RaceCommunicationData* psRaceCommunication);

}  // namespace Handlers
