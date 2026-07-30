// ============================================================================
// handlers/run_lap_handler.h
// Processes player lap timing data (RunLap events are player-only)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRunLap(Unified::PlayerLapData* psLapData);

}  // namespace Handlers
