// ============================================================================
// handlers/run_split_handler.cpp
// Processes player split timing data (RunSplit events are player-only)
// ============================================================================
#include "run_split_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RunSplitHandler)

void RunSplitHandler::handleRunSplit(SPluginsBikeSplit_t* psSplitData) {
    HANDLER_NULL_CHECK(psSplitData);

    // NOTE: RunSplit events are player-only, limiting spectate mode functionality.
    // All current lap tracking and lap log updates moved to RaceSplitHandler (all-riders event).
    // This handler is kept as a stub for potential future player-only functionality.
}
