// ============================================================================
// handlers/race_track_position_handler.h
// Processes race track position data for all riders
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRaceTrackPosition(int iNumVehicles, Unified::TrackPositionData* pasRaceTrackPosition);

}  // namespace Handlers
