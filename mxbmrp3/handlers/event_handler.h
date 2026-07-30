// ============================================================================
// handlers/event_handler.h
// Processes event lifecycle data (event init/deinit)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleEventInit(Unified::VehicleEventData* psEventData);
void handleEventDeinit();

}  // namespace Handlers
