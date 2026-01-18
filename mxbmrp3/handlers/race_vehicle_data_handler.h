// ============================================================================
// handlers/race_vehicle_data_handler.h
// Processes race vehicle data (telemetry for all riders during races/replays)
// ============================================================================
#pragma once

#include "../game/unified_types.h"

class RaceVehicleDataHandler {
public:
    static RaceVehicleDataHandler& getInstance();

    void handleRaceVehicleData(Unified::RaceVehicleData* psRaceVehicleData);

private:
    RaceVehicleDataHandler() = default;
    ~RaceVehicleDataHandler() = default;
    RaceVehicleDataHandler(const RaceVehicleDataHandler&) = delete;
    RaceVehicleDataHandler& operator=(const RaceVehicleDataHandler&) = delete;
};
