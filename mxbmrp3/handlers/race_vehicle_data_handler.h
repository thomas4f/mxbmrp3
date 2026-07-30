// ============================================================================
// handlers/race_vehicle_data_handler.h
// Processes race vehicle data (telemetry for all riders during races/replays)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

namespace Handlers {

void handleRaceVehicleData(Unified::RaceVehicleData* psRaceVehicleData);

}  // namespace Handlers
