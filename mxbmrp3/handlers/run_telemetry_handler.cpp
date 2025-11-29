// ============================================================================
// handlers/run_telemetry_handler.cpp
// Processes run telemetry data (input, controller, bike telemetry)
// ============================================================================
#include "run_telemetry_handler.h"
#include "../core/handler_singleton.h"
#include "../core/plugin_data.h"
#include "../core/xinput_reader.h"

DEFINE_HANDLER_SINGLETON(RunTelemetryHandler)

void RunTelemetryHandler::handleRunTelemetry(SPluginsBikeData_t* psBikeData, float fTime, float fPos) {
    // Update input telemetry data
    if (psBikeData) {
        // Update speedometer, gear, RPM, and fuel
        PluginData::getInstance().updateSpeedometer(psBikeData->m_fSpeedometer, psBikeData->m_iGear, psBikeData->m_iRPM, psBikeData->m_fFuel);

        // Update input telemetry data
        PluginData::getInstance().updateInputTelemetry(
            psBikeData->m_fSteer,
            psBikeData->m_fThrottle,
            psBikeData->m_fFrontBrake,
            psBikeData->m_fRearBrake,
            psBikeData->m_fClutch
        );

        // Update suspension telemetry data
        PluginData::getInstance().updateSuspensionLength(
            psBikeData->m_afSuspLength[0],  // Front suspension current length
            psBikeData->m_afSuspLength[1]   // Rear suspension current length
        );
    } else {
        // No telemetry data available (e.g., spectating retired rider, menu, etc.)
        PluginData::getInstance().invalidateSpeedometer();
    }

    // Update XInput controller state (same rate as telemetry)
    XInputReader::getInstance().update();
    PluginData::getInstance().updateXInputData(XInputReader::getInstance().getData());

    // Additional telemetry data available:
    // psBikeData contains: RPM, gear, speed, position, velocity, acceleration, suspension, etc.
    // fTime: on-track time in seconds
    // fPos: position on centerline (0.0 to 1.0)
}
