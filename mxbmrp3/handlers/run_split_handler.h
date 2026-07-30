// ============================================================================
// handlers/run_split_handler.h
// Processes player split timing data (RunSplit events are player-only)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRunSplit(Unified::PlayerSplitData* psSplitData);

}  // namespace Handlers
