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

void RaceVehicleDataHandler::handleRaceVehicleData(SPluginsRaceVehicleData_t* psRaceVehicleData) {
    // Defensive null check and active vehicle check
    if (!psRaceVehicleData || !psRaceVehicleData->m_iActive) {
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
    // Only updates data available in SPluginsRaceVehicleData_t (throttle, frontBrake, rpm, gear, speedometer, lean)
    // Other data (rearBrake, clutch, steer, fuel, suspension) is not available when spectating
    if (psRaceVehicleData->m_iRaceNum == displayRaceNum) {
        // Note: m_fLean uses opposite sign convention from m_fRoll (player telemetry)
        // m_fLean: "Negative = left", m_fRoll: standard rotation (positive = right)
        // Negate to match the convention used by LeanWidget
        pluginData.updateRaceVehicleTelemetry(
            psRaceVehicleData->m_fSpeedometer,
            psRaceVehicleData->m_iGear,
            psRaceVehicleData->m_iRPM,
            psRaceVehicleData->m_fThrottle,
            psRaceVehicleData->m_fFrontBrake,
            -psRaceVehicleData->m_fLean
        );
    }
}
