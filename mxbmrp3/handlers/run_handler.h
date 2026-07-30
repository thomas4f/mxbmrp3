// ============================================================================
// handlers/run_handler.h
// Processes run lifecycle data (run init/deinit/start/stop)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRunInit(Unified::SessionData* psSessionData);
void handleRunStart();
void handleRunStop();
void handleRunDeinit();

}  // namespace Handlers
