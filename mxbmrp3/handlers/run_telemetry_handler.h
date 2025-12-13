// ============================================================================
// handlers/run_telemetry_handler.h
// Processes run telemetry data (input, controller, bike telemetry)
// ============================================================================
#pragma once

#include "../vendor/piboso/mxb_api.h"

class RunTelemetryHandler {
public:
    static RunTelemetryHandler& getInstance();

    void handleRunTelemetry(SPluginsBikeData_t* psBikeData, float fTime, float fPos);

private:
    RunTelemetryHandler() = default;
    ~RunTelemetryHandler() = default;
    RunTelemetryHandler(const RunTelemetryHandler&) = delete;
    RunTelemetryHandler& operator=(const RunTelemetryHandler&) = delete;
};
