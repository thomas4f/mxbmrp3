// ============================================================================
// handlers/race_vehicle_data_handler.h
// Processes race vehicle data (telemetry for all riders during races/replays)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RaceVehicleDataHandler {
public:
    static RaceVehicleDataHandler& getInstance();

    void handleRaceVehicleData(SPluginsRaceVehicleData_t* psRaceVehicleData);

private:
    RaceVehicleDataHandler() {}
    ~RaceVehicleDataHandler() {}
    RaceVehicleDataHandler(const RaceVehicleDataHandler&) = delete;
    RaceVehicleDataHandler& operator=(const RaceVehicleDataHandler&) = delete;
};
