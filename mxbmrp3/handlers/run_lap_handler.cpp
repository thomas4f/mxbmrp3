// ============================================================================
// handlers/run_lap_handler.cpp
// Processes player lap timing data (RunLap events are player-only)
// ============================================================================
#include "run_lap_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_utils.h"
#include "../core/plugin_data.h"

DEFINE_HANDLER_SINGLETON(RunLapHandler)

void RunLapHandler::handleRunLap(SPluginsBikeLap_t* psLapData) {
    HANDLER_NULL_CHECK(psLapData);

    // NOTE: RunLap events are player-only, limiting spectate mode functionality.
    // All session best and lap log updates moved to RaceLapHandler (all-riders event).
    // This handler is kept as a stub for potential future player-only functionality.
}
