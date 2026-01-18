// ============================================================================
// handlers/race_vehicle_data_handler.cpp
// Processes race vehicle data (telemetry for all riders during races/replays)
// ============================================================================
#include "race_vehicle_data_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/plugin_constants.h"
#include "../diagnostics/logger.h"

DEFINE_HANDLER_SINGLETON(RaceVehicleDataHandler)

void RaceVehicleDataHandler::handleRaceVehicleData(Unified::RaceVehicleData* psRaceVehicleData) {
    // Defensive null check and active vehicle check
    if (!psRaceVehicleData || !psRaceVehicleData->active) {
        return;
    }

    PluginData& pluginData = PluginData::getInstance();

    // Skip when player is on track - RunTelemetryHandler provides complete data
    // Only update during spectate/replay when this is the only data source
    if (pluginData.getDrawState() == PluginConstants::ViewState::ON_TRACK) {
        return;
    }

    // Get the display race number (spectated rider when spectating/replay)
    int displayRaceNum = pluginData.getDisplayRaceNum();

    // If this vehicle data is for the rider we're displaying, update telemetry
    // Only updates data available in RaceVehicleData (throttle, brake, rpm, gear, speedometer, lean)
    // Other data (rearBrake, clutch, steer, fuel, suspension) is not available when spectating
    if (psRaceVehicleData->raceNum == displayRaceNum) {
        // Note: lean uses opposite sign convention from roll (player telemetry)
        // lean: "Negative = left", roll: standard rotation (positive = right)
        // Negate to match the convention used by LeanWidget
        pluginData.updateRaceVehicleTelemetry(
            psRaceVehicleData->speedometer,
            psRaceVehicleData->gear,
            psRaceVehicleData->rpm,
            psRaceVehicleData->throttle,
            psRaceVehicleData->brake,
            -psRaceVehicleData->lean
        );
    }
}
